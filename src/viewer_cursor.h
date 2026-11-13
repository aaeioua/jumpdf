#pragma once

#include <stdbool.h>

typedef struct ViewerCursor {
    int current_page;
    double x_offset, y_offset, scale;
    bool center_mode;
    bool dark_mode;
} ViewerCursor;

ViewerCursor *viewer_cursor_new(int current_page,
                                double x_offset,
                                double y_offset,
                                double scale,
                                bool center_mode,
                                bool dark_mode);
ViewerCursor *viewer_cursor_copy(ViewerCursor *cursor);
void viewer_cursor_init(ViewerCursor *cursor,
                        int current_page,
                        double x_offset,
                        double y_offset,
                        double scale,
                        bool center_mode,
                        bool dark_mode);
void viewer_cursor_destroy(ViewerCursor *cursor);

void viewer_cursor_toggle_center_mode(ViewerCursor *cursor);
void viewer_cursor_center(ViewerCursor *cursor);
void viewer_cursor_toggle_dark_mode(ViewerCursor *cursor);
void viewer_cursor_set_scale(ViewerCursor *cursor, double new_scale);
void viewer_cursor_handle_offset_update(ViewerCursor *cursor, int n_pages);
