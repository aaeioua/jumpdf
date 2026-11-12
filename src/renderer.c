#include "renderer.h"

#include <math.h>
#include <string.h>

#define SCALE_EPSILON 1e-6
#define PREFETCH_PAGES 1

typedef enum {
    RENDERER_PAGE_NOT_RENDERED,
    RENDERER_PAGE_RENDERING,
    RENDERER_PAGE_RENDERED
} RendererPageStatus;

typedef struct {
    double x1;
    double y1;
    double x2;
    double y2;
} RenderLink;

typedef struct {
    double scale;
    double page_width;
    double page_height;
    char *search_text;
    guint link_offset;
    guint n_links;
    RenderLink *links;
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
    PopplerPage *poppler_page;
    guint64 generation;
    RenderInput *input;
} RenderJob;

struct Renderer {
    GWeakRef redraw_target;
    GThreadPool *render_pool;
    GMutex pool_mutex;
    GMutex poppler_render_mutex;
    gint shutting_down;

    RendererPageState *page_states;
    int page_count;
    int last_requested_from;
    int last_requested_to;
};

static void renderer_draw_page(Renderer *renderer, cairo_t *cr,
    const Viewport *viewport, const ViewportLayout *layout, int page_idx);
static void renderer_init(Renderer *renderer, GtkWidget *view);
static void renderer_reset_page(RendererPageState *page_state);
static void renderer_prefetch_page(Renderer *renderer, Viewer *viewer,
    const ViewportLayout *layout, int page_idx);
static void renderer_clear_surface_locked(RendererPageState *page_state);
static void renderer_request_page(Renderer *renderer,
    RendererPageState *page_state, PopplerPage *poppler_page,
    RenderInput *input);
static RenderJob *renderer_create_job_locked(RendererPageState *page_state,
    PopplerPage *poppler_page);
static void render_job_free(RenderJob *job);
static void renderer_push_job(Renderer *renderer, RenderJob *job);
static void render_page_async(gpointer data, gpointer user_data);
static void renderer_render_page(Renderer *renderer, cairo_t *cr,
    PopplerPage *page, const RenderInput *input);
static cairo_surface_t *create_loading_surface(int width, int height);
static void viewer_highlight_search(cairo_t *cr, PopplerPage *page,
    const RenderInput *input);
static void viewer_draw_links(cairo_t *cr, const RenderInput *input);

static RenderInput *render_input_new(Viewer *viewer,
    const ViewportLayout *layout, int page_idx, guint link_offset,
    guint n_links);
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
    g_mutex_init(&renderer->poppler_render_mutex);
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
    g_mutex_clear(&renderer->poppler_render_mutex);
    g_mutex_clear(&renderer->pool_mutex);
    g_free(renderer);
}

void renderer_draw(Renderer *renderer, cairo_t *cr, Viewer *viewer)
{
    renderer_ensure_page_states(renderer, viewer);

    if (viewer->cursor->dark_mode) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }

    ViewportLayout layout = viewport_layout(viewer->viewport, viewer->cursor);
    for (int i = layout.from; i <= layout.to; i++) {
        renderer_draw_page(renderer, cr, viewer->viewport, &layout, i);
    }

    if (viewer->cursor->dark_mode) {
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }
}

void renderer_render_visible_pages(Renderer *renderer, Viewer *viewer)
{
    renderer_ensure_page_states(renderer, viewer);

    ViewportLayout layout = viewport_layout(viewer->viewport, viewer->cursor);
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

    viewer_links_clear_links(viewer->links);
    for (int i = visible_from; i <= visible_to; i++) {
        PopplerPage *page = viewer_info_get_poppler_page(viewer->info, i);
        guint link_offset = viewer->links->visible_links->len;
        guint n_links = 0;

        if (viewer->links->follow_links_mode) {
            n_links = viewer_links_get_links(viewer->links, page);
            g_assert(viewer->links->visible_links->len == link_offset + n_links);
        }

        RenderInput *input = render_input_new(viewer, &layout, i,
            link_offset, n_links);
        renderer_request_page(renderer, &renderer->page_states[i], page,
            input);
    }

    for (int i = visible_to + 1; i <= requested_to; i++) {
        renderer_prefetch_page(renderer, viewer, &layout, i);
    }
    for (int i = visible_from - 1; i >= requested_from; i--) {
        renderer_prefetch_page(renderer, viewer, &layout, i);
    }
}

static void renderer_prefetch_page(Renderer *renderer, Viewer *viewer,
    const ViewportLayout *layout, int page_idx)
{
    PopplerPage *page = viewer_info_get_poppler_page(viewer->info, page_idx);
    RenderInput *input = render_input_new(viewer, layout, page_idx, 0, 0);
    renderer_request_page(renderer, &renderer->page_states[page_idx], page,
        input);
}

static void renderer_draw_page(Renderer *renderer, cairo_t *cr,
    const Viewport *viewport, const ViewportLayout *layout, int page_idx)
{
    const ViewportRect rect = viewport_layout_page_rect(viewport, layout,
        page_idx);

    cairo_surface_t *surface = NULL;
    gboolean is_cached = FALSE;
    RendererPageState *page_state = &renderer->page_states[page_idx];
    g_mutex_lock(&page_state->mutex);
    if (page_state->status == RENDERER_PAGE_RENDERED &&
        page_state->surface != NULL) {
        surface = page_state->surface;
        is_cached = TRUE;
    } else {
        g_mutex_unlock(&page_state->mutex);
        surface = create_loading_surface(MAX(1, (int)rect.width),
            MAX(1, (int)rect.height));
    }

    cairo_set_source_surface(cr, surface, rect.x, rect.y);
    cairo_paint(cr);

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

    if (is_cached) {
        g_mutex_unlock(&page_state->mutex);
    } else {
        cairo_surface_destroy(surface);
    }
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

static void renderer_request_page(Renderer *renderer,
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
        job = renderer_create_job_locked(page_state, poppler_page);
    }

    g_mutex_unlock(&page_state->mutex);

    if (job != NULL) {
        renderer_push_job(renderer, job);
    }
}

static RenderJob *renderer_create_job_locked(RendererPageState *page_state,
    PopplerPage *poppler_page)
{
    RenderJob *job = g_new0(RenderJob, 1);
    job->page_state = page_state;
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
    renderer_render_page(renderer, cr, job->poppler_page, input);
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
            retry_job = renderer_create_job_locked(page_state,
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

static void renderer_render_page(Renderer *renderer, cairo_t *cr,
    PopplerPage *page, const RenderInput *input)
{
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, 0, 0, input->page_width, input->page_height);
    cairo_fill(cr);

    /* poppler_page_render is not thread-safe
     * https://gitlab.freedesktop.org/poppler/poppler/-/issues/1503
     */
    g_mutex_lock(&renderer->poppler_render_mutex);
    poppler_page_render(page, cr);
    g_mutex_unlock(&renderer->poppler_render_mutex);

    viewer_highlight_search(cr, page, input);
    viewer_draw_links(cr, input);
}

static RenderInput *render_input_new(Viewer *viewer,
    const ViewportLayout *layout, int page_idx, guint link_offset,
    guint n_links)
{
    const ViewportSize page_size = viewport_get_page_size(viewer->viewport,
        page_idx);

    RenderInput *input = g_new0(RenderInput, 1);
    input->scale = layout->scale;
    input->page_width = page_size.width;
    input->page_height = page_size.height;
    input->search_text = g_strdup(viewer->search->search_text);
    input->link_offset = link_offset;
    input->n_links = n_links;

    if (n_links > 0) {
        input->links = g_new0(RenderLink, n_links);
        for (guint i = 0; i < n_links; i++) {
            PopplerLinkMapping *mapping = g_ptr_array_index(
                viewer->links->visible_links, link_offset + i);
            input->links[i] = (RenderLink){
                .x1 = mapping->area.x1,
                .y1 = mapping->area.y1,
                .x2 = mapping->area.x2,
                .y2 = mapping->area.y2
            };
        }
    }

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

    RenderInput *copy = g_new0(RenderInput, 1);
    *copy = *input;
    copy->search_text = g_strdup(input->search_text);
    copy->links = NULL;
    if (input->n_links > 0) {
        copy->links = g_memdup2(input->links,
            sizeof(RenderLink) * input->n_links);
    }

    return copy;
}

static void render_input_free(RenderInput *input)
{
    if (input == NULL) {
        return;
    }

    g_free(input->search_text);
    g_free(input->links);
    g_free(input);
}

static gboolean render_input_equal(const RenderInput *left,
    const RenderInput *right)
{
    if (left == NULL || right == NULL) {
        return left == right;
    }

    if (fabs(left->scale - right->scale) >= SCALE_EPSILON ||
        left->page_width != right->page_width ||
        left->page_height != right->page_height ||
        g_strcmp0(left->search_text, right->search_text) != 0 ||
        left->link_offset != right->link_offset ||
        left->n_links != right->n_links) {
        return FALSE;
    }

    for (guint i = 0; i < left->n_links; i++) {
        if (left->links[i].x1 != right->links[i].x1 ||
            left->links[i].y1 != right->links[i].y1 ||
            left->links[i].x2 != right->links[i].x2 ||
            left->links[i].y2 != right->links[i].y2) {
            return FALSE;
        }
    }

    return TRUE;
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

static cairo_surface_t *create_loading_surface(int width, int height)
{
    cairo_surface_t *loading_surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, MAX(1, width), MAX(1, height));
    cairo_t *cr = cairo_create(loading_surface);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
        CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 32);

    cairo_text_extents_t extents;
    cairo_text_extents(cr, "Loading...", &extents);
    double x = (width - extents.width) / 2 - extents.x_bearing;
    double y = (height - extents.height) / 2 - extents.y_bearing;

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, "Loading...");

    cairo_destroy(cr);

    return loading_surface;
}

static void viewer_highlight_search(cairo_t *cr, PopplerPage *page,
    const RenderInput *input)
{
    if (input->search_text == NULL) {
        return;
    }

    GList *matches = poppler_page_find_text(page, input->search_text);
    for (GList *elem = matches; elem; elem = elem->next) {
        PopplerRectangle *highlight_rect = elem->data;
        double x = highlight_rect->x1;
        double y = input->page_height - highlight_rect->y1;
        double width = highlight_rect->x2 - highlight_rect->x1;
        double height = highlight_rect->y1 - highlight_rect->y2;

        cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.5);
        cairo_rectangle(cr, x, y, width, height);
        cairo_fill(cr);
    }

    g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);
}

static void viewer_draw_links(cairo_t *cr, const RenderInput *input)
{
    for (guint i = 0; i < input->n_links; i++) {
        const RenderLink *link = &input->links[i];
        char *link_text = g_strdup_printf("%u", input->link_offset + i + 1);

        cairo_move_to(cr, link->x1,
            input->page_height - link->y1);
        cairo_text_path(cr, link_text);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_fill(cr);
        g_free(link_text);
    }
}
