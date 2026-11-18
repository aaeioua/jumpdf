#include "viewport.h"

#include <glib.h>
#include <math.h>

struct Viewport {
    ViewportSize *page_sizes;
    int n_pages;
    ViewportRules rules;
    double max_scale;
    double view_width;
    double view_height;
};

static bool rules_are_valid(const ViewportRules *rules);
static int clamp_page(const Viewport *viewport, int page);
static void bring_into_range(const Viewport *viewport, ViewerCursor *mark);
static double span_from(const Viewport *viewport, int page, double y_offset);
static void place_below_centre(const Viewport *viewport, ViewerCursor *mark,
    int page, double distance);
static void visible_range(const Viewport *viewport, int current_page,
    double current_top, ViewportLayout *layout);

Viewport *viewport_new(const ViewportSize *page_sizes, int n_pages,
    const ViewportRules *rules)
{
    if (page_sizes == NULL || n_pages <= 0 || !rules_are_valid(rules)) {
        return NULL;
    }

    Viewport *viewport = g_new0(Viewport, 1);
    viewport->page_sizes = g_memdup2(page_sizes,
        sizeof(ViewportSize) * n_pages);
    viewport->n_pages = n_pages;
    viewport->rules = *rules;

    double largest_side = 0;
    for (int i = 0; i < n_pages; i++) {
        largest_side = MAX(largest_side,
            MAX(page_sizes[i].width, page_sizes[i].height));
    }
    viewport->max_scale = largest_side > 0
        ? rules->max_page_pixels / largest_side
        : G_MAXDOUBLE;

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

ViewportSize viewport_get_view_size(const Viewport *viewport)
{
    return (ViewportSize){
        .width = viewport->view_width,
        .height = viewport->view_height
    };
}

ViewportSize viewport_get_page_size(const Viewport *viewport, int page)
{
    return viewport->page_sizes[clamp_page(viewport, page)];
}

ViewportLayout viewport_layout(const Viewport *viewport,
    const ViewerCursor *mark)
{
    const int steps = viewport->rules.steps;
    const int current_page = clamp_page(viewport, mark->current_page);
    const double scale = mark->scale;
    const double x_offset = mark->center_mode ? 0.0 : mark->x_offset;

    const double current_height = viewport->page_sizes[current_page].height;
    const double span = span_from(viewport, current_page, mark->y_offset);
    const double current_top = round(viewport->view_height / 2.0 -
        scale * (current_height / 2.0 + mark->y_offset / steps * span));

    ViewportLayout layout = { .scale = scale };
    visible_range(viewport, current_page, current_top, &layout);

    for (int i = layout.from; i <= layout.to; i++) {
        layout.max_page_width = MAX(layout.max_page_width,
            viewport->page_sizes[i].width);
    }

    const double max_width = layout.max_page_width;
    layout.column_left = round(viewport->view_width / 2.0 -
        scale * max_width / 2.0 + (x_offset / steps) * max_width);

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

void viewport_scroll(const Viewport *viewport, ViewerCursor *mark,
    double dx_steps, double dy_steps)
{
    if (!mark->center_mode) {
        mark->x_offset += dx_steps;
    }
    mark->y_offset += dy_steps;
    bring_into_range(viewport, mark);
}

void viewport_scroll_pages(const Viewport *viewport, ViewerCursor *mark,
    double pages)
{
    viewport_scroll(viewport, mark, 0, pages * viewport->rules.steps);
}

void viewport_zoom(const Viewport *viewport, ViewerCursor *mark,
    double zoom_steps)
{
    const double scale = mark->scale + zoom_steps * viewport->rules.scale_step;

    /* Zooming out of a fit below min_scale stays where it is */
    mark->scale = zoom_steps < 0
        ? MAX(scale, MIN(mark->scale, viewport->rules.min_scale))
        : scale;
    bring_into_range(viewport, mark);
}

void viewport_reset_zoom(const Viewport *viewport, ViewerCursor *mark)
{
    mark->scale = MAX(1.0, viewport->rules.min_scale);
    bring_into_range(viewport, mark);
}

void viewport_fit_width(const Viewport *viewport, ViewerCursor *mark)
{
    ViewportLayout layout = viewport_layout(viewport, mark);
    if (viewport->view_width > 0 && layout.max_page_width > 0) {
        mark->scale = viewport->view_width / layout.max_page_width;
    }
    mark->x_offset = 0;
    bring_into_range(viewport, mark);
}

void viewport_fit_height(const Viewport *viewport, ViewerCursor *mark)
{
    const ViewportSize page = viewport_get_page_size(viewport,
        mark->current_page);
    if (viewport->view_height > 0 && page.height > 0) {
        mark->scale = viewport->view_height / page.height;
    }
    mark->x_offset = 0;
    bring_into_range(viewport, mark);
}

void viewport_goto_page(const Viewport *viewport, ViewerCursor *mark, int page)
{
    mark->current_page = clamp_page(viewport, page);
    mark->y_offset = 0;
    bring_into_range(viewport, mark);
}

void viewport_settle(const Viewport *viewport, ViewerCursor *mark)
{
    bring_into_range(viewport, mark);
}

void viewport_goto_dest(const Viewport *viewport, ViewerCursor *mark,
    int page, bool has_top, double top)
{
    mark->current_page = clamp_page(viewport, page);
    mark->y_offset = 0;
    viewport_fit_height(viewport, mark);

    const double height = viewport->page_sizes[mark->current_page].height;

    /* Sanity check for PDFs with invalid top values */
    if (has_top && top < height) {
        place_below_centre(viewport, mark, mark->current_page, height - top);
    }
}

static bool rules_are_valid(const ViewportRules *rules)
{
    return rules != NULL && rules->steps > 0 &&
        isfinite(rules->min_scale) && rules->min_scale > 0 &&
        isfinite(rules->scale_step) && rules->scale_step > 0 &&
        rules->max_page_pixels > 0;
}

static int clamp_page(const Viewport *viewport, int page)
{
    return CLAMP(page, 0, viewport->n_pages - 1);
}

/*
 * Ends every motion function: clamps the vertical position, as one number, to
 * half a page beyond either end of the document and splits it back into a
 * page and an offset. Also caps the scale and centres in center mode.
 */
static void bring_into_range(const Viewport *viewport, ViewerCursor *mark)
{
    const int steps = viewport->rules.steps;
    const int last = viewport->n_pages - 1;

    mark->current_page = clamp_page(viewport, mark->current_page);

    const double position = CLAMP(
        mark->current_page * (double)steps + mark->y_offset,
        -steps / 2.0, last * (double)steps + steps / 2.0);
    int page = CLAMP((int)floor(position / steps), 0, last);
    double y_offset = position - page * (double)steps;

    /* The division may round across a page boundary */
    if (y_offset < 0 && page > 0) {
        page--;
        y_offset += steps;
    } else if (y_offset >= steps && page < last) {
        page++;
        y_offset -= steps;
    }

    mark->current_page = page;
    mark->y_offset = y_offset;
    mark->scale = MIN(mark->scale, viewport->max_scale);

    if (mark->center_mode) {
        mark->x_offset = 0;
    }
}

/*
 * How far the view's centre moves while y_offset goes from 0 to `steps` on
 * `page`: from its centre to the next page's centre. On the last page, and
 * before the first page's centre, the page's own height.
 */
static double span_from(const Viewport *viewport, int page, double y_offset)
{
    const double height = viewport->page_sizes[page].height;

    if (y_offset < 0 || page == viewport->n_pages - 1) {
        return height;
    }

    return (height + viewport->page_sizes[page + 1].height) / 2.0;
}

/* Puts the view's centre `distance` (>= 0) points below `page`'s centre */
static void place_below_centre(const Viewport *viewport, ViewerCursor *mark,
    int page, double distance)
{
    double span = span_from(viewport, page, distance);

    while (page < viewport->n_pages - 1 && distance >= span) {
        distance -= span;
        page++;
        span = span_from(viewport, page, distance);
    }

    mark->current_page = page;
    mark->y_offset = distance / span * viewport->rules.steps;
    bring_into_range(viewport, mark);
}

static void visible_range(const Viewport *viewport, int current_page,
    double current_top, ViewportLayout *layout)
{
    const double scale = layout->scale;

    int from = current_page;
    double top = current_top;
    while (from > 0 && top > 0) {
        from--;
        top -= scale * viewport->page_sizes[from].height;
    }

    int to = current_page;
    double bottom = current_top +
        scale * viewport->page_sizes[current_page].height;
    while (to < viewport->n_pages - 1 && bottom < viewport->view_height) {
        to++;
        bottom += scale * viewport->page_sizes[to].height;
    }

    layout->from = from;
    layout->to = to;
    layout->stack_top = top;
}
