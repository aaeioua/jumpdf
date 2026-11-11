#pragma once

#include "viewer.h"

typedef struct Renderer Renderer;

Renderer *renderer_new(GtkWidget *view);
void renderer_destroy(Renderer *renderer);

void renderer_draw(Renderer *renderer, cairo_t *cr, Viewer *viewer);
void renderer_render_visible_pages(Renderer *renderer, Viewer *viewer);
