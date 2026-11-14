#include <math.h>
#include <string.h>

#include "statusline_format.h"

#define SEARCH_MAX_CHARS 32

static gchar *statusline_search_format(const StatuslineState *state);

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
        return statusline_search_format(state);
    default:
        return NULL;
    }
}

/*
 * "/query [position/total]", the query cut after SEARCH_MAX_CHARS characters,
 * with "?" for a position not known yet and "+" while still scanning, then
 * " …" while seeking
 */
static gchar *statusline_search_format(const StatuslineState *state)
{
    const char *query = state->search_text;
    const char *scanning = state->search_scanning ? "+" : "";
    const char *pending = state->search_pending ? " …" : "";
    int query_bytes;
    const char *cut_mark = "";
    gchar position[16] = "?";

    if (query == NULL || query[0] == '\0') {
        return NULL;
    }
    query_bytes = (int)strlen(query);
    if (g_utf8_strlen(query, -1) > SEARCH_MAX_CHARS) {
        query_bytes = (int)(g_utf8_offset_to_pointer(query, SEARCH_MAX_CHARS) -
            query);
        cut_mark = "…";
    }
    if (state->search_position >= 0) {
        g_snprintf(position, sizeof(position), "%d", state->search_position);
    }

    return g_strdup_printf("/%.*s%s [%s/%u%s]%s", query_bytes, query,
        cut_mark, position, state->search_total, scanning, pending);
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
