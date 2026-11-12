#pragma once

#include <stdbool.h>

#include "viewer_cursor.h"

typedef struct {
    double width;
    double height;
} ViewportSize;

typedef struct {
    double x;
    double y;
    double width;
    double height;
} ViewportRect;

typedef struct {
    int from;
    int to;
    double scale;
    /* Widest page within [from, to], unscaled */
    double max_page_width;
    /* View position of the top edge of page `from` */
    double stack_top;
    /* View position of the left edge of a page with width max_page_width */
    double column_left;
} ViewportLayout;

typedef struct Viewport Viewport;

/* Copies `page_sizes` (unscaled, PDF points). `n_pages` must be > 0. */
Viewport *viewport_new(const ViewportSize *page_sizes, int n_pages);
void viewport_destroy(Viewport *viewport);

/* The view size is 0x0 until set. Fitting is a no-op while it is empty. */
void viewport_set_view_size(Viewport *viewport, double width, double height);

ViewportSize viewport_get_page_size(const Viewport *viewport, int page);

ViewportLayout viewport_layout(const Viewport *viewport, const ViewerCursor *cursor);
/* `page` must lie within [layout->from, layout->to]. */
ViewportRect viewport_layout_page_rect(const Viewport *viewport,
    const ViewportLayout *layout, int page);

void viewport_fit_width(const Viewport *viewport, ViewerCursor *cursor);
void viewport_fit_height(const Viewport *viewport, ViewerCursor *cursor);
void viewport_goto_page(const Viewport *viewport, ViewerCursor *cursor, int page);
/*
 * `top` is measured from the bottom of the page, as in PopplerDest.
 * Pass has_top = FALSE when the destination does not specify one.
 */
void viewport_goto_page_top(const Viewport *viewport, ViewerCursor *cursor,
    int page, bool has_top, double top);
