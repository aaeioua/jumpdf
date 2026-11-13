#include <stdlib.h>

#include <gtk/gtk.h>

#include "input.h"
#include "config.h"
#include "utils.h"

/* The Count, and a link number, stop growing after this many digits */
#define MAX_COUNT_DIGITS 4
/*
 * Longest sequence: f, a link number and Enter. Pending keys are always a
 * proper prefix of some sequence, so one more key always fits.
 */
#define MAX_SEQUENCE_KEYS (1 + MAX_COUNT_DIGITS + 1)
#define MAX_SEQUENCES 2

typedef enum {
    COMMAND_NONE,
    COMMAND_MOVE_DOWN,
    COMMAND_MOVE_UP,
    COMMAND_MOVE_LEFT,
    COMMAND_MOVE_RIGHT,
    COMMAND_HALF_PAGE_DOWN,
    COMMAND_HALF_PAGE_UP,
    COMMAND_ZOOM_IN,
    COMMAND_ZOOM_OUT,
    COMMAND_SEARCH_NEXT,
    COMMAND_SEARCH_PREVIOUS,
    COMMAND_RESET_ZOOM,
    COMMAND_TOGGLE_CENTER_MODE,
    COMMAND_TOGGLE_DARK_MODE,
    COMMAND_FIT_WIDTH,
    COMMAND_FIT_HEIGHT,
    COMMAND_FIRST_PAGE,
    COMMAND_LAST_PAGE,
    COMMAND_GOTO_PAGE,
    COMMAND_FOLLOW_LINK,
    COMMAND_SWITCH_MARK,
    COMMAND_PREVIOUS_MARK,
    COMMAND_CLEAR_MARK,
    COMMAND_SWAP_MARK,
    COMMAND_OVERWRITE_MARK,
    COMMAND_SWITCH_GROUP,
    COMMAND_PREVIOUS_GROUP,
    COMMAND_SWAP_GROUP,
    COMMAND_OVERWRITE_GROUP,
    COMMAND_REPEAT,
    COMMAND_REPEAT_JUMP,
    COMMAND_SHOW_SEARCH,
    COMMAND_OPEN_FILE,
    COMMAND_TOGGLE_OUTLINE,
    COMMAND_OUTLINE_NEXT,
    COMMAND_OUTLINE_PREVIOUS,
    COMMAND_OUTLINE_FOCUS_FILTER,
    COMMAND_OUTLINE_ACTIVATE,
    COMMAND_TOGGLE_FULLSCREEN,
    COMMAND_SHOW_HELP,
} CommandKind;

/* A Command is data: what to do, its Count, and the Group or Mark it acts on */
typedef struct {
    CommandKind kind;
    /* How many times, or which page or link */
    unsigned int count;
    /* The Group or Mark */
    unsigned int index;
} Command;

typedef enum {
    MODE_NORMAL = 1 << 0,
    MODE_OUTLINE = 1 << 1,
} Mode;

typedef enum {
    /* A Count may precede it */
    BINDING_COUNTED = 1 << 0,
    /* `.` repeats it */
    BINDING_REPEATABLE = 1 << 1,
    /* `,` repeats it */
    BINDING_JUMP = 1 << 2,
} BindingFlags;

/*
 * Key sequences are written in help notation: "<1-9>" is the index digit,
 * "<number>" is a Count, a named key's name stands for that key and any other
 * character for itself. A sequence never ends in "<number>".
 */
typedef struct {
    /* COMMAND_NONE for a section header, which describes no binding */
    CommandKind kind;
    const char *keys[MAX_SEQUENCES];
    const char *description;
    int indent;
    /* The Modes it is active in; 0 for a section header */
    unsigned int modes;
    unsigned int flags;
} Binding;

#define NORMAL MODE_NORMAL
#define OUTLINE MODE_OUTLINE
#define COUNTED_REPEATABLE (BINDING_COUNTED | BINDING_REPEATABLE)
#define REPEATABLE_JUMP (BINDING_REPEATABLE | BINDING_JUMP)

static const Binding bindings[] = {
    {COMMAND_NONE, {"<number><command>"}, "Repeats <command> <number> times", 0, 0, 0},
    {COMMAND_MOVE_DOWN, {"j", "Down"}, "Move down", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_MOVE_UP, {"k", "Up"}, "Move up", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_MOVE_LEFT, {"h", "Left"}, "Move left (not in center mode)", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_MOVE_RIGHT, {"l", "Right"}, "Move right (not in center mode)", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_HALF_PAGE_DOWN, {"d", "PgDn"}, "Move down half a page", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_HALF_PAGE_UP, {"u", "PgUp"}, "Move up half a page", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_ZOOM_IN, {"+"}, "Zoom in", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_ZOOM_OUT, {"-"}, "Zoom out", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_SEARCH_NEXT, {"n"}, "Go to the next page containing the search text", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_SEARCH_PREVIOUS, {"N"}, "Go to the previous page containing the search text", 1, NORMAL, COUNTED_REPEATABLE},
    {COMMAND_RESET_ZOOM, {"0"}, "Reset zoom", 0, NORMAL, 0},
    {COMMAND_TOGGLE_CENTER_MODE, {"c"}, "Toggle center mode", 0, NORMAL, 0},
    {COMMAND_TOGGLE_DARK_MODE, {"b"}, "Toggle dark mode", 0, NORMAL, 0},
    {COMMAND_FIT_WIDTH, {"s"}, "Fit horizontally to page", 0, NORMAL, 0},
    {COMMAND_FIT_HEIGHT, {"a"}, "Fit vertically to page", 0, NORMAL, 0},
    {COMMAND_FIRST_PAGE, {"gg", "Home"}, "Go to the first page", 0, NORMAL, 0},
    {COMMAND_LAST_PAGE, {"G", "End"}, "Go to the last page", 0, NORMAL, 0},
    {COMMAND_GOTO_PAGE, {"<number>G"}, "Go to page <number>", 0, NORMAL, 0},
    {COMMAND_FOLLOW_LINK, {"f<number>Enter"}, "Show link numbers and follow link <number>", 0, NORMAL, 0},
    {COMMAND_SWITCH_MARK, {"m<1-9>"}, "Switch to mark <1-9>, setting it to the current Mark if empty", 0, NORMAL, 0},
    {COMMAND_PREVIOUS_MARK, {"mn"}, "Switch to the previous mark", 0, NORMAL, REPEATABLE_JUMP},
    {COMMAND_CLEAR_MARK, {"mc<1-9>"}, "Clear mark <1-9>", 0, NORMAL, 0},
    {COMMAND_SWAP_MARK, {"ms<1-9>"}, "Swap the current mark with mark <1-9> and switch to it", 0, NORMAL, 0},
    {COMMAND_OVERWRITE_MARK, {"mo<1-9>"}, "Overwrite mark <1-9> with the current Mark and switch to it", 0, NORMAL, 0},
    {COMMAND_SWITCH_GROUP, {"g<1-9>"}, "Switch to group <1-9>, copying the current Mark if it has none", 0, NORMAL, 0},
    {COMMAND_PREVIOUS_GROUP, {"gn"}, "Switch to the previous group", 0, NORMAL, REPEATABLE_JUMP},
    {COMMAND_SWAP_GROUP, {"gs<1-9>"}, "Swap the current group with group <1-9> and switch to it", 0, NORMAL, 0},
    {COMMAND_OVERWRITE_GROUP, {"go<1-9>"}, "Overwrite group <1-9> with the current group and switch to it", 0, NORMAL, 0},
    {COMMAND_REPEAT, {"."}, "Repeat the last command (zoom, scroll, search, mn or gn)", 0, NORMAL, 0},
    {COMMAND_REPEAT_JUMP, {","}, "Repeat the last jump command (mn or gn)", 0, NORMAL, 0},
    {COMMAND_SHOW_SEARCH, {"/"}, "Show search dialog (Esc hides it)", 0, NORMAL, 0},
    {COMMAND_OPEN_FILE, {"o"}, "Open file chooser", 0, NORMAL, 0},
    {COMMAND_TOGGLE_OUTLINE, {"Tab"}, "Toggle table of contents", 0, NORMAL | OUTLINE, 0},
    {COMMAND_OUTLINE_NEXT, {"j"}, "Move down in table of contents", 1, OUTLINE, 0},
    {COMMAND_OUTLINE_PREVIOUS, {"k"}, "Move up in table of contents", 1, OUTLINE, 0},
    {COMMAND_OUTLINE_FOCUS_FILTER, {"/"}, "Focus the outline filter (Esc returns to the outline)", 1, OUTLINE, 0},
    {COMMAND_OUTLINE_ACTIVATE, {"Enter"}, "Go to the selected entry", 1, OUTLINE, 0},
    {COMMAND_TOGGLE_FULLSCREEN, {"F11"}, "Toggle fullscreen", 0, NORMAL, 0},
    {COMMAND_SHOW_HELP, {"?"}, "Show help dialog", 0, NORMAL, 0},
};

#undef NORMAL
#undef OUTLINE
#undef COUNTED_REPEATABLE
#undef REPEATABLE_JUMP

typedef struct {
    const char *name;
    guint keyval;
} NamedKey;

static const NamedKey named_keys[] = {
    {"Down", GDK_KEY_Down},
    {"Up", GDK_KEY_Up},
    {"Left", GDK_KEY_Left},
    {"Right", GDK_KEY_Right},
    {"PgDn", GDK_KEY_Page_Down},
    {"PgUp", GDK_KEY_Page_Up},
    {"Home", GDK_KEY_Home},
    {"End", GDK_KEY_End},
    {"Tab", GDK_KEY_Tab},
    {"Enter", GDK_KEY_Return},
    {"F11", GDK_KEY_F11},
};

typedef enum {
    TOKEN_END,
    TOKEN_KEY,
    TOKEN_INDEX,
    TOKEN_NUMBER,
} TokenKind;

typedef struct {
    TokenKind kind;
    guint keyval;
} Token;

typedef enum {
    MATCH_NONE,
    MATCH_PARTIAL,
    MATCH_COMPLETE,
} Match;

/* What the pending keys match in the binding table */
typedef struct {
    const Binding *complete;
    Command command;
    gboolean partial;
    /* The most digits a partial match's trailing Count has */
    guint open_digits;
    /* A link is being chosen: the pending keys start COMMAND_FOLLOW_LINK */
    gboolean following;
} Lookup;

struct Input {
    Mode mode;
    guint keys[MAX_SEQUENCE_KEYS];
    guint n_keys;
    /* Digits in the Count the pending keys end in, if they end in one */
    guint open_digits;
    GString *pending;
    Command last;
    Command last_jump;
};

static InputRequest dispatch(Input *input, Viewer *viewer, guint keyval);
static Lookup lookup(const Input *input);
static Match match_sequence(const char *sequence, gboolean counted,
    const guint *keys, guint n_keys, Command *command, guint *open_digits);
static guint match_number(const guint *keys, guint n_keys, guint k,
    unsigned int *number);
static Token next_token(const char **cursor);
/* Records the Command as the last (jump) Command if it is one, then executes it */
static InputRequest run_binding(Input *input, Viewer *viewer,
    const Binding *binding, Command command);
static InputRequest execute(Input *input, Viewer *viewer, Command command);
static void search(Viewer *viewer, unsigned int count, gboolean forward);
static void follow_link(Viewer *viewer, unsigned int number);
static void clear_pending(Input *input);
static void update_pending_text(Input *input);
static gboolean is_digit(guint keyval);
static gboolean is_modifier(guint keyval);

Input *input_new(void)
{
    Input *input = g_new0(Input, 1);

    input->mode = MODE_NORMAL;
    input->pending = g_string_new(NULL);

    return input;
}

void input_free(Input *input)
{
    if (input == NULL) {
        return;
    }

    g_string_free(input->pending, TRUE);
    g_free(input);
}

InputRequest input_feed(Input *input, Viewer *viewer, guint keyval)
{
    InputRequest request = INPUT_REQUEST_NONE;

    if (!is_modifier(keyval) &&
        !(is_digit(keyval) && input->open_digits >= MAX_COUNT_DIGITS)) {
        request = dispatch(input, viewer, keyval);
    }

    viewer_cursor_handle_offset_update(viewer_current_mark(viewer),
        viewer->info->n_pages);

    return request;
}

void input_scroll(Input *input, Viewer *viewer, double dx, double dy,
    gboolean zoom)
{
    UNUSED(input);

    ViewerCursor *mark = viewer_current_mark(viewer);

    if (zoom) {
        viewer_cursor_set_scale(mark, mark->scale - dy * g_config->scale_step);
    } else {
        mark->x_offset -= dx;
        mark->y_offset += dy;
        viewer_cursor_handle_offset_update(mark, viewer->info->n_pages);
    }
}

const char *input_pending(const Input *input)
{
    return input->pending->str;
}

GArray *input_help(void)
{
    GArray *rows = g_array_sized_new(FALSE, FALSE, sizeof(InputHelpRow),
        G_N_ELEMENTS(bindings));

    for (gsize i = 0; i < G_N_ELEMENTS(bindings); i++) {
        const Binding *binding = &bindings[i];
        GStrvBuilder *builder = g_strv_builder_new();
        GStrv keys;
        InputHelpRow row;

        for (gsize s = 0; s < MAX_SEQUENCES && binding->keys[s] != NULL; s++) {
            g_strv_builder_add(builder, binding->keys[s]);
        }
        keys = g_strv_builder_end(builder);
        g_strv_builder_unref(builder);

        row.keys = g_strjoinv(", ", keys);
        row.description = binding->description;
        row.indent = binding->indent;
        g_array_append_val(rows, row);

        g_strfreev(keys);
    }

    return rows;
}

void input_help_free(GArray *rows)
{
    for (guint i = 0; i < rows->len; i++) {
        g_free(g_array_index(rows, InputHelpRow, i).keys);
    }

    g_array_free(rows, TRUE);
}

/* Adds the key to the pending keys and runs the Command they complete, if any */
static InputRequest dispatch(Input *input, Viewer *viewer, guint keyval)
{
    InputRequest request = INPUT_REQUEST_NONE;
    Lookup found;

    g_assert(input->n_keys < MAX_SEQUENCE_KEYS);
    input->keys[input->n_keys++] = keyval;
    found = lookup(input);
    viewer->links->follow_links_mode = found.following;

    if (found.complete != NULL) {
        clear_pending(input);
        request = run_binding(input, viewer, found.complete, found.command);
    } else if (found.partial) {
        input->open_digits = found.open_digits;
    } else {
        clear_pending(input);
    }

    update_pending_text(input);

    return request;
}

/* The first complete match in table order wins over any partial one */
static Lookup lookup(const Input *input)
{
    Lookup found = {0};

    for (gsize i = 0; i < G_N_ELEMENTS(bindings); i++) {
        const Binding *binding = &bindings[i];

        if ((binding->modes & input->mode) == 0) {
            continue;
        }

        for (gsize s = 0; s < MAX_SEQUENCES && binding->keys[s] != NULL; s++) {
            Command command = {.kind = binding->kind};
            guint open_digits;
            Match match = match_sequence(binding->keys[s],
                binding->flags & BINDING_COUNTED, input->keys, input->n_keys,
                &command, &open_digits);

            if (match == MATCH_COMPLETE) {
                found.complete = binding;
                found.command = command;
                return found;
            } else if (match == MATCH_PARTIAL) {
                found.partial = TRUE;
                found.open_digits = MAX(found.open_digits, open_digits);
                found.following |= binding->kind == COMMAND_FOLLOW_LINK;
            }
        }
    }

    return found;
}

/*
 * A partial match is a proper prefix of the sequence. `counted` lets a Count,
 * which never starts with 0, precede the sequence. Fills in the command's
 * count and index. On a partial match that ends in a Count, *open_digits is
 * its number of digits.
 */
static Match match_sequence(const char *sequence, gboolean counted,
    const guint *keys, guint n_keys, Command *command, guint *open_digits)
{
    const char *cursor = sequence;
    guint k = 0;

    command->count = 1;
    command->index = 0;
    *open_digits = 0;

    if (counted && keys[0] >= GDK_KEY_1 && keys[0] <= GDK_KEY_9) {
        k = match_number(keys, n_keys, 0, &command->count);
        if (k == n_keys) {
            *open_digits = k;
            return MATCH_PARTIAL;
        }
    }

    for (Token token = next_token(&cursor); token.kind != TOKEN_END;
         token = next_token(&cursor)) {
        guint start = k;

        if (k == n_keys) {
            return MATCH_PARTIAL;
        }

        switch (token.kind) {
        case TOKEN_KEY:
            if (keys[k] != token.keyval) {
                return MATCH_NONE;
            }
            k++;
            break;
        case TOKEN_INDEX:
            if (keys[k] < GDK_KEY_1 || keys[k] > GDK_KEY_9) {
                return MATCH_NONE;
            }
            command->index = keys[k] - GDK_KEY_1;
            k++;
            break;
        case TOKEN_NUMBER:
            k = match_number(keys, n_keys, k, &command->count);
            if (k == start) {
                return MATCH_NONE;
            } else if (k == n_keys) {
                *open_digits = k - start;
                return MATCH_PARTIAL;
            }
            break;
        case TOKEN_END:
            g_assert_not_reached();
        }
    }

    return k == n_keys ? MATCH_COMPLETE : MATCH_NONE;
}

/* Returns the index after the digits starting at k */
static guint match_number(const guint *keys, guint n_keys, guint k,
    unsigned int *number)
{
    *number = 0;

    while (k < n_keys && is_digit(keys[k])) {
        *number = *number * 10 + (keys[k] - GDK_KEY_0);
        k++;
    }

    return k;
}

static Token next_token(const char **cursor)
{
    const char *s = *cursor;

    if (*s == '\0') {
        return (Token){TOKEN_END, 0};
    } else if (g_str_has_prefix(s, "<1-9>")) {
        *cursor += strlen("<1-9>");
        return (Token){TOKEN_INDEX, 0};
    } else if (g_str_has_prefix(s, "<number>")) {
        *cursor += strlen("<number>");
        return (Token){TOKEN_NUMBER, 0};
    }

    for (gsize i = 0; i < G_N_ELEMENTS(named_keys); i++) {
        if (g_str_has_prefix(s, named_keys[i].name)) {
            *cursor += strlen(named_keys[i].name);
            return (Token){TOKEN_KEY, named_keys[i].keyval};
        }
    }

    *cursor += 1;
    return (Token){TOKEN_KEY, gdk_unicode_to_keyval((guchar)*s)};
}

static InputRequest run_binding(Input *input, Viewer *viewer,
    const Binding *binding, Command command)
{
    if (binding->flags & BINDING_REPEATABLE) {
        input->last = command;
    }
    if (binding->flags & BINDING_JUMP) {
        input->last_jump = command;
    }

    return execute(input, viewer, command);
}

static InputRequest execute(Input *input, Viewer *viewer, Command command)
{
    ViewerCursor *mark = viewer_current_mark(viewer);
    const double half_page = g_config->steps / 2.0;

    switch (command.kind) {
    case COMMAND_NONE:
        break;
    case COMMAND_MOVE_DOWN:
        mark->y_offset += command.count;
        break;
    case COMMAND_MOVE_UP:
        mark->y_offset -= command.count;
        break;
    case COMMAND_MOVE_LEFT:
        mark->x_offset += command.count;
        break;
    case COMMAND_MOVE_RIGHT:
        mark->x_offset -= command.count;
        break;
    case COMMAND_HALF_PAGE_DOWN:
        mark->y_offset += command.count * half_page;
        break;
    case COMMAND_HALF_PAGE_UP:
        mark->y_offset -= command.count * half_page;
        break;
    case COMMAND_ZOOM_IN:
        viewer_cursor_set_scale(mark,
            mark->scale + command.count * g_config->scale_step);
        break;
    case COMMAND_ZOOM_OUT:
        viewer_cursor_set_scale(mark,
            mark->scale - command.count * g_config->scale_step);
        break;
    case COMMAND_SEARCH_NEXT:
        search(viewer, command.count, TRUE);
        break;
    case COMMAND_SEARCH_PREVIOUS:
        search(viewer, command.count, FALSE);
        break;
    case COMMAND_RESET_ZOOM:
        viewer_cursor_set_scale(mark, 1.0);
        break;
    case COMMAND_TOGGLE_CENTER_MODE:
        viewer_cursor_toggle_center_mode(mark);
        break;
    case COMMAND_TOGGLE_DARK_MODE:
        viewer_cursor_toggle_dark_mode(mark);
        break;
    case COMMAND_FIT_WIDTH:
        viewport_fit_width(viewer->viewport, mark);
        break;
    case COMMAND_FIT_HEIGHT:
        viewport_fit_height(viewer->viewport, mark);
        break;
    case COMMAND_FIRST_PAGE:
        mark->current_page = 0;
        mark->y_offset = 0;
        break;
    case COMMAND_LAST_PAGE:
        mark->current_page = viewer->info->n_pages - 1;
        mark->y_offset = 0;
        break;
    case COMMAND_GOTO_PAGE:
        viewport_goto_page(viewer->viewport, mark, (int)command.count - 1);
        break;
    case COMMAND_FOLLOW_LINK:
        follow_link(viewer, command.count);
        break;
    case COMMAND_SWITCH_MARK:
        group_selection_switch_mark(viewer->selection, command.index);
        break;
    case COMMAND_PREVIOUS_MARK:
        group_selection_switch_to_previous_mark(viewer->selection);
        break;
    case COMMAND_CLEAR_MARK:
        group_selection_clear_mark(viewer->selection, command.index);
        break;
    case COMMAND_SWAP_MARK:
        group_selection_swap_mark(viewer->selection, command.index);
        break;
    case COMMAND_OVERWRITE_MARK:
        group_selection_overwrite_mark(viewer->selection, command.index);
        break;
    case COMMAND_SWITCH_GROUP:
        group_selection_switch_group(viewer->selection, command.index);
        break;
    case COMMAND_PREVIOUS_GROUP:
        group_selection_switch_to_previous_group(viewer->selection);
        break;
    case COMMAND_SWAP_GROUP:
        group_selection_swap_group(viewer->selection, command.index);
        break;
    case COMMAND_OVERWRITE_GROUP:
        group_selection_overwrite_group(viewer->selection, command.index);
        break;
    case COMMAND_REPEAT:
        /* Never itself repeatable, so this does not recurse further */
        execute(input, viewer, input->last);
        break;
    case COMMAND_REPEAT_JUMP:
        execute(input, viewer, input->last_jump);
        break;
    case COMMAND_SHOW_SEARCH:
        return INPUT_REQUEST_SHOW_SEARCH;
    case COMMAND_OPEN_FILE:
        return INPUT_REQUEST_OPEN_FILE;
    case COMMAND_TOGGLE_OUTLINE:
        input->mode = input->mode == MODE_OUTLINE ? MODE_NORMAL : MODE_OUTLINE;
        return INPUT_REQUEST_TOGGLE_OUTLINE;
    case COMMAND_OUTLINE_NEXT:
        return INPUT_REQUEST_OUTLINE_NEXT;
    case COMMAND_OUTLINE_PREVIOUS:
        return INPUT_REQUEST_OUTLINE_PREVIOUS;
    case COMMAND_OUTLINE_FOCUS_FILTER:
        return INPUT_REQUEST_OUTLINE_FOCUS_FILTER;
    case COMMAND_OUTLINE_ACTIVATE:
        return INPUT_REQUEST_OUTLINE_ACTIVATE;
    case COMMAND_TOGGLE_FULLSCREEN:
        return INPUT_REQUEST_TOGGLE_FULLSCREEN;
    case COMMAND_SHOW_HELP:
        return INPUT_REQUEST_SHOW_HELP;
    }

    return INPUT_REQUEST_NONE;
}

/* Moves `count` matching pages on, stopping at the last match */
static void search(Viewer *viewer, unsigned int count, gboolean forward)
{
    ViewerCursor *mark = viewer_current_mark(viewer);

    for (unsigned int i = 0; i < count; i++) {
        ViewerCursor *found = forward
            ? viewer_search_get_next_search(viewer->search, viewer->info,
                viewer->viewport, mark)
            : viewer_search_get_prev_search(viewer->search, viewer->info,
                viewer->viewport, mark);
        if (found == NULL) {
            break;
        }

        *mark = *found;
        viewer_cursor_destroy(found);
        free(found);
    }
}

/* Links are numbered from 1; a number out of range does nothing */
static void follow_link(Viewer *viewer, unsigned int number)
{
    GPtrArray *visible_links = viewer->links->visible_links;
    PopplerLinkMapping *link_mapping;

    if (number < 1 || number > visible_links->len) {
        return;
    }

    link_mapping = g_ptr_array_index(visible_links, number - 1);
    viewer_execute_action(viewer, link_mapping->action);
}

static void clear_pending(Input *input)
{
    input->n_keys = 0;
    input->open_digits = 0;
}

static void update_pending_text(Input *input)
{
    g_string_truncate(input->pending, 0);

    for (guint i = 0; i < input->n_keys; i++) {
        g_string_append_unichar(input->pending,
            gdk_keyval_to_unicode(input->keys[i]));
    }
}

static gboolean is_digit(guint keyval)
{
    return keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9;
}

/* As X11's IsModifierKey */
static gboolean is_modifier(guint keyval)
{
    return (keyval >= GDK_KEY_Shift_L && keyval <= GDK_KEY_Hyper_R) ||
        (keyval >= GDK_KEY_ISO_Lock && keyval <= GDK_KEY_ISO_Level5_Lock) ||
        keyval == GDK_KEY_Mode_switch ||
        keyval == GDK_KEY_Num_Lock;
}
