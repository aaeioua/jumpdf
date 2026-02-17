#include "renderer.h"
#include "config.h"
#include "utils.h"

#define SCALE_EPSILON 1e-6

typedef struct {
    int reset_from;
    int reset_to;
    int render_from;
    int render_to;
} RenderRequest;

typedef struct {
    Viewer *viewer;
    Page *page;
    unsigned int draw_links_from;
    unsigned int draw_links_to;
} RenderPageData;

static void renderer_draw_page(cairo_t *cr, Viewer *viewer, int page_idx, double *base);
static RenderRequest renderer_generate_request(Renderer *renderer, Viewer *viewer);
static void renderer_reset_pages(Viewer *viewer, int from, int to);
static void renderer_queue_page_render(Renderer *renderer, Viewer *viewer, Page* page, unsigned int* const draw_links_from, unsigned int* const draw_links_to);
static void render_page_async(gpointer data, gpointer user_data);
static void renderer_render_page(Renderer *renderer, Viewer *viewer, cairo_t *cr, PopplerPage *page, unsigned int draw_links_from, unsigned int draw_links_to);
static cairo_surface_t* create_loading_surface(int width, int height);
static void viewer_translate(Viewer *viewer, cairo_t *cr);
static void viewer_highlight_search(Viewer *viewer, cairo_t *cr, PopplerPage *page);
static void viewer_draw_links(Viewer *viewer, cairo_t *cr, unsigned int from, unsigned int to);

Renderer *renderer_new(GtkWidget *view)
{
    Renderer *renderer = g_new0(Renderer, 1);
    renderer_init(renderer, view);

    return renderer;
}

void renderer_init(Renderer *renderer, GtkWidget *view)
{
    renderer->view = view;

    GError *error = NULL;
    renderer->render_tp = g_thread_pool_new((GFunc)render_page_async, renderer, g_get_num_processors(), TRUE, &error);
    if (error != NULL) {
        g_warning("Failed to create render thread pool: %s", error->message);
        g_error_free(error);
    } else {
        g_mutex_init(&renderer->render_mutex);
    }

    renderer->last_visible_pages_before = -1;
    renderer->last_visible_pages_after = -1;
    renderer->last_scale = NAN;
    renderer->last_follow_links_mode = FALSE;
    renderer->last_search_text = NULL;
}

void renderer_destroy(Renderer *renderer)
{
    g_thread_pool_free(renderer->render_tp, FALSE, TRUE);
    g_mutex_clear(&renderer->render_mutex);

    g_free(renderer->last_search_text);
}

void renderer_draw(cairo_t *cr, Viewer *viewer)
{
    if (viewer->cursor->dark_mode) {
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }

    viewer_translate(viewer, cr);

    double base = 0;
    int from, to;
    viewer_cursor_get_visible_pages(viewer->cursor, &from, &to);
    for (int i = from; i <= to; i++) {
        renderer_draw_page(cr, viewer, i, &base);
    }

    if (viewer->cursor->dark_mode) {
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_paint(cr);
    }
}

void renderer_render_visible_pages(Renderer *renderer, Viewer *viewer)
{
    RenderRequest request = renderer_generate_request(renderer, viewer);

    renderer_reset_pages(viewer, request.reset_from, request.reset_to);
    renderer_render_pages(renderer, viewer, request.render_from, request.render_to);
}

void renderer_render_pages(Renderer *renderer, Viewer *viewer, int from, int to)
{
    unsigned int draw_links_from = 0;
    unsigned int draw_links_to = 0;

    if (from < 0 || to < 0) {
        return;
    }

    if (viewer->links->follow_links_mode) {
        viewer_links_clear_links(viewer->links);
    }

    for (int i = from; i <= to; i++) {
        renderer_queue_page_render(renderer, viewer, viewer->info->pages[i], &draw_links_from, &draw_links_to);
    }
}

static void renderer_draw_page(cairo_t *cr, Viewer *viewer, int page_idx, double *base)
{
    Page *page = viewer->info->pages[page_idx];
    cairo_surface_t *surface = NULL;
    bool is_surface_temporary = FALSE;

    double page_width, page_height;
    poppler_page_get_size(page->poppler_page, &page_width, &page_height);
    page_width *= viewer->cursor->scale;
    page_height *= viewer->cursor->scale;
    double center_offset = round((viewer->info->max_page_width * viewer->cursor->scale - page_width) / 2.0);

    if (page->render_status == PAGE_RENDERED) {
        g_mutex_lock(&page->render_mutex);
        g_assert(page->surface != NULL);
        surface = page->surface;
        g_mutex_unlock(&page->render_mutex);
    } else if (page->render_status == PAGE_RENDERING) {
        surface = create_loading_surface(page_width, page_height);
        is_surface_temporary = true;
    }

    cairo_set_source_surface(cr, surface, center_offset, *base);

    if (page_idx > 0) {
        /* Draw page separator */
        cairo_save(cr);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, center_offset, *base);
        cairo_rel_line_to(cr, page_width, 0);
        cairo_stroke(cr);
        cairo_restore(cr);
    }

    cairo_paint(cr);

    if (is_surface_temporary && surface != NULL) {
        cairo_surface_destroy(surface);
    }

    *base += page_height;
}

static RenderRequest renderer_generate_request(Renderer *renderer, Viewer *viewer)
{
    RenderRequest request = {
        .reset_from = -1,
        .reset_to = -1,
        .render_from = -1,
        .render_to = -1
    };

    int visible_pages_before, visible_pages_after;
    viewer_cursor_get_visible_pages(viewer->cursor, &visible_pages_before, &visible_pages_after);

    const bool visible_pages_invariant = visible_pages_before == renderer->last_visible_pages_before && visible_pages_after == renderer->last_visible_pages_after;
    const bool scale_invariant = fabs(viewer->cursor->scale - renderer->last_scale) < SCALE_EPSILON;
    const bool follow_links_mode_invariant = viewer->links->follow_links_mode == renderer->last_follow_links_mode;
    const bool search_text_invariant = g_strcmp0(viewer->search->search_text, renderer->last_search_text) == 0;

    const bool needs_rerender = !visible_pages_invariant || !scale_invariant || !follow_links_mode_invariant || !search_text_invariant;

    if (needs_rerender) {
        const bool visible_pages_subset_of_last =
            scale_invariant &&
            (renderer->last_visible_pages_before < visible_pages_before &&
            visible_pages_after < renderer->last_visible_pages_after);
        const bool scrolling_down =
            scale_invariant &&
            !visible_pages_subset_of_last &&
            renderer->last_visible_pages_before < visible_pages_before &&
            visible_pages_before <= renderer->last_visible_pages_after;
        const bool scrolling_up =
            scale_invariant &&
            !visible_pages_subset_of_last &&
            renderer->last_visible_pages_before <= visible_pages_after &&
            visible_pages_after < renderer->last_visible_pages_after;
        g_assert(!(scrolling_down && scrolling_up));
        
        if (scrolling_down) {
            request.reset_from = renderer->last_visible_pages_before;
            request.reset_to = visible_pages_before - 1;
            request.render_from = renderer->last_visible_pages_after + 1;
            request.render_to = visible_pages_after;
        } else if (scrolling_up) {
            request.reset_from = visible_pages_after + 1;
            request.reset_to = renderer->last_visible_pages_after;
            request.render_from = visible_pages_before;
            request.render_to = renderer->last_visible_pages_before - 1;
        } else {
            request.reset_from = renderer->last_visible_pages_before;
            request.reset_to = renderer->last_visible_pages_after;
            request.render_from = visible_pages_before;
            request.render_to = visible_pages_after;
        }

        renderer->last_visible_pages_before = visible_pages_before;
        renderer->last_visible_pages_after = visible_pages_after;
        renderer->last_scale = viewer->cursor->scale;
        renderer->last_follow_links_mode = viewer->links->follow_links_mode;
        g_free(renderer->last_search_text);
        renderer->last_search_text = g_strdup(viewer->search->search_text);
    }

    return request;
}

static void renderer_reset_pages(Viewer *viewer, int from, int to)
{
    if (from < 0 || to < 0) {
        return;
    }

    for (int i = from; i <= to; i++) {
        Page *page = viewer->info->pages[i];

        g_mutex_lock(&page->render_mutex);
        if (page->surface != NULL) {
            cairo_surface_destroy(page->surface);
            page->surface = NULL;
        }
        page->render_status = PAGE_NOT_RENDERED;
        g_mutex_unlock(&page->render_mutex);
    }
}

static void renderer_queue_page_render(Renderer *renderer, Viewer *viewer, Page* page, unsigned int* const draw_links_from, unsigned int* const draw_links_to)
{
    g_mutex_lock(&page->render_mutex);
    if (page->render_status == PAGE_NOT_RENDERED) {
        g_mutex_unlock(&page->render_mutex);

        if (viewer->links->follow_links_mode) {
            PopplerPage* poppler_page = page->poppler_page;
            *draw_links_from = *draw_links_to;
            *draw_links_to = *draw_links_from + viewer_links_get_links(viewer->links, poppler_page);
            g_assert(*draw_links_from <= *draw_links_to);
            g_assert(*draw_links_to == viewer->links->visible_links->len);
        }

        RenderPageData* data = g_new0(RenderPageData, 1);
        data->viewer = viewer;
        data->page = page;
        data->draw_links_from = *draw_links_from;
        data->draw_links_to = *draw_links_to;

        GError *error = NULL;
        g_thread_pool_push(renderer->render_tp, data, &error);
        if (error != NULL) {
            g_warning("Failed to push render task to thread pool: %s", error->message);
            g_error_free(error);
            g_free(data);
        } else {
            g_mutex_lock(&page->render_mutex);
            page->render_status = PAGE_RENDERING;
            g_mutex_unlock(&page->render_mutex);
        }
    } else {
        g_mutex_unlock(&page->render_mutex);
    }
}

static void render_page_async(gpointer data, gpointer user_data)
{
    RenderPageData *render_page_data = (RenderPageData *)data;
    Viewer *viewer = render_page_data->viewer;
    Page *page = render_page_data->page;
    unsigned int draw_links_from = render_page_data->draw_links_from;
    unsigned int draw_links_to = render_page_data->draw_links_to;
    Renderer *renderer = (Renderer *)user_data;
    GtkWidget *view = renderer->view;

    double width, height;
    poppler_page_get_size(page->poppler_page, &width, &height);

    const double scale = viewer->cursor->scale;
    const int scaled_width = (int)(scale * width);
    const int scaled_height = (int)(scale * height);

    cairo_surface_t *page_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, scaled_width, scaled_height);
    cairo_t *cr = cairo_create(page_surface);

    cairo_scale(cr, scale, scale);

    g_mutex_lock(&page->render_mutex);

    renderer_render_page(renderer, viewer, cr, page->poppler_page, draw_links_from, draw_links_to);

    if (page->surface != NULL) {
        cairo_surface_destroy(page->surface);
    }
    page->surface = page_surface;
    page->render_status = PAGE_RENDERED;

    g_mutex_unlock(&page->render_mutex);

    g_idle_add_once((GSourceOnceFunc)gtk_widget_queue_draw, view);

    cairo_destroy(cr);
    g_free(render_page_data);
}

static void renderer_render_page(Renderer *renderer, Viewer *viewer, cairo_t *cr, PopplerPage *page, unsigned int draw_links_from, unsigned int draw_links_to)
{
    double width, height;
    poppler_page_get_size(page, &width, &height);

    // Clear to white background (for PDFs with missing background)
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    /* poppler_page_render is not thread-safe
    * https://gitlab.freedesktop.org/poppler/poppler/-/issues/1503
    */
    g_mutex_lock(&renderer->render_mutex);
    poppler_page_render(page, cr);
    g_mutex_unlock(&renderer->render_mutex);

    viewer_highlight_search(viewer, cr, page);
    viewer_draw_links(viewer, cr, draw_links_from, draw_links_to);
}

static cairo_surface_t* create_loading_surface(int width, int height)
{
    cairo_surface_t *loading_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t *cr = cairo_create(loading_surface);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
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

static void viewer_translate(Viewer *viewer, cairo_t *cr)
{
    if (viewer->cursor->center_mode) {
        viewer_cursor_center(viewer->cursor);
    }

    const double x_offset = viewer->cursor->x_offset;
    const double y_offset = viewer->cursor->y_offset;
    const double scale = viewer->cursor->scale;
    const double page_width = viewer->info->max_page_width;
    const double page_height = viewer->info->max_page_height;
    const double view_width = viewer->info->view_width;
    const double view_height = viewer->info->view_height;

    int visible_from, visible_to;
    viewer_cursor_get_visible_pages(viewer->cursor, &visible_from, &visible_to);
    const int page_offset_idx = viewer->cursor->current_page - visible_from;
    g_assert(page_offset_idx >= 0 && page_offset_idx <= visible_to - visible_from);

    const double page_center_x = scale * (page_width / 2.0);
    const double page_center_y = scale * (page_height / 2.0);
    const double view_center_x = view_width / 2.0;
    const double view_center_y = view_height / 2.0;

    const double x_center_translate = view_center_x - page_center_x;
    const double x_offset_translate = (x_offset / g_config->steps) * page_width;
    const double x_translate = round(x_center_translate + x_offset_translate);

    const double y_page_translate = -page_offset_idx * page_height * scale;
    const double y_center_translate = view_center_y - page_center_y;
    const double y_offset_translate = -(y_offset / g_config->steps) * page_height * scale;
    const double y_translate = round(y_page_translate + y_center_translate + y_offset_translate);

    cairo_translate(cr, x_translate, y_translate);
}

static void viewer_highlight_search(Viewer *viewer, cairo_t *cr, PopplerPage *page)
{
    PopplerRectangle *highlight_rect;
    double highlight_rect_x, highlight_rect_y, highlight_rect_width,
        highlight_rect_height;
    GList *matches;

    if (!viewer->search->search_text) {
        return;
    }

    matches = poppler_page_find_text(page, viewer->search->search_text);
    for (GList *elem = matches; elem; elem = elem->next) {
        highlight_rect = elem->data;
        highlight_rect_x = highlight_rect->x1;
        highlight_rect_y = viewer->info->max_page_height - highlight_rect->y1;
        highlight_rect_width = highlight_rect->x2 - highlight_rect->x1;
        highlight_rect_height = highlight_rect->y1 - highlight_rect->y2;

        cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.5);
        cairo_rectangle(cr, highlight_rect_x, highlight_rect_y, highlight_rect_width,
            highlight_rect_height);
        cairo_fill(cr);
    }

    g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);
}

static void viewer_draw_links(Viewer *viewer, cairo_t *cr, unsigned int from, unsigned int to)
{
    PopplerLinkMapping *link_mapping = NULL;
    char *link_text = NULL;

    g_assert(to <= viewer->links->visible_links->len);
    if (!viewer->links->follow_links_mode) {
        return;
    }

    for (unsigned int i = from; i < to; i++) {
        link_mapping = g_ptr_array_index(viewer->links->visible_links, i);
        link_text = g_strdup_printf("%d", i + 1);

        // Outline
        cairo_move_to(cr, link_mapping->area.x1, viewer->info->max_page_height - link_mapping->area.y1);
        cairo_text_path(cr, link_text);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_set_line_width(cr, 2.0);
        cairo_stroke_preserve(cr);

        // Actual text
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_fill(cr);

        g_free(link_text);
    }
}