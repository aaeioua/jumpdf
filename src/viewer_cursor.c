#include <stdlib.h>

#include <glib.h>

#include "viewer_cursor.h"
#include "config.h"
#include "utils.h"

ViewerCursor *viewer_cursor_new(int current_page,
                                double x_offset,
                                double y_offset,
                                double scale,
                                bool center_mode,
                                bool dark_mode,
                                unsigned int input_number)
{
    ViewerCursor *cursor = malloc(sizeof(ViewerCursor));
    if (cursor == NULL) {
        return NULL;
    }

    viewer_cursor_init(cursor, current_page, x_offset, y_offset, scale, center_mode, dark_mode, input_number);

    return cursor;
}

ViewerCursor *viewer_cursor_copy(ViewerCursor *cursor)
{
    return viewer_cursor_new(cursor->current_page,
                             cursor->x_offset,
                             cursor->y_offset,
                             cursor->scale,
                             cursor->center_mode,
                             cursor->dark_mode,
                             cursor->input_number);
}

void viewer_cursor_init(ViewerCursor *cursor,
                        int current_page,
                        double x_offset,
                        double y_offset,
                        double scale,
                        bool center_mode,
                        bool dark_mode,
                        unsigned int input_number)
{
    cursor->current_page = current_page;
    cursor->x_offset = x_offset;
    cursor->y_offset = y_offset;
    cursor->scale = scale;
    cursor->center_mode = center_mode;
    cursor->dark_mode = dark_mode;
    cursor->input_number = input_number;
}

void viewer_cursor_destroy(ViewerCursor *cursor)
{
    UNUSED(cursor);
}

void viewer_cursor_toggle_center_mode(ViewerCursor *cursor)
{
    cursor->center_mode = !cursor->center_mode;
}

void viewer_cursor_toggle_dark_mode(ViewerCursor *cursor) {
    cursor->dark_mode = !cursor->dark_mode;
}

void viewer_cursor_center(ViewerCursor *cursor)
{
    cursor->x_offset = 0;
}

void viewer_cursor_set_scale(ViewerCursor *cursor, double new)
{
    cursor->scale = MAX(g_config->min_scale, new);
}

void viewer_cursor_handle_offset_update(ViewerCursor *cursor, int n_pages)
{
    if (cursor->center_mode) {
        viewer_cursor_center(cursor);
    }

    const int old_page = cursor->current_page;
    cursor->current_page = CLAMP(
        cursor->current_page + cursor->y_offset / g_config->steps,
        0,
        n_pages - 1);

    if (cursor->y_offset < 0 && cursor->current_page < old_page) {
        cursor->y_offset = g_config->steps - 1;
    } else if (cursor->y_offset >= g_config->steps && cursor->current_page > old_page) {
        cursor->y_offset = 0;
    }
}
