#pragma once

#include <glib.h>

#include "viewer_mark_group.h"

#define NUM_GROUPS 9

typedef struct GroupSelection GroupSelection;

typedef struct ViewerMarkManager {
    ViewerMarkGroup *groups[NUM_GROUPS];
    /*
     * The remembered Group selection: the most recent one, which new Group
     * selections start from and the Mark store keeps.
     */
    unsigned int current_group;
    unsigned int previous_group;
    /* GroupSelection *, every attached one; the Mark store ignores it */
    GSList *selections;
} ViewerMarkManager;

ViewerMarkManager *viewer_mark_manager_new(ViewerMarkGroup *groups[NUM_GROUPS], unsigned int current_group, unsigned int previous_group);
ViewerMarkManager *viewer_mark_manager_new_default(void);
void viewer_mark_manager_init(ViewerMarkManager *manager, ViewerMarkGroup *groups[NUM_GROUPS], unsigned int current_group, unsigned int previous_group);
/* No Group selection may still be attached */
void viewer_mark_manager_destroy(ViewerMarkManager *manager);

void viewer_mark_manager_set_mark(ViewerMarkManager *manager, ViewerCursor *cursor, unsigned int group_i, unsigned int mark_i);
ViewerCursor *viewer_mark_manager_get_mark(ViewerMarkManager *manager, unsigned int group_i, unsigned int mark_i);
/*
 * The remembered current Group's current Mark is never empty: sets it to the
 * top of the first page if it is.
 */
void viewer_mark_manager_ensure_current_mark(ViewerMarkManager *manager);

/* Prepends a new Group selection to the manager */
GroupSelection *viewer_mark_manager_attach(ViewerMarkManager *manager);
/* Makes the selection the remembered one, then frees it */
void viewer_mark_manager_detach(GroupSelection *selection);

ViewerCursor *group_selection_current_mark(const GroupSelection *selection);
unsigned int group_selection_current_group(const GroupSelection *selection);
unsigned int group_selection_current_mark_index(const GroupSelection *selection);

void group_selection_switch_group(GroupSelection *selection, unsigned int group_i);
void group_selection_switch_to_previous_group(GroupSelection *selection);
/*
 * Swaps the current Group's slot with group_i. Every Group selection follows
 * the Marks it shows, so only the labels change.
 */
void group_selection_swap_group(GroupSelection *selection, unsigned int group_i);
/* Replaces Group group_i's Marks with copies of the current Group's, then switches to it */
void group_selection_overwrite_group(GroupSelection *selection, unsigned int group_i);

void group_selection_switch_mark(GroupSelection *selection, unsigned int mark_i);
void group_selection_switch_to_previous_mark(GroupSelection *selection);
/* Refused for the Group's current Mark, which every window on the Group shows */
void group_selection_clear_mark(GroupSelection *selection, unsigned int mark_i);
void group_selection_swap_mark(GroupSelection *selection, unsigned int mark_i);
void group_selection_overwrite_mark(GroupSelection *selection, unsigned int mark_i);
