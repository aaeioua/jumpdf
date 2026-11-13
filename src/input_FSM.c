#include "app.h"
#include "input_FSM.h"
#include "input_cmd.h"
#include "config.h"
#include "viewer_info.h"
#include "viewer_cursor.h"
#include "viewer_search.h"
#include "viewer_links.h"

typedef InputState (*input_state_func)(Window *, guint);

static InputState execute_command(Window *window, guint keyval, unsigned int repeat_count);

input_state_func input_state_funcs[] = {
    on_state_normal,
    on_state_g,
    on_state_number,
    on_state_follow_links,
    on_state_toc_focus,
    on_state_group_swap,
    on_state_group_overwrite,
    on_state_mark,
    on_state_mark_clear,
    on_state_mark_swap,
    on_state_mark_overwrite,
};

InputState on_state_normal(Window *window, guint keyval)
{
    InputState next_state = STATE_NORMAL;
    Viewer *viewer = window_get_viewer(window);
    ViewerCursor *mark = viewer_current_mark(viewer);

    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        mark->input_number = keyval - GDK_KEY_0;
        next_state = STATE_NUMBER;
    } else {
        switch (keyval) {
        case GDK_KEY_0:
            viewer_cursor_set_scale(mark, 1.0);
            break;
        case GDK_KEY_c:
            viewer_cursor_toggle_center_mode(mark);
            break;
        case GDK_KEY_b:
            viewer_cursor_toggle_dark_mode(mark);
            break;
        case GDK_KEY_s:
            viewport_fit_width(viewer->viewport, mark);
            break;
        case GDK_KEY_a:
            viewport_fit_height(viewer->viewport, mark);
            break;
        case GDK_KEY_g:
            next_state = STATE_g;
            break;
        case GDK_KEY_Home:
            mark->current_page = 0;
            mark->y_offset = 0;
            break;
        case GDK_KEY_G:
        case GDK_KEY_End:
            mark->current_page = viewer->info->n_pages - 1;
            mark->y_offset = 0;
            break;
        case GDK_KEY_f:
            viewer->links->follow_links_mode = true;
            mark->input_number = 0;
            next_state = STATE_FOLLOW_LINKS;
            break;
        case GDK_KEY_m:
            next_state = STATE_MARK;
            break;
        case GDK_KEY_Tab:
            window_toggle_toc(window);
            next_state = STATE_TOC_FOCUS;
            break;
        case GDK_KEY_slash:
            window_show_search_dialog(window);
            break;
        case GDK_KEY_question:
            window_show_help_dialog(window);
            break;
        case GDK_KEY_o:
            app_open_file_chooser(JUMPDF_APP(gtk_window_get_application(GTK_WINDOW(window))));
            break;
        case GDK_KEY_F11:
            window_toggle_fullscreen(window);
            break;
        case GDK_KEY_period:
            command_execute(&viewer->last_command, viewer);
            break;
        case GDK_KEY_comma:
            command_execute(&viewer->last_jump_command, viewer);
            break;
        default:
            next_state = execute_command(window, keyval, 1);
            break;
        }
    }

    return next_state;
}

InputState on_state_g(Window *window, guint keyval)
{
    InputState next_state = STATE_NORMAL;
    GroupSelection *selection = window_get_group_selection(window);
    Viewer *viewer = window_get_viewer(window);
    unsigned int group_i = keyval - GDK_KEY_0 - 1;

    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        group_selection_switch_group(selection, group_i);
    } else if (keyval == GDK_KEY_n) {
        viewer->last_command.execute = (CommandExecute)switch_to_previous_group;
        viewer->last_command.repeat_count = 1;
        viewer->last_command.data = NULL;
        viewer->last_jump_command = command_copy(&viewer->last_command);

        command_execute(&viewer->last_command, viewer);
    } else if (keyval == GDK_KEY_s) {
        next_state = STATE_GROUP_SWAP;
    } else if (keyval == GDK_KEY_o) {
        next_state = STATE_GROUP_OVERWRITE;
    } else if (keyval == GDK_KEY_g) {
        ViewerCursor *mark = viewer_current_mark(viewer);
        mark->current_page = 0;
        mark->y_offset = 0;
    }

    return next_state;
}

InputState on_state_number(Window *window, guint keyval)
{
    InputState next_state;
    Viewer *viewer = window_get_viewer(window);
    ViewerCursor *mark = viewer_current_mark(viewer);

    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) {
        mark->input_number = mark->input_number * 10 + (keyval - GDK_KEY_0);
        next_state = STATE_NUMBER;
    } else if (keyval == GDK_KEY_G) {
        viewport_goto_page(viewer->viewport, mark,
            (int)mark->input_number - 1);
        mark->input_number = 0;
        next_state = STATE_NORMAL;
    } else if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R) {
        // Ignore shift key. Necessary for GDK_KEY_G
        next_state = STATE_NUMBER;
    } else {
        next_state = execute_command(window, keyval, mark->input_number);
        mark->input_number = 0;
    }

    return next_state;
}

InputState on_state_follow_links(Window *window, guint keyval)
{
    InputState next_state;
    Viewer *viewer = window_get_viewer(window);
    ViewerCursor *mark = viewer_current_mark(viewer);
    PopplerLinkMapping *link_mapping;

    if (keyval >= GDK_KEY_0 && keyval <= GDK_KEY_9) {
        mark->input_number = mark->input_number * 10 + (keyval - GDK_KEY_0);
        next_state = STATE_FOLLOW_LINKS;
    } else if (keyval == GDK_KEY_Return && mark->input_number - 1 < viewer->links->visible_links->len) {
        link_mapping = g_ptr_array_index(viewer->links->visible_links, mark->input_number - 1);
        viewer_execute_action(viewer, link_mapping->action);
        viewer->links->follow_links_mode = false;
        next_state = STATE_NORMAL;
    } else {
        mark->input_number = 0;
        viewer->links->follow_links_mode = false;
        next_state = STATE_NORMAL;
    }

    return next_state;
}

InputState on_state_toc_focus(Window *window, guint keyval)
{
    InputState next_state;
    GtkListBox *toc_list_box = window_get_toc_listbox(window);
    GtkListBoxRow *current_row = gtk_list_box_get_selected_row(toc_list_box);
    int current_index;
    GtkListBoxRow *new_row = NULL;

    if (current_row != NULL) {
        current_index = gtk_list_box_row_get_index(current_row);
    }

    switch (keyval) {
    case GDK_KEY_Tab:
        window_toggle_toc(window);
        next_state = STATE_NORMAL;
        break;
    case GDK_KEY_j:
        if (current_row != NULL) {
            new_row = gtk_list_box_get_row_at_index(toc_list_box, current_index + 1);
            while (new_row != NULL && !gtk_widget_get_visible(GTK_WIDGET(new_row))) {
                new_row = gtk_list_box_get_row_at_index(toc_list_box, gtk_list_box_row_get_index(new_row) + 1);
            }
        }
        next_state = STATE_TOC_FOCUS;
        break;
    case GDK_KEY_k:
        if (current_row != NULL) {
            new_row = gtk_list_box_get_row_at_index(toc_list_box, current_index - 1);
            while (new_row != NULL && !gtk_widget_get_visible(GTK_WIDGET(new_row))) {
                new_row = gtk_list_box_get_row_at_index(toc_list_box, gtk_list_box_row_get_index(new_row) - 1);
            }
        }
        next_state = STATE_TOC_FOCUS;
        break;
    case GDK_KEY_Return:
        window_execute_toc_row(window, current_row);
        next_state = STATE_TOC_FOCUS;
        break;
    case GDK_KEY_slash:
        window_focus_toc_search(window);
        next_state = STATE_TOC_FOCUS;
        break;
    default:
        next_state = STATE_TOC_FOCUS;
        break;
    }

    if (new_row != NULL) {
        gtk_list_box_select_row(toc_list_box, new_row);
    }

    return next_state;
}

InputState on_state_group_swap(Window *window, guint keyval)
{
    InputState next_state = STATE_NORMAL;
    GroupSelection *selection = window_get_group_selection(window);
    unsigned int group_i = keyval - GDK_KEY_0 - 1;

    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        group_selection_swap_group(selection, group_i);
    }

    return next_state;
}

InputState on_state_group_overwrite(Window *window, guint keyval)
{
    InputState next_state = STATE_NORMAL;
    GroupSelection *selection = window_get_group_selection(window);
    unsigned int group_i = keyval - GDK_KEY_0 - 1;

    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        group_selection_overwrite_group(selection, group_i);
    }

    return next_state;
}

InputState on_state_mark(Window *window, guint keyval)
{
    InputState next_state = STATE_NORMAL;
    GroupSelection *selection = window_get_group_selection(window);
    unsigned int mark_i = keyval - GDK_KEY_0 - 1;
    Viewer *viewer = window_get_viewer(window);

    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        group_selection_switch_mark(selection, mark_i);
    } else if (keyval == GDK_KEY_n) {
        viewer->last_command.execute = (CommandExecute)switch_to_previous_mark;
        viewer->last_command.repeat_count = 1;
        viewer->last_command.data = NULL;
        viewer->last_jump_command = command_copy(&viewer->last_command);

        command_execute(&viewer->last_command, viewer);
    } else if (keyval == GDK_KEY_c) {
        next_state = STATE_MARK_CLEAR;
    } else if (keyval == GDK_KEY_s) {
        next_state = STATE_MARK_SWAP;
    } else if (keyval == GDK_KEY_o) {
        next_state = STATE_MARK_OVERWRITE;
    }

    return next_state;
}

InputState on_state_mark_clear(Window *window, guint keyval)
{
    InputState next_state = STATE_NORMAL;
    GroupSelection *selection = window_get_group_selection(window);
    unsigned int mark_i = keyval - GDK_KEY_0 - 1;

    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        group_selection_clear_mark(selection, mark_i);
    }

    return next_state;
}

InputState on_state_mark_swap(Window *window, guint keyval)
{
    InputState next_state = STATE_NORMAL;
    GroupSelection *selection = window_get_group_selection(window);
    unsigned int mark_i = keyval - GDK_KEY_0 - 1;

    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        group_selection_swap_mark(selection, mark_i);
    }

    return next_state;
}

InputState on_state_mark_overwrite(Window *window, guint keyval) {
    InputState next_state = STATE_NORMAL;
    GroupSelection *selection = window_get_group_selection(window);
    unsigned int mark_i = keyval - GDK_KEY_0 - 1;

    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9) {
        group_selection_overwrite_mark(selection, mark_i);
    }

    return next_state;
}

InputState execute_state(InputState current_state, Window *window, guint keyval)
{
    return input_state_funcs[current_state](window, keyval);
}

static InputState execute_command(Window *window, guint keyval, unsigned int repeat_count)
{
    InputState next_state = STATE_NORMAL;
    Viewer *viewer = window_get_viewer(window);

    switch (keyval) {
    case GDK_KEY_plus:
        viewer->last_command.execute = (CommandExecute)zoom_in;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = &g_config->scale_step;
        break;
    case GDK_KEY_minus:
        viewer->last_command.execute = (CommandExecute)zoom_out;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = &g_config->scale_step;
        break;
    case GDK_KEY_u:
    case GDK_KEY_Page_Up:
        viewer->last_command.execute = (CommandExecute)scroll_half_page_up;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = &g_config->steps;
        break;
    case GDK_KEY_d:
    case GDK_KEY_Page_Down:
        viewer->last_command.execute = (CommandExecute)scroll_half_page_down;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = &g_config->steps;
        break;
    case GDK_KEY_h:
    case GDK_KEY_Left:
        viewer->last_command.execute = (CommandExecute)scroll_left;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = NULL;
        break;
    case GDK_KEY_j:
    case GDK_KEY_Down:
        viewer->last_command.execute = (CommandExecute)scroll_down;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = NULL;
        break;
    case GDK_KEY_k:
    case GDK_KEY_Up:
        viewer->last_command.execute = (CommandExecute)scroll_up;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = NULL;
        break;
    case GDK_KEY_l:
    case GDK_KEY_Right:
        viewer->last_command.execute = (CommandExecute)scroll_right;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = NULL;
        break;
    case GDK_KEY_n:
        viewer->last_command.execute = (CommandExecute)forward_search;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = NULL;
        break;
    case GDK_KEY_N:
        viewer->last_command.execute = (CommandExecute)backward_search;
        viewer->last_command.repeat_count = repeat_count;
        viewer->last_command.data = NULL;
        break;
    default:
        /* Avoid executing the last command if no key was matched */
        return next_state;
        break;
    }

    command_execute(&viewer->last_command, viewer);

    return next_state;
}