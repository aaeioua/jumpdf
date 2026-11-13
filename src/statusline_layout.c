#include "statusline_layout.h"

StatuslineComponent statusline_component_from_str(const char *str)
{
    if (g_strcmp0(str, "Page") == 0) {
        return STATUSLINE_COMPONENT_PAGE;
    } else if (g_strcmp0(str, "Center mode") == 0) {
        return STATUSLINE_COMPONENT_CENTER_MODE;
    } else if (g_strcmp0(str, "Scale") == 0) {
        return STATUSLINE_COMPONENT_SCALE;
    } else if (g_strcmp0(str, "Mark selection") == 0) {
        return STATUSLINE_COMPONENT_MARK_SELECTION;
    } else if (g_strcmp0(str, "Pending keys") == 0) {
        return STATUSLINE_COMPONENT_PENDING_KEYS;
    } else if (g_strcmp0(str, "Search") == 0) {
        return STATUSLINE_COMPONENT_SEARCH;
    } else {
        return 0;
    }
}

void statusline_layout_init(StatuslineLayout *layout)
{
    layout->separator = NULL;
    for (int i = 0; i < STATUSLINE_N_SECTIONS; i++) {
        layout->sections[i] = g_array_new(FALSE, FALSE,
            sizeof(StatuslineComponent));
    }
}

void statusline_layout_clear(StatuslineLayout *layout)
{
    g_clear_pointer(&layout->separator, g_free);
    for (int i = 0; i < STATUSLINE_N_SECTIONS; i++) {
        if (layout->sections[i] != NULL) {
            g_array_free(layout->sections[i], TRUE);
            layout->sections[i] = NULL;
        }
    }
}
