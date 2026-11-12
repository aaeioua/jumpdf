#include <stdlib.h>

#include "viewer_search.h"

ViewerSearch *viewer_search_new(void)
{
    ViewerSearch *search = malloc(sizeof(ViewerSearch));
    if (search == NULL) {
        return NULL;
    }

    viewer_search_init(search);

    return search;
}

void viewer_search_init(ViewerSearch *search)
{
    search->search_text = NULL;
    search->last_goto_page = -1;
}

void viewer_search_destroy(ViewerSearch *search)
{
    g_clear_pointer(&search->search_text, g_free);
}

void viewer_search_set_text(ViewerSearch *search, const char *text)
{
    g_free(search->search_text);
    search->search_text = g_strdup(text);
}

ViewerCursor *viewer_search_get_next_search(ViewerSearch *search, ViewerInfo *info,
    const Viewport *viewport, ViewerCursor *current_cursor)
{
    GList *matches = NULL;
    ViewerCursor *new_cursor = NULL;
    int next_page = -1;

    if (!search->search_text) {
        return NULL;
    }

    for (int i = current_cursor->current_page; i < info->n_pages; i++) {
        matches = poppler_page_find_text(viewer_info_get_poppler_page(info, i), search->search_text);
        if (matches && (i != search->last_goto_page || current_cursor->current_page != search->last_goto_page)) {
            next_page = i;
            search->last_goto_page = i;
            g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);

            break;
        }

        g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);
    }

    if (next_page == -1) {
        return NULL;
    } else {
        new_cursor = viewer_cursor_copy(current_cursor);
        viewport_goto_page(viewport, new_cursor, next_page);

        return new_cursor;
    }
}

ViewerCursor *viewer_search_get_prev_search(ViewerSearch *search, ViewerInfo *info,
    const Viewport *viewport, ViewerCursor *current_cursor)
{
    GList *matches = NULL;
    ViewerCursor *new_cursor = NULL;
    int prev_page = -1;

    if (!search->search_text) {
        return NULL;
    }

    for (int i = current_cursor->current_page; i >= 0; i--) {
        matches = poppler_page_find_text(viewer_info_get_poppler_page(info, i), search->search_text);
        if (matches && (i != search->last_goto_page || current_cursor->current_page != search->last_goto_page)) {
            prev_page = i;
            search->last_goto_page = i;
            g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);

            break;
        }

        g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);
    }

    if (prev_page == -1) {
        return NULL;
    } else {
        new_cursor = viewer_cursor_copy(current_cursor);
        viewport_goto_page(viewport, new_cursor, prev_page);

        return new_cursor;
    }
}