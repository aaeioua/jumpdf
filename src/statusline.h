#pragma once

#include "window.h"

/* 0 is "unknown" */
typedef enum StatuslineComponent {
    STATUSLINE_COMPONENT_PAGE = 1,
    STATUSLINE_COMPONENT_CENTER_MODE,
    STATUSLINE_COMPONENT_SCALE,
    STATUSLINE_COMPONENT_MARK_SELECTION,
    STATUSLINE_COMPONENT_PENDING_KEYS,
} StatuslineComponent;

StatuslineComponent statusline_component_from_str(gchar *str);
gchar *statusline_section_to_str(GArray *section, Window *win);