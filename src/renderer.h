#pragma once

#include "viewer.h"

/* Cairo's image surface limit, in pixels on either side */
#define RENDERER_MAX_PAGE_PIXELS 32767

typedef struct Renderer Renderer;

Renderer *renderer_new(GtkWidget *view);
void renderer_destroy(Renderer *renderer);

void renderer_draw(Renderer *renderer, cairo_t *cr, Viewer *viewer);
void renderer_render_visible_pages(Renderer *renderer, Viewer *viewer);
