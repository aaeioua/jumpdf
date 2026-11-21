#pragma once

#include <gtk/gtk.h>

typedef struct {
    int width;
    int height;
    gboolean maximized;
} WindowState;

WindowState window_state_default(void);

WindowState window_state_load(const char *path);
gboolean window_state_save(const char *path, const WindowState *state,
    GError **error);

void window_state_apply(const WindowState *state, GtkWindow *window);
WindowState window_state_of(GtkWindow *window);
