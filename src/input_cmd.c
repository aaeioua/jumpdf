#include <stdlib.h>

#include "input_cmd.h"
#include "viewer.h"
#include "viewer_mark_manager.h"
#include "utils.h"

Command command_copy(Command *command)
{
    Command new_command = {0};

    if (command == NULL) {
        return new_command;
    }

    new_command.execute = command->execute;
    new_command.repeat_count = command->repeat_count;
    new_command.data = command->data;

    return new_command;
}

void command_execute(Command *command, Viewer *viewer)
{
    if (command != NULL && command->execute != NULL) {
        command->execute(viewer, command->repeat_count, command->data);
    }
}

void zoom_in(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    ViewerCursor *mark = viewer_current_mark(viewer);
    double scale_step = *(double *)data;

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer_cursor_set_scale(mark, mark->scale + scale_step);
    }
}

void zoom_out(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    ViewerCursor *mark = viewer_current_mark(viewer);
    double scale_step = *(double *)data;

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer_cursor_set_scale(mark, mark->scale - scale_step);
    }
}

void scroll_half_page_up(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    ViewerCursor *mark = viewer_current_mark(viewer);
    int step = *(int *)data;

    for (unsigned int i = 0; i < repeat_count; i++) {
        mark->y_offset -= step / 2.0;
    }
}

void scroll_half_page_down(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    ViewerCursor *mark = viewer_current_mark(viewer);
    int step = *(int *)data;

    for (unsigned int i = 0; i < repeat_count; i++) {
        mark->y_offset += step / 2.0;
    }
}

void scroll_left(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    ViewerCursor *mark = viewer_current_mark(viewer);

    for (unsigned int i = 0; i < repeat_count; i++) {
        mark->x_offset++;
    }
}

void scroll_down(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    ViewerCursor *mark = viewer_current_mark(viewer);

    for (unsigned int i = 0; i < repeat_count; i++) {
        mark->y_offset++;
    }
}

void scroll_up(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    ViewerCursor *mark = viewer_current_mark(viewer);

    for (unsigned int i = 0; i < repeat_count; i++) {
        mark->y_offset--;
    }
}

void scroll_right(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    ViewerCursor *mark = viewer_current_mark(viewer);

    for (unsigned int i = 0; i < repeat_count; i++) {
        mark->x_offset--;
    }
}

void forward_search(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    ViewerCursor *mark = viewer_current_mark(viewer);
    ViewerCursor *found = NULL;

    for (unsigned int i = 0; i < repeat_count; i++) {
        found = viewer_search_get_next_search(viewer->search, viewer->info,
            viewer->viewport, mark);
        if (found == NULL) {
            break;
        }

        /* In place, as other windows on the Group show the same Mark */
        *mark = *found;
        viewer_cursor_destroy(found);
        free(found);
    }
}

void backward_search(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    ViewerCursor *mark = viewer_current_mark(viewer);
    ViewerCursor *found = NULL;

    for (unsigned int i = 0; i < repeat_count; i++) {
        found = viewer_search_get_prev_search(viewer->search, viewer->info,
            viewer->viewport, mark);
        if (found == NULL) {
            break;
        }

        *mark = *found;
        viewer_cursor_destroy(found);
        free(found);
    }
}

void switch_to_previous_mark(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(repeat_count);
    UNUSED(data);

    group_selection_switch_to_previous_mark(viewer->selection);
}

void switch_to_previous_group(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(repeat_count);
    UNUSED(data);

    group_selection_switch_to_previous_group(viewer->selection);
}
