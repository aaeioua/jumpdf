#pragma once

#include <poppler.h>
#include <stdbool.h>

typedef struct ViewerInfo {
    PopplerDocument *doc;
    PopplerPage **pages;
    int n_pages;
    GMutex poppler_lock;
} ViewerInfo;

ViewerInfo *viewer_info_new(PopplerDocument *doc);
ViewerInfo *viewer_info_new_from_bytes(GBytes *bytes, GError **error);
/* Returns false with `error` set if `doc` has no pages or a page can't be read */
bool viewer_info_init(ViewerInfo *info, PopplerDocument *doc, GError **error);
void viewer_info_destroy(ViewerInfo *info);

void viewer_info_lock(ViewerInfo *info);
void viewer_info_unlock(ViewerInfo *info);

/* Where a destination leads */
typedef struct {
    int page;
    bool has_top;
    double top; /* from the page's bottom */
} ViewerTarget;

/*
 * Looks up named destinations under the document lock. Never frees or keeps
 * `dest`.
 */
ViewerTarget viewer_info_resolve_dest(ViewerInfo *info,
    const PopplerDest *dest);
PopplerPage *viewer_info_get_poppler_page(ViewerInfo *info, int page_num);
