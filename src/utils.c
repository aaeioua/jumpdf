#include "utils.h"

void ensure_path_exists(const char *path)
{
    gchar *dir = g_path_get_dirname(path);
    gint rc = g_mkdir_with_parents(dir, 0700);
    int saved_errno = errno;

    if (rc == -1) {
        g_printerr("GLib: %s\n", g_strerror(saved_errno));
    }

    g_free(dir);
}

gchar *expand_path(const char *path)
{
    if (path[0] == '~') {
        // path + 1 to skip the tilde
        return g_build_filename(g_get_home_dir(), path + 1, NULL);
    } else {
        return g_strdup(path);
    }
}