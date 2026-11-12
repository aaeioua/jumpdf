#define _GNU_SOURCE
#include <stdio.h>
#include <cairo.h>
#include <gtk/gtk.h>
#include <poppler.h>
#include <stdbool.h>

#include "window.h"
#include "project_config.h"
#include "utils.h"
#include "app.h"
#include "config.h"
#include "viewer.h"
#include "viewer_mark_manager.h"
#include "input_FSM.h"
#include "renderer.h"

// TODO: Load from file or resource
static const char *css = 
    ".statusline {"
    "    background-color: black;"
    "}";

typedef struct {
    const char *key;
    const char *description;
    const int indent_level;
} Keybinding;

static const Keybinding keybindings[] = {
    {"<number><command>", "Repeats <command> <number> times", 0},
    {"j, k", "Move down, up", 1},
    {"h, l", "Move left, right. Must not be in center mode", 1},
    {"d, u", "Move down, up half a page", 1},
    {"+, -", "Zoom in, out", 1},
    {"n, N", "Goto next, previous page containing the search string", 1},
    {"0", "Reset zoom", 0},
    {"c", "Toggle center mode", 0},
    {"b", "Toggle dark mode", 0},
    {"s", "Fit horizontally to page", 0},
    {"a", "Fit vertically to page", 0},
    {"gg, G, <number>G", "Goto first, last page, page <number>", 0},
    {"f + <number> + Enter", "Show link numbers and execute link", 0},
    {"m<1-9>", "Set current mark to <1-9>. If the mark hasn't been set, it will be set to the current cursor", 0},
    {"mn", "Switch to previous mark", 0},
    {"mc<1-9>", "Clear mark <1-9>", 0},
    {"ms<1-9>", "Swap current mark with mark <1-9> and switch to it", 0},
    {"mo<1-9>", "Overwrite mark <1-9> with the current cursor and switch to it", 0},
    {"g<1-9>", "Set current group to <1-9>. If the current mark of the group hasn't been set, it will be set to the current cursor", 0},
    {"gn", "Switch to previous group", 0},
    {"gs<1-9>", "Swap current group with group <1-9> and switch to it", 0},
    {"go<1-9>", "Overwrite group <1-9> with the current group and switch to it", 0},
    {".", "Repeat last command (zoom, scroll, search or switch to previous mark or group)", 0},
    {",", "Repeat last jump command (switch to previous mark or group)", 0},
    {"/, Esc", "Show/hide search dialog", 0},
    {"o", "Open file chooser", 0},
    {"Tab", "Toggle table of contents", 0},
    {"j, k", "Move down, up in table of contents", 1},
    {"/, Esc", "Focus/unfocus search entry in table of contents", 1},
    {"Enter", "Goto selected page in table of contents", 1},
    {"F11", "Toggle fullscreen", 0},
    {"?", "Show help dialog", 0}
};

static void window_update_cursors(Window *win);
static void window_redraw_all_windows(Window *win);
static void window_update_statusline(Window *win);
static void window_populate_toc(Window *win);
static void window_add_toc_entries(Window *win, PopplerIndexIter *iter, int level);

static gboolean on_key_pressed(GtkWidget *user_data, guint keyval,
                               guint keycode, GdkModifierType state,
                               GtkEventControllerKey *event_controller);
static void on_scroll(GtkEventControllerScroll *controller, double dx,
                      double dy, gpointer user_data);
static void on_resize(GtkDrawingArea *area, int width, int height,
                      gpointer user_data);
static void draw_function(GtkDrawingArea *area, cairo_t *cr, int width,
                          int height, gpointer user_data);
static void on_search_entry_activate(GtkEntry *entry, gpointer user_data);
static gboolean on_search_window_key_press(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);
static void on_toc_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
static void on_toc_search_changed(GtkSearchEntry *entry, gpointer user_data);
static void on_toc_search_stopped(GtkSearchEntry *entry, gpointer user_data);

struct _Window {
    // TODO: Breakdown into separate structs
    GtkApplicationWindow parent;

    GtkEventController *event_controller;
    GtkEventController *scroll_controller;

    GtkWidget *main_container;

    GtkWidget *view_box;
    GtkWidget *view;

    GtkWidget *statusline;
    GtkWidget *statusline_left;
    GtkWidget *statusline_middle;
    GtkWidget *statusline_right;
    GtkWidget *left_label;
    GtkWidget *middle_label;
    GtkWidget *right_label;

    GtkWidget *toc_scroll_window;
    GtkWidget *toc_box;
    GtkWidget *toc_search_entry;
    GtkWidget *toc_container;

    GtkWidget *search_window;
    GtkWidget *search_box;
    GtkWidget *search_entry;
    GtkEventController *search_event_controller;

    /*
    This reference to App is necessary, as the Window is not associated
    with a GtkApplication in the destruction phase, making
    gtk_window_get_application not viable.
    */
    App *app;
    ViewerMarkManager *mark_manager;
    Viewer *viewer;
    Renderer *renderer;
    bool first_draw;
    InputState current_input_state;
};

G_DEFINE_TYPE(Window, window, GTK_TYPE_APPLICATION_WINDOW)

static void window_init(Window *win)
{
    GtkCssProvider *css_provider;

    win->viewer = NULL;
    win->renderer = NULL;
    win->first_draw = TRUE;
    win->current_input_state = STATE_NORMAL;

    win->event_controller = gtk_event_controller_key_new();
    g_signal_connect_object(win->event_controller, "key-pressed",
        G_CALLBACK(on_key_pressed), win, G_CONNECT_SWAPPED);
    gtk_widget_add_controller(GTK_WIDGET(win), win->event_controller);

    win->scroll_controller =
        gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    g_signal_connect(win->scroll_controller, "scroll", G_CALLBACK(on_scroll),
        win);
    gtk_widget_add_controller(GTK_WIDGET(win),
        GTK_EVENT_CONTROLLER(win->scroll_controller));

    win->view = gtk_drawing_area_new();
    gtk_widget_set_hexpand(win->view, TRUE);
    gtk_widget_set_vexpand(win->view, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(win->view), draw_function,
        win, NULL);
    g_signal_connect(win->view, "resize", G_CALLBACK(on_resize), win);

    win->statusline = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(win->statusline, "statusline");

    win->statusline_left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(win->statusline_left, FALSE);
    gtk_widget_set_halign(win->statusline_left, GTK_ALIGN_START);
    win->left_label = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(win->statusline_left), win->left_label);

    win->statusline_middle = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(win->statusline_middle, TRUE);
    gtk_widget_set_halign(win->statusline_middle, GTK_ALIGN_CENTER);
    win->middle_label = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(win->statusline_middle), win->middle_label);

    win->statusline_right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(win->statusline_right, FALSE);
    gtk_widget_set_halign(win->statusline_right, GTK_ALIGN_END);
    win->right_label = gtk_label_new(NULL);
    gtk_box_append(GTK_BOX(win->statusline_right), win->right_label);

    gtk_box_append(GTK_BOX(win->statusline), win->statusline_left);
    gtk_box_append(GTK_BOX(win->statusline), win->statusline_middle);
    gtk_box_append(GTK_BOX(win->statusline), win->statusline_right);

    win->view_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(win->view_box, TRUE);
    gtk_widget_set_vexpand(win->view_box, TRUE);
    gtk_box_append(GTK_BOX(win->view_box), win->view);
    gtk_box_append(GTK_BOX(win->view_box), win->statusline);

    win->toc_search_entry = gtk_search_entry_new();
    g_signal_connect(win->toc_search_entry, "search-changed", G_CALLBACK(on_toc_search_changed), win);
    g_signal_connect(win->toc_search_entry, "stop-search", G_CALLBACK(on_toc_search_stopped), win);

    win->toc_container = gtk_list_box_new();
    gtk_widget_set_hexpand(win->toc_container, TRUE);
    g_signal_connect(win->toc_container, "row-activated", G_CALLBACK(on_toc_row_activated), win);

    win->toc_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(win->toc_box), win->toc_search_entry);
    gtk_box_append(GTK_BOX(win->toc_box), win->toc_container);

    win->toc_scroll_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(win->toc_scroll_window), win->toc_box);
    gtk_widget_set_visible(win->toc_scroll_window, FALSE);
    /*
    * in window_toggle_toc, gtk_box_prepend only increases the ref count
    * the first time it is called, so we need to manually increase it here
    */
    g_object_ref(win->toc_scroll_window);

    win->search_window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(win->search_window), "Search");
    gtk_window_set_modal(GTK_WINDOW(win->search_window), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(win->search_window), GTK_WINDOW(win));
    gtk_window_set_hide_on_close(GTK_WINDOW(win->search_window), TRUE);

    win->search_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_window_set_child(GTK_WINDOW(win->search_window), win->search_box);

    win->search_entry = gtk_entry_new();
    gtk_box_append(GTK_BOX(win->search_box), win->search_entry);
    g_signal_connect(win->search_entry, "activate", G_CALLBACK(on_search_entry_activate), win);

    win->search_event_controller = gtk_event_controller_key_new();
    g_signal_connect(win->search_event_controller, "key-pressed",
        G_CALLBACK(on_search_window_key_press), win);
    gtk_widget_add_controller(win->search_window, win->search_event_controller);

    win->main_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(win), win->main_container);
    gtk_box_append(GTK_BOX(win->main_container), win->view_box);

    gtk_window_set_title(GTK_WINDOW(win), APP_NAME_STR);

    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css_provider, css);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
}

static void window_finalize(GObject *object)
{
    Window *win = (Window *)object;

    renderer_destroy(win->renderer);
    win->renderer = NULL;

    viewer_destroy(win->viewer);
    win->viewer = NULL;

    app_release_mark_manager(win->app, win->mark_manager);
    win->mark_manager = NULL;

    app_remove_window(win->app, win);

    G_OBJECT_CLASS(window_parent_class)->finalize(object);
}

static void window_class_init(WindowClass *class)
{
    G_OBJECT_CLASS(class)->finalize = window_finalize;
}

Window *window_new(App *app)
{
    Window *win = g_object_new(WINDOW_TYPE, "application", app, NULL);
    win->app = app;

    return win;
}

void window_open(Window *win, GFile *file, ViewerMarkManager *mark_manager,
    ViewerInfo *info)
{
    GError *error = NULL;
    ViewerCursor *cursor;
    ViewerSearch *search;
    ViewerLinks *links;
    GFileInfo *file_info;
    double default_width, default_height;

    cursor = viewer_mark_manager_get_current_cursor(mark_manager);
    search = viewer_search_new();
    links = viewer_links_new();

    win->mark_manager = mark_manager;
    win->viewer = viewer_new(info, cursor, search, links);
    win->renderer = renderer_new(win->view);

    window_populate_toc(win);
    window_update_statusline(win);

    file_info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME, G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (file_info == NULL) {
        g_printerr("GFile: %s\n", error->message);
        g_error_free(error);
    } else {
        gtk_window_set_title(GTK_WINDOW(win), g_file_info_get_display_name(file_info));
        g_object_unref(file_info);
    }

    poppler_page_get_size(viewer_info_get_poppler_page(win->viewer->info, 0), &default_width, &default_height);
    gtk_window_set_default_size(GTK_WINDOW(win), (int)default_width,
        (int)default_width);
}

void window_update_cursor(Window *win)
{
    win->viewer->cursor = viewer_mark_manager_get_current_cursor(win->mark_manager);
    viewer_cursor_handle_offset_update(win->viewer->cursor,
        win->viewer->info->n_pages);
}

void window_redraw(Window *win)
{
    gtk_widget_queue_draw(win->view);
    window_update_statusline(win);
    renderer_render_visible_pages(win->renderer, win->viewer);
}

void window_toggle_fullscreen(Window *win)
{
    if (gtk_window_is_fullscreen(GTK_WINDOW(win))) {
        gtk_window_unfullscreen(GTK_WINDOW(win));
    } else {
        gtk_window_fullscreen(GTK_WINDOW(win));
    }
}

void window_show_help_dialog(Window *win)
{
    GtkWidget *dialog;
    GtkWidget *box;
    GtkWidget *scrolled_window;
    GtkWidget *text_box;
    GtkWidget *keybindings_header_label;
    GtkWidget *keybindings_grid;
    GtkWidget *key_label;
    GtkWidget *description_label;
    const int margin = 10;
    GdkSurface *win_surface;
    int win_width, win_height;
    int dialog_width, dialog_height;

    dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Help");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    win_surface = gtk_native_get_surface(GTK_NATIVE(win));
    win_width = gdk_surface_get_width(win_surface);
    win_height = gdk_surface_get_height(win_surface);
    dialog_width = win_width * 0.8;
    dialog_height = win_height * 0.8;
    gtk_window_set_default_size(GTK_WINDOW(dialog), dialog_width, dialog_height);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    scrolled_window = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    gtk_box_append(GTK_BOX(box), scrolled_window);

    text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), text_box);

    keybindings_header_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(keybindings_header_label), "<b>Keybindings</b>");
    gtk_label_set_xalign(GTK_LABEL(keybindings_header_label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(keybindings_header_label), 0.0);
    gtk_widget_set_margin_top(keybindings_header_label, margin);
    gtk_widget_set_margin_bottom(keybindings_header_label, margin);
    gtk_widget_set_margin_start(keybindings_header_label, margin);
    gtk_widget_set_margin_end(keybindings_header_label, margin);
    gtk_box_append(GTK_BOX(text_box), keybindings_header_label);

    keybindings_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(keybindings_grid), margin);
    gtk_widget_set_margin_bottom(keybindings_grid, margin);
    gtk_widget_set_margin_start(keybindings_grid, margin);
    gtk_widget_set_margin_end(keybindings_grid, margin);
    gtk_box_append(GTK_BOX(text_box), keybindings_grid);

    for (size_t i = 0; i < G_N_ELEMENTS(keybindings); i++) {
        int indent_margin;

        key_label = gtk_label_new(keybindings[i].key);
        description_label = gtk_label_new(keybindings[i].description);

        gtk_label_set_xalign(GTK_LABEL(key_label), 0.0);
        gtk_label_set_xalign(GTK_LABEL(description_label), 0.0);

        indent_margin = keybindings[i].indent_level * margin;
        gtk_widget_set_margin_start(key_label, indent_margin);

        gtk_grid_attach(GTK_GRID(keybindings_grid), key_label, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(keybindings_grid), description_label, 1, i, 1, 1);
    }

    gtk_window_present(GTK_WINDOW(dialog));
}

void window_show_search_dialog(Window *win)
{
    gtk_window_present(GTK_WINDOW(win->search_window));
}

void window_toggle_toc(Window *win)
{
    gboolean is_visible = gtk_widget_get_visible(win->toc_scroll_window);

    gtk_widget_set_visible(win->toc_scroll_window, !is_visible);

    // It is necessary to remove from main_container in addition,
    // otherwise it will still occupy space
    if (is_visible) {
        g_object_ref(win->toc_scroll_window);
        gtk_box_remove(GTK_BOX(win->main_container), win->toc_scroll_window);
    } else {
        gtk_box_prepend(GTK_BOX(win->main_container), win->toc_scroll_window);
        g_object_unref(win->toc_scroll_window);
    }
}

void window_focus_toc_search(Window *win)
{
    gtk_widget_grab_focus(win->toc_search_entry);
}

void window_execute_toc_row(Window *win, GtkListBoxRow *row)
{
    GtkWidget *toc_entry_box;
    PopplerDest *dest;

    if (row != NULL) {
        toc_entry_box = gtk_list_box_row_get_child(row);
        dest = g_object_get_data(G_OBJECT(toc_entry_box), "dest");

        if (dest) {
            viewer_goto_dest(win->viewer, dest);
            window_redraw_all_windows(win);
        } else {
            g_printerr("Error: TOC entry has no destination\n");
        }
    }
}

ViewerMarkManager *window_get_mark_manager(Window *win)
{
    return win->mark_manager;
}

Viewer *window_get_viewer(Window *win)
{
    return win->viewer;
}

GtkListBox *window_get_toc_listbox(Window *win)
{
    return GTK_LIST_BOX(win->toc_container);
}

static void window_update_cursors(Window *win)
{
    app_update_cursors(win->app);
}

static void window_redraw_all_windows(Window *win)
{
    app_redraw_windows(win->app);
}

static void window_update_statusline(Window *win)
{
    gchar *statusline_left_str = statusline_section_to_str(g_config->statusline_left, win);
    gchar *statusline_middle_str = statusline_section_to_str(g_config->statusline_middle, win);
    gchar *statusline_right_str = statusline_section_to_str(g_config->statusline_right, win);

    gtk_label_set_text(GTK_LABEL(win->left_label), statusline_left_str);
    gtk_label_set_text(GTK_LABEL(win->middle_label), statusline_middle_str);
    gtk_label_set_text(GTK_LABEL(win->right_label), statusline_right_str);

    g_free(statusline_left_str);
    g_free(statusline_middle_str);
    g_free(statusline_right_str);
}

static gboolean on_key_pressed(GtkWidget *user_data, guint keyval,
    guint keycode, GdkModifierType state,
    GtkEventControllerKey *event_controller)
{
    UNUSED(keycode);
    UNUSED(state);
    UNUSED(event_controller);

    Window *win = (Window *)user_data;

    win->current_input_state = execute_state(win->current_input_state, win, keyval);
    window_update_cursors(win);
    window_redraw_all_windows(win);

    return TRUE;
}

static void on_scroll(GtkEventControllerScroll *controller, double dx,
    double dy, gpointer user_data)
{
    Window *win;
    GdkEvent *event;
    GdkModifierType state;

    win = (Window *)user_data;

    event =
        gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(controller));
    if (event) {
        state = gdk_event_get_modifier_state(event);
        switch (state) {
        case GDK_CONTROL_MASK:
            viewer_cursor_set_scale(win->viewer->cursor, win->viewer->cursor->scale - dy * g_config->scale_step);
            break;
        default:
            win->viewer->cursor->x_offset -= dx;
            win->viewer->cursor->y_offset += dy;

            viewer_cursor_handle_offset_update(win->viewer->cursor,
                win->viewer->info->n_pages);
        }
    }

    window_redraw_all_windows(win);
}

static void on_resize(GtkDrawingArea *area, int width, int height,
    gpointer user_data)
{
    UNUSED(area);

    Window *win;

    win = (Window *)user_data;

    viewport_set_view_size(win->viewer->viewport, width, height);
}

static void draw_function(GtkDrawingArea *area, cairo_t *cr, int width,
    int height, gpointer user_data)
{
    UNUSED(area);
    UNUSED(width);
    UNUSED(height);

    Window *win = (Window *)user_data;
    
    if (win->first_draw) {
        renderer_render_visible_pages(win->renderer, win->viewer);
        win->first_draw = FALSE;
    }
    
    renderer_draw(win->renderer, cr, win->viewer);
}

static void window_populate_toc(Window *win)
{
    PopplerIndexIter *iter = poppler_index_iter_new(win->viewer->info->doc);
    if (iter) {
        window_add_toc_entries(win, iter, 0);
        poppler_index_iter_free(iter);
    }
}

static void window_add_toc_entries(Window *win, PopplerIndexIter *iter, int level)
{
    PopplerAction *action;
    PopplerDest *dest;
    gchar *title_markup, *page_markup;
    GtkWidget *title_label, *page_label;
    GtkWidget *toc_entry_box;
    PopplerIndexIter *child;
    GtkListBoxRow *first_row;

    while (iter) {
        action = poppler_index_iter_get_action(iter);
        if (action && action->type == POPPLER_ACTION_GOTO_DEST) {
            dest = viewer_info_get_dest(win->viewer->info, action->goto_dest.dest);

            if (dest == NULL) {
                poppler_action_free(action);
                if (!poppler_index_iter_next(iter)) {
                    break;
                }
                continue;
            }

            title_markup = g_strdup_printf("%*s%s", level * 2, " ", action->any.title);
            title_label = gtk_label_new(title_markup);
            g_free(title_markup);
            gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
            gtk_label_set_justify(GTK_LABEL(title_label), GTK_JUSTIFY_LEFT);
            gtk_widget_set_hexpand(title_label, TRUE);

            page_markup = g_markup_printf_escaped("%d", dest->page_num + 1);
            page_label = gtk_label_new(page_markup);
            g_free(page_markup);
            gtk_label_set_xalign(GTK_LABEL(page_label), 1.0);
            gtk_label_set_justify(GTK_LABEL(page_label), GTK_JUSTIFY_RIGHT);

            toc_entry_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_box_append(GTK_BOX(toc_entry_box), title_label);
            gtk_box_append(GTK_BOX(toc_entry_box), page_label);

            g_object_set_data_full(G_OBJECT(toc_entry_box), "dest", poppler_dest_copy(dest), (GDestroyNotify)poppler_dest_free);
            if (action->goto_dest.dest->type == POPPLER_DEST_NAMED) {
                // Won't be freed by poppler_action_free
                poppler_dest_free(dest);
            }

            gtk_list_box_append(GTK_LIST_BOX(win->toc_container), toc_entry_box);

            child = poppler_index_iter_get_child(iter);
            if (child) {
                window_add_toc_entries(win, child, level + 1);
                poppler_index_iter_free(child);
            }
        }

        poppler_action_free(action);

        if (!poppler_index_iter_next(iter)) {
            break;
        }
    }

    first_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(win->toc_container), 0);
    if (first_row != NULL) {
        gtk_list_box_select_row(GTK_LIST_BOX(win->toc_container), first_row);
    }
}

static void on_search_entry_activate(GtkEntry *entry, gpointer user_data) {
    Window *win = (Window *)user_data;
    const gchar *text = gtk_editable_get_text(GTK_EDITABLE(entry));

    viewer_search_set_text(win->viewer->search, text);

    gtk_window_close(GTK_WINDOW(win->search_window));
    window_redraw(win);
}

static gboolean on_search_window_key_press(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data)
{
    UNUSED(controller);
    UNUSED(state);
    UNUSED(keycode);

    Window *win = (Window *)user_data;

    switch (keyval) {
    case GDK_KEY_Escape:
        gtk_window_close(GTK_WINDOW(win->search_window));
        window_redraw(win);
        return TRUE;
    default:
        return FALSE;
    }
}

static void on_toc_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data)
{
    UNUSED(row);

    Window *win = (Window *)user_data;
    GtkListBoxRow *selected_row = gtk_list_box_get_selected_row(box);

    window_execute_toc_row(win, selected_row);
}

static void on_toc_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
    Window *win = (Window *)user_data;
    const gchar *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(win->toc_container));
    GtkWidget *box;
    GtkWidget *title_label;
    const gchar *child_text;
    gboolean visible;
    GtkListBoxRow *first_visible_row;

    while (child) {
        box = gtk_widget_get_first_child(child);
        title_label = gtk_widget_get_first_child(box);
        if (GTK_IS_LABEL(title_label)) {
            child_text = gtk_label_get_text(GTK_LABEL(title_label));

            visible = strcasestr(child_text, text) != NULL;
            gtk_widget_set_visible(child, visible);
        }

        child = gtk_widget_get_next_sibling(child);
    }

    first_visible_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(win->toc_container), 0);
    while (first_visible_row != NULL && !gtk_widget_get_visible(GTK_WIDGET(first_visible_row))) {
        first_visible_row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(win->toc_container), gtk_list_box_row_get_index(first_visible_row) + 1);
    }
    if (first_visible_row != NULL) {
        gtk_list_box_select_row(GTK_LIST_BOX(win->toc_container), first_visible_row);
    }
}

static void on_toc_search_stopped(GtkSearchEntry *entry, gpointer user_data)
{
    UNUSED(entry);

    Window *win = (Window *)user_data;

    gtk_widget_grab_focus(win->toc_scroll_window);
}
