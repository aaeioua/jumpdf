#pragma once

#include "viewer_info.h"
#include "viewer_cursor.h"
#include "viewer_mark_manager.h"
#include "viewer_search.h"
#include "viewer_links.h"
#include "viewport.h"
#include "input_cmd.h"

typedef struct Viewer {
    ViewerInfo *info;
    GroupSelection *selection;
    ViewerSearch *search;
    ViewerLinks *links;
    Viewport *viewport;
    Command last_command;
    Command last_jump_command;
} Viewer;

Viewer *viewer_new(ViewerInfo *info, GroupSelection *selection, ViewerSearch *search, ViewerLinks *links);
void viewer_destroy(Viewer *viewer);

ViewerCursor *viewer_current_mark(const Viewer *viewer);

void viewer_goto_dest(Viewer *viewer, PopplerDest *dest);
void viewer_execute_action(Viewer *viewer, PopplerAction *action);
