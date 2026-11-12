#pragma once

#include "viewer_mark_group.h"

#define NUM_GROUPS 9

typedef struct ViewerMarkManager {
    ViewerMarkGroup *groups[NUM_GROUPS];
    unsigned int current_group;
    unsigned int previous_group;
} ViewerMarkManager;

ViewerMarkManager *viewer_mark_manager_new(ViewerMarkGroup *groups[NUM_GROUPS], unsigned int current_group, unsigned int previous_group);
ViewerMarkManager *viewer_mark_manager_copy(ViewerMarkManager *manager);
void viewer_mark_manager_init(ViewerMarkManager *manager, ViewerMarkGroup *groups[NUM_GROUPS], unsigned int current_group, unsigned int previous_group);
void viewer_mark_manager_destroy(ViewerMarkManager *manager);
/* Frees only a copy's own selection state; its Groups are left intact. */
void viewer_mark_manager_free_copy(ViewerMarkManager *copy);

void viewer_mark_manager_set_mark(ViewerMarkManager *manager, ViewerCursor *cursor, unsigned int group_i, unsigned int mark_i);
ViewerCursor *viewer_mark_manager_get_mark(ViewerMarkManager *manager, unsigned int group_i, unsigned int mark_i);
void viewer_mark_manager_set_current_group(ViewerMarkManager *manager, unsigned int group_i);
unsigned int viewer_mark_manager_get_current_group_index(ViewerMarkManager *manager);
void viewer_mark_manager_set_current_mark(ViewerMarkManager *manager, unsigned int mark_i);
unsigned int viewer_mark_manager_get_current_mark_index(ViewerMarkManager *manager);
ViewerCursor *viewer_mark_manager_get_current_cursor(ViewerMarkManager *manager);
void viewer_mark_manager_set_current_cursor(ViewerMarkManager *manager, ViewerCursor *cursor);

void viewer_mark_manager_switch_group(ViewerMarkManager *manager, unsigned int group_i);
void viewer_mark_manager_switch_to_previous_group(ViewerMarkManager *manager);
void viewer_mark_manager_swap_group(ViewerMarkManager *manager, unsigned int group_i);
void viewer_mark_manager_overwrite_group(ViewerMarkManager *manager, unsigned int group_i);

void viewer_mark_manager_switch_mark(ViewerMarkManager *manager, unsigned int mark_i);
void viewer_mark_manager_switch_to_previous_mark(ViewerMarkManager *manager);
void viewer_mark_manager_clear_mark(ViewerMarkManager *manager, unsigned int mark_i);
void viewer_mark_manager_swap_mark(ViewerMarkManager *manager, unsigned int mark_i);
void viewer_mark_manager_overwrite_mark(ViewerMarkManager *manager, unsigned int mark_i);