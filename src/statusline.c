#include "statusline.h"
#include "config.h"

static gchar *statusline_component_to_str(StatuslineComponent component, Window *win);

StatuslineComponent statusline_component_from_str(gchar *str)
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
    } else {
        return 0;
    }
}

gchar *statusline_section_to_str(GArray *section, Window *win)
{
    GStrv str_array = NULL;
    char *final_str = NULL;
    GStrvBuilder *builder = g_strv_builder_new();
    StatuslineComponent component = 0;
    gchar *component_str = NULL;

    for (guint i = 0; i < section->len; i++) {
        component = g_array_index(section, StatuslineComponent, i);
        component_str = statusline_component_to_str(component, win);

        if (component_str != NULL) {
            g_strv_builder_add(builder, component_str);
            g_free(component_str);
        }
    }

    str_array = g_strv_builder_end(builder);
    g_strv_builder_unref(builder);

    final_str = g_strjoinv(g_config->statusline_separator, str_array);
    g_strfreev(str_array);

    return final_str;
}

static gchar *statusline_component_to_str(StatuslineComponent component, Window *win)
{
    Viewer *viewer = window_get_viewer(win);
    GroupSelection *selection = window_get_group_selection(win);
    ViewerCursor *mark = group_selection_current_mark(selection);
    const char *pending_keys;

    switch (component) {
    case STATUSLINE_COMPONENT_PAGE:
        return g_strdup_printf("%d/%d",
            mark->current_page + 1,
            viewer->info->n_pages);
    case STATUSLINE_COMPONENT_CENTER_MODE:
        return mark->center_mode ? g_strdup("Center") : NULL;
    case STATUSLINE_COMPONENT_SCALE:
        return g_strdup_printf("%d%%",
            (int)round(mark->scale * 100));
    case STATUSLINE_COMPONENT_MARK_SELECTION:
        return g_strdup_printf("%u:%u",
            group_selection_current_group(selection) + 1,
            group_selection_current_mark_index(selection) + 1);
    case STATUSLINE_COMPONENT_PENDING_KEYS:
        pending_keys = input_pending(window_get_input(win));
        return pending_keys[0] == '\0' ? NULL : g_strdup(pending_keys);
    default:
        return NULL;
    }
}
