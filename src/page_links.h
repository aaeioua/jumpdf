#pragma once

#include <glib.h>
#include <poppler.h>

#include "viewer_info.h"

/* The links on a page */
typedef struct {
    PopplerRectangle area;
    PopplerAction *action;
} PageLink;

typedef struct PageLinks PageLinks;

/* `info` must outlive it */
PageLinks *page_links_new(ViewerInfo *info);
void page_links_free(PageLinks *links);

/*
 * The links on `page`, top to bottom then left to right, with ties in
 * Poppler's order. Read under the document lock the first time.
 */
const PageLink *page_links_get(PageLinks *links, int page, guint *n_links);
