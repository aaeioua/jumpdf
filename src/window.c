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
#include "input.h"
#include "renderer.h"
#include "statusline.h"
#include "outline.h"
#include "outline_panel.h"

static void window_redraw_document_windows(Window *win);
static void window_handle_request(Window *win, InputRequest request);
static void window_toggle_fullscreen(Window *win);
static void window_show_search_dialog(Window *win);
static void window_show_help_dialog(Window *win);
static void window_on_outline_activated(ViewerTarget target,
    gpointer user_data);
static void window_update_statusline(Window *win);
static void window_on_search_changed(gpointer user_data);
static void window_on_search_found(int from, int to, gpointer user_data);
static void window_on_search_tallied(gpointer user_data);

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

struct _Window {
    // TODO: Breakdown into separate structs
    GtkApplicationWindow parent;

    GtkEventController *event_controller;
    GtkEventController *scroll_controller;

    GtkWidget *main_container;

    GtkWidget *view_box;
    GtkWidget *view;

    Statusline *statusline;

    OutlinePanel *outline_panel;

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
    char *uri;
    ViewerMarkManager *mark_manager;
    GroupSelection *selection;
    Viewer *viewer;
    Renderer *renderer;
    bool first_draw;
    Input *input;
};

G_DEFINE_TYPE(Window, window, GTK_TYPE_APPLICATION_WINDOW)

static void window_init(Window *win)
{
    win->uri = NULL;
    win->mark_manager = NULL;
    win->selection = NULL;
    win->viewer = NULL;
    win->renderer = NULL;
    win->first_draw = TRUE;
    win->input = input_new();
    win->outline_panel = NULL;

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

    win->statusline = statusline_new(&g_config->statusline);

    win->view_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(win->view_box, TRUE);
    gtk_widget_set_vexpand(win->view_box, TRUE);
    gtk_box_append(GTK_BOX(win->view_box), win->view);
    gtk_box_append(GTK_BOX(win->view_box), GTK_WIDGET(win->statusline));

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
}

static void window_finalize(GObject *object)
{
    Window *win = (Window *)object;

    renderer_destroy(win->renderer);
    win->renderer = NULL;

    g_clear_pointer(&win->input, input_free);

    viewer_destroy(win->viewer);
    win->viewer = NULL;

    viewer_mark_manager_detach(win->selection);
    win->selection = NULL;
    win->mark_manager = NULL;

    if (win->uri != NULL) {
        app_save_document(win->app, win->uri);
        g_clear_pointer(&win->uri, g_free);
    }

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
    const SearchCallbacks search_callbacks = {
        .changed = window_on_search_changed,
        .found = window_on_search_found,
        .tallied = window_on_search_tallied,
    };
    Search *search;
    ViewerLinks *links;
    GFileInfo *file_info;
    double default_width, default_height;

    search = search_new(info, &search_callbacks, win);
    links = viewer_links_new();

    win->uri = g_file_get_uri(file);
    win->mark_manager = mark_manager;
    win->selection = viewer_mark_manager_attach(mark_manager);
    const ViewportRules rules = {
        .steps = g_config->steps,
        .min_scale = g_config->min_scale,
        .scale_step = g_config->scale_step,
        .max_page_pixels = RENDERER_MAX_PAGE_PIXELS,
    };
    win->viewer = viewer_new(info, win->selection, search, links, &rules);
    win->renderer = renderer_new(win->view);

    /* Marks from the Mark store may lie beyond a since-shortened document */
    for (unsigned int g = 0; g < NUM_GROUPS; g++) {
        ViewerMarkGroup *group = mark_manager->groups[g];
        for (unsigned int m = 0; group != NULL && m < NUM_MARKS; m++) {
            if (group->marks[m] != NULL) {
                viewport_settle(win->viewer->viewport, group->marks[m]);
            }
        }
    }

    win->outline_panel = outline_panel_new(outline_read(info),
        window_on_outline_activated, win);
    gtk_box_prepend(GTK_BOX(win->main_container),
        GTK_WIDGET(win->outline_panel));

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

void window_redraw(Window *win)
{
    gtk_widget_queue_draw(win->view);
    window_update_statusline(win);
    renderer_render_visible_pages(win->renderer, win->viewer);
}

static void window_toggle_fullscreen(Window *win)
{
    if (gtk_window_is_fullscreen(GTK_WINDOW(win))) {
        gtk_window_unfullscreen(GTK_WINDOW(win));
    } else {
        gtk_window_fullscreen(GTK_WINDOW(win));
    }
}

static void window_show_help_dialog(Window *win)
{
    GtkWidget *dialog;
    GtkWidget *box;
    GtkWidget *scrolled_window;
    GtkWidget *text_box;
    GtkWidget *keybindings_header_label;
    GtkWidget *keybindings_grid;
    GtkWidget *key_label;
    GtkWidget *description_label;
    GArray *help_rows;
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

    help_rows = input_help();
    for (guint i = 0; i < help_rows->len; i++) {
        const InputHelpRow *row = &g_array_index(help_rows, InputHelpRow, i);

        key_label = gtk_label_new(row->keys);
        description_label = gtk_label_new(row->description);

        gtk_label_set_xalign(GTK_LABEL(key_label), 0.0);
        gtk_label_set_xalign(GTK_LABEL(description_label), 0.0);

        gtk_widget_set_margin_start(key_label, row->indent * margin);

        gtk_grid_attach(GTK_GRID(keybindings_grid), key_label, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(keybindings_grid), description_label, 1, i, 1, 1);
    }
    input_help_free(help_rows);

    gtk_window_present(GTK_WINDOW(dialog));
}

/* Pre-filled with the current query, selected so typing replaces it */
static void window_show_search_dialog(Window *win)
{
    const char *query = search_query(win->viewer->search);
    GtkEditable *entry = GTK_EDITABLE(win->search_entry);

    gtk_editable_set_text(entry, query != NULL ? query : "");
    gtk_window_present(GTK_WINDOW(win->search_window));
    gtk_widget_grab_focus(win->search_entry);
    gtk_editable_select_region(entry, 0, -1);
}

ViewerMarkManager *window_get_mark_manager(Window *win)
{
    return win->mark_manager;
}

static void window_redraw_document_windows(Window *win)
{
    app_redraw_windows(win->app, win->mark_manager);
}

static void window_on_outline_activated(ViewerTarget target,
    gpointer user_data)
{
    Window *win = user_data;

    viewer_goto_target(win->viewer, target);
    window_redraw_document_windows(win);
}

static void window_handle_request(Window *win, InputRequest request)
{
    switch (request) {
    case INPUT_REQUEST_NONE:
        break;
    case INPUT_REQUEST_OPEN_OUTLINE:
        outline_panel_set_open(win->outline_panel, true);
        break;
    case INPUT_REQUEST_CLOSE_OUTLINE:
        outline_panel_set_open(win->outline_panel, false);
        break;
    case INPUT_REQUEST_OUTLINE_NEXT:
        outline_panel_step(win->outline_panel, 1);
        break;
    case INPUT_REQUEST_OUTLINE_PREVIOUS:
        outline_panel_step(win->outline_panel, -1);
        break;
    case INPUT_REQUEST_OUTLINE_ACTIVATE:
        outline_panel_activate(win->outline_panel);
        break;
    case INPUT_REQUEST_OUTLINE_FOCUS_FILTER:
        outline_panel_focus_filter(win->outline_panel);
        break;
    case INPUT_REQUEST_SHOW_SEARCH:
        window_show_search_dialog(win);
        break;
    case INPUT_REQUEST_SHOW_HELP:
        window_show_help_dialog(win);
        break;
    case INPUT_REQUEST_OPEN_FILE:
        app_open_file_chooser(win->app);
        break;
    case INPUT_REQUEST_TOGGLE_FULLSCREEN:
        window_toggle_fullscreen(win);
        break;
    }
}

static void window_update_statusline(Window *win)
{
    ViewerCursor *mark = group_selection_current_mark(win->selection);
    const SearchTally tally = search_tally(win->viewer->search,
        mark->current_page);
    StatuslineState state = {
        .page = mark->current_page,
        .n_pages = win->viewer->info->n_pages,
        .scale = mark->scale,
        .center_mode = mark->center_mode,
        .group = group_selection_current_group(win->selection),
        .mark = group_selection_current_mark_index(win->selection),
        .pending_keys = input_pending(win->input),
        .search_text = search_query(win->viewer->search),
        .search_pending = search_seeking(win->viewer->search),
        .search_position = tally.position,
        .search_total = tally.total,
        .search_scanning = !tally.complete,
    };

    statusline_update(win->statusline, &state);
}

static void window_on_search_changed(gpointer user_data)
{
    Window *win = user_data;

    gtk_widget_queue_draw(win->view);
    window_update_statusline(win);
}

static void window_on_search_found(int from, int to, gpointer user_data)
{
    Window *win = user_data;

    if (viewer_goto_found_page(win->viewer, from, to)) {
        window_redraw_document_windows(win);
    }
}

static void window_on_search_tallied(gpointer user_data)
{
    window_update_statusline(user_data);
}

static gboolean on_key_pressed(GtkWidget *user_data, guint keyval,
    guint keycode, GdkModifierType state,
    GtkEventControllerKey *event_controller)
{
    UNUSED(keycode);
    UNUSED(state);
    UNUSED(event_controller);

    Window *win = (Window *)user_data;

    InputRequest request = input_feed(win->input, win->viewer, keyval);
    window_handle_request(win, request);
    window_redraw_document_windows(win);

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
        input_scroll(win->input, win->viewer, dx, dy,
            state == GDK_CONTROL_MASK);
    }

    window_redraw_document_windows(win);
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

static void on_search_entry_activate(GtkEntry *entry, gpointer user_data) {
    Window *win = (Window *)user_data;
    const gchar *text = gtk_editable_get_text(GTK_EDITABLE(entry));

    search_set_query(win->viewer->search, text,
        viewer_current_mark(win->viewer)->current_page);

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
