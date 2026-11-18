#pragma once

#include "viewer_info.h"
#include "viewer_cursor.h"
#include "viewer_mark_manager.h"
#include "search.h"
#include "viewport.h"

struct PageLinks;

typedef struct Viewer {
    ViewerInfo *info;
    GroupSelection *selection;
    Search *search;
    Viewport *viewport;
    struct PageLinks *page_links;
} Viewer;

typedef struct {
    ViewportRect area;
    unsigned int number;
} ViewerLinkNumber;

Viewer *viewer_new(ViewerInfo *info, GroupSelection *selection, Search *search,
    const ViewportRules *rules);
void viewer_destroy(Viewer *viewer);

ViewerCursor *viewer_current_mark(const Viewer *viewer);

gboolean viewer_goto_found_page(Viewer *viewer, int from, int to);
/*
 * Fits and places the current Mark on `target`; does nothing if target.page
 * is -1
 */
void viewer_goto_target(Viewer *viewer, ViewerTarget target);

/*
 * The Link numbers for the current Mark: each link on the pages at
 * least partly in the view, numbered from 1, top to bottom then left
 * to right on a page. Element i has number i + 1.
 * Reads a page's links under the document lock the first time they are
 * needed. Never NULL. Free with g_array_unref.
 */
GArray *viewer_link_numbers(Viewer *viewer);
/*
 * Follows the link that viewer_link_numbers would number `number`:
 * moves the current Mark or opens a URI. Any other number does nothing.
 */
void viewer_follow_link(Viewer *viewer, unsigned int number);
