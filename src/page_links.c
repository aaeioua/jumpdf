#include "page_links.h"

struct PageLinks {
    ViewerInfo *info;
    /* PageLink per page, NULL until the page is read */
    GArray **pages;
};

static GArray *read_page(ViewerInfo *info, int page);
static PopplerRectangle normalise(const PopplerRectangle *area);
static gint compare_reading_order(gconstpointer left, gconstpointer right);

PageLinks *page_links_new(ViewerInfo *info)
{
    PageLinks *links = g_new0(PageLinks, 1);
    links->info = info;
    links->pages = g_new0(GArray *, info->n_pages);

    return links;
}

void page_links_free(PageLinks *links)
{
    if (links == NULL) {
        return;
    }

    for (int page = 0; page < links->info->n_pages; page++) {
        GArray *entries = links->pages[page];
        if (entries == NULL) {
            continue;
        }

        for (guint i = 0; i < entries->len; i++) {
            poppler_action_free(g_array_index(entries, PageLink, i).action);
        }
        g_array_unref(entries);
    }

    g_free(links->pages);
    g_free(links);
}

const PageLink *page_links_get(PageLinks *links, int page, guint *n_links)
{
    g_assert(page >= 0 && page < links->info->n_pages);

    if (links->pages[page] == NULL) {
        links->pages[page] = read_page(links->info, page);
    }

    *n_links = links->pages[page]->len;
    return (const PageLink *)links->pages[page]->data;
}

static GArray *read_page(ViewerInfo *info, int page)
{
    PopplerPage *poppler_page = viewer_info_get_poppler_page(info, page);
    GArray *entries = g_array_new(FALSE, FALSE, sizeof(PageLink));

    viewer_info_lock(info);
    GList *mappings = poppler_page_get_link_mapping(poppler_page);
    for (GList *l = mappings; l != NULL; l = l->next) {
        const PopplerLinkMapping *mapping = l->data;
        const PageLink link = {
            .area = normalise(&mapping->area),
            .action = poppler_action_copy(mapping->action),
        };
        g_array_append_val(entries, link);
    }
    poppler_page_free_link_mapping(mappings);
    viewer_info_unlock(info);

    g_array_sort(entries, compare_reading_order);

    return entries;
}

static PopplerRectangle normalise(const PopplerRectangle *area)
{
    PopplerRectangle normalised = *area;

    normalised.x1 = MIN(area->x1, area->x2);
    normalised.x2 = MAX(area->x1, area->x2);
    normalised.y1 = MIN(area->y1, area->y2);
    normalised.y2 = MAX(area->y1, area->y2);

    return normalised;
}

/* Highest top edge first, then leftmost left edge */
static gint compare_reading_order(gconstpointer left, gconstpointer right)
{
    const PopplerRectangle *a = &((const PageLink *)left)->area;
    const PopplerRectangle *b = &((const PageLink *)right)->area;

    if (a->y2 != b->y2) {
        return a->y2 > b->y2 ? -1 : 1;
    }
    if (a->x1 != b->x1) {
        return a->x1 < b->x1 ? -1 : 1;
    }
    return 0;
}
