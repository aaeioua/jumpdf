#include <math.h>

#include "statusline_format.h"

#define SEARCH_MAX_CHARS 32

static gchar *statusline_component_format(StatuslineComponent component,
    const StatuslineState *state)
{
    switch (component) {
    case STATUSLINE_COMPONENT_PAGE:
        return g_strdup_printf("%d/%d", state->page + 1, state->n_pages);
    case STATUSLINE_COMPONENT_CENTER_MODE:
        return state->center_mode ? g_strdup("Center") : NULL;
    case STATUSLINE_COMPONENT_SCALE:
        return g_strdup_printf("%d%%", (int)round(state->scale * 100));
    case STATUSLINE_COMPONENT_MARK_SELECTION:
        return g_strdup_printf("%u:%u", state->group + 1, state->mark + 1);
    case STATUSLINE_COMPONENT_PENDING_KEYS:
        if (state->pending_keys == NULL || state->pending_keys[0] == '\0') {
            return NULL;
        }
        return g_strdup(state->pending_keys);
    case STATUSLINE_COMPONENT_SEARCH:
        if (state->search_text == NULL || state->search_text[0] == '\0') {
            return NULL;
        }
        if (g_utf8_strlen(state->search_text, -1) > SEARCH_MAX_CHARS) {
            const char *cut = g_utf8_offset_to_pointer(state->search_text,
                SEARCH_MAX_CHARS);

            return g_strdup_printf("/%.*s…",
                (int)(cut - state->search_text), state->search_text);
        }
        return g_strconcat("/", state->search_text, NULL);
    default:
        return NULL;
    }
}

static gchar *statusline_section_format(const GArray *section,
    const char *separator, const StatuslineState *state)
{
    GString *text = g_string_new(NULL);
    gboolean first = TRUE;

    for (guint i = 0; i < section->len; i++) {
        gchar *item = statusline_component_format(
            g_array_index(section, StatuslineComponent, i), state);

        if (item == NULL) {
            continue;
        }

        if (!first) {
            g_string_append(text, separator);
        }
        g_string_append(text, item);
        first = FALSE;
        g_free(item);
    }

    return g_string_free(text, FALSE);
}

void statusline_format(const StatuslineLayout *layout,
    const StatuslineState *state, gchar *out[STATUSLINE_N_SECTIONS])
{
    const char *separator = layout->separator != NULL ? layout->separator : "";

    for (int i = 0; i < STATUSLINE_N_SECTIONS; i++) {
        out[i] = statusline_section_format(layout->sections[i], separator,
            state);
    }
}
