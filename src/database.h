#pragma once

#include "sqlite3.h"
#include "viewer_cursor.h"
#include "viewer_mark_group.h"
#include "viewer_mark_manager.h"

typedef struct Database {
    sqlite3 *handle;
} Database;

Database *database_open(const char *path);
void database_init(Database *db, const char *path);
void database_close(Database *db);

// Table creation
void database_create_tables(Database *db);

// Version
int database_get_version(Database *db);
void database_check_update(Database *db);

// Cursor
sqlite3_int64 database_insert_cursor(Database *db, ViewerCursor *cursor);
void database_update_cursor(Database *db, int id, ViewerCursor *cursor);
void database_delete_cursor(Database *db, int id);
ViewerCursor *database_get_cursor(Database *db, int id);

// Group
sqlite3_int64 database_insert_group(Database *db, ViewerMarkGroup *group);
sqlite3_int64 database_insert_cursor_into_group(Database *db, int group_id, int cursor_id, int cursor_index);
void database_update_group(Database *db, int id, ViewerMarkGroup *group);
void database_update_cursor_in_group(Database *db, int group_id, int cursor_index, ViewerCursor *cursor);
ViewerMarkGroup *database_get_group(Database *db, int id);
ViewerCursor **database_get_group_cursors(Database *db, int id);

// Mark manager
void database_insert_mark_manager(Database *db, const char *uri, ViewerMarkManager *manager);
sqlite3_int64 database_insert_group_into_mark_manager(Database *db, const char *uri, int group_id, int group_index);
void database_update_mark_manager(Database *db, const char *uri, ViewerMarkManager *manager);
void database_update_groups_in_mark_manager(Database *db, const char *uri, ViewerMarkGroup **groups);
ViewerMarkManager *database_get_mark_manager(Database *db, const char *uri);
ViewerMarkGroup **database_get_mark_manager_groups(Database *db, const char *uri);