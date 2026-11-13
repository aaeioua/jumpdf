#pragma once

#include <glib.h>

#include "viewer.h"

/*
 * Turns key presses and scroll events into Commands and applies them to a
 * Viewer.
 */
typedef struct Input Input;

/* What the window must do itself after a key press */
typedef enum {
    INPUT_REQUEST_NONE,
    INPUT_REQUEST_TOGGLE_OUTLINE,
    INPUT_REQUEST_OUTLINE_NEXT,
    INPUT_REQUEST_OUTLINE_PREVIOUS,
    INPUT_REQUEST_OUTLINE_ACTIVATE,
    INPUT_REQUEST_OUTLINE_FOCUS_FILTER,
    INPUT_REQUEST_SHOW_SEARCH,
    INPUT_REQUEST_SHOW_HELP,
    INPUT_REQUEST_OPEN_FILE,
    INPUT_REQUEST_TOGGLE_FULLSCREEN,
} InputRequest;

typedef struct {
    gchar *keys;
    const char *description;
    int indent;
} InputHelpRow;

Input *input_new(void);
void input_free(Input *input);

/*
 * Applies the Command `keyval` completes, if any, to the Viewer, then
 * normalises the current Mark's offset.
 */
InputRequest input_feed(Input *input, Viewer *viewer, guint keyval);
/*
 * Scrolls the current Mark by (dx, dy), or with `zoom` scales it by dy steps.
 * Leaves the pending keys, the Count and the last Command alone.
 */
void input_scroll(Input *input, Viewer *viewer, double dx, double dy,
    gboolean zoom);
/* What has been typed so far. Valid until the next input_feed. */
const char *input_pending(const Input *input);

/* One row per binding table row, in table order. */
GArray *input_help(void);
void input_help_free(GArray *rows);
