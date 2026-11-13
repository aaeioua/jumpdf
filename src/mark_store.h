#pragma once

#include <glib.h>

#include "viewer_mark_manager.h"

/* Keeps each document's Mark Manager between sessions, keyed by URI. */
typedef struct MarkStore MarkStore;

typedef enum {
    MARK_STORE_FOUND,
    MARK_STORE_NOT_FOUND,
    MARK_STORE_FAILED,
} MarkStoreLoadResult;

/*
 * Opens the store at `path`, creating the file and its directory if needed,
 * and migrates older schemas (backing the file up first). Returns NULL and
 * sets `error` if the file can't be opened or migrated, or was written by a
 * newer jumpdf; in that case the file is left untouched. Every function below
 * treats a NULL store as "no persistence". ":memory:" works for tests.
 */
MarkStore *mark_store_open(const char *path, GError **error);
void mark_store_close(MarkStore *store);

/*
 * On FOUND, `*manager` is a new Mark Manager owned by the caller, with every
 * index in range and the current Mark set. A NULL store gives NOT_FOUND.
 * After FAILED, mark_store_save refuses `uri` for the rest of the session so
 * that default Marks never overwrite stored ones.
 */
MarkStoreLoadResult mark_store_load(MarkStore *store, const char *uri,
    ViewerMarkManager **manager);

/*
 * Replaces everything stored for `uri` in one transaction. Returns FALSE and
 * writes nothing on error, for a NULL store, or for a URI whose load FAILED.
 */
gboolean mark_store_save(MarkStore *store, const char *uri,
    const ViewerMarkManager *manager);
