#pragma once

#include <glib.h>

#include "statusline_layout.h"

typedef struct Config {
    gchar *file_chooser_initial_folder_path;
    int steps;
    double min_scale;
    double scale_step;

    StatuslineLayout statusline;
} Config;

extern Config *g_config;

Config *config_new(void);
void config_init(Config *config);
void config_destroy(Config *config);

void config_set_file_chooser_initial_folder_path(Config *config, gchar *file_chooser_initial_folder_path);
void config_set_steps(Config *config, int steps);
void config_set_min_scale(Config *config, double min_scale);
void config_set_scale_step(Config *config, double scale_step);
void config_set_statusline_separator(Config *config, gchar *statusline_separator);

void config_load(Config *config);
void config_load_default(Config *config);
