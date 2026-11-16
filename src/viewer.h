#pragma once

#include "viewer_info.h"
#include "viewer_cursor.h"
#include "viewer_mark_manager.h"
#include "search.h"
#include "viewer_links.h"
#include "viewport.h"

typedef struct Viewer {
    ViewerInfo *info;
    GroupSelection *selection;
    Search *search;
    ViewerLinks *links;
    Viewport *viewport;
} Viewer;

Viewer *viewer_new(ViewerInfo *info, GroupSelection *selection, Search *search,
    ViewerLinks *links, const ViewportRules *rules);
void viewer_destroy(Viewer *viewer);

ViewerCursor *viewer_current_mark(const Viewer *viewer);

gboolean viewer_goto_found_page(Viewer *viewer, int from, int to);
void viewer_goto_dest(Viewer *viewer, PopplerDest *dest);
void viewer_execute_action(Viewer *viewer, PopplerAction *action);
