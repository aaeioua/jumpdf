#pragma once

#include <gtk/gtk.h>

#include "window.h"

#define APP_TYPE (app_get_type())
G_DECLARE_FINAL_TYPE(App, app, JUMPDF, APP, GtkApplication)

int app_run(int argc, char *argv[]);

App *app_new(void);

/*
 * The first window on a document gets the app's own manager for it; later
 * windows get a copy sharing its Groups. Hand either back with
 * app_release_mark_manager when the window is done with it.
 */
ViewerMarkManager *app_get_mark_manager(App *app, GFile *file);
void app_release_mark_manager(App *app, ViewerMarkManager *manager);
void app_remove_window(App *app, Window *win);
void app_update_cursors(App *app);
void app_redraw_windows(App *app);
void app_update_database_mark_managers(App *app);
void app_open_file_chooser(App *app);