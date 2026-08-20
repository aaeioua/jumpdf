#include <stddef.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include "database.h"
#include "utils.h"
#include "config.h"

// TODO: A lot of repetition in these functions

#define DATABASE_VERSION 1

#define CREATE_TABLES_SQL \
    "CREATE TABLE IF NOT EXISTS cursor (" \
    "   id INTEGER PRIMARY KEY AUTOINCREMENT," \
    "   current_page INTEGER NOT NULL," \
    "   x_offset REAL NOT NULL," \
    "   y_offset REAL NOT NULL," \
    "   scale REAL NOT NULL," \
    "   center_mode BOOLEAN NOT NULL," \
    "   dark_mode BOOLEAN NOT NULL," \
    "   input_number INTEGER NOT NULL" \
    ");" \
    "CREATE TABLE IF NOT EXISTS cursor_group (" \
    "   id INTEGER PRIMARY KEY AUTOINCREMENT," \
    "   current_mark INTEGER NOT NULL," \
    "   previous_mark INTEGER NOT NULL" \
    ");" \
    "CREATE TABLE IF NOT EXISTS mark_manager (" \
    "   uri TEXT PRIMARY KEY," \
    "   current_group INTEGER NOT NULL," \
    "   previous_group INTEGER NOT NULL" \
    ");" \
    "CREATE TABLE IF NOT EXISTS group_contains_cursor (" \
    "   group_id INTEGER NOT NULL," \
    "   cursor_id INTEGER NOT NULL," \
    "   cursor_index INTEGER NOT NULL," \
    "   PRIMARY KEY(group_id, cursor_index)," \
    "   FOREIGN KEY(group_id) REFERENCES cursor_group(id)," \
    "   FOREIGN KEY(cursor_id) REFERENCES cursor(id)" \
    ");" \
    "CREATE TABLE IF NOT EXISTS mark_manager_contains_group (" \
    "   mark_manager_uri INTEGER NOT NULL," \
    "   group_id INTEGER NOT NULL," \
    "   group_index INTEGER NOT NULL," \
    "   PRIMARY KEY(mark_manager_uri, group_index)," \
    "   FOREIGN KEY(mark_manager_uri) REFERENCES mark_manager(uri)," \
    "   FOREIGN KEY(group_id) REFERENCES cursor_group(id)" \
    ");" \
    "CREATE INDEX IF NOT EXISTS idx_cursor_id ON cursor(id);" \
    "CREATE INDEX IF NOT EXISTS idx_cursor_group_id ON cursor_group(id);" \
    "CREATE INDEX IF NOT EXISTS idx_mark_manager_uri ON mark_manager(uri);" \
    "CREATE INDEX IF NOT EXISTS idx_group_contains_cursor_group_id_cursor_id ON group_contains_cursor(group_id, cursor_id);" \
    "CREATE INDEX IF NOT EXISTS idx_mark_manager_contains_group_mark_manager_uri ON mark_manager_contains_group(mark_manager_uri);"

static void database_recreate_tables(Database *db);
static void database_printerr_stmt(Database *db, sqlite3_stmt *stmt);
static void database_printerr_sql(Database *db, char *sql);

Database *database_open(const char *path) {
    Database *db = malloc(sizeof(Database));
    if (db == NULL) {
        return NULL;
    }

    database_init(db, path);

    return db;
}

void database_init(Database *db, const char *path) {
    int rc;

    if (db == NULL) {
        return;
    }

    rc = sqlite3_open(path, &(db->handle));
    if (rc != SQLITE_OK) {
        g_printerr("database_init: %s\n", sqlite3_errmsg(db->handle));
        sqlite3_close(db->handle);
        db->handle = NULL;
        return;
    }

    sqlite3_busy_timeout(db->handle, 5000);
}

void database_close(Database *db)
{
    int rc;

    if (db == NULL) {
        return;
    }

    rc = sqlite3_close(db->handle);
    db->handle = NULL;
    if (rc != SQLITE_OK) {
        g_printerr("database_close: %s\n", sqlite3_errmsg(db->handle));
    }
}

void database_create_tables(Database *db)
{
    const char *sql =
        "PRAGMA user_version = " G_STRINGIFY(DATABASE_VERSION) ";"
        CREATE_TABLES_SQL;
    char *errmsg = NULL;
    int rc = sqlite3_exec(db->handle, sql, NULL, NULL, &errmsg);

    if (rc != SQLITE_OK) {
        g_printerr("database_create_tables: %s\n", errmsg);
        sqlite3_free(errmsg);
    }
}

int database_get_version(Database *db)
{
    const char *sql = "PRAGMA user_version;";
    sqlite3_stmt *stmt;
    int rc;
    int version = -1;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_get_version: %s\n", sqlite3_errmsg(db->handle));
        return -1;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    } else {
        database_printerr_stmt(db, stmt);
    }

    sqlite3_finalize(stmt);

    return version;
}

void database_check_update(Database *db)
{
    if (db->handle == NULL) {
        g_printerr("database_check_update: database handle is not open\n");
        return;
    }

    int version = database_get_version(db);

    if (version < 0) {
        g_printerr("database_check_update: failed to read the database version, "
                   "leaving the database untouched\n");
    } else if (version != DATABASE_VERSION) {
        g_print("Database version is %d, expected %d. Recreating tables\n",
            version, DATABASE_VERSION);
        database_recreate_tables(db);
    }
}

sqlite3_int64 database_insert_cursor(Database *db, ViewerCursor *cursor)
{
    const char *sql = "INSERT INTO cursor (current_page, x_offset, y_offset, scale, center_mode, dark_mode, input_number) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt *stmt;
    int rc;
    int cursor_id = -1;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_insert_cursor: %s\n", sqlite3_errmsg(db->handle));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, cursor->current_page);
    sqlite3_bind_double(stmt, 2, cursor->x_offset);
    sqlite3_bind_double(stmt, 3, cursor->y_offset);
    sqlite3_bind_double(stmt, 4, cursor->scale);
    sqlite3_bind_int(stmt, 5, cursor->center_mode);
    sqlite3_bind_int(stmt, 6, cursor->dark_mode);
    sqlite3_bind_int(stmt, 7, cursor->input_number);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    } else {
        cursor_id = sqlite3_last_insert_rowid(db->handle);
    }

    sqlite3_finalize(stmt);

    return cursor_id;
}

void database_delete_cursor(Database *db, int id)
{
    const char *sql_delete_cursor =
        "DELETE FROM cursor "
        "WHERE id = ?;";
    const char *sql_delete_group_contains_cursor =
        "DELETE FROM group_contains_cursor "
        "WHERE cursor_id = ?;";
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_prepare_v2(db->handle, sql_delete_group_contains_cursor, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_delete_cursor (DELETE group_contains_cursor): %s\n", sqlite3_errmsg(db->handle));
        return;
    }

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(db->handle, sql_delete_cursor, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_delete_cursor (DELETE cursor): %s\n", sqlite3_errmsg(db->handle));
        return;
    }

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    }
    sqlite3_finalize(stmt);
}

void database_update_cursor(Database *db, int id, ViewerCursor *cursor)
{
    const char *sql =
        "UPDATE cursor "
        "SET current_page = ?, x_offset = ?, y_offset = ?, scale = ?, center_mode = ?, dark_mode = ?, input_number = ? "
        "WHERE id = ?;";
    sqlite3_stmt *stmt;
    int rc;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_update_cursor: %s\n", sqlite3_errmsg(db->handle));
        return;
    }

    sqlite3_bind_int(stmt, 1, cursor->current_page);
    sqlite3_bind_double(stmt, 2, cursor->x_offset);
    sqlite3_bind_double(stmt, 3, cursor->y_offset);
    sqlite3_bind_double(stmt, 4, cursor->scale);
    sqlite3_bind_int(stmt, 5, cursor->center_mode);
    sqlite3_bind_int(stmt, 6, cursor->dark_mode);
    sqlite3_bind_int(stmt, 7, cursor->input_number);
    sqlite3_bind_int(stmt, 8, id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    }

    sqlite3_finalize(stmt);
}

ViewerCursor *database_get_cursor(Database *db, int id)
{
    const char *sql = "SELECT current_page, x_offset, y_offset, scale, center_mode, dark_mode, input_number FROM cursor WHERE id = ?;";
    sqlite3_stmt *stmt;
    int rc;
    int current_page;
    double x_offset, y_offset, scale;
    int center_mode;
    int dark_mode;
    int input_number;
    ViewerCursor *cursor = NULL;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_get_cursor: %s\n", sqlite3_errmsg(db->handle));
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        current_page = sqlite3_column_int(stmt, 0);
        x_offset = sqlite3_column_double(stmt, 1);
        y_offset = sqlite3_column_double(stmt, 2);
        scale = sqlite3_column_double(stmt, 3);
        center_mode = sqlite3_column_int(stmt, 4);
        dark_mode = sqlite3_column_int(stmt, 5);
        input_number = sqlite3_column_int(stmt, 6);

        cursor = viewer_cursor_new(NULL, current_page, x_offset, y_offset, scale, center_mode, dark_mode, input_number);
    } else {
        database_printerr_stmt(db, stmt);
    }

    sqlite3_finalize(stmt);

    return cursor;
}

sqlite3_int64 database_insert_group(Database *db, ViewerMarkGroup *group)
{
    const char *sql =
        "INSERT INTO cursor_group (current_mark, previous_mark)"
        "VALUES (?, ?);";
    sqlite3_stmt *stmt;
    int rc;
    sqlite3_int64 group_id = -1;
    sqlite3_int64 cursor_id = -1;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_insert_group: %s\n", sqlite3_errmsg(db->handle));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, group->current_mark);
    sqlite3_bind_int(stmt, 2, group->previous_mark);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    } else {
        group_id = sqlite3_last_insert_rowid(db->handle);
    }

    sqlite3_finalize(stmt);

    for (unsigned int i = 0; i < NUM_MARKS; i++) {
        if (group->marks[i] != NULL) {
            cursor_id = database_insert_cursor(db, group->marks[i]);
            database_insert_cursor_into_group(db, group_id, cursor_id, i);
        }
    }

    return group_id;
}

sqlite3_int64 database_insert_cursor_into_group(Database *db, int group_id, int cursor_id, int cursor_index)
{
    const char *sql =
        "INSERT INTO "
        "group_contains_cursor (group_id, cursor_id, cursor_index) "
        "VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    int rc;
    sqlite3_int64 inserted_rowid = -1;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_insert_cursor_into_group: %s\n", sqlite3_errmsg(db->handle));
        return -1;
    }

    sqlite3_bind_int(stmt, 1, group_id);
    sqlite3_bind_int(stmt, 2, cursor_id);
    sqlite3_bind_int(stmt, 3, cursor_index);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    } else {
        inserted_rowid = sqlite3_last_insert_rowid(db->handle);
    }

    sqlite3_finalize(stmt);

    return inserted_rowid;
}

void database_update_group(Database *db, int id, ViewerMarkGroup *group)
{
    const char *sql =
        "UPDATE cursor_group "
        "SET current_mark = ?, previous_mark = ? "
        "WHERE id = ?;";
    sqlite3_stmt *stmt;
    int rc;
    ViewerCursor **cursors_db;
    int new_cursor_id = -1;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_update_group: %s\n", sqlite3_errmsg(db->handle));
        return;
    }

    sqlite3_bind_int(stmt, 1, group->current_mark);
    sqlite3_bind_int(stmt, 2, group->previous_mark);
    sqlite3_bind_int(stmt, 3, id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    }

    sqlite3_finalize(stmt);

    cursors_db = database_get_group_cursors(db, id);
    for (unsigned int i = 0; i < NUM_MARKS; i++) {
        if (cursors_db[i] == NULL && group->marks[i] != NULL) {
            new_cursor_id = database_insert_cursor(db, group->marks[i]);
            database_insert_cursor_into_group(db, id, new_cursor_id, i);
        } else if (cursors_db[i] != NULL) {
            database_update_cursor_in_group(db, id, i, group->marks[i]);
        }
    }

    for (unsigned int i = 0; i < NUM_MARKS; i++) {
        if (cursors_db[i] != NULL) {
            viewer_cursor_destroy(cursors_db[i]);
            free(cursors_db[i]);
        }
    }
    free(cursors_db);
}

void database_update_cursor_in_group(Database *db, int group_id, int cursor_index, ViewerCursor *cursor)
{
    const char *sql =
        "SELECT cursor_id "
        "FROM group_contains_cursor "
        "WHERE group_id = ? AND cursor_index = ?;";
    sqlite3_stmt *stmt;
    int rc;
    int cursor_id;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_update_cursor_in_group: %s\n", sqlite3_errmsg(db->handle));
        return;
    }

    sqlite3_bind_int(stmt, 1, group_id);
    sqlite3_bind_int(stmt, 2, cursor_index);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        cursor_id = sqlite3_column_int(stmt, 0);
        if (cursor == NULL) {
            database_delete_cursor(db, cursor_id);
        } else {
            database_update_cursor(db, cursor_id, cursor);
        }
    } else if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    }

    sqlite3_finalize(stmt);
}

ViewerMarkGroup *database_get_group(Database *db, int id)
{
    const char *sql =
        "SELECT current_mark, previous_mark "
        "FROM cursor_group "
        "WHERE id = ?;";
    sqlite3_stmt *stmt;
    int rc;
    ViewerCursor **cursors;
    int current_mark;
    int previous_mark;
    ViewerMarkGroup *group = NULL;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_get_group: %s\n", sqlite3_errmsg(db->handle));
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        cursors = database_get_group_cursors(db, id);
        current_mark = sqlite3_column_int(stmt, 0);
        previous_mark = sqlite3_column_int(stmt, 1);
        group = viewer_mark_group_new(cursors, current_mark, previous_mark);

        free(cursors);
    } else {
        database_printerr_stmt(db, stmt);
    }

    sqlite3_finalize(stmt);

    return group;
}

ViewerCursor **database_get_group_cursors(Database *db, int id)
{
    const char *sql =
        "SELECT cursor_id, cursor_index "
        "FROM group_contains_cursor "
        "WHERE group_id = ?;";
    sqlite3_stmt *stmt;
    int rc;
    int cursor_id;
    int cursor_index;
    ViewerCursor **cursors = NULL;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_get_group_cursors: %s\n", sqlite3_errmsg(db->handle));
        return NULL;
    }

    sqlite3_bind_int(stmt, 1, id);

    cursors = malloc(NUM_MARKS * sizeof(ViewerCursor *));
    for (unsigned int i = 0; i < NUM_MARKS; i++) {
        cursors[i] = NULL;
    }

    while (TRUE) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            cursor_id = sqlite3_column_int(stmt, 0);
            cursor_index = sqlite3_column_int(stmt, 1);
            cursors[cursor_index] = database_get_cursor(db, cursor_id);
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            database_printerr_stmt(db, stmt);
        }
    }

    sqlite3_finalize(stmt);

    return cursors;
}

void database_insert_mark_manager(Database *db, const char *uri, ViewerMarkManager *manager)
{
    const char *sql =
        "INSERT INTO mark_manager (uri, current_group, previous_group) "
        "VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    int rc;
    sqlite3_int64 group_id = -1;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_insert_mark_manager: %s\n", sqlite3_errmsg(db->handle));
        return;
    }

    sqlite3_bind_text(stmt, 1, uri, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, manager->current_group);
    sqlite3_bind_int(stmt, 3, manager->previous_group);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    }

    sqlite3_finalize(stmt);

    for (unsigned int i = 0; i < NUM_GROUPS; i++) {
        if (manager->groups[i] != NULL) {
            group_id = database_insert_group(db, manager->groups[i]);
            database_insert_group_into_mark_manager(db, uri, group_id, i);
        }
    }
}

sqlite3_int64 database_insert_group_into_mark_manager(Database *db, const char *uri, int group_id, int group_index)
{
    const char *sql =
        "INSERT INTO "
        "mark_manager_contains_group (mark_manager_uri, group_id, group_index) "
        "VALUES (?, ?, ?);";
    sqlite3_stmt *stmt;
    int rc;
    sqlite3_int64 inserted_id = -1;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_insert_group_into_mark_manager: %s\n", sqlite3_errmsg(db->handle));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, uri, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, group_id);
    sqlite3_bind_int(stmt, 3, group_index);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    } else {
        inserted_id = sqlite3_last_insert_rowid(db->handle);
    }

    sqlite3_finalize(stmt);

    return inserted_id;
}

void database_update_mark_manager(Database *db, const char *uri, ViewerMarkManager *manager)
{
    const char *sql =
        "UPDATE mark_manager "
        "SET current_group = ?, previous_group = ? "
        "WHERE uri = ?;";
    sqlite3_stmt *stmt;
    int rc;
    ViewerMarkGroup **groups_db;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_update_mark_manager: %s\n", sqlite3_errmsg(db->handle));
        return;
    }

    sqlite3_bind_int(stmt, 1, manager->current_group);
    sqlite3_bind_int(stmt, 2, manager->previous_group);
    sqlite3_bind_text(stmt, 3, uri, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        database_printerr_stmt(db, stmt);
    }

    sqlite3_finalize(stmt);

    database_update_groups_in_mark_manager(db, uri, manager->groups);
    groups_db = database_get_mark_manager_groups(db, uri);
    for (unsigned int i = 0; i < NUM_GROUPS; i++) {
        if (groups_db[i] == NULL && manager->groups[i] != NULL) {
            database_insert_group(db, manager->groups[i]);
        }
    }

    for (unsigned int i = 0; i < NUM_GROUPS; i++) {
        if (groups_db[i] != NULL) {
            viewer_mark_group_destroy(groups_db[i]);
            free(groups_db[i]);
        }
    }
    free(groups_db);
}

void database_update_groups_in_mark_manager(Database *db, const char *uri, ViewerMarkGroup **groups)
{
    const char *sql =
        "SELECT group_id, group_index "
        "FROM mark_manager_contains_group "
        "WHERE mark_manager_uri = ?;";
    sqlite3_stmt *stmt;
    int rc;
    int group_id;
    int group_index;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_update_groups_in_mark_manager: %s\n", sqlite3_errmsg(db->handle));
        return;
    }

    sqlite3_bind_text(stmt, 1, uri, -1, SQLITE_STATIC);

    while (TRUE) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            group_id = sqlite3_column_int(stmt, 0);
            group_index = sqlite3_column_int(stmt, 1);
            database_update_group(db, group_id, groups[group_index]);
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            database_printerr_stmt(db, stmt);
        }
    }

    sqlite3_finalize(stmt);
}

ViewerMarkManager *database_get_mark_manager(Database *db, const char *uri)
{
    const char *sql =
        "SELECT current_group, previous_group "
        "FROM mark_manager "
        "WHERE uri = ?;";
    sqlite3_stmt *stmt;
    int rc;
    int current_group;
    int previous_group;
    ViewerMarkGroup **groups = NULL;
    ViewerMarkManager *manager = NULL;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_get_mark_manager: %s\n", sqlite3_errmsg(db->handle));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, uri, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        current_group = sqlite3_column_int(stmt, 0);
        previous_group = sqlite3_column_int(stmt, 1);
        groups = database_get_mark_manager_groups(db, uri);
        manager = viewer_mark_manager_new(groups, current_group, previous_group);

        free(groups);
    } else if (rc == SQLITE_DONE) {
        // No mark manager found
    } else {
        database_printerr_stmt(db, stmt);
    }

    sqlite3_finalize(stmt);

    return manager;
}

ViewerMarkGroup **database_get_mark_manager_groups(Database *db, const char *uri)
{
    const char *sql =
        "SELECT group_id, group_index "
        "FROM mark_manager_contains_group "
        "WHERE mark_manager_uri = ?;";
    sqlite3_stmt *stmt;
    int rc;
    int group_id;
    int group_index;
    ViewerMarkGroup **groups = NULL;

    rc = sqlite3_prepare_v2(db->handle, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        g_printerr("database_get_mark_manager_groups: %s\n", sqlite3_errmsg(db->handle));
        return NULL;
    }

    sqlite3_bind_text(stmt, 1, uri, -1, SQLITE_STATIC);

    groups = malloc(NUM_GROUPS * sizeof(ViewerMarkGroup *));
    for (unsigned int i = 0; i < NUM_GROUPS; i++) {
        groups[i] = NULL;
    }

    while (TRUE) {
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            group_id = sqlite3_column_int(stmt, 0);
            group_index = sqlite3_column_int(stmt, 1);
            groups[group_index] = database_get_group(db, group_id);
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            database_printerr_stmt(db, stmt);
        }
    }

    sqlite3_finalize(stmt);

    return groups;
}

static void database_recreate_tables(Database *db)
{
    char *errmsg = NULL;
    int rc;

    const char *sql =
        "BEGIN TRANSACTION;"
        "PRAGMA user_version = " G_STRINGIFY(DATABASE_VERSION) ";"
        "DROP TABLE IF EXISTS group_contains_cursor;"
        "DROP TABLE IF EXISTS mark_manager_contains_group;"
        "DROP TABLE IF EXISTS cursor;"
        "DROP TABLE IF EXISTS cursor_group;"
        "DROP TABLE IF EXISTS mark_manager;"
        CREATE_TABLES_SQL
        "COMMIT;";

    rc = sqlite3_exec(db->handle, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        g_printerr("database_recreate_tables: %s\n", errmsg);
        sqlite3_free(errmsg);
    }
}

static void database_printerr_stmt(Database *db, sqlite3_stmt *stmt)
{
    char *query = sqlite3_expanded_sql(stmt);
    if (query == NULL) {
        g_printerr("database_printerr: sqlite3_expanded_sql failed\n");
    } else {
        database_printerr_sql(db, query);
    }

}
static void database_printerr_sql(Database *db, char *sql)
{
    g_print("\"%s\": %s\n", sql, sqlite3_errmsg(db->handle));
}