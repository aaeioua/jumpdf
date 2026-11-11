#include "page.h"

#include <stdlib.h>

Page *page_new(PopplerPage *poppler_page)
{
    Page *page = malloc(sizeof(Page));
    if (page == NULL) {
        return NULL;
    }

    page->poppler_page = poppler_page;

    return page;
}

void page_destroy(Page *page)
{
    if (page->poppler_page) {
        g_object_unref(page->poppler_page);
        page->poppler_page = NULL;
    }
}
