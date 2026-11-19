#include "renderer.h"

#include <math.h>

#define SCALE_EPSILON 1e-6
#define PREFETCH_PAGES 1
#define LINK_NUMBER_FONT_SIZE 12

/*
 * Guarded by `mutex`. A scale of 0 means none, since real scales are > 0.
 * A render ending on a worker publishes its surface or failure.
 */
typedef struct {
    GMutex mutex;
    /* The last render published, at surface_scale; NULL if none */
    cairo_surface_t *surface;
    double surface_scale;
    /* 0 while the page is not wanted */
    double wanted_scale;
    double failed_scale;
    /* Bumped when the page stops being wanted, to ignore running renders */
    guint64 generation;
    gboolean in_progress;
} RendererPageState;

typedef struct {
    RendererPageState *page_state;
    ViewerInfo *info;
    PopplerPage *poppler_page;
    guint64 generation;
    double scale;
    /* Unscaled */
    ViewportSize page_size;
} RenderJob;

struct Renderer {
    Viewer *viewer;
    GWeakRef redraw_target;
    GThreadPool *render_pool;
    GMutex pool_mutex;
    gint shutting_down;

    RendererPageState *page_states;
    int page_count;
    /* The pages the last draw wanted; -1 before the first */
    int last_wanted_from;
    int last_wanted_to;
};

static void renderer_want_pages(Renderer *renderer, int wanted_from,
    int wanted_to, double scale);
static void renderer_drop_page(RendererPageState *page_state);
static gboolean renderer_draw_page(Renderer *renderer, cairo_t *cr,
    const ViewportLayout *layout, int page_idx);
static void draw_page_surface(cairo_t *cr, const ViewportRect *rect,
    cairo_surface_t *surface, gboolean current);
static void renderer_draw_highlights(cairo_t *cr, const ViewportRect *rect,
    double scale, double page_height, const SearchRect *rects, guint n_rects);
static void renderer_draw_link_numbers(cairo_t *cr, Viewer *viewer);
static gboolean renderer_request_page_for_layout(Renderer *renderer,
    const ViewportLayout *layout, int page_idx);
static gboolean renderer_page_settled_locked(
    const RendererPageState *page_state, double scale);
static gboolean renderer_failed_at_locked(const RendererPageState *page_state,
    double scale);
static gboolean scales_equal(double left, double right);
static RenderJob *renderer_create_job_locked(RendererPageState *page_state,
    ViewerInfo *info, PopplerPage *poppler_page, ViewportSize page_size);
static void render_job_free(RenderJob *job);
static void renderer_push_job(Renderer *renderer, RenderJob *job);
static void render_page_async(gpointer data, gpointer user_data);
static cairo_surface_t *renderer_render_page(const RenderJob *job);
static void draw_loading_page(cairo_t *cr, const ViewportRect *rect,
    const char *text);
static void renderer_schedule_redraw(Renderer *renderer);
static gboolean queue_redraw_on_main(gpointer data);
static void weak_ref_free(gpointer data);

Renderer *renderer_new(GtkWidget *view, Viewer *viewer)
{
    Renderer *renderer = g_new0(Renderer, 1);
    renderer->viewer = viewer;
    g_weak_ref_init(&renderer->redraw_target,
        view != NULL ? G_OBJECT(view) : NULL);
    g_mutex_init(&renderer->pool_mutex);
    renderer->last_wanted_from = -1;
    renderer->last_wanted_to = -1;

    renderer->page_count = viewer->info->n_pages;
    renderer->page_states = g_new0(RendererPageState, renderer->page_count);
    for (int i = 0; i < renderer->page_count; i++) {
        g_mutex_init(&renderer->page_states[i].mutex);
    }

    GError *error = NULL;
    renderer->render_pool = g_thread_pool_new(render_page_async, renderer,
        g_get_num_processors(), TRUE, &error);
    if (error != NULL) {
        g_warning("Failed to create render thread pool: %s", error->message);
        g_error_free(error);
        renderer->render_pool = NULL;
    }

    return renderer;
}

void renderer_destroy(Renderer *renderer)
{
    if (renderer == NULL) {
        return;
    }

    g_mutex_lock(&renderer->pool_mutex);
    g_atomic_int_set(&renderer->shutting_down, 1);
    GThreadPool *render_pool = renderer->render_pool;
    renderer->render_pool = NULL;
    g_weak_ref_set(&renderer->redraw_target, NULL);
    g_mutex_unlock(&renderer->pool_mutex);

    if (render_pool != NULL) {
        g_thread_pool_free(render_pool, FALSE, TRUE);
    }

    for (int i = 0; i < renderer->page_count; i++) {
        RendererPageState *page_state = &renderer->page_states[i];
        g_clear_pointer(&page_state->surface, cairo_surface_destroy);
        g_mutex_clear(&page_state->mutex);
    }
    g_free(renderer->page_states);

    g_weak_ref_clear(&renderer->redraw_target);
    g_mutex_clear(&renderer->pool_mutex);
    g_free(renderer);
}

gboolean renderer_draw(Renderer *renderer, cairo_t *cr, gboolean link_numbers)
{
    Viewer *viewer = renderer->viewer;
    const ViewerCursor *mark = viewer_current_mark(viewer);
    const ViewportLayout layout = viewport_layout(viewer->viewport, mark);
    const int wanted_from = MAX(0, layout.from - PREFETCH_PAGES);
    const int wanted_to = MIN(renderer->page_count - 1,
        layout.to + PREFETCH_PAGES);
    gboolean settled = TRUE;

    renderer_want_pages(renderer, wanted_from, wanted_to, layout.scale);

    if (mark->dark_mode) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }

    for (int i = layout.from; i <= layout.to; i++) {
        settled &= renderer_draw_page(renderer, cr, &layout, i);
    }

    if (link_numbers) {
        renderer_draw_link_numbers(cr, viewer);
    }

    if (mark->dark_mode) {
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }

    for (int i = layout.from; i <= layout.to; i++) {
        settled &= renderer_request_page_for_layout(renderer, &layout, i);
    }
    for (int i = layout.to + 1; i <= wanted_to; i++) {
        settled &= renderer_request_page_for_layout(renderer, &layout, i);
    }
    for (int i = layout.from - 1; i >= wanted_from; i--) {
        settled &= renderer_request_page_for_layout(renderer, &layout, i);
    }

    return settled;
}

/*
 * Drops the pages the last draw wanted that this one doesn't, and makes
 * each page in [wanted_from, wanted_to] want `scale`
 */
static void renderer_want_pages(Renderer *renderer, int wanted_from,
    int wanted_to, double scale)
{
    if (renderer->last_wanted_from >= 0) {
        for (int i = renderer->last_wanted_from;
             i <= renderer->last_wanted_to; i++) {
            if (i < wanted_from || i > wanted_to) {
                renderer_drop_page(&renderer->page_states[i]);
            }
        }
    }
    renderer->last_wanted_from = wanted_from;
    renderer->last_wanted_to = wanted_to;

    for (int i = wanted_from; i <= wanted_to; i++) {
        RendererPageState *page_state = &renderer->page_states[i];
        g_mutex_lock(&page_state->mutex);
        page_state->wanted_scale = scale;
        g_mutex_unlock(&page_state->mutex);
    }
}

static void renderer_drop_page(RendererPageState *page_state)
{
    g_mutex_lock(&page_state->mutex);
    page_state->generation++;
    page_state->wanted_scale = 0;
    page_state->failed_scale = 0;
    g_clear_pointer(&page_state->surface, cairo_surface_destroy);
    page_state->surface_scale = 0;
    g_mutex_unlock(&page_state->mutex);
}

/* Paints the page and returns whether it was settled when painted */
static gboolean renderer_draw_page(Renderer *renderer, cairo_t *cr,
    const ViewportLayout *layout, int page_idx)
{
    Viewer *viewer = renderer->viewer;
    const ViewportRect rect = viewport_layout_page_rect(viewer->viewport,
        layout, page_idx);

    const SearchRect *matches;
    guint n_matches;
    const gboolean scanned = search_page_matches(viewer->search, page_idx,
        &matches, &n_matches);

    RendererPageState *page_state = &renderer->page_states[page_idx];
    g_mutex_lock(&page_state->mutex);
    const gboolean has_surface = page_state->surface != NULL;
    const gboolean failed = renderer_failed_at_locked(page_state,
        layout->scale);
    const gboolean settled = renderer_page_settled_locked(page_state,
        layout->scale);
    if (has_surface) {
        draw_page_surface(cr, &rect, page_state->surface,
            scales_equal(page_state->surface_scale, layout->scale));
    }
    g_mutex_unlock(&page_state->mutex);

    if (!has_surface) {
        draw_loading_page(cr, &rect, failed ? "Can't render" : "Loading...");
    } else if (scanned) {
        renderer_draw_highlights(cr, &rect, layout->scale,
            viewport_get_page_size(viewer->viewport, page_idx).height,
            matches, n_matches);
    }

    if (page_idx > 0) {
        /* Draw page separator */
        cairo_save(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, rect.x, rect.y);
        cairo_rel_line_to(cr, rect.width, 0);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    return settled;
}

/*
 * A `current` surface was rendered at the layout's scale; any other is
 * scaled to fill `rect`
 */
static void draw_page_surface(cairo_t *cr, const ViewportRect *rect,
    cairo_surface_t *surface, gboolean current)
{
    if (current) {
        cairo_set_source_surface(cr, surface, rect->x, rect->y);
        cairo_paint(cr);
        return;
    }

    cairo_save(cr);
    cairo_rectangle(cr, rect->x, rect->y, rect->width, rect->height);
    cairo_clip(cr);
    cairo_translate(cr, rect->x, rect->y);
    cairo_scale(cr, rect->width / cairo_image_surface_get_width(surface),
        rect->height / cairo_image_surface_get_height(surface));
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void renderer_draw_highlights(cairo_t *cr, const ViewportRect *rect,
    double scale, double page_height, const SearchRect *rects, guint n_rects)
{
    if (n_rects == 0) {
        return;
    }

    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.5);
    for (guint i = 0; i < n_rects; i++) {
        const SearchRect *r = &rects[i];
        cairo_rectangle(cr, rect->x + r->x1 * scale,
            rect->y + (page_height - r->y2) * scale,
            (r->x2 - r->x1) * scale, (r->y2 - r->y1) * scale);
        cairo_fill(cr);
    }
    cairo_restore(cr);
}

static void renderer_draw_link_numbers(cairo_t *cr, Viewer *viewer)
{
    GArray *numbers = viewer_link_numbers(viewer);
    const ViewportSize view = viewport_get_view_size(viewer->viewport);

    cairo_save(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, LINK_NUMBER_FONT_SIZE);
    cairo_set_line_width(cr, 2.0);

    for (guint i = 0; i < numbers->len; i++) {
        const ViewerLinkNumber *label = &g_array_index(numbers,
            ViewerLinkNumber, i);
        char text[16];
        cairo_text_extents_t extents;

        g_snprintf(text, sizeof(text), "%u", label->number);
        cairo_text_extents(cr, text, &extents);

        double x = MIN(label->area.x, view.width - extents.width);
        double y = MIN(label->area.y, view.height - extents.height);
        x = MAX(x, 0);
        y = MAX(y, 0);

        cairo_move_to(cr, x - extents.x_bearing, y - extents.y_bearing);
        cairo_text_path(cr, text);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_fill(cr);
    }

    cairo_restore(cr);
    g_array_unref(numbers);
}

/*
 * Requests a render of a wanted page that needs one and returns whether
 * the page was settled
 */
static gboolean renderer_request_page_for_layout(Renderer *renderer,
    const ViewportLayout *layout, int page_idx)
{
    Viewer *viewer = renderer->viewer;
    RendererPageState *page_state = &renderer->page_states[page_idx];
    RenderJob *job = NULL;

    g_mutex_lock(&page_state->mutex);
    const gboolean settled = renderer_page_settled_locked(page_state,
        layout->scale);
    if (!settled && !page_state->in_progress) {
        job = renderer_create_job_locked(page_state, viewer->info,
            viewer_info_get_poppler_page(viewer->info, page_idx),
            viewport_get_page_size(viewer->viewport, page_idx));
    }
    g_mutex_unlock(&page_state->mutex);

    if (job != NULL) {
        renderer_push_job(renderer, job);
    }

    return settled;
}

/* Rendered at `scale`, or failed at it: no render at `scale` is needed */
static gboolean renderer_page_settled_locked(
    const RendererPageState *page_state, double scale)
{
    return (page_state->surface != NULL &&
        scales_equal(page_state->surface_scale, scale)) ||
        renderer_failed_at_locked(page_state, scale);
}

static gboolean renderer_failed_at_locked(const RendererPageState *page_state,
    double scale)
{
    return page_state->failed_scale > 0 &&
        scales_equal(page_state->failed_scale, scale);
}

static gboolean scales_equal(double left, double right)
{
    return fabs(left - right) < SCALE_EPSILON;
}

/* A render of the page at the scale it wants; marks the render running */
static RenderJob *renderer_create_job_locked(RendererPageState *page_state,
    ViewerInfo *info, PopplerPage *poppler_page, ViewportSize page_size)
{
    RenderJob *job = g_new0(RenderJob, 1);

    job->page_state = page_state;
    job->info = info;
    job->poppler_page = g_object_ref(poppler_page);
    job->generation = page_state->generation;
    job->scale = page_state->wanted_scale;
    job->page_size = page_size;
    page_state->in_progress = TRUE;

    return job;
}

static void renderer_push_job(Renderer *renderer, RenderJob *job)
{
    gboolean run_inline = FALSE;
    GError *error = NULL;

    g_mutex_lock(&renderer->pool_mutex);
    if (g_atomic_int_get(&renderer->shutting_down)) {
        g_mutex_unlock(&renderer->pool_mutex);
        g_mutex_lock(&job->page_state->mutex);
        job->page_state->in_progress = FALSE;
        g_mutex_unlock(&job->page_state->mutex);
        render_job_free(job);
        return;
    }

    if (renderer->render_pool == NULL) {
        run_inline = TRUE;
    } else if (!g_thread_pool_push(renderer->render_pool, job, &error)) {
        g_warning("Failed to push render task to thread pool: %s",
            error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        run_inline = TRUE;
    }
    g_mutex_unlock(&renderer->pool_mutex);

    if (run_inline) {
        render_page_async(job, renderer);
    }
}

static void render_page_async(gpointer data, gpointer user_data)
{
    RenderJob *job = data;
    Renderer *renderer = user_data;
    RendererPageState *page_state = job->page_state;

    cairo_surface_t *page_surface = renderer_render_page(job);

    gboolean changed = FALSE;
    RenderJob *retry_job = NULL;
    g_mutex_lock(&page_state->mutex);
    page_state->in_progress = FALSE;
    if (page_state->generation == job->generation &&
        scales_equal(page_state->wanted_scale, job->scale)) {
        if (page_surface != NULL) {
            g_clear_pointer(&page_state->surface, cairo_surface_destroy);
            page_state->surface = g_steal_pointer(&page_surface);
            page_state->surface_scale = job->scale;
        } else {
            page_state->failed_scale = job->scale;
        }
        changed = TRUE;
    } else if (!g_atomic_int_get(&renderer->shutting_down) &&
        page_state->wanted_scale > 0 &&
        !renderer_page_settled_locked(page_state, page_state->wanted_scale)) {
        /* Its draw skipped the page while this render ran */
        retry_job = renderer_create_job_locked(page_state, job->info,
            job->poppler_page, job->page_size);
    }
    g_mutex_unlock(&page_state->mutex);

    if (page_surface != NULL) {
        cairo_surface_destroy(page_surface);
    }
    if (changed) {
        renderer_schedule_redraw(renderer);
    }

    render_job_free(job);

    if (retry_job != NULL) {
        renderer_push_job(renderer, retry_job);
    }
}

/* NULL if the surface fails to render */
static cairo_surface_t *renderer_render_page(const RenderJob *job)
{
    const int width = MAX(1, (int)(job->scale * job->page_size.width));
    const int height = MAX(1, (int)(job->scale * job->page_size.height));
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
        width, height);

    if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
        cairo_t *cr = cairo_create(surface);
        cairo_scale(cr, job->scale, job->scale);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_rectangle(cr, 0, 0, job->page_size.width,
            job->page_size.height);
        cairo_fill(cr);

        viewer_info_lock(job->info);
        poppler_page_render(job->poppler_page, cr);
        viewer_info_unlock(job->info);
        cairo_destroy(cr);
        cairo_surface_flush(surface);
    }

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    return surface;
}

static void render_job_free(RenderJob *job)
{
    g_object_unref(job->poppler_page);
    g_free(job);
}

static void renderer_schedule_redraw(Renderer *renderer)
{
    if (g_atomic_int_get(&renderer->shutting_down)) {
        return;
    }

    GObject *target = g_weak_ref_get(&renderer->redraw_target);
    if (target == NULL) {
        return;
    }

    GWeakRef *target_ref = g_new0(GWeakRef, 1);
    g_weak_ref_init(target_ref, target);
    g_object_unref(target);
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, queue_redraw_on_main, target_ref,
        weak_ref_free);
}

static gboolean queue_redraw_on_main(gpointer data)
{
    GWeakRef *target_ref = data;
    GtkWidget *view = GTK_WIDGET(g_weak_ref_get(target_ref));
    if (view != NULL) {
        gtk_widget_queue_draw(view);
        g_object_unref(view);
    }

    return G_SOURCE_REMOVE;
}

static void weak_ref_free(gpointer data)
{
    GWeakRef *target_ref = data;
    g_weak_ref_clear(target_ref);
    g_free(target_ref);
}

static void draw_loading_page(cairo_t *cr, const ViewportRect *rect,
    const char *text)
{
    cairo_save(cr);
    cairo_rectangle(cr, rect->x, rect->y, rect->width, rect->height);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 32);

    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);
    cairo_move_to(cr,
        rect->x + (rect->width - extents.width) / 2 - extents.x_bearing,
        rect->y + (rect->height - extents.height) / 2 - extents.y_bearing);
    cairo_show_text(cr, text);
    cairo_restore(cr);
}
