#pragma once

#include <gtk/gtk.h>
#include <stdbool.h>

#include "viewer_info.h"

/*
 * A window's view of the Outline: a filter above one row per entry, with a
 * selection that can be stepped through and activated. Starts closed, and
 * builds its rows the first time it is opened.
 */
#define OUTLINE_PANEL_TYPE (outline_panel_get_type())
G_DECLARE_FINAL_TYPE(OutlinePanel, outline_panel, JUMPDF, OUTLINE_PANEL, GtkWidget)

typedef void (*OutlinePanelActivated)(ViewerTarget target, gpointer user_data);

/* Takes ownership of `entries` (an array of OutlineEntry). Starts closed. */
OutlinePanel *outline_panel_new(GArray *entries,
    OutlinePanelActivated activated, gpointer user_data);
void outline_panel_set_open(OutlinePanel *panel, bool open);
/* Selects the next (1) or previous (-1) visible row; stays put at either end */
void outline_panel_step(OutlinePanel *panel, int step);
/* Passes the selected entry's target to `activated`, unless it goes nowhere */
void outline_panel_activate(OutlinePanel *panel);
void outline_panel_focus_filter(OutlinePanel *panel);
