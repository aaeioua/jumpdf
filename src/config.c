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
static void config_parse_settings(Config *config, const toml_table_t *settings);
static gchar *settings_string(const toml_table_t *settings, const char *key,
    const char *fallback);
static int settings_int(const toml_table_t *settings, const char *key,
    int fallback);
static double settings_double(const toml_table_t *settings, const char *key,
    double fallback);
static void settings_statusline_section(const toml_table_t *settings,
    const char *key, GArray *section, void (*load_default)(GArray *section));
static void statusline_section_load_default_left(GArray *section);
static void statusline_section_load_default_middle(GArray *section);
static void statusline_section_load_default_right(GArray *section);

static const struct {
    const char *key;
    StatuslineSection section;
    void (*load_default)(GArray *section);
} statusline_section_keys[] = {
    {"statusline_left", STATUSLINE_SECTION_LEFT, statusline_section_load_default_left},
    {"statusline_middle", STATUSLINE_SECTION_MIDDLE, statusline_section_load_default_middle},
    {"statusline_right", STATUSLINE_SECTION_RIGHT, statusline_section_load_default_right},
};

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

    statusline_layout_init(&config->statusline);
}

void config_destroy(Config *config)
{
    if (config->file_chooser_initial_folder_path) {
        g_free(config->file_chooser_initial_folder_path);
        config->file_chooser_initial_folder_path = NULL;
    }

    statusline_layout_clear(&config->statusline);
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
    g_free(config->statusline.separator);
    config->statusline.separator = statusline_separator;
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

    for (guint i = 0; i < G_N_ELEMENTS(statusline_section_keys); i++) {
        statusline_section_keys[i].load_default(
            config->statusline.sections[statusline_section_keys[i].section]);
    }
}

static void config_parse(Config *config, FILE *fp)
{
    toml_table_t *conf;
    char errbuf[256];
    toml_table_t *settings;

    conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
    if (conf == 0) {
        g_printerr("Error parsing config file: %s\n\tUsing default values.\n", errbuf);
        config_load_default(config);
        return;
    }

    settings = toml_table_in(conf, "Settings");
    if (settings) {
        config_parse_settings(config, settings);
    } else {
        config_load_default(config);
    }

    toml_free(conf);
}

/* A missing key uses its default silently; a present but invalid one prints an error */
static void config_parse_settings(Config *config, const toml_table_t *settings)
{
    config_set_file_chooser_initial_folder_path(config,
        settings_string(settings, "file_chooser_initial_folder_path",
            DEFAULT_FILE_CHOOSER_INITIAL_FOLDER_PATH));
    config_set_steps(config,
        settings_int(settings, "steps", DEFAULT_STEPS));
    config_set_min_scale(config,
        settings_double(settings, "min_scale", DEFAULT_MIN_SCALE));
    config_set_scale_step(config,
        settings_double(settings, "scale_step", DEFAULT_SCALE_STEP));
    config_set_statusline_separator(config,
        settings_string(settings, "statusline_separator",
            DEFAULT_STATUSLINE_SEPARATOR));

    for (guint i = 0; i < G_N_ELEMENTS(statusline_section_keys); i++) {
        settings_statusline_section(settings, statusline_section_keys[i].key,
            config->statusline.sections[statusline_section_keys[i].section],
            statusline_section_keys[i].load_default);
    }
}

static gchar *settings_string(const toml_table_t *settings, const char *key,
    const char *fallback)
{
    toml_datum_t datum;
    gchar *value;

    if (!toml_key_exists(settings, key)) {
        return g_strdup(fallback);
    }

    datum = toml_string_in(settings, key);
    if (!datum.ok) {
        g_printerr("Error parsing \"%s\". Using default value.\n", key);
        return g_strdup(fallback);
    }

    value = g_strdup(datum.u.s);
    free(datum.u.s);
    return value;
}

static int settings_int(const toml_table_t *settings, const char *key,
    int fallback)
{
    toml_datum_t datum;

    if (!toml_key_exists(settings, key)) {
        return fallback;
    }

    datum = toml_int_in(settings, key);
    if (!datum.ok) {
        g_printerr("Error parsing \"%s\". Using default value.\n", key);
        return fallback;
    }

    return datum.u.i;
}

static double settings_double(const toml_table_t *settings, const char *key,
    double fallback)
{
    toml_datum_t datum;

    if (!toml_key_exists(settings, key)) {
        return fallback;
    }

    datum = toml_double_in(settings, key);
    if (!datum.ok) {
        g_printerr("Error parsing \"%s\". Using default value.\n", key);
        return fallback;
    }

    return datum.u.d;
}

static void settings_statusline_section(const toml_table_t *settings,
    const char *key, GArray *section, void (*load_default)(GArray *section))
{
    toml_array_t *array;
    toml_datum_t datum;
    StatuslineComponent component;

    if (!toml_key_exists(settings, key)) {
        load_default(section);
        return;
    }

    array = toml_array_in(settings, key);
    if (array == NULL) {
        g_printerr("Error parsing \"%s\". Using default value.\n", key);
        load_default(section);
        return;
    }

    g_array_set_size(section, 0);
    for (int i = 0; i < toml_array_nelem(array); i++) {
        datum = toml_string_at(array, i);
        if (!datum.ok) {
            g_printerr("Error: %s[%d] is not a string. Skipping.\n", key, i);
            continue;
        }

        component = statusline_component_from_str(datum.u.s);
        if (component != 0) {
            g_array_append_val(section, component);
        } else {
            g_printerr("Error: Component \"%s\" in \"%s\" is invalid. Skipping.\n",
                datum.u.s, key);
        }

        free(datum.u.s);
    }
}

static void statusline_section_load_default_left(GArray *section)
{
    static const StatuslineComponent components[] = {
        STATUSLINE_COMPONENT_PAGE,
        STATUSLINE_COMPONENT_SEARCH,
        STATUSLINE_COMPONENT_PENDING_KEYS,
    };

    g_array_set_size(section, 0);
    g_array_append_vals(section, components, G_N_ELEMENTS(components));
}

static void statusline_section_load_default_middle(GArray *section)
{
    g_array_set_size(section, 0);
}

static void statusline_section_load_default_right(GArray *section)
{
    static const StatuslineComponent components[] = {
        STATUSLINE_COMPONENT_CENTER_MODE,
        STATUSLINE_COMPONENT_SCALE,
        STATUSLINE_COMPONENT_MARK_SELECTION,
    };

    g_array_set_size(section, 0);
    g_array_append_vals(section, components, G_N_ELEMENTS(components));
}
