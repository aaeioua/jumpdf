#pragma once

#include "viewer_cursor.h"
#include "viewer_info.h"
#include "viewport.h"

typedef struct ViewerSearch {
    char *search_text;
    int last_goto_page;
} ViewerSearch;

ViewerSearch *viewer_search_new(void);
void viewer_search_init(ViewerSearch *search);
void viewer_search_destroy(ViewerSearch *search);

/* Copies `text`, replacing (and freeing) any previous search text. NULL clears it. */
void viewer_search_set_text(ViewerSearch *search, const char *text);

ViewerCursor *viewer_search_get_next_search(ViewerSearch *search, ViewerInfo *info,
    const Viewport *viewport, ViewerCursor *current_cursor);
ViewerCursor *viewer_search_get_prev_search(ViewerSearch *search, ViewerInfo *info,
    const Viewport *viewport, ViewerCursor *current_cursor);
