#include "renderer.h"

#include <math.h>
#include <string.h>

#define SCALE_EPSILON 1e-6
#define PREFETCH_PAGES 1
#define LINK_NUMBER_FONT_SIZE 12

typedef enum {
    RENDERER_PAGE_NOT_RENDERED,
    RENDERER_PAGE_RENDERING,
    RENDERER_PAGE_RENDERED
} RendererPageStatus;

typedef struct {
    double scale;
    double page_width;
    double page_height;
} RenderInput;

typedef struct {
    GMutex mutex;
    RendererPageStatus status;
    cairo_surface_t *surface;
    RenderInput *desired_input;
    guint64 generation;
    gboolean in_progress;
} RendererPageState;

typedef struct {
    RendererPageState *page_state;
    ViewerInfo *info;
    PopplerPage *poppler_page;
    guint64 generation;
    RenderInput *input;
} RenderJob;

struct Renderer {
    GWeakRef redraw_target;
    GThreadPool *render_pool;
    GMutex pool_mutex;
    gint shutting_down;

    RendererPageState *page_states;
    int page_count;
    int last_requested_from;
    int last_requested_to;
};

static void renderer_draw_page(Renderer *renderer, cairo_t *cr,
    Viewer *viewer, const ViewportLayout *layout, int page_idx);
static void renderer_draw_highlights(cairo_t *cr, const ViewportRect *rect,
    double scale, double page_height, const SearchRect *rects, guint n_rects);
static void renderer_draw_link_numbers(cairo_t *cr, Viewer *viewer);
static void renderer_init(Renderer *renderer, GtkWidget *view);
static void renderer_reset_page(RendererPageState *page_state);
static void renderer_request_page_for_layout(Renderer *renderer, Viewer *viewer,
    const ViewportLayout *layout, int page_idx);
static void renderer_clear_surface_locked(RendererPageState *page_state);
static void renderer_request_page(Renderer *renderer, ViewerInfo *info,
    RendererPageState *page_state, PopplerPage *poppler_page,
    RenderInput *input);
static RenderJob *renderer_create_job_locked(RendererPageState *page_state,
    ViewerInfo *info, PopplerPage *poppler_page);
static void render_job_free(RenderJob *job);
static void renderer_push_job(Renderer *renderer, RenderJob *job);
static void render_page_async(gpointer data, gpointer user_data);
static void renderer_render_page(cairo_t *cr, ViewerInfo *info,
    PopplerPage *page, const RenderInput *input);
static void draw_loading_page(cairo_t *cr, const ViewportRect *rect);

static RenderInput *render_input_new(Viewer *viewer,
    const ViewportLayout *layout, int page_idx);
static RenderInput *render_input_copy(const RenderInput *input);
static void render_input_free(RenderInput *input);
static gboolean render_input_equal(const RenderInput *left,
    const RenderInput *right);
static void renderer_ensure_page_states(Renderer *renderer, Viewer *viewer);
static void renderer_schedule_redraw(Renderer *renderer);
static gboolean queue_redraw_on_main(gpointer data);
static void weak_ref_free(gpointer data);

Renderer *renderer_new(GtkWidget *view)
{
    Renderer *renderer = g_new0(Renderer, 1);
    renderer_init(renderer, view);

    return renderer;
}

void renderer_init(Renderer *renderer, GtkWidget *view)
{
    g_weak_ref_init(&renderer->redraw_target,
        view != NULL ? G_OBJECT(view) : NULL);
    g_mutex_init(&renderer->pool_mutex);
    renderer->last_requested_from = -1;
    renderer->last_requested_to = -1;

    GError *error = NULL;
    renderer->render_pool = g_thread_pool_new(render_page_async, renderer,
        g_get_num_processors(), TRUE, &error);
    if (error != NULL) {
        g_warning("Failed to create render thread pool: %s", error->message);
        g_error_free(error);
        renderer->render_pool = NULL;
    }
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
        if (page_state->surface != NULL) {
            cairo_surface_destroy(page_state->surface);
        }
        render_input_free(page_state->desired_input);
        g_mutex_clear(&page_state->mutex);
    }
    g_free(renderer->page_states);

    g_weak_ref_clear(&renderer->redraw_target);
    g_mutex_clear(&renderer->pool_mutex);
    g_free(renderer);
}

void renderer_draw(Renderer *renderer, cairo_t *cr, Viewer *viewer,
    gboolean link_numbers)
{
    const ViewerCursor *mark = viewer_current_mark(viewer);

    renderer_ensure_page_states(renderer, viewer);

    if (mark->dark_mode) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }

    ViewportLayout layout = viewport_layout(viewer->viewport, mark);
    for (int i = layout.from; i <= layout.to; i++) {
        renderer_draw_page(renderer, cr, viewer, &layout, i);
    }

    if (link_numbers) {
        renderer_draw_link_numbers(cr, viewer);
    }

    if (mark->dark_mode) {
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }
}

void renderer_render_visible_pages(Renderer *renderer, Viewer *viewer)
{
    renderer_ensure_page_states(renderer, viewer);

    ViewportLayout layout = viewport_layout(viewer->viewport,
        viewer_current_mark(viewer));
    const int visible_from = layout.from;
    const int visible_to = layout.to;
    const int requested_from = MAX(0, visible_from - PREFETCH_PAGES);
    const int requested_to = MIN(renderer->page_count - 1,
        visible_to + PREFETCH_PAGES);

    if (renderer->last_requested_from >= 0) {
        for (int i = renderer->last_requested_from;
             i <= renderer->last_requested_to; i++) {
            if (i < requested_from || i > requested_to) {
                renderer_reset_page(&renderer->page_states[i]);
            }
        }
    }

    renderer->last_requested_from = requested_from;
    renderer->last_requested_to = requested_to;

    for (int i = visible_from; i <= visible_to; i++) {
        renderer_request_page_for_layout(renderer, viewer, &layout, i);
    }
    for (int i = visible_to + 1; i <= requested_to; i++) {
        renderer_request_page_for_layout(renderer, viewer, &layout, i);
    }
    for (int i = visible_from - 1; i >= requested_from; i--) {
        renderer_request_page_for_layout(renderer, viewer, &layout, i);
    }
}

static void renderer_request_page_for_layout(Renderer *renderer, Viewer *viewer,
    const ViewportLayout *layout, int page_idx)
{
    PopplerPage *page = viewer_info_get_poppler_page(viewer->info, page_idx);
    RenderInput *input = render_input_new(viewer, layout, page_idx);
    renderer_request_page(renderer, viewer->info,
        &renderer->page_states[page_idx], page, input);
}

static void renderer_draw_page(Renderer *renderer, cairo_t *cr,
    Viewer *viewer, const ViewportLayout *layout, int page_idx)
{
    const ViewportRect rect = viewport_layout_page_rect(viewer->viewport,
        layout, page_idx);

    const SearchRect *matches;
    guint n_matches;
    const gboolean scanned = search_page_matches(viewer->search, page_idx,
        &matches, &n_matches);

    RendererPageState *page_state = &renderer->page_states[page_idx];
    g_mutex_lock(&page_state->mutex);
    const gboolean is_cached = page_state->status == RENDERER_PAGE_RENDERED &&
        page_state->surface != NULL;
    if (is_cached) {
        cairo_set_source_surface(cr, page_state->surface, rect.x, rect.y);
        cairo_paint(cr);
    }
    g_mutex_unlock(&page_state->mutex);

    if (!is_cached) {
        draw_loading_page(cr, &rect);
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

static void renderer_reset_page(RendererPageState *page_state)
{
    g_mutex_lock(&page_state->mutex);
    page_state->generation++;
    render_input_free(page_state->desired_input);
    page_state->desired_input = NULL;
    renderer_clear_surface_locked(page_state);
    g_mutex_unlock(&page_state->mutex);
}

static void renderer_clear_surface_locked(RendererPageState *page_state)
{
    if (page_state->surface != NULL) {
        cairo_surface_destroy(page_state->surface);
        page_state->surface = NULL;
    }
    page_state->status = RENDERER_PAGE_NOT_RENDERED;
}

static void renderer_request_page(Renderer *renderer, ViewerInfo *info,
    RendererPageState *page_state, PopplerPage *poppler_page,
    RenderInput *input)
{
    RenderJob *job = NULL;

    g_mutex_lock(&page_state->mutex);

    if (!render_input_equal(page_state->desired_input, input)) {
        render_input_free(page_state->desired_input);
        page_state->desired_input = input;
        input = NULL;
        page_state->generation++;
        renderer_clear_surface_locked(page_state);
    }
    render_input_free(input);

    if (!page_state->in_progress &&
        page_state->status != RENDERER_PAGE_RENDERED &&
        page_state->desired_input != NULL) {
        page_state->status = RENDERER_PAGE_RENDERING;
        job = renderer_create_job_locked(page_state, info, poppler_page);
    }

    g_mutex_unlock(&page_state->mutex);

    if (job != NULL) {
        renderer_push_job(renderer, job);
    }
}

static RenderJob *renderer_create_job_locked(RendererPageState *page_state,
    ViewerInfo *info, PopplerPage *poppler_page)
{
    RenderJob *job = g_new0(RenderJob, 1);
    job->page_state = page_state;
    job->info = info;
    job->poppler_page = g_object_ref(poppler_page);
    job->generation = page_state->generation;
    job->input = render_input_copy(page_state->desired_input);
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
        if (job->page_state->generation == job->generation) {
            job->page_state->in_progress = FALSE;
            job->page_state->status = RENDERER_PAGE_NOT_RENDERED;
        }
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
    const RenderInput *input = job->input;

    int scaled_width = MAX(1, (int)(input->scale * input->page_width));
    int scaled_height = MAX(1, (int)(input->scale * input->page_height));
    cairo_surface_t *page_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, scaled_width, scaled_height);
    cairo_t *cr = cairo_create(page_surface);
    cairo_scale(cr, input->scale, input->scale);
    renderer_render_page(cr, job->info, job->poppler_page, input);
    cairo_destroy(cr);
    cairo_surface_flush(page_surface);

    gboolean published = FALSE;
    RenderJob *retry_job = NULL;
    g_mutex_lock(&page_state->mutex);
    page_state->in_progress = FALSE;
    if (page_state->generation == job->generation) {
        if (page_state->surface != NULL) {
            cairo_surface_destroy(page_state->surface);
        }
        page_state->surface = page_surface;
        page_surface = NULL;
        page_state->status = RENDERER_PAGE_RENDERED;
        published = TRUE;
    } else {
        page_state->status = RENDERER_PAGE_NOT_RENDERED;
        if (!g_atomic_int_get(&renderer->shutting_down) &&
            page_state->desired_input != NULL) {
            retry_job = renderer_create_job_locked(page_state, job->info,
                job->poppler_page);
            page_state->status = RENDERER_PAGE_RENDERING;
        }
    }
    g_mutex_unlock(&page_state->mutex);

    if (page_surface != NULL) {
        cairo_surface_destroy(page_surface);
    }
    if (published) {
        renderer_schedule_redraw(renderer);
    }

    render_job_free(job);

    if (retry_job != NULL) {
        renderer_push_job(renderer, retry_job);
    }
}

static void renderer_render_page(cairo_t *cr, ViewerInfo *info,
    PopplerPage *page, const RenderInput *input)
{
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, 0, 0, input->page_width, input->page_height);
    cairo_fill(cr);

    viewer_info_lock(info);
    poppler_page_render(page, cr);
    viewer_info_unlock(info);
}

static RenderInput *render_input_new(Viewer *viewer,
    const ViewportLayout *layout, int page_idx)
{
    const ViewportSize page_size = viewport_get_page_size(viewer->viewport,
        page_idx);

    RenderInput *input = g_new0(RenderInput, 1);
    input->scale = layout->scale;
    input->page_width = page_size.width;
    input->page_height = page_size.height;

    return input;
}

static void render_job_free(RenderJob *job)
{
    g_object_unref(job->poppler_page);
    render_input_free(job->input);
    g_free(job);
}

static RenderInput *render_input_copy(const RenderInput *input)
{
    if (input == NULL) {
        return NULL;
    }

    return g_memdup2(input, sizeof(RenderInput));
}

static void render_input_free(RenderInput *input)
{
    g_free(input);
}

static gboolean render_input_equal(const RenderInput *left,
    const RenderInput *right)
{
    if (left == NULL || right == NULL) {
        return left == right;
    }

    return fabs(left->scale - right->scale) < SCALE_EPSILON &&
        left->page_width == right->page_width &&
        left->page_height == right->page_height;
}

static void renderer_ensure_page_states(Renderer *renderer, Viewer *viewer)
{
    if (renderer->page_states == NULL) {
        renderer->page_count = viewer->info->n_pages;
        renderer->page_states = g_new0(RendererPageState,
            renderer->page_count);
        for (int i = 0; i < renderer->page_count; i++) {
            g_mutex_init(&renderer->page_states[i].mutex);
            renderer->page_states[i].status = RENDERER_PAGE_NOT_RENDERED;
        }
        return;
    }
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

static void draw_loading_page(cairo_t *cr, const ViewportRect *rect)
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
    cairo_text_extents(cr, "Loading...", &extents);
    cairo_move_to(cr,
        rect->x + (rect->width - extents.width) / 2 - extents.x_bearing,
        rect->y + (rect->height - extents.height) / 2 - extents.y_bearing);
    cairo_show_text(cr, "Loading...");
    cairo_restore(cr);
}
