#include "utils.h"

gchar *expand_path(const char *path)
{
    if (path[0] == '~') {
        // path + 1 to skip the tilde
        return g_build_filename(g_get_home_dir(), path + 1, NULL);
    } else {
        return g_strdup(path);
    }
}