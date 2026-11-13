#include <stdlib.h>

#include "viewer_mark_manager.h"

struct GroupSelection {
    ViewerMarkManager *manager;
    unsigned int current_group;
    unsigned int previous_group;
};

static ViewerMarkGroup *selection_group(const GroupSelection *selection);
static void remember_selection(const GroupSelection *selection);
static unsigned int swapped_index(unsigned int index, unsigned int a, unsigned int b);

ViewerMarkManager *viewer_mark_manager_new(ViewerMarkGroup *groups[NUM_GROUPS], unsigned int current_group, unsigned int previous_group)
{
    ViewerMarkManager *manager = malloc(sizeof(ViewerMarkManager));
    if (manager == NULL) {
        return NULL;
    }

    viewer_mark_manager_init(manager, groups, current_group, previous_group);

    return manager;
}

ViewerMarkManager *viewer_mark_manager_new_default(void)
{
    ViewerCursor *empty_marks[NUM_MARKS] = {0};
    ViewerMarkGroup *groups[NUM_GROUPS];

    for (unsigned int i = 0; i < NUM_GROUPS; i++) {
        groups[i] = viewer_mark_group_new(empty_marks, 0, 0);
    }

    ViewerMarkManager *manager = viewer_mark_manager_new(groups, 0, 0);
    viewer_mark_manager_ensure_current_mark(manager);

    return manager;
}

void viewer_mark_manager_init(ViewerMarkManager *manager, ViewerMarkGroup *groups[NUM_GROUPS], unsigned int current_group, unsigned int previous_group)
{
    if (manager == NULL) {
        return;
    }

    for (unsigned int i = 0; i < NUM_GROUPS; i++) {
        manager->groups[i] = groups[i];
    }

    manager->current_group = current_group;
    manager->previous_group = previous_group;
    manager->selections = NULL;
}

void viewer_mark_manager_destroy(ViewerMarkManager *manager)
{
    g_return_if_fail(manager->selections == NULL);

    for (unsigned int i = 0; i < NUM_GROUPS; i++) {
        viewer_mark_group_destroy(manager->groups[i]);
        free(manager->groups[i]);
    }

    free(manager);
}

void viewer_mark_manager_set_mark(ViewerMarkManager *manager, ViewerCursor *cursor, unsigned int group_i, unsigned int mark_i)
{
    manager->groups[group_i]->marks[mark_i] = cursor;
}

ViewerCursor *viewer_mark_manager_get_mark(ViewerMarkManager *manager, unsigned int group_i, unsigned int mark_i)
{
    return manager->groups[group_i]->marks[mark_i];
}

void viewer_mark_manager_ensure_current_mark(ViewerMarkManager *manager)
{
    ViewerMarkGroup *group = manager->groups[manager->current_group];

    if (group->marks[group->current_mark] == NULL) {
        group->marks[group->current_mark] =
            viewer_cursor_new(0, 0.0, 0.0, 1.0, true, false);
    }
}

GroupSelection *viewer_mark_manager_attach(ViewerMarkManager *manager)
{
    GroupSelection *selection = malloc(sizeof(GroupSelection));
    if (selection == NULL) {
        return NULL;
    }

    selection->manager = manager;
    selection->current_group = manager->current_group;
    selection->previous_group = manager->previous_group;
    manager->selections = g_slist_prepend(manager->selections, selection);

    viewer_mark_manager_ensure_current_mark(manager);

    return selection;
}

void viewer_mark_manager_detach(GroupSelection *selection)
{
    if (selection == NULL) {
        return;
    }

    ViewerMarkManager *manager = selection->manager;

    remember_selection(selection);
    manager->selections = g_slist_remove(manager->selections, selection);

    free(selection);
}

ViewerCursor *group_selection_current_mark(const GroupSelection *selection)
{
    ViewerMarkGroup *group = selection_group(selection);
    return group->marks[group->current_mark];
}

unsigned int group_selection_current_group(const GroupSelection *selection)
{
    return selection->current_group;
}

unsigned int group_selection_current_mark_index(const GroupSelection *selection)
{
    return selection_group(selection)->current_mark;
}

void group_selection_switch_group(GroupSelection *selection, unsigned int group_i)
{
    if (group_i == selection->current_group) {
        return;
    }

    ViewerCursor *old_mark = group_selection_current_mark(selection);
    ViewerMarkGroup *group;

    selection->previous_group = selection->current_group;
    selection->current_group = group_i;

    group = selection_group(selection);
    if (group->marks[group->current_mark] == NULL) {
        group->marks[group->current_mark] = viewer_cursor_copy(old_mark);
    }

    remember_selection(selection);
}

void group_selection_switch_to_previous_group(GroupSelection *selection)
{
    group_selection_switch_group(selection, selection->previous_group);
}

void group_selection_swap_group(GroupSelection *selection, unsigned int group_i)
{
    if (group_i == selection->current_group) {
        return;
    }

    ViewerMarkManager *manager = selection->manager;
    const unsigned int current_i = selection->current_group;
    ViewerMarkGroup *current = manager->groups[current_i];

    manager->groups[current_i] = manager->groups[group_i];
    manager->groups[group_i] = current;

    for (GSList *l = manager->selections; l != NULL; l = l->next) {
        GroupSelection *other = l->data;
        other->current_group = swapped_index(other->current_group, current_i, group_i);
        other->previous_group = swapped_index(other->previous_group, current_i, group_i);
    }

    selection->previous_group = current_i;
    remember_selection(selection);
}

void group_selection_overwrite_group(GroupSelection *selection, unsigned int group_i)
{
    if (group_i == selection->current_group) {
        return;
    }

    ViewerMarkGroup *source = selection_group(selection);
    ViewerMarkGroup *target = selection->manager->groups[group_i];

    for (unsigned int i = 0; i < NUM_MARKS; i++) {
        viewer_cursor_destroy(target->marks[i]);
        free(target->marks[i]);
        target->marks[i] = source->marks[i] == NULL
            ? NULL
            : viewer_cursor_copy(source->marks[i]);
    }

    target->current_mark = source->current_mark;
    target->previous_mark = source->previous_mark;

    group_selection_switch_group(selection, group_i);
}

void group_selection_switch_mark(GroupSelection *selection, unsigned int mark_i)
{
    ViewerMarkGroup *group = selection_group(selection);

    if (mark_i == group->current_mark) {
        return;
    }

    if (group->marks[mark_i] == NULL) {
        group->marks[mark_i] = viewer_cursor_copy(group->marks[group->current_mark]);
    }

    group->previous_mark = group->current_mark;
    group->current_mark = mark_i;
}

void group_selection_switch_to_previous_mark(GroupSelection *selection)
{
    group_selection_switch_mark(selection, selection_group(selection)->previous_mark);
}

void group_selection_clear_mark(GroupSelection *selection, unsigned int mark_i)
{
    ViewerMarkGroup *group = selection_group(selection);

    if (mark_i == group->current_mark) {
        return;
    }

    viewer_cursor_destroy(group->marks[mark_i]);
    free(group->marks[mark_i]);
    group->marks[mark_i] = NULL;
}

void group_selection_swap_mark(GroupSelection *selection, unsigned int mark_i)
{
    ViewerMarkGroup *group = selection_group(selection);

    if (mark_i == group->current_mark) {
        return;
    }

    ViewerCursor *current = group->marks[group->current_mark];

    group->marks[group->current_mark] = group->marks[mark_i];
    group->marks[mark_i] = current;
    group->previous_mark = group->current_mark;
    group->current_mark = mark_i;
}

void group_selection_overwrite_mark(GroupSelection *selection, unsigned int mark_i)
{
    ViewerMarkGroup *group = selection_group(selection);

    if (mark_i == group->current_mark) {
        return;
    }

    viewer_cursor_destroy(group->marks[mark_i]);
    free(group->marks[mark_i]);
    group->marks[mark_i] = viewer_cursor_copy(group->marks[group->current_mark]);
    group->previous_mark = group->current_mark;
    group->current_mark = mark_i;
}

static ViewerMarkGroup *selection_group(const GroupSelection *selection)
{
    return selection->manager->groups[selection->current_group];
}

static void remember_selection(const GroupSelection *selection)
{
    selection->manager->current_group = selection->current_group;
    selection->manager->previous_group = selection->previous_group;
}

static unsigned int swapped_index(unsigned int index, unsigned int a, unsigned int b)
{
    if (index == a) {
        return b;
    } else if (index == b) {
        return a;
    } else {
        return index;
    }
}
