#include <stdlib.h>

#include "document.h"
#include "utils.h"
#include "viewer_info.h"

typedef struct {
    Document *document;
    GCancellable *cancellable;
} DocumentLoad;

struct Document {
    GFile *file;
    char *uri;
    GBytes *bytes;
    DocumentReloaded reloaded;
    gpointer user_data;
    /* NULL when not watching */
    GFileMonitor *monitor;

    /* The running load */
    DocumentLoad *load;
    /* document_reload was called during it, so its result is already old */
    gboolean load_again;
};

static void document_watch(Document *document);
static void on_file_changed(GFileMonitor *monitor, GFile *file,
    GFile *other_file, GFileMonitorEvent event_type, gpointer user_data);
static void document_start_load(Document *document);
static void load_thread(GTask *task, gpointer source_object, gpointer data,
    GCancellable *cancellable);
static void load_done(GObject *source_object, GAsyncResult *result,
    gpointer user_data);
static void load_free(DocumentLoad *load);
static GBytes *read_version(GFile *file, GCancellable *cancellable,
    GError **error);

Document *document_open(GFile *file, gboolean watch,
    DocumentReloaded reloaded, gpointer user_data, GError **error)
{
    GBytes *bytes = read_version(file, NULL, error);
    Document *document;

    if (bytes == NULL) {
        return NULL;
    }

    document = g_new0(Document, 1);
    document->file = g_object_ref(file);
    document->uri = g_file_get_uri(file);
    document->bytes = bytes;
    document->reloaded = reloaded;
    document->user_data = user_data;

    if (watch) {
        document_watch(document);
    }

    return document;
}

void document_free(Document *document)
{
    if (document == NULL) {
        return;
    }

    if (document->monitor != NULL) {
        g_signal_handlers_disconnect_by_data(document->monitor, document);
        g_file_monitor_cancel(document->monitor);
        g_object_unref(document->monitor);
    }

    if (document->load != NULL) {
        document->load->document = NULL;
        g_cancellable_cancel(document->load->cancellable);
    }

    g_bytes_unref(document->bytes);
    g_free(document->uri);
    g_object_unref(document->file);
    g_free(document);
}

const char *document_uri(const Document *document)
{
    return document->uri;
}

GBytes *document_bytes(const Document *document)
{
    return document->bytes;
}

void document_reload(Document *document)
{
    if (document->load != NULL) {
        document->load_again = TRUE;
        return;
    }

    document_start_load(document);
}

gboolean document_loading(const Document *document)
{
    return document->load != NULL;
}

static void document_watch(Document *document)
{
    GError *error = NULL;

    document->monitor = g_file_monitor_file(document->file,
        G_FILE_MONITOR_WATCH_MOVES, NULL, &error);
    if (document->monitor == NULL) {
        g_printerr("Error watching %s: %s\n", document->uri, error->message);
        g_error_free(error);
        return;
    }

    g_signal_connect(document->monitor, "changed",
        G_CALLBACK(on_file_changed), document);
}

static void on_file_changed(GFileMonitor *monitor, GFile *file,
    GFile *other_file, GFileMonitorEvent event_type, gpointer user_data)
{
    UNUSED(monitor);

    Document *document = user_data;

    switch (event_type) {
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
        if (g_file_equal(file, document->file)) {
            document_reload(document);
        }
        break;
    case G_FILE_MONITOR_EVENT_RENAMED:
        if (other_file != NULL && g_file_equal(other_file, document->file)) {
            document_reload(document);
        }
        break;
    default:
        break;
    }
}

static void document_start_load(Document *document)
{
    DocumentLoad *load = g_new0(DocumentLoad, 1);
    GTask *task;

    load->document = document;
    load->cancellable = g_cancellable_new();
    document->load = load;

    task = g_task_new(NULL, load->cancellable, load_done, load);
    g_task_set_task_data(task, g_object_ref(document->file), g_object_unref);
    g_task_run_in_thread(task, load_thread);
    g_object_unref(task);
}

static void load_thread(GTask *task, gpointer source_object, gpointer data,
    GCancellable *cancellable)
{
    UNUSED(source_object);

    GError *error = NULL;
    GBytes *bytes = read_version(data, cancellable, &error);

    if (bytes == NULL) {
        g_task_return_error(task, error);
    } else {
        g_task_return_pointer(task, bytes, (GDestroyNotify)g_bytes_unref);
    }
}

static void load_done(GObject *source_object, GAsyncResult *result,
    gpointer user_data)
{
    UNUSED(source_object);

    DocumentLoad *load = user_data;
    Document *document = load->document;
    GError *error = NULL;
    GBytes *bytes = g_task_propagate_pointer(G_TASK(result), &error);

    load_free(load);

    if (document == NULL) {
        g_clear_error(&error);
        g_clear_pointer(&bytes, g_bytes_unref);
        return;
    }

    document->load = NULL;

    if (document->load_again) {
        document->load_again = FALSE;
        g_clear_error(&error);
        g_clear_pointer(&bytes, g_bytes_unref);
        document_start_load(document);
        return;
    }

    if (bytes == NULL) {
        g_printerr("Error reloading %s: %s\n", document->uri, error->message);
        g_error_free(error);
        return;
    }

    g_bytes_unref(document->bytes);
    document->bytes = bytes;

    document->reloaded(document, document->user_data);
}

static void load_free(DocumentLoad *load)
{
    g_object_unref(load->cancellable);
    g_free(load);
}

static GBytes *read_version(GFile *file, GCancellable *cancellable,
    GError **error)
{
    GBytes *bytes = g_file_load_bytes(file, cancellable, NULL, error);
    ViewerInfo *check;

    if (bytes == NULL) {
        return NULL;
    }

    check = viewer_info_new_from_bytes(bytes, error);
    if (check == NULL) {
        g_bytes_unref(bytes);
        return NULL;
    }

    viewer_info_destroy(check);
    free(check);

    return bytes;
}
