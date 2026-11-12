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
    double scale_step = *(double *)data;

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer_cursor_set_scale(viewer->cursor, viewer->cursor->scale + scale_step);
    }
}

void zoom_out(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    double scale_step = *(double *)data;

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer_cursor_set_scale(viewer->cursor, viewer->cursor->scale - scale_step);
    }
}

void scroll_half_page_up(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    int step = *(int *)data;

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer->cursor->y_offset -= step / 2.0;
    }
}

void scroll_half_page_down(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    int step = *(int *)data;

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer->cursor->y_offset += step / 2.0;
    }
}

void scroll_left(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer->cursor->x_offset++;
    }
}

void scroll_down(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer->cursor->y_offset++;
    }
}

void scroll_up(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer->cursor->y_offset--;
    }
}

void scroll_right(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(data);

    for (unsigned int i = 0; i < repeat_count; i++) {
        viewer->cursor->x_offset--;
    }
}

void forward_search(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    ViewerMarkManager *mark_manager = (ViewerMarkManager *)data;

    ViewerCursor *current_cursor = viewer_mark_manager_get_current_cursor(mark_manager);
    ViewerCursor *search_new_cursor = current_cursor;
    ViewerCursor *last_search_cursor = NULL;
    ViewerCursor *resulting_cursor = NULL;

    for (unsigned int i = 0; i < repeat_count; i++) {
        last_search_cursor = search_new_cursor;
        search_new_cursor = viewer_search_get_next_search(viewer->search, viewer->info,
            viewer->viewport, search_new_cursor);
        if (search_new_cursor == NULL) {
            resulting_cursor = last_search_cursor;
            break;
        }

        viewer_cursor_destroy(last_search_cursor);
        free(last_search_cursor);
        resulting_cursor = search_new_cursor;
    }

    viewer_mark_manager_set_current_cursor(mark_manager, resulting_cursor);
}

void backward_search(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    ViewerMarkManager *mark_manager = (ViewerMarkManager *)data;

    ViewerCursor *current_cursor = viewer_mark_manager_get_current_cursor(mark_manager);
    ViewerCursor *search_new_cursor = current_cursor;
    ViewerCursor *last_search_cursor = NULL;
    ViewerCursor *resulting_cursor = NULL;

    for (unsigned int i = 0; i < repeat_count; i++) {
        last_search_cursor = search_new_cursor;
        search_new_cursor = viewer_search_get_prev_search(viewer->search, viewer->info,
            viewer->viewport, search_new_cursor);
        if (search_new_cursor == NULL) {
            resulting_cursor = last_search_cursor;
            break;
        }

        viewer_cursor_destroy(last_search_cursor);
        free(last_search_cursor);
        resulting_cursor = search_new_cursor;
    }

    viewer_mark_manager_set_current_cursor(mark_manager, resulting_cursor);
}

void switch_to_previous_mark(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(viewer);
    UNUSED(repeat_count);

    ViewerMarkManager *mark_manager = (ViewerMarkManager *)data;
    viewer_mark_manager_switch_to_previous_mark(mark_manager);
}

void switch_to_previous_group(struct Viewer *viewer, unsigned int repeat_count, void *data)
{
    UNUSED(viewer);
    UNUSED(repeat_count);

    ViewerMarkManager *mark_manager = (ViewerMarkManager *)data;
    viewer_mark_manager_switch_to_previous_group(mark_manager);
}