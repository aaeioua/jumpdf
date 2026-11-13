#include <gtk/gtk.h>
#include <stdio.h>

#include "app.h"
#include "project_config.h"
#include "config.h"
#include "utils.h"
#include "mark_store.h"

static void window_redraw_cb(gpointer win_ptr, gpointer manager_ptr);
static void save_all_documents(App *app);

static void on_file_dialog_response(GObject *source_object, GAsyncResult *res, gpointer user_data);

struct _App {
    GtkApplication parent;

    /*
    * Key: URI
    * Value: ViewerMarkManager *
    * NOTE: one Mark Manager per document, shared by every window on it.
    * Each window attaches its own Group selection. Kept from first open
    * until exit, so closing a window only detaches its selection.
    */
    GHashTable *uri_mark_manager_map;
    GPtrArray *windows;
    MarkStore *mark_store;
};

G_DEFINE_TYPE(App, app, GTK_TYPE_APPLICATION)

int app_run(int argc, char *argv[])
{
    App *app = app_new();
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

static void app_init(App *app)
{
    app->uri_mark_manager_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)viewer_mark_manager_destroy);
    app->windows = g_ptr_array_new();
}

static void app_startup(GApplication *application)
{
    App *app = JUMPDF_APP(application);
    GError *error = NULL;
    gchar *store_path = NULL;

    G_APPLICATION_CLASS(app_parent_class)->startup(application);

    g_config = config_new();
    config_load(g_config);

    store_path = g_build_filename(g_get_user_data_dir(), APP_NAME_STR, "jumpdf.db", NULL);
    app->mark_store = mark_store_open(store_path, &error);
    if (app->mark_store == NULL) {
        g_printerr("Marks will not be saved this session: %s\n", error->message);
        g_error_free(error);
    }
    g_free(store_path);
}

static void app_shutdown(GApplication *application)
{
    App *app = JUMPDF_APP(application);

    save_all_documents(app);
    mark_store_close(app->mark_store);
    app->mark_store = NULL;

    G_APPLICATION_CLASS(app_parent_class)->shutdown(application);
}

static void app_finalize(GObject *object)
{
    App *app = JUMPDF_APP(object);

    g_hash_table_destroy(app->uri_mark_manager_map);

    g_ptr_array_free(app->windows, TRUE);

    /* NULL in a process that only forwarded files */
    if (g_config != NULL) {
        config_destroy(g_config);
        free(g_config);
        g_config = NULL;
    }

    G_OBJECT_CLASS(app_parent_class)->finalize(object);
}

static void app_activate(GApplication *app)
{
    app_open_file_chooser(JUMPDF_APP(app));
}

static void app_open(GApplication *app, GFile **files, int n_files,
    const char *hint)
{
    UNUSED(hint);

    ViewerInfo *info;
    ViewerMarkManager *mark_manager;
    Window *win;
    GPtrArray *new_windows = g_ptr_array_sized_new(n_files);

    for (int i = 0; i < n_files; i++) {
        info = viewer_info_new_from_gfile(files[i]);
        if (info == NULL) {
            continue;
        }

        mark_manager = app_get_mark_manager(JUMPDF_APP(app), files[i]);
        win = window_new(JUMPDF_APP(app));
        g_ptr_array_add(new_windows, win);

        window_open(win, files[i], mark_manager, info);
        gtk_window_present(GTK_WINDOW(win));
    }

    g_ptr_array_extend_and_steal(JUMPDF_APP(app)->windows, new_windows);
}

static void app_class_init(AppClass *class)
{
    G_OBJECT_CLASS(class)->finalize = app_finalize;
    G_APPLICATION_CLASS(class)->startup = app_startup;
    G_APPLICATION_CLASS(class)->shutdown = app_shutdown;
    G_APPLICATION_CLASS(class)->activate = app_activate;
    G_APPLICATION_CLASS(class)->open = app_open;
}

App *app_new(void)
{
    return g_object_new(APP_TYPE, "application-id", APP_ID_STR, "flags",
        G_APPLICATION_HANDLES_OPEN, NULL);
}

ViewerMarkManager *app_get_mark_manager(App *app, GFile *file)
{
    char *uri = g_file_get_uri(file);
    ViewerMarkManager *mark_manager = g_hash_table_lookup(app->uri_mark_manager_map, uri);

    if (mark_manager != NULL) {
        g_free(uri);
        return mark_manager;
    }

    if (mark_store_load(app->mark_store, uri, &mark_manager) != MARK_STORE_FOUND) {
        mark_manager = viewer_mark_manager_new_default();
    }

    g_hash_table_insert(app->uri_mark_manager_map, uri, mark_manager);

    return mark_manager;
}

void app_remove_window(App *app, Window *win)
{
    g_ptr_array_remove_fast(app->windows, win);
}

void app_redraw_windows(App *app, ViewerMarkManager *manager)
{
    g_ptr_array_foreach(app->windows, window_redraw_cb, manager);
}

void app_save_document(App *app, const char *uri)
{
    ViewerMarkManager *manager = g_hash_table_lookup(app->uri_mark_manager_map, uri);

    if (manager != NULL) {
        mark_store_save(app->mark_store, uri, manager);
    }
}

void app_open_file_chooser(App *app)
{
    GtkFileDialog *file_dialog = gtk_file_dialog_new();
    gchar *initial_folder_path = expand_path(g_config->file_chooser_initial_folder_path);
    GFile *initial_folder = g_file_new_for_path(initial_folder_path);

    g_free(initial_folder_path);

    gtk_file_dialog_set_initial_folder(file_dialog, initial_folder);
    g_object_unref(initial_folder);

    g_application_hold(G_APPLICATION(app));
    gtk_file_dialog_open_multiple(file_dialog, NULL, NULL, (GAsyncReadyCallback)on_file_dialog_response, app);
}

static void window_redraw_cb(gpointer win_ptr, gpointer manager_ptr)
{
    Window *win = win_ptr;

    if (window_get_mark_manager(win) == manager_ptr) {
        window_redraw(win);
    }
}

static void save_all_documents(App *app)
{
    GHashTableIter iter;
    gpointer uri;

    g_hash_table_iter_init(&iter, app->uri_mark_manager_map);
    while (g_hash_table_iter_next(&iter, &uri, NULL)) {
        app_save_document(app, uri);
    }
}

static void on_file_dialog_response(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    GError *error = NULL;
    GListModel *files_model = gtk_file_dialog_open_multiple_finish(dialog, res, &error);
    GApplication *app = G_APPLICATION(user_data);
    int n_files = 0;
    GFile **file_array = NULL;

    if (error != NULL) {
        g_printerr("Error opening files: %s\n", error->message);
        g_error_free(error);
    } else if (files_model != NULL) {
        n_files = g_list_model_get_n_items(files_model);
        file_array = g_new(GFile *, n_files);
        for (int i = 0; i < n_files; i++) {
            file_array[i] = G_FILE(g_list_model_get_item(files_model, i));
        }

        app_open(app, file_array, n_files, NULL);

        for (int i = 0; i < n_files; i++) {
            g_object_unref(file_array[i]);
        }
        g_free(file_array);

        g_object_unref(files_model);
    }

    g_object_unref(dialog);
    g_application_release(app);
}