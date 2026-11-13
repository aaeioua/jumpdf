#pragma once

#include <gtk/gtk.h>

#include "viewer_mark_manager.h"
#include "viewer.h"

typedef struct _App App;

#define WINDOW_TYPE (window_get_type())
G_DECLARE_FINAL_TYPE(Window, window, JUMPDF, WINDOW, GtkApplicationWindow)

Window *window_new(App *app);
void window_open(Window *win, GFile *file, ViewerMarkManager *mark_manager,
    ViewerInfo *info);

void window_redraw(Window *win);
void window_toggle_fullscreen(Window *win);
void window_show_search_dialog(Window *win);
void window_show_help_dialog(Window *win);
void window_toggle_toc(Window *win);
void window_focus_toc_search(Window *win);
void window_execute_toc_row(Window *win, GtkListBoxRow *row);

ViewerMarkManager *window_get_mark_manager(Window *win);
GroupSelection *window_get_group_selection(Window *win);
Viewer *window_get_viewer(Window *win);
GtkListBox *window_get_toc_listbox(Window *win);