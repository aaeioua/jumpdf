#pragma once

#include <gtk/gtk.h>

#include "statusline_layout.h"

#define STATUSLINE_TYPE (statusline_get_type())
G_DECLARE_FINAL_TYPE(Statusline, statusline, JUMPDF, STATUSLINE, GtkWidget)

Statusline *statusline_new(const StatuslineLayout *layout);
void statusline_update(Statusline *statusline, const StatuslineState *state);
