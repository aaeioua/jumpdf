#pragma once

#include <gtk/gtk.h>

#include "viewer.h"

/* Cairo's image surface limit, in pixels on either side */
#define RENDERER_MAX_PAGE_PIXELS 32767

typedef struct Renderer Renderer;

/*
 * Draws `viewer`'s current Mark. Borrows `viewer`, which must outlive
 * it. Queues a draw of `view` when a render ends. `view` may be NULL.
 */
Renderer *renderer_new(GtkWidget *view, Viewer *viewer);
/* Waits for running renders to end */
void renderer_destroy(Renderer *renderer);
/* Moves `from`'s surfaces to `to` for a new version of the document */
void renderer_take_surfaces(Renderer *to, Renderer *from);

/*
 * Paints the frame for the current Mark, then requests renders of the pages
 * in view and pages that have no surface at the current scale.
 * Until its render arrives, a page shows its last surface scaled or a
 * placeholder. With `link_numbers`, paints the Link numbers over the frame.
 * Returns TRUE when rendering has settled: each of those pages had a surface
 * at the current scale, or had failed to render it, so this draw
 * requested nothing.
 */
gboolean renderer_draw(Renderer *renderer, cairo_t *cr, gboolean link_numbers);
