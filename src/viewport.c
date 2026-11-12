#include "viewport.h"
#include "config.h"

#include <glib.h>
#include <math.h>

struct Viewport {
    ViewportSize *page_sizes;
    int n_pages;
    double min_page_height;
    double view_width;
    double view_height;
};

static int clamp_page(const Viewport *viewport, int page);
static void visible_range(const Viewport *viewport, int current_page,
    double scale, int *from, int *to);

Viewport *viewport_new(const ViewportSize *page_sizes, int n_pages)
{
    g_return_val_if_fail(page_sizes != NULL && n_pages > 0, NULL);

    Viewport *viewport = g_new0(Viewport, 1);
    viewport->page_sizes = g_memdup2(page_sizes,
        sizeof(ViewportSize) * n_pages);
    viewport->n_pages = n_pages;

    viewport->min_page_height = page_sizes[0].height;
    for (int i = 1; i < n_pages; i++) {
        viewport->min_page_height = MIN(viewport->min_page_height,
            page_sizes[i].height);
    }

    return viewport;
}

void viewport_destroy(Viewport *viewport)
{
    if (viewport == NULL) {
        return;
    }

    g_free(viewport->page_sizes);
    g_free(viewport);
}

void viewport_set_view_size(Viewport *viewport, double width, double height)
{
    viewport->view_width = width;
    viewport->view_height = height;
}

ViewportSize viewport_get_page_size(const Viewport *viewport, int page)
{
    return viewport->page_sizes[clamp_page(viewport, page)];
}

ViewportLayout viewport_layout(const Viewport *viewport,
    const ViewerCursor *cursor)
{
    const int steps = g_config->steps;
    const int current_page = clamp_page(viewport, cursor->current_page);
    const double scale = cursor->scale;
    const double x_offset = cursor->center_mode ? 0.0 : cursor->x_offset;

    ViewportLayout layout = { .scale = scale };
    visible_range(viewport, current_page, scale, &layout.from, &layout.to);

    for (int i = layout.from; i <= layout.to; i++) {
        layout.max_page_width = MAX(layout.max_page_width,
            viewport->page_sizes[i].width);
        layout.max_page_height = MAX(layout.max_page_height,
            viewport->page_sizes[i].height);
    }

    const double max_width = layout.max_page_width;
    const double max_height = layout.max_page_height;

    layout.column_left = round(viewport->view_width / 2.0 -
        scale * max_width / 2.0 + (x_offset / steps) * max_width);

    double above = 0;
    for (int i = layout.from; i < current_page; i++) {
        above += viewport->page_sizes[i].height * scale;
    }
    layout.stack_top = round(viewport->view_height / 2.0 -
        scale * max_height / 2.0 -
        (cursor->y_offset / steps) * max_height * scale - above);

    return layout;
}

ViewportRect viewport_layout_page_rect(const Viewport *viewport,
    const ViewportLayout *layout, int page)
{
    g_assert(page >= layout->from && page <= layout->to);

    double y = layout->stack_top;
    for (int i = layout->from; i < page; i++) {
        y += viewport->page_sizes[i].height * layout->scale;
    }

    const ViewportSize size = viewport->page_sizes[page];
    const double width = size.width * layout->scale;
    const double center_offset = round(
        (layout->max_page_width * layout->scale - width) / 2.0);

    return (ViewportRect){
        .x = layout->column_left + center_offset,
        .y = y,
        .width = width,
        .height = size.height * layout->scale
    };
}

void viewport_fit_width(const Viewport *viewport, ViewerCursor *cursor)
{
    ViewportLayout layout = viewport_layout(viewport, cursor);
    if (viewport->view_width > 0 && layout.max_page_width > 0) {
        cursor->scale = viewport->view_width / layout.max_page_width;
    }
    viewer_cursor_center(cursor);
}

void viewport_fit_height(const Viewport *viewport, ViewerCursor *cursor)
{
    ViewportLayout layout = viewport_layout(viewport, cursor);
    if (viewport->view_height > 0 && layout.max_page_height > 0) {
        cursor->scale = viewport->view_height / layout.max_page_height;
    }
    viewer_cursor_center(cursor);
}

void viewport_goto_page(const Viewport *viewport, ViewerCursor *cursor, int page)
{
    cursor->current_page = clamp_page(viewport, page);
    cursor->y_offset = 0;
    viewport_fit_height(viewport, cursor);
}

void viewport_goto_page_top(const Viewport *viewport, ViewerCursor *cursor,
    int page, bool has_top, double top)
{
    const int steps = g_config->steps;

    cursor->current_page = clamp_page(viewport, page);
    ViewportLayout layout = viewport_layout(viewport, cursor);

    /* Sanity check for PDFs with invalid top values */
    if (!has_top || top >= layout.max_page_height) {
        viewport_goto_page(viewport, cursor, page);
        return;
    }

    /* Top is relative to the bottom of the page */
    cursor->y_offset = steps - steps * (top / layout.max_page_height);
    viewport_fit_height(viewport, cursor);
}

static int clamp_page(const Viewport *viewport, int page)
{
    return CLAMP(page, 0, viewport->n_pages - 1);
}

static void visible_range(const Viewport *viewport, int current_page,
    double scale, int *from, int *to)
{
    /*
     * Use the smallest page height in the document so the range is never
     * too short, whichever pages happen to be on screen.
     */
    const double page_height = scale * viewport->min_page_height;
    const double visible_pages = page_height > 0
        ? MIN(viewport->view_height / page_height, (double)viewport->n_pages)
        : 0;
    const int half = (int)ceil(visible_pages / 2.0);

    *from = MAX(0, current_page - half);
    *to = MIN(viewport->n_pages - 1, current_page + half + 1);
}
