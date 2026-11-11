#pragma once

#include <poppler.h>

typedef struct {
    PopplerPage *poppler_page;
} Page;

Page *page_new(PopplerPage *poppler_page);
void page_destroy(Page *page);
