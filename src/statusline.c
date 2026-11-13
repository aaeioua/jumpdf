#include "statusline.h"
#include "statusline_format.h"

static const char *css =
    ".statusline {"
    "    background-color: black;"
    "    color: white;"
    "}";

struct _Statusline {
    GtkWidget parent;

    const StatuslineLayout *layout;
    GtkWidget *center_box;
    GtkWidget *labels[STATUSLINE_N_SECTIONS];
};

G_DEFINE_TYPE(Statusline, statusline, GTK_TYPE_WIDGET)

static void statusline_init(Statusline *self)
{
    self->layout = NULL;

    for (int i = 0; i < STATUSLINE_N_SECTIONS; i++) {
        self->labels[i] = gtk_label_new(NULL);
    }

    self->center_box = gtk_center_box_new();
    gtk_center_box_set_start_widget(GTK_CENTER_BOX(self->center_box),
        self->labels[STATUSLINE_SECTION_LEFT]);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(self->center_box),
        self->labels[STATUSLINE_SECTION_MIDDLE]);
    gtk_center_box_set_end_widget(GTK_CENTER_BOX(self->center_box),
        self->labels[STATUSLINE_SECTION_RIGHT]);
    gtk_widget_set_parent(self->center_box, GTK_WIDGET(self));

    gtk_widget_add_css_class(GTK_WIDGET(self), "statusline");
}

static void statusline_dispose(GObject *object)
{
    Statusline *self = JUMPDF_STATUSLINE(object);

    g_clear_pointer(&self->center_box, gtk_widget_unparent);

    G_OBJECT_CLASS(statusline_parent_class)->dispose(object);
}

static void statusline_install_css(void)
{
    GdkDisplay *display = gdk_display_get_default();
    GtkCssProvider *provider;

    if (display == NULL) {
        return;
    }

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(display,
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
}

static void statusline_class_init(StatuslineClass *class)
{
    G_OBJECT_CLASS(class)->dispose = statusline_dispose;
    gtk_widget_class_set_layout_manager_type(GTK_WIDGET_CLASS(class),
        GTK_TYPE_BIN_LAYOUT);

    statusline_install_css();
}

Statusline *statusline_new(const StatuslineLayout *layout)
{
    Statusline *statusline = g_object_new(STATUSLINE_TYPE, NULL);

    statusline->layout = layout;

    return statusline;
}

void statusline_update(Statusline *statusline, const StatuslineState *state)
{
    gchar *text[STATUSLINE_N_SECTIONS];
    GtkLabel *label;

    statusline_format(statusline->layout, state, text);

    for (int i = 0; i < STATUSLINE_N_SECTIONS; i++) {
        label = GTK_LABEL(statusline->labels[i]);
        if (g_strcmp0(gtk_label_get_text(label), text[i]) != 0) {
            gtk_label_set_text(label, text[i]);
        }
        g_free(text[i]);
    }
}
