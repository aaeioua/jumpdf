#define _GNU_SOURCE
#include <string.h>

#include "outline_panel.h"
#include "outline.h"
#include "utils.h"

#define OUTLINE_PANEL_INDENT 12

struct _OutlinePanel {
    GtkWidget parent;

    GArray *entries;
    OutlinePanelActivated activated;
    gpointer user_data;
    bool rows_built;

    GtkWidget *scrolled_window;
    GtkWidget *filter;
    GtkWidget *list;
};

G_DEFINE_TYPE(OutlinePanel, outline_panel, GTK_TYPE_WIDGET)

static GtkListBoxRow *outline_panel_visible_row(OutlinePanel *self, int index,
    int step);
static void outline_panel_append_rows(OutlinePanel *self);
static void outline_panel_apply_filter(OutlinePanel *self);
static int outline_panel_visible_position(OutlinePanel *self,
    GtkListBoxRow *row);
static GtkListBoxRow *outline_panel_nth_visible_row(OutlinePanel *self, int n);
static void outline_panel_select(OutlinePanel *self, GtkListBoxRow *row);
static void on_filter_changed(GtkSearchEntry *filter, gpointer user_data);
static void on_filter_stopped(GtkSearchEntry *filter, gpointer user_data);
static void on_row_activated(GtkListBox *list, GtkListBoxRow *row,
    gpointer user_data);

static void outline_panel_init(OutlinePanel *self)
{
    GtkWidget *box;

    self->entries = NULL;
    self->activated = NULL;
    self->user_data = NULL;
    self->rows_built = false;

    self->filter = gtk_search_entry_new();
    g_signal_connect(self->filter, "search-changed",
        G_CALLBACK(on_filter_changed), self);
    g_signal_connect(self->filter, "stop-search",
        G_CALLBACK(on_filter_stopped), self);

    self->list = gtk_list_box_new();
    gtk_widget_set_hexpand(self->list, TRUE);
    g_signal_connect(self->list, "row-activated",
        G_CALLBACK(on_row_activated), self);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(box), self->filter);
    gtk_box_append(GTK_BOX(box), self->list);

    self->scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(self->scrolled_window),
        box);
    gtk_widget_set_parent(self->scrolled_window, GTK_WIDGET(self));

    gtk_widget_set_visible(GTK_WIDGET(self), FALSE);
}

static void outline_panel_dispose(GObject *object)
{
    OutlinePanel *self = JUMPDF_OUTLINE_PANEL(object);

    g_clear_pointer(&self->scrolled_window, gtk_widget_unparent);
    g_clear_pointer(&self->entries, g_array_unref);

    G_OBJECT_CLASS(outline_panel_parent_class)->dispose(object);
}

static void outline_panel_compute_expand(GtkWidget *widget, gboolean *hexpand,
    gboolean *vexpand)
{
    OutlinePanel *self = JUMPDF_OUTLINE_PANEL(widget);

    *hexpand = self->scrolled_window != NULL && gtk_widget_compute_expand(
        self->scrolled_window, GTK_ORIENTATION_HORIZONTAL);
    *vexpand = self->scrolled_window != NULL && gtk_widget_compute_expand(
        self->scrolled_window, GTK_ORIENTATION_VERTICAL);
}

static void outline_panel_class_init(OutlinePanelClass *class)
{
    G_OBJECT_CLASS(class)->dispose = outline_panel_dispose;
    GTK_WIDGET_CLASS(class)->compute_expand = outline_panel_compute_expand;
    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(class),
        GTK_TYPE_BIN_LAYOUT);
}

OutlinePanel *outline_panel_new(GArray *entries,
    OutlinePanelActivated activated, gpointer user_data)
{
    OutlinePanel *panel = g_object_new(OUTLINE_PANEL_TYPE, NULL);

    panel->entries = entries;
    panel->activated = activated;
    panel->user_data = user_data;

    return panel;
}

static GtkWidget *outline_panel_row_new(const OutlineEntry *entry)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *title = gtk_label_new(entry->title);
    GtkWidget *page;
    gchar *page_text;

    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(box), title);

    if (entry->target.page >= 0) {
        page_text = g_strdup_printf("%d", entry->target.page + 1);
        page = gtk_label_new(page_text);
        g_free(page_text);
        gtk_label_set_xalign(GTK_LABEL(page), 1.0);
        gtk_box_append(GTK_BOX(box), page);
    }

    gtk_widget_set_margin_start(box, entry->depth * OUTLINE_PANEL_INDENT);

    return box;
}

static void outline_panel_append_rows(OutlinePanel *self)
{
    for (guint i = 0; i < self->entries->len; i++) {
        gtk_list_box_append(GTK_LIST_BOX(self->list), outline_panel_row_new(
            &g_array_index(self->entries, OutlineEntry, i)));
    }
}

static void outline_panel_build_rows(OutlinePanel *self)
{
    GtkListBox *list = GTK_LIST_BOX(self->list);
    GtkListBoxRow *first;

    outline_panel_append_rows(self);

    first = gtk_list_box_get_row_at_index(list, 0);
    if (first != NULL) {
        gtk_list_box_select_row(list, first);
    }
    self->rows_built = true;
}

void outline_panel_set_entries(OutlinePanel *panel, GArray *entries)
{
    GtkListBox *list = GTK_LIST_BOX(panel->list);
    GtkListBoxRow *selected;
    int position;

    if (!panel->rows_built) {
        g_clear_pointer(&panel->entries, g_array_unref);
        panel->entries = entries;
        return;
    }

    selected = gtk_list_box_get_selected_row(list);
    position = selected != NULL
        ? outline_panel_visible_position(panel, selected) : 0;

    gtk_list_box_remove_all(list);
    g_clear_pointer(&panel->entries, g_array_unref);
    panel->entries = entries;
    outline_panel_append_rows(panel);
    outline_panel_apply_filter(panel);

    outline_panel_select(panel,
        outline_panel_nth_visible_row(panel, position));
}

void outline_panel_set_open(OutlinePanel *panel, bool open)
{
    if (open && !panel->rows_built) {
        outline_panel_build_rows(panel);
    }

    gtk_widget_set_visible(GTK_WIDGET(panel), open);
}

static GtkListBoxRow *outline_panel_visible_row(OutlinePanel *self, int index,
    int step)
{
    GtkListBoxRow *row;

    for (; index >= 0; index += step) {
        row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(self->list), index);
        if (row == NULL || gtk_widget_get_visible(GTK_WIDGET(row))) {
            return row;
        }
    }

    return NULL;
}

void outline_panel_step(OutlinePanel *panel, int step)
{
    GtkListBox *list = GTK_LIST_BOX(panel->list);
    GtkListBoxRow *current = gtk_list_box_get_selected_row(list);
    GtkListBoxRow *next;

    if (current == NULL) {
        return;
    }

    next = outline_panel_visible_row(panel,
        gtk_list_box_row_get_index(current) + step, step);
    if (next != NULL) {
        gtk_list_box_select_row(list, next);
    }
}

void outline_panel_activate(OutlinePanel *panel)
{
    GtkListBoxRow *row;
    const OutlineEntry *entry;

    row = gtk_list_box_get_selected_row(GTK_LIST_BOX(panel->list));

    if (row == NULL) {
        return;
    }

    entry = &g_array_index(panel->entries, OutlineEntry,
        gtk_list_box_row_get_index(row));
    if (entry->target.page >= 0 && panel->activated != NULL) {
        panel->activated(entry->target, panel->user_data);
    }
}

void outline_panel_focus_filter(OutlinePanel *panel)
{
    if (!panel->rows_built) {
        return;
    }

    gtk_widget_grab_focus(panel->filter);
}

static void outline_panel_apply_filter(OutlinePanel *self)
{
    GtkListBox *list = GTK_LIST_BOX(self->list);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(self->filter));
    GtkListBoxRow *row;
    const char *title;

    for (int i = 0; (row = gtk_list_box_get_row_at_index(list, i)) != NULL;
         i++) {
        title = g_array_index(self->entries, OutlineEntry, i).title;
        gtk_widget_set_visible(GTK_WIDGET(row),
            strcasestr(title, text) != NULL);
    }
}

static int outline_panel_visible_position(OutlinePanel *self,
    GtkListBoxRow *row)
{
    const int index = gtk_list_box_row_get_index(row);
    int position = 0;

    for (int i = 0; i < index; i++) {
        position += gtk_widget_get_visible(GTK_WIDGET(
            gtk_list_box_get_row_at_index(GTK_LIST_BOX(self->list), i)));
    }

    return position;
}

static GtkListBoxRow *outline_panel_nth_visible_row(OutlinePanel *self, int n)
{
    GtkListBoxRow *found = NULL;
    GtkListBoxRow *row;

    for (int i = 0;
         (row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(self->list), i))
            != NULL;
         i++) {
        if (gtk_widget_get_visible(GTK_WIDGET(row))) {
            found = row;
            if (n-- == 0) {
                break;
            }
        }
    }

    return found;
}

static void outline_panel_select(OutlinePanel *self, GtkListBoxRow *row)
{
    if (row != NULL) {
        gtk_list_box_select_row(GTK_LIST_BOX(self->list), row);
    } else {
        gtk_list_box_unselect_all(GTK_LIST_BOX(self->list));
    }
}

static void on_filter_changed(GtkSearchEntry *filter, gpointer user_data)
{
    UNUSED(filter);

    OutlinePanel *self = user_data;

    outline_panel_apply_filter(self);
    outline_panel_select(self, outline_panel_visible_row(self, 0, 1));
}

static void on_filter_stopped(GtkSearchEntry *filter, gpointer user_data)
{
    UNUSED(filter);

    OutlinePanel *self = user_data;

    gtk_widget_grab_focus(self->scrolled_window);
}

static void on_row_activated(GtkListBox *list, GtkListBoxRow *row,
    gpointer user_data)
{
    gtk_list_box_select_row(list, row);
    outline_panel_activate(user_data);
}
