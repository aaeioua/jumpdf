#pragma once

#include <glib.h>

#define UNUSED(x) (void)(x)

void ensure_path_exists(const char *path);
gchar *expand_path(const char *path);