#pragma once

#include <glib.h>

/* 0 is "unknown" */
typedef enum StatuslineComponent {
    STATUSLINE_COMPONENT_PAGE = 1,
    STATUSLINE_COMPONENT_CENTER_MODE,
    STATUSLINE_COMPONENT_SCALE,
    STATUSLINE_COMPONENT_MARK_SELECTION,
    STATUSLINE_COMPONENT_PENDING_KEYS,
    STATUSLINE_COMPONENT_SEARCH,
} StatuslineComponent;

typedef enum {
    STATUSLINE_SECTION_LEFT,
    STATUSLINE_SECTION_MIDDLE,
    STATUSLINE_SECTION_RIGHT,
    STATUSLINE_N_SECTIONS,
} StatuslineSection;

typedef struct {
    gchar *separator;
    GArray *sections[STATUSLINE_N_SECTIONS];
} StatuslineLayout;

typedef struct {
    int page;                 /* 0-based */
    int n_pages;
    double scale;
    gboolean center_mode;
    unsigned int group;       /* 0-based Group index */
    unsigned int mark;        /* 0-based Mark index */
    const char *pending_keys;
    const char *search_text;
} StatuslineState;

StatuslineComponent statusline_component_from_str(const char *str);
void statusline_layout_init(StatuslineLayout *layout);
void statusline_layout_clear(StatuslineLayout *layout);
