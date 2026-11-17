#pragma once

#include "viewer_info.h"

/* One item of a document's outline, flattened */
typedef struct {
    char *title;          /* UTF-8, never NULL */
    int depth;            /* 0 at the top level */
    ViewerTarget target;
} OutlineEntry;

/*
 * One entry per outline item, depth first in document order, children always
 * included. Takes the document lock. Never NULL; empty when the PDF has no
 * outline.
 */
GArray *outline_read(ViewerInfo *info);
