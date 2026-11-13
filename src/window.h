#pragma once

#include <gtk/gtk.h>

#include "viewer_mark_manager.h"
#include "viewer.h"
#include "input.h"

typedef struct _App App;

#define WINDOW_TYPE (window_get_type())
G_DECLARE_FINAL_TYPE(Window, window, JUMPDF, WINDOW, GtkApplicationWindow)

Window *window_new(App *app);
void window_open(Window *win, GFile *file, ViewerMarkManager *mark_manager,
    ViewerInfo *info);

void window_redraw(Window *win);

ViewerMarkManager *window_get_mark_manager(Window *win);