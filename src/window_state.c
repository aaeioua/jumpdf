#include "window_state.h"

#include <errno.h>
#include <glib/gstdio.h>

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 1000
#define DEFAULT_MAXIMIZED FALSE

#define GROUP "Window"

static int read_size(GKeyFile *key_file, const char *path, const char *key,
    int fallback);
static gboolean read_bool(GKeyFile *key_file, const char *path,
    const char *key, gboolean fallback);

WindowState window_state_default(void)
{
    return (WindowState){
        .width = DEFAULT_WIDTH,
        .height = DEFAULT_HEIGHT,
        .maximized = DEFAULT_MAXIMIZED,
    };
}

WindowState window_state_load(const char *path)
{
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    WindowState state = window_state_default();

    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
        if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_printerr("Error reading %s: %s. Using the default window size.\n",
                path, error->message);
        }
        g_error_free(error);
        g_key_file_free(key_file);
        return state;
    }

    state.width = read_size(key_file, path, "width", state.width);
    state.height = read_size(key_file, path, "height", state.height);
    state.maximized = read_bool(key_file, path, "maximized", state.maximized);

    g_key_file_free(key_file);

    return state;
}

gboolean window_state_save(const char *path, const WindowState *state,
    GError **error)
{
    GKeyFile *key_file = g_key_file_new();
    gchar *dir = g_path_get_dirname(path);
    gboolean saved;

    g_key_file_set_integer(key_file, GROUP, "width", state->width);
    g_key_file_set_integer(key_file, GROUP, "height", state->height);
    g_key_file_set_boolean(key_file, GROUP, "maximized", state->maximized);

    if (g_mkdir_with_parents(dir, 0700) != 0) {
        const int saved_errno = errno;

        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
            "Can't create %s: %s", dir, g_strerror(saved_errno));
        saved = FALSE;
    } else {
        saved = g_key_file_save_to_file(key_file, path, error);
    }

    g_free(dir);
    g_key_file_free(key_file);

    return saved;
}

void window_state_apply(const WindowState *state, GtkWindow *window)
{
    gtk_window_set_default_size(window, state->width, state->height);
    if (state->maximized) {
        gtk_window_maximize(window);
    }
}

WindowState window_state_of(GtkWindow *window)
{
    WindowState state;

    gtk_window_get_default_size(window, &state.width, &state.height);
    state.maximized = gtk_window_is_maximized(window);

    return state;
}

static int read_size(GKeyFile *key_file, const char *path, const char *key,
    int fallback)
{
    GError *error = NULL;
    int size;

    if (!g_key_file_has_key(key_file, GROUP, key, NULL)) {
        return fallback;
    }

    size = g_key_file_get_integer(key_file, GROUP, key, &error);
    if (error != NULL || size <= 0) {
        g_printerr("Error reading \"%s\" in %s: it must be a whole number "
            "greater than 0. Using the default.\n", key, path);
        g_clear_error(&error);
        return fallback;
    }

    return size;
}

static gboolean read_bool(GKeyFile *key_file, const char *path,
    const char *key, gboolean fallback)
{
    GError *error = NULL;
    gboolean value;

    if (!g_key_file_has_key(key_file, GROUP, key, NULL)) {
        return fallback;
    }

    value = g_key_file_get_boolean(key_file, GROUP, key, &error);
    if (error != NULL) {
        g_printerr("Error reading \"%s\" in %s: it must be true or false. "
            "Using the default.\n", key, path);
        g_error_free(error);
        return fallback;
    }

    return value;
}
