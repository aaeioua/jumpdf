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

typedef struct {
    int steps;            /* > 0 */
    double min_scale;     /* finite, > 0 */
    double scale_step;    /* finite, > 0 */
    int max_page_pixels;  /* > 0: the most pixels a page may take on either side */
} ViewportRules;

typedef struct Viewport Viewport;

/*
 * Copies `page_sizes` (unscaled, PDF points) and `rules`. NULL if
 * n_pages <= 0 or the rules are invalid.
 */
Viewport *viewport_new(const ViewportSize *page_sizes, int n_pages,
    const ViewportRules *rules);
void viewport_destroy(Viewport *viewport);

/* The view size is 0x0 until set. Fitting is a no-op while it is empty. */
void viewport_set_view_size(Viewport *viewport, double width, double height);
ViewportSize viewport_get_view_size(const Viewport *viewport);

ViewportSize viewport_get_page_size(const Viewport *viewport, int page);

ViewportLayout viewport_layout(const Viewport *viewport, const ViewerCursor *mark);
/* `page` must lie within [layout->from, layout->to]. */
ViewportRect viewport_layout_page_rect(const Viewport *viewport,
    const ViewportLayout *layout, int page);

/*
 * Mark motion. Each call leaves the Mark in range: on a page of the document,
 * no further than half a page beyond either end, and with x_offset 0 in
 * center mode.
 */

/* `dx_steps` has the sign of x_offset and only applies outside center mode */
void viewport_scroll(const Viewport *viewport, ViewerCursor *mark,
    double dx_steps, double dy_steps);
/* Scrolls `pages` * steps */
void viewport_scroll_pages(const Viewport *viewport, ViewerCursor *mark,
    double pages);
/*
 * Changes the scale by zoom_steps * scale_step, up to max_scale: the scale at
 * which the document's largest page side takes max_page_pixels. Zooming out
 * stops at min_scale, or where it started if that was already below.
 */
void viewport_zoom(const Viewport *viewport, ViewerCursor *mark,
    double zoom_steps);
/* Sets the scale to 1 within [min_scale, max_scale] */
void viewport_reset_zoom(const Viewport *viewport, ViewerCursor *mark);
/* A fit may go below min_scale, but never above max_scale */
void viewport_fit_width(const Viewport *viewport, ViewerCursor *mark);
void viewport_fit_height(const Viewport *viewport, ViewerCursor *mark);
/* Centres the page, clamped to the document, and keeps the zoom */
void viewport_goto_page(const Viewport *viewport, ViewerCursor *mark, int page);
/*
 * Fits the page's height, then puts the destination line at the view's top,
 * or centres the page without one. `top` is measured from the bottom of the
 * page, as in PopplerDest; one at or beyond the page's height counts as none.
 */
void viewport_goto_dest(const Viewport *viewport, ViewerCursor *mark,
    int page, bool has_top, double top);
/*
 * Brings a Mark from elsewhere, such as the Mark store, into range: clamps
 * its page and position, caps its scale at max_scale (keeping one below
 * min_scale) and centres it in center mode. A Mark in range is unchanged.
 */
void viewport_settle(const Viewport *viewport, ViewerCursor *mark);
