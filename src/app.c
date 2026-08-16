#include <gtk/gtk.h>
#include <stdio.h>

#include "app.h"
#include "project_config.h"
#include "config.h"
#include "utils.h"
#include "database.h"

static void window_update_cursor_cb(gpointer win_ptr, gpointer user_data);
static void window_redraw_cb(gpointer win_ptr, gpointer user_data);
static void database_update_mark_manager_cb(gpointer uri_ptr, gpointer manager_ptr, gpointer user_data);

static void on_file_dialog_response(GObject *source_object, GAsyncResult *res, gpointer user_data);

struct _App {
    GtkApplication parent;

    /*
    * Key: URI
    * Value: ViewerMarkManager *
    * NOTE: a URI can have multiple mark managers with the same groups,
    * but with a different current_group. The value here is just a default
    */
    GHashTable *uri_mark_manager_map;
    GPtrArray *windows;
    Database *db;
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
    gchar *db_filename = NULL;

    g_config = config_new();
    config_load(g_config);

    db_filename = g_build_filename(g_get_user_data_dir(), APP_NAME_STR, "jumpdf.db", NULL);
    ensure_path_exists(db_filename);
    app->db = database_open(db_filename);
    database_check_update(app->db);
    g_free(db_filename);

    app->uri_mark_manager_map = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)viewer_mark_manager_destroy);
    app->windows = g_ptr_array_new();
}

static void app_finalize(GObject *object)
{
    App *app = JUMPDF_APP(object);

    app_update_database_mark_managers(app);
    g_hash_table_destroy(app->uri_mark_manager_map);

    g_ptr_array_free(app->windows, TRUE);

    database_close(app->db);
    free(app->db);

    config_destroy(g_config);
    free(g_config);

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

    ViewerMarkManager *mark_manager;
    Window *win;
    GPtrArray *new_windows = g_ptr_array_sized_new(n_files);

    for (int i = 0; i < n_files; i++) {
        mark_manager = app_get_mark_manager(JUMPDF_APP(app), files[i]);
        if (mark_manager != NULL) {
            win = window_new(JUMPDF_APP(app));
            g_ptr_array_add(new_windows, win);

            window_open(win, files[i], mark_manager);
            gtk_window_present(GTK_WINDOW(win));
        }
    }

    g_ptr_array_extend_and_steal(JUMPDF_APP(app)->windows, new_windows);
}

static void app_class_init(AppClass *class)
{
    G_OBJECT_CLASS(class)->finalize = app_finalize;
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
    char *uri = NULL;
    ViewerInfo *info = NULL;
    ViewerMarkGroup **groups = NULL;
    ViewerCursor **default_cursors = NULL;
    ViewerCursor *default_cursor = NULL;
    ViewerCursor *cursor = NULL;
    ViewerMarkManager *mark_manager = NULL;
    ViewerMarkManager *mark_manager_memory = NULL;
    ViewerMarkManager *mark_manager_db = NULL;

    info = viewer_info_new_from_gfile(file);
    if (info == NULL) {
        g_free(uri);
        return NULL;
    }

    uri = g_file_get_uri(file);
    mark_manager_memory = g_hash_table_lookup(JUMPDF_APP(app)->uri_mark_manager_map, uri);
    if (mark_manager_memory == NULL) {
        app_update_database_mark_managers(JUMPDF_APP(app));
        mark_manager_db = database_get_mark_manager(app->db, uri);

        if (mark_manager_db == NULL) {
            groups = malloc(NUM_GROUPS * sizeof(ViewerMarkGroup *));

            for (unsigned int i = 0; i < NUM_GROUPS; i++) {
                default_cursors = malloc(NUM_MARKS * sizeof(ViewerCursor *));
                for (unsigned int j = 0; j < NUM_MARKS; j++) {
                    default_cursors[j] = NULL;
                }

                groups[i] = viewer_mark_group_new(default_cursors, 0, 0);
                free(default_cursors);
            }

            mark_manager = viewer_mark_manager_new(groups, 0, 0);
            free(groups);

            database_insert_mark_manager(app->db, uri, mark_manager);
        } else {
            mark_manager = mark_manager_db;
        }

        g_hash_table_insert(JUMPDF_APP(app)->uri_mark_manager_map,
            uri, mark_manager);
    } else {
        // Copy to have the same groups and marks, but different selections 
        mark_manager = viewer_mark_manager_copy(mark_manager_memory);
    }

    for (unsigned int j = 0; j < NUM_GROUPS; j++) {
        for (unsigned int k = 0; k < NUM_MARKS; k++) {
            cursor = viewer_mark_manager_get_mark(mark_manager, j, k);
            if (cursor != NULL) {
                cursor->info = info;
            }
        }
    }

    if (viewer_mark_manager_get_current_cursor(mark_manager) == NULL) {
        default_cursor = viewer_cursor_new(info, 0, 0.0, 0.0, 1.0, TRUE, FALSE, 0);
        viewer_mark_manager_set_mark(mark_manager, viewer_cursor_copy(default_cursor),
            viewer_mark_manager_get_current_group_index(mark_manager),
            viewer_mark_manager_get_current_mark_index(mark_manager));

        viewer_cursor_destroy(default_cursor);
        free(default_cursor);
    }

    return mark_manager;
}

void app_remove_window(App *app, Window *win)
{
    g_ptr_array_remove_fast(app->windows, win);
}

void app_update_cursors(App *app)
{
    g_ptr_array_foreach(app->windows, window_update_cursor_cb, NULL);
}

void app_redraw_windows(App *app)
{
    g_ptr_array_foreach(app->windows, window_redraw_cb, NULL);
}

void app_update_database_mark_managers(App *app)
{
    g_hash_table_foreach(app->uri_mark_manager_map,
        (GHFunc)database_update_mark_manager_cb, app);
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

static void window_update_cursor_cb(gpointer win_ptr, gpointer user_data)
{
    UNUSED(user_data);

    Window *win = win_ptr;

    window_update_cursor(win);
}

static void window_redraw_cb(gpointer win_ptr, gpointer user_data)
{
    UNUSED(user_data);

    Window *win = win_ptr;

    window_redraw(win);
}

static void database_update_mark_manager_cb(gpointer uri_ptr, gpointer manager_ptr, gpointer user_data)
{
    const char *uri = uri_ptr;
    ViewerMarkManager *manager = manager_ptr;
    App *app = (App *)user_data;

    database_update_mark_manager(app->db, uri, manager);
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