#pragma once

#include <gio/gio.h>

typedef struct Document Document;

/* Callback for when a Document is reloaded. */
typedef void (*DocumentReloaded)(Document *document, gpointer user_data);

/*
 * Reads `file` whole and checks that it opens. NULL, with `error` set, if it
 * can't be read or doesn't open. With `watch`, Reloads after each finished
 * write to the file.
 */
Document *document_open(GFile *file, gboolean watch,
    DocumentReloaded reloaded, gpointer user_data, GError **error);
/* Stops watching and drops any load. */
void document_free(Document *document);

const char *document_uri(const Document *document);
GBytes *document_bytes(const Document *document);

/* Reloads the document in the background. */
void document_reload(Document *document);
gboolean document_loading(const Document *document);
