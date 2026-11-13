#include "mark_store.h"

#include <errno.h>
#include <math.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

#define SCHEMA_VERSION 2
#define BUSY_TIMEOUT_MS 5000

#define SCHEMA_V2_SQL \
    "CREATE TABLE document (" \
    "    uri TEXT PRIMARY KEY," \
    "    current_group INTEGER NOT NULL," \
    "    previous_group INTEGER NOT NULL" \
    ");" \
    "CREATE TABLE mark_group (" \
    "    uri TEXT NOT NULL," \
    "    group_index INTEGER NOT NULL," \
    "    current_mark INTEGER NOT NULL," \
    "    previous_mark INTEGER NOT NULL," \
    "    PRIMARY KEY (uri, group_index)" \
    ") WITHOUT ROWID;" \
    "CREATE TABLE mark (" \
    "    uri TEXT NOT NULL," \
    "    group_index INTEGER NOT NULL," \
    "    mark_index INTEGER NOT NULL," \
    "    page INTEGER NOT NULL," \
    "    x_offset REAL NOT NULL," \
    "    y_offset REAL NOT NULL," \
    "    scale REAL NOT NULL," \
    "    center_mode INTEGER NOT NULL," \
    "    dark_mode INTEGER NOT NULL," \
    "    PRIMARY KEY (uri, group_index, mark_index)" \
    ") WITHOUT ROWID;"

/* migrations[v] moves the schema from version v to v + 1 */
static const char *const migrations[SCHEMA_VERSION] = {
    /* jumpdf <= 1.1 stored no previous Mark or previous Group */
    [0] =
        "ALTER TABLE cursor_group"
        "    ADD COLUMN previous_mark INTEGER NOT NULL DEFAULT 0;"
        "ALTER TABLE mark_manager"
        "    ADD COLUMN previous_group INTEGER NOT NULL DEFAULT 0;",
    /* jumpdf 1.2 linked Marks and Groups through id tables */
    [1] =
        SCHEMA_V2_SQL
        "INSERT INTO document (uri, current_group, previous_group)"
        "    SELECT uri, current_group, previous_group FROM mark_manager;"
        "INSERT OR IGNORE INTO mark_group"
        "    (uri, group_index, current_mark, previous_mark)"
        "    SELECT link.mark_manager_uri, link.group_index,"
        "           g.current_mark, g.previous_mark"
        "    FROM mark_manager_contains_group AS link"
        "    JOIN cursor_group AS g ON g.id = link.group_id;"
        "INSERT OR IGNORE INTO mark"
        "    (uri, group_index, mark_index, page, x_offset, y_offset, scale,"
        "     center_mode, dark_mode)"
        "    SELECT link.mark_manager_uri, link.group_index, gc.cursor_index,"
        "           c.current_page, c.x_offset, c.y_offset, c.scale,"
        "           c.center_mode, c.dark_mode"
        "    FROM mark_manager_contains_group AS link"
        "    JOIN group_contains_cursor AS gc ON gc.group_id = link.group_id"
        "    JOIN cursor AS c ON c.id = gc.cursor_id;"
        "DROP TABLE group_contains_cursor;"
        "DROP TABLE mark_manager_contains_group;"
        "DROP TABLE cursor;"
        "DROP TABLE cursor_group;"
        "DROP TABLE mark_manager;",
};

typedef enum {
    STMT_LOAD_DOCUMENT,
    STMT_LOAD_GROUPS,
    STMT_LOAD_MARKS,
    STMT_DELETE_MARKS,
    STMT_DELETE_GROUPS,
    STMT_SAVE_DOCUMENT,
    STMT_SAVE_GROUP,
    STMT_SAVE_MARK,
    N_STATEMENTS
} StatementId;

static const char *const statement_sql[N_STATEMENTS] = {
    [STMT_LOAD_DOCUMENT] =
        "SELECT current_group, previous_group FROM document WHERE uri = ?1;",
    [STMT_LOAD_GROUPS] =
        "SELECT group_index, current_mark, previous_mark"
        "    FROM mark_group WHERE uri = ?1;",
    [STMT_LOAD_MARKS] =
        "SELECT group_index, mark_index, page, x_offset, y_offset, scale,"
        "       center_mode, dark_mode"
        "    FROM mark WHERE uri = ?1;",
    [STMT_DELETE_MARKS] = "DELETE FROM mark WHERE uri = ?1;",
    [STMT_DELETE_GROUPS] = "DELETE FROM mark_group WHERE uri = ?1;",
    [STMT_SAVE_DOCUMENT] =
        "INSERT OR REPLACE INTO document (uri, current_group, previous_group)"
        "    VALUES (?1, ?2, ?3);",
    [STMT_SAVE_GROUP] =
        "INSERT INTO mark_group (uri, group_index, current_mark, previous_mark)"
        "    VALUES (?1, ?2, ?3, ?4);",
    [STMT_SAVE_MARK] =
        "INSERT INTO mark (uri, group_index, mark_index, page, x_offset,"
        "                  y_offset, scale, center_mode, dark_mode)"
        "    VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);",
};

struct MarkStore {
    sqlite3 *db;
    sqlite3_stmt *statements[N_STATEMENTS];
    /* URIs whose load FAILED this session; saving them is refused */
    GHashTable *unsaveable_uris;
};

static GQuark mark_store_error_quark(void);
static gboolean is_in_memory(const char *path);
static gboolean ensure_parent_directory(const char *path, GError **error);
static gboolean migrate(sqlite3 *db, const char *path, GError **error);
static gboolean run_migration(sqlite3 *db, int from, const char *sql, int to,
    GError **error);
static gboolean run_migration_locked(sqlite3 *db, int from, const char *sql,
    int to, GError **error);
static gboolean back_up(sqlite3 *db, const char *path, int version,
    GError **error);
static gboolean prepare_statements(MarkStore *store, GError **error);
static gboolean query_int(sqlite3 *db, const char *sql, const char *arg,
    int *result, GError **error);
static gboolean exec(sqlite3 *db, const char *sql, GError **error);
static void set_sqlite_error(GError **error, sqlite3 *db, const char *what);

static MarkStoreLoadResult load_locked(MarkStore *store, const char *uri,
    ViewerMarkManager **manager, GError **error);
static gboolean load_groups(MarkStore *store, const char *uri,
    ViewerMarkGroup *groups[NUM_GROUPS], GError **error);
static gboolean load_marks(MarkStore *store, const char *uri,
    ViewerMarkGroup *groups[NUM_GROUPS], GError **error);
static gboolean save_locked(MarkStore *store, const char *uri,
    const ViewerMarkManager *manager, GError **error);
static gboolean save_group(MarkStore *store, const char *uri,
    unsigned int group_i, const ViewerMarkGroup *group, GError **error);

static sqlite3_stmt *statement(MarkStore *store, StatementId id,
    const char *uri);
static gboolean finish(MarkStore *store, sqlite3_stmt *stmt, int rc,
    GError **error);
static gboolean run(MarkStore *store, sqlite3_stmt *stmt, GError **error);
static gboolean index_column(sqlite3_stmt *stmt, int column,
    unsigned int count, unsigned int *index);
static unsigned int clamped_index_column(sqlite3_stmt *stmt, int column,
    unsigned int count);
static double finite_or(double value, double fallback);

MarkStore *mark_store_open(const char *path, GError **error)
{
    g_return_val_if_fail(path != NULL, NULL);

    if (!is_in_memory(path) && !ensure_parent_directory(path, error)) {
        return NULL;
    }

    MarkStore *store = g_new0(MarkStore, 1);
    store->unsaveable_uris = g_hash_table_new_full(g_str_hash, g_str_equal,
        g_free, NULL);

    if (sqlite3_open_v2(path, &store->db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK) {
        set_sqlite_error(error, store->db, "Could not open the Mark store");
        mark_store_close(store);
        return NULL;
    }
    sqlite3_busy_timeout(store->db, BUSY_TIMEOUT_MS);

    /* The journal mode is only changed once the schema is known to be ours */
    if (!migrate(store->db, path, error) ||
        !exec(store->db, "PRAGMA journal_mode = WAL;"
                         "PRAGMA synchronous = NORMAL;", error) ||
        !prepare_statements(store, error)) {
        g_prefix_error(error, "%s: ", path);
        mark_store_close(store);
        return NULL;
    }

    return store;
}

void mark_store_close(MarkStore *store)
{
    if (store == NULL) {
        return;
    }

    for (int i = 0; i < N_STATEMENTS; i++) {
        sqlite3_finalize(store->statements[i]);
    }

    if (sqlite3_close(store->db) != SQLITE_OK) {
        g_printerr("Could not close the Mark store: %s\n",
            sqlite3_errmsg(store->db));
    }

    g_hash_table_destroy(store->unsaveable_uris);
    g_free(store);
}

MarkStoreLoadResult mark_store_load(MarkStore *store, const char *uri,
    ViewerMarkManager **manager)
{
    g_return_val_if_fail(uri != NULL && manager != NULL, MARK_STORE_FAILED);

    *manager = NULL;
    if (store == NULL) {
        return MARK_STORE_NOT_FOUND;
    }

    GError *error = NULL;
    MarkStoreLoadResult result = MARK_STORE_FAILED;

    if (exec(store->db, "BEGIN;", &error)) {
        result = load_locked(store, uri, manager, &error);
        sqlite3_exec(store->db, "COMMIT;", NULL, NULL, NULL);
    }

    if (result == MARK_STORE_FAILED) {
        g_printerr("Could not load the Marks of %s: %s. "
                   "They will not be saved this session.\n",
            uri, error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
        g_hash_table_add(store->unsaveable_uris, g_strdup(uri));
    }

    return result;
}

gboolean mark_store_save(MarkStore *store, const char *uri,
    const ViewerMarkManager *manager)
{
    g_return_val_if_fail(uri != NULL && manager != NULL, FALSE);

    if (store == NULL || g_hash_table_contains(store->unsaveable_uris, uri)) {
        return FALSE;
    }

    GError *error = NULL;
    gboolean saved = exec(store->db, "BEGIN IMMEDIATE;", &error);
    if (saved) {
        saved = save_locked(store, uri, manager, &error) &&
            exec(store->db, "COMMIT;", &error);
        if (!saved) {
            sqlite3_exec(store->db, "ROLLBACK;", NULL, NULL, NULL);
        }
    }

    if (!saved) {
        g_printerr("Could not save the Marks of %s: %s\n", uri,
            error != NULL ? error->message : "unknown error");
        g_clear_error(&error);
    }

    return saved;
}

static GQuark mark_store_error_quark(void)
{
    return g_quark_from_static_string("mark-store-error-quark");
}

static gboolean is_in_memory(const char *path)
{
    return path[0] == '\0' || strcmp(path, ":memory:") == 0;
}

static gboolean ensure_parent_directory(const char *path, GError **error)
{
    gchar *dir = g_path_get_dirname(path);
    gboolean created = g_mkdir_with_parents(dir, 0700) == 0;

    if (!created) {
        int saved_errno = errno;
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
            "Could not create %s: %s", dir, g_strerror(saved_errno));
    }

    g_free(dir);
    return created;
}

static gboolean migrate(sqlite3 *db, const char *path, GError **error)
{
    int version;
    int has_data;

    if (!query_int(db, "PRAGMA user_version;", NULL, &version, error)) {
        return FALSE;
    }

    if (version > SCHEMA_VERSION) {
        g_set_error(error, mark_store_error_quark(), 0,
            "written by a newer jumpdf (schema version %d, expected at most %d)",
            version, SCHEMA_VERSION);
        return FALSE;
    }

    if (version == SCHEMA_VERSION) {
        return TRUE;
    }

    /* Before versioning, jumpdf left user_version at 0 */
    if (version == 0) {
        if (!query_int(db,
                "SELECT count(*) FROM sqlite_master"
                "    WHERE type = 'table' AND name = ?1;",
                "mark_manager", &has_data, error)) {
            return FALSE;
        }

        if (!has_data) {
            return run_migration(db, 0, SCHEMA_V2_SQL, SCHEMA_VERSION, error);
        }
    }

    if (!back_up(db, path, version, error)) {
        return FALSE;
    }

    for (int from = version; from < SCHEMA_VERSION; from++) {
        if (!run_migration(db, from, migrations[from], from + 1, error)) {
            return FALSE;
        }
    }

    return TRUE;
}

/* Runs `sql` and moves user_version from `from` to `to` in one transaction */
static gboolean run_migration(sqlite3 *db, int from, const char *sql, int to,
    GError **error)
{
    if (!exec(db, "BEGIN IMMEDIATE;", error)) {
        return FALSE;
    }

    gboolean migrated = run_migration_locked(db, from, sql, to, error) &&
        exec(db, "COMMIT;", error);
    if (!migrated) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        g_prefix_error(error, "Could not migrate from schema version %d: ",
            from);
    }

    return migrated;
}

static gboolean run_migration_locked(sqlite3 *db, int from, const char *sql,
    int to, GError **error)
{
    int version;

    if (!query_int(db, "PRAGMA user_version;", NULL, &version, error)) {
        return FALSE;
    }

    /* Another jumpdf process got here first */
    if (version != from) {
        return TRUE;
    }

    gchar *set_version = g_strdup_printf("PRAGMA user_version = %d;", to);
    gboolean migrated = exec(db, sql, error) && exec(db, set_version, error);
    g_free(set_version);

    return migrated;
}

/* Copies the store to <path>.v<version>.bak, keeping any earlier copy */
static gboolean back_up(sqlite3 *db, const char *path, int version,
    GError **error)
{
    if (is_in_memory(path)) {
        return TRUE;
    }

    gchar *backup_path = g_strdup_printf("%s.v%d.bak", path, version);
    gboolean backed_up = TRUE;

    if (!g_file_test(backup_path, G_FILE_TEST_EXISTS)) {
        char *sql = sqlite3_mprintf("VACUUM INTO %Q;", backup_path);
        backed_up = exec(db, sql, error);
        sqlite3_free(sql);

        if (!backed_up) {
            g_prefix_error(error, "Could not back up to %s: ", backup_path);
        }
    }

    g_free(backup_path);
    return backed_up;
}

static gboolean prepare_statements(MarkStore *store, GError **error)
{
    for (int i = 0; i < N_STATEMENTS; i++) {
        if (sqlite3_prepare_v3(store->db, statement_sql[i], -1,
                SQLITE_PREPARE_PERSISTENT, &store->statements[i],
                NULL) != SQLITE_OK) {
            set_sqlite_error(error, store->db, "Could not prepare a query");
            return FALSE;
        }
    }

    return TRUE;
}

/* Runs a one-row, one-column query with an optional text parameter ?1 */
static gboolean query_int(sqlite3 *db, const char *sql, const char *arg,
    int *result, GError **error)
{
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_sqlite_error(error, db, sql);
        return FALSE;
    }

    if (arg != NULL) {
        sqlite3_bind_text(stmt, 1, arg, -1, SQLITE_STATIC);
    }

    gboolean has_row = sqlite3_step(stmt) == SQLITE_ROW;
    if (has_row) {
        *result = sqlite3_column_int(stmt, 0);
    } else {
        set_sqlite_error(error, db, sql);
    }

    sqlite3_finalize(stmt);
    return has_row;
}

static gboolean exec(sqlite3 *db, const char *sql, GError **error)
{
    char *message = NULL;

    if (sqlite3_exec(db, sql, NULL, NULL, &message) != SQLITE_OK) {
        g_set_error(error, mark_store_error_quark(), 0, "%s",
            message != NULL ? message : sqlite3_errmsg(db));
        sqlite3_free(message);
        return FALSE;
    }

    return TRUE;
}

static void set_sqlite_error(GError **error, sqlite3 *db, const char *what)
{
    g_set_error(error, mark_store_error_quark(), 0, "%s: %s", what,
        db != NULL ? sqlite3_errmsg(db) : "out of memory");
}

static MarkStoreLoadResult load_locked(MarkStore *store, const char *uri,
    ViewerMarkManager **manager, GError **error)
{
    sqlite3_stmt *stmt = statement(store, STMT_LOAD_DOCUMENT, uri);
    int rc = sqlite3_step(stmt);

    if (rc == SQLITE_DONE) {
        finish(store, stmt, rc, error);
        return MARK_STORE_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) {
        finish(store, stmt, rc, error);
        return MARK_STORE_FAILED;
    }

    const unsigned int current_group = clamped_index_column(stmt, 0,
        NUM_GROUPS);
    const unsigned int previous_group = clamped_index_column(stmt, 1,
        NUM_GROUPS);
    finish(store, stmt, SQLITE_DONE, NULL);

    ViewerCursor *empty_marks[NUM_MARKS] = {0};
    ViewerMarkGroup *groups[NUM_GROUPS];
    for (unsigned int i = 0; i < NUM_GROUPS; i++) {
        groups[i] = viewer_mark_group_new(empty_marks, 0, 0);
    }

    if (!load_groups(store, uri, groups, error) ||
        !load_marks(store, uri, groups, error)) {
        for (unsigned int i = 0; i < NUM_GROUPS; i++) {
            viewer_mark_group_destroy(groups[i]);
            free(groups[i]);
        }
        return MARK_STORE_FAILED;
    }

    *manager = viewer_mark_manager_new(groups, current_group, previous_group);
    viewer_mark_manager_ensure_current_mark(*manager);

    return MARK_STORE_FOUND;
}

static gboolean load_groups(MarkStore *store, const char *uri,
    ViewerMarkGroup *groups[NUM_GROUPS], GError **error)
{
    sqlite3_stmt *stmt = statement(store, STMT_LOAD_GROUPS, uri);
    int rc;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        unsigned int group_i;
        if (!index_column(stmt, 0, NUM_GROUPS, &group_i)) {
            continue;
        }

        groups[group_i]->current_mark = clamped_index_column(stmt, 1,
            NUM_MARKS);
        groups[group_i]->previous_mark = clamped_index_column(stmt, 2,
            NUM_MARKS);
    }

    return finish(store, stmt, rc, error);
}

static gboolean load_marks(MarkStore *store, const char *uri,
    ViewerMarkGroup *groups[NUM_GROUPS], GError **error)
{
    sqlite3_stmt *stmt = statement(store, STMT_LOAD_MARKS, uri);
    int rc;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        unsigned int group_i, mark_i;
        if (!index_column(stmt, 0, NUM_GROUPS, &group_i) ||
            !index_column(stmt, 1, NUM_MARKS, &mark_i)) {
            continue;
        }

        const double scale = sqlite3_column_double(stmt, 5);
        groups[group_i]->marks[mark_i] = viewer_cursor_new(
            MAX(0, sqlite3_column_int(stmt, 2)),
            finite_or(sqlite3_column_double(stmt, 3), 0.0),
            finite_or(sqlite3_column_double(stmt, 4), 0.0),
            isfinite(scale) && scale > 0.0 ? scale : 1.0,
            sqlite3_column_int(stmt, 6) != 0,
            sqlite3_column_int(stmt, 7) != 0);
    }

    return finish(store, stmt, rc, error);
}

static gboolean save_locked(MarkStore *store, const char *uri,
    const ViewerMarkManager *manager, GError **error)
{
    if (!run(store, statement(store, STMT_DELETE_MARKS, uri), error) ||
        !run(store, statement(store, STMT_DELETE_GROUPS, uri), error)) {
        return FALSE;
    }

    sqlite3_stmt *stmt = statement(store, STMT_SAVE_DOCUMENT, uri);
    sqlite3_bind_int64(stmt, 2, manager->current_group);
    sqlite3_bind_int64(stmt, 3, manager->previous_group);
    if (!run(store, stmt, error)) {
        return FALSE;
    }

    for (unsigned int i = 0; i < NUM_GROUPS; i++) {
        if (manager->groups[i] != NULL &&
            !save_group(store, uri, i, manager->groups[i], error)) {
            return FALSE;
        }
    }

    return TRUE;
}

static gboolean save_group(MarkStore *store, const char *uri,
    unsigned int group_i, const ViewerMarkGroup *group, GError **error)
{
    sqlite3_stmt *stmt = statement(store, STMT_SAVE_GROUP, uri);
    sqlite3_bind_int64(stmt, 2, group_i);
    sqlite3_bind_int64(stmt, 3, group->current_mark);
    sqlite3_bind_int64(stmt, 4, group->previous_mark);
    if (!run(store, stmt, error)) {
        return FALSE;
    }

    for (unsigned int i = 0; i < NUM_MARKS; i++) {
        const ViewerCursor *mark = group->marks[i];
        if (mark == NULL) {
            continue;
        }

        stmt = statement(store, STMT_SAVE_MARK, uri);
        sqlite3_bind_int64(stmt, 2, group_i);
        sqlite3_bind_int64(stmt, 3, i);
        sqlite3_bind_int64(stmt, 4, mark->current_page);
        sqlite3_bind_double(stmt, 5, mark->x_offset);
        sqlite3_bind_double(stmt, 6, mark->y_offset);
        sqlite3_bind_double(stmt, 7, mark->scale);
        sqlite3_bind_int(stmt, 8, mark->center_mode);
        sqlite3_bind_int(stmt, 9, mark->dark_mode);
        if (!run(store, stmt, error)) {
            return FALSE;
        }
    }

    return TRUE;
}

/* A cached statement, reset, with `uri` bound to ?1 */
static sqlite3_stmt *statement(MarkStore *store, StatementId id,
    const char *uri)
{
    sqlite3_stmt *stmt = store->statements[id];
    sqlite3_bind_text(stmt, 1, uri, -1, SQLITE_STATIC);
    return stmt;
}

/* Resets `stmt` for its next use; `rc` is its last sqlite3_step result */
static gboolean finish(MarkStore *store, sqlite3_stmt *stmt, int rc,
    GError **error)
{
    const gboolean done = rc == SQLITE_DONE;
    if (!done) {
        set_sqlite_error(error, store->db, sqlite3_sql(stmt));
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);

    return done;
}

static gboolean run(MarkStore *store, sqlite3_stmt *stmt, GError **error)
{
    return finish(store, stmt, sqlite3_step(stmt), error);
}

/* Reads an index, rejecting values outside [0, count) */
static gboolean index_column(sqlite3_stmt *stmt, int column,
    unsigned int count, unsigned int *index)
{
    const sqlite3_int64 value = sqlite3_column_int64(stmt, column);
    if (value < 0 || value >= (sqlite3_int64)count) {
        return FALSE;
    }

    *index = (unsigned int)value;
    return TRUE;
}

/* Reads an index, clamping it into [0, count) */
static unsigned int clamped_index_column(sqlite3_stmt *stmt, int column,
    unsigned int count)
{
    const sqlite3_int64 value = sqlite3_column_int64(stmt, column);
    return (unsigned int)CLAMP(value, 0, (sqlite3_int64)count - 1);
}

static double finite_or(double value, double fallback)
{
    return isfinite(value) ? value : fallback;
}
