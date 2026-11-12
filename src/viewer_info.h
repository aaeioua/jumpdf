#pragma once

#include "page.h"

#include <poppler.h>
#include <stdbool.h>

typedef struct ViewerInfo {
    PopplerDocument *doc;
    Page **pages;
    int n_pages;
} ViewerInfo;

ViewerInfo *viewer_info_new(PopplerDocument *doc);
ViewerInfo *viewer_info_new_from_gfile(GFile *file);
bool viewer_info_init(ViewerInfo *info, PopplerDocument *doc);
void viewer_info_destroy(ViewerInfo *info);

PopplerDest *viewer_info_get_dest(ViewerInfo *info, PopplerDest *dest);
PopplerPage *viewer_info_get_poppler_page(ViewerInfo *info, int page_num);
