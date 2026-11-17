#include "outline.h"

typedef struct {
    char *title;
    int depth;
    PopplerDest *dest; /* a copy, or NULL when the item leads nowhere */
} OutlineItem;

static void outline_entry_clear(gpointer data);
static void outline_collect(PopplerIndexIter *iter, int depth, GArray *items);

GArray *outline_read(ViewerInfo *info)
{
    GArray *entries = g_array_new(FALSE, FALSE, sizeof(OutlineEntry));
    GArray *items = g_array_new(FALSE, FALSE, sizeof(OutlineItem));
    PopplerIndexIter *iter;

    g_array_set_clear_func(entries, outline_entry_clear);

    viewer_info_lock(info);
    iter = poppler_index_iter_new(info->doc);
    if (iter != NULL) {
        outline_collect(iter, 0, items);
        poppler_index_iter_free(iter);
    }
    viewer_info_unlock(info);

    for (guint i = 0; i < items->len; i++) {
        OutlineItem *item = &g_array_index(items, OutlineItem, i);
        OutlineEntry entry = {
            .title = item->title,
            .depth = item->depth,
            .target = {.page = -1, .has_top = false, .top = 0},
        };

        if (item->dest != NULL) {
            entry.target = viewer_info_resolve_dest(info, item->dest);
            poppler_dest_free(item->dest);
        }
        g_array_append_val(entries, entry);
    }
    g_array_unref(items);

    return entries;
}

static void outline_entry_clear(gpointer data)
{
    OutlineEntry *entry = data;

    g_clear_pointer(&entry->title, g_free);
}

static void outline_collect(PopplerIndexIter *iter, int depth, GArray *items)
{
    do {
        PopplerAction *action = poppler_index_iter_get_action(iter);
        PopplerIndexIter *child;
        OutlineItem item = {
            .title = NULL,
            .depth = depth,
            .dest = NULL,
        };

        if (action != NULL) {
            if (action->any.title != NULL) {
                item.title = g_strdup(action->any.title);
            }
            if (action->type == POPPLER_ACTION_GOTO_DEST
                && action->goto_dest.dest != NULL) {
                item.dest = poppler_dest_copy(action->goto_dest.dest);
            }
            poppler_action_free(action);
        }
        if (item.title == NULL) {
            item.title = g_strdup("");
        }
        g_array_append_val(items, item);

        child = poppler_index_iter_get_child(iter);
        if (child != NULL) {
            outline_collect(child, depth + 1, items);
            poppler_index_iter_free(child);
        }
    } while (poppler_index_iter_next(iter));
}
