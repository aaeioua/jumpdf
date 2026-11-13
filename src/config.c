#include <stdlib.h>

#include "config.h"
#include "toml.h"
#include "project_config.h"

#define DEFAULT_FILE_CHOOSER_INITIAL_FOLDER_PATH "~/Documents"
#define DEFAULT_STEPS 15 // Number of steps in a page.
#define DEFAULT_MIN_SCALE 0.3 // To prevent divide by zero
#define DEFAULT_SCALE_STEP 0.1 // How much to scale the PDF on each event
#define DEFAULT_STATUSLINE_SEPARATOR " | "

Config *g_config = NULL;

static void config_parse(Config *config, FILE *fp);
static void config_load_default_statusline_left(Config *config);
static void config_load_default_statusline_middle(Config *config);
static void config_load_default_statusline_right(Config *config);
/* Necessary because g_array_append_val requires a reference, not a literal */
static void statusline_section_add_component(GArray *section, StatuslineComponent component);

Config *config_new(void)
{
    Config *config = malloc(sizeof(Config));
    if (config == NULL) {
        return NULL;
    }

    config_init(config);

    return config;
}

void config_init(Config *config)
{
    config->file_chooser_initial_folder_path = NULL;
    config->steps = -1;
    config->min_scale = -1.0;
    config->scale_step = -1.0;

    config->statusline_separator = NULL;
    config->statusline_left = g_array_new(FALSE, TRUE, sizeof(StatuslineComponent));
    config->statusline_middle = g_array_new(FALSE, TRUE, sizeof(StatuslineComponent));
    config->statusline_right = g_array_new(FALSE, TRUE, sizeof(StatuslineComponent));
}

void config_destroy(Config *config)
{
    if (config->file_chooser_initial_folder_path) {
        g_free(config->file_chooser_initial_folder_path);
        config->file_chooser_initial_folder_path = NULL;
    }

    g_free(config->statusline_separator);
    g_array_free(config->statusline_left, TRUE);
    g_array_free(config->statusline_middle, TRUE);
    g_array_free(config->statusline_right, TRUE);
}

void config_set_file_chooser_initial_folder_path(Config *config, gchar *file_chooser_initial_folder_path)
{
    config->file_chooser_initial_folder_path = file_chooser_initial_folder_path;
}

void config_set_steps(Config *config, int steps)
{
    if (steps <= 0) {
        g_printerr("\"steps\" must be greater than 0. Using default value.\n");
        config->steps = DEFAULT_STEPS;
    } else {
        config->steps = steps;
    }
}

void config_set_min_scale(Config *config, double min_scale)
{
    if (min_scale <= 0.0) {
        g_printerr("\"min_scale\" must be greater than 0.0. Using default value.\n");
        config->min_scale = DEFAULT_MIN_SCALE;
    } else {
        config->min_scale = min_scale;
    }
}

void config_set_scale_step(Config *config, double scale_step)
{
    if (scale_step <= 0.0) {
        g_printerr("\"scale_step\" must be greater than 0.0. Using default value.\n");
        config->scale_step = DEFAULT_SCALE_STEP;
    } else {
        config->scale_step = scale_step;
    }
}

void config_set_statusline_separator(Config *config, gchar *statusline_separator)
{
    config->statusline_separator = statusline_separator;
}

void config_load(Config *config)
{
    FILE *fp;
    gchar *config_file_path;

    config_file_path = g_build_filename(g_get_user_config_dir(), APP_NAME_STR, "config.toml", NULL);
    fp = fopen(config_file_path, "r");
    g_free(config_file_path);

    if (fp) {
        config_parse(config, fp);
        fclose(fp);
    } else {
        config_load_default(config);
    }
}

void config_load_default(Config *config)
{
    config_set_file_chooser_initial_folder_path(config, g_strdup(DEFAULT_FILE_CHOOSER_INITIAL_FOLDER_PATH));
    config_set_steps(config, DEFAULT_STEPS);
    config_set_min_scale(config, DEFAULT_MIN_SCALE);
    config_set_scale_step(config, DEFAULT_SCALE_STEP);
    config_set_statusline_separator(config, g_strdup(DEFAULT_STATUSLINE_SEPARATOR));

    config_load_default_statusline_left(config);
    config_load_default_statusline_middle(config);
    config_load_default_statusline_right(config);
}

static void config_parse(Config *config, FILE *fp)
{
    // TODO: Very long function
    toml_table_t *conf;
    char errbuf[256];
    toml_table_t *settings;
    toml_datum_t datum;
    toml_array_t *array;
    StatuslineComponent component = 0;

    conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    if (conf == 0) {
        g_printerr("Error parsing config file: %s\n\tUsing default values.\n", errbuf);
        config_load_default(config);
        return;
    }

    settings = toml_table_in(conf, "Settings");
    if (settings) {
        datum = toml_string_in(settings, "file_chooser_initial_folder_path");
        if (datum.ok) {
            config_set_file_chooser_initial_folder_path(config, datum.u.s);
        } else {
            g_printerr("Error parsing \"file_chooser_initial_folder_path\". Using default value.\n");
            config_set_file_chooser_initial_folder_path(config, g_strdup(DEFAULT_FILE_CHOOSER_INITIAL_FOLDER_PATH));
        }

        datum = toml_int_in(settings, "steps");
        if (datum.ok) {
            config_set_steps(config, datum.u.i);
        } else {
            g_printerr("Error parsing \"steps\". Using default value.\n");
            config_set_steps(config, DEFAULT_STEPS);
        }

        datum = toml_double_in(settings, "min_scale");
        if (datum.ok) {
            config_set_min_scale(config, datum.u.d);
        } else {
            g_printerr("Error parsing \"min_scale\". Using default value.\n");
            config_set_min_scale(config, DEFAULT_MIN_SCALE);
        }

        datum = toml_double_in(settings, "scale_step");
        if (datum.ok) {
            config_set_scale_step(config, datum.u.d);
        } else {
            g_printerr("Error parsing \"scale_step\". Using default value.\n");
            config_set_scale_step(config, DEFAULT_SCALE_STEP);
        }

        datum = toml_string_in(settings, "statusline_separator");
        if (datum.ok) {
            config_set_statusline_separator(config, datum.u.s);
        } else {
            g_printerr("Error parsing \"statusline_separator\". Using default value.\n");
            config_set_statusline_separator(config, g_strdup(DEFAULT_STATUSLINE_SEPARATOR));
        }

        array = toml_array_in(settings, "statusline_left");
        if (array) {
            g_array_set_size(config->statusline_left, toml_array_nelem(array));
            for (int i = 0; i < toml_array_nelem(array); i++) {
                datum = toml_string_at(array, i);
                if (datum.ok) {
                    component = statusline_component_from_str(datum.u.s);
                    if (component != 0) {
                        statusline_section_add_component(config->statusline_left, component);
                    } else {
                        g_printerr("Error: Component \"%s\" in \"statusline_left\" is invalid. Skipping.\n", datum.u.s);
                    }

                    free(datum.u.s);
                } else {
                    g_printerr("Error: statusline_left[%d] is not a string. Skipping.\n", i);
                }
            }
        } else {
            g_printerr("Error parsing \"statusline_left\". Using default value.\n");
            config_load_default_statusline_left(config);
        }

        array = toml_array_in(settings, "statusline_middle");
        if (array) {
            g_array_set_size(config->statusline_middle, toml_array_nelem(array));
            for (int i = 0; i < toml_array_nelem(array); i++) {
                datum = toml_string_at(array, i);
                if (datum.ok) {
                    component = statusline_component_from_str(datum.u.s);
                    if (component != 0) {
                        statusline_section_add_component(config->statusline_middle, component);
                    } else {
                        g_printerr("Error: Component \"%s\" in \"statusline_middle\" is invalid. Skipping.\n", datum.u.s);
                    }

                    free(datum.u.s);
                } else {
                    g_printerr("Error: statusline_middle[%d] is not a string. Skipping.\n", i);
                }
            }
        } else {
            g_printerr("Error parsing \"statusline_middle\". Using default value.\n");
            config_load_default_statusline_middle(config);
        }

        array = toml_array_in(settings, "statusline_right");
        if (array) {
            g_array_set_size(config->statusline_right, toml_array_nelem(array));
            for (int i = 0; i < toml_array_nelem(array); i++) {
                datum = toml_string_at(array, i);
                if (datum.ok) {
                    component = statusline_component_from_str(datum.u.s);
                    if (component != 0) {
                        statusline_section_add_component(config->statusline_right, component);
                    } else {
                        g_printerr("Error: Component \"%s\" in \"statusline_right\" is invalid. Skipping.\n", datum.u.s);
                    }

                    free(datum.u.s);
                } else {
                    g_printerr("Error: statusline_right[%d] is not a string. Skipping.\n", i);
                }
            }
        } else {
            g_printerr("Error parsing \"statusline_right\". Using default value.\n");
            config_load_default_statusline_right(config);
        }
    } else {
        g_printerr("Error parsing config file: No \"Settings\" table found. Using default values.\n");
        config_load_default(config);
    }

    toml_free(conf);
}

static void config_load_default_statusline_left(Config *config)
{
    StatuslineComponent components[] = {
        STATUSLINE_COMPONENT_PAGE,
    };

    g_array_set_size(config->statusline_left, 0);
    g_array_append_vals(config->statusline_left, components, sizeof(components) / sizeof(components[0]));
}

static void config_load_default_statusline_middle(Config *config)
{
    g_array_set_size(config->statusline_middle, 0);
}

static void config_load_default_statusline_right(Config *config)
{
    StatuslineComponent components[] = {
        STATUSLINE_COMPONENT_PENDING_KEYS,
        STATUSLINE_COMPONENT_CENTER_MODE,
        STATUSLINE_COMPONENT_SCALE,
        STATUSLINE_COMPONENT_MARK_SELECTION,
    };

    g_array_set_size(config->statusline_right, 0);
    g_array_append_vals(config->statusline_right, components, sizeof(components) / sizeof(components[0]));
}

static void statusline_section_add_component(GArray *section, StatuslineComponent component)
{
    g_array_append_val(section, component);
}
