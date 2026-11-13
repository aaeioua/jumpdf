#pragma once

#include <gtk/gtk.h>

#include "window.h"

#define APP_TYPE (app_get_type())
G_DECLARE_FINAL_TYPE(App, app, JUMPDF, APP, GtkApplication)

int app_run(int argc, char *argv[]);

App *app_new(void);

/*
 * The one Mark Manager for the document, shared by every window on it. The
 * first call loads it from the Mark store, or creates the default. The app
 * owns it until exit; windows attach and detach Group selections.
 */
ViewerMarkManager *app_get_mark_manager(App *app, GFile *file);
void app_remove_window(App *app, Window *win);
/* Redraws every window whose Group selection belongs to `manager` */
void app_redraw_windows(App *app, ViewerMarkManager *manager);
void app_save_document(App *app, const char *uri);
void app_open_file_chooser(App *app);