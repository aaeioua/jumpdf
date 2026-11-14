#include <stdlib.h>

#include "viewer_info.h"

ViewerInfo *viewer_info_new(PopplerDocument *doc)
{
    ViewerInfo *info = malloc(sizeof(ViewerInfo));
    if (info == NULL) {
        return NULL;
    }

    if (!viewer_info_init(info, doc)) {
        /* On failure the caller keeps ownership of doc */
        info->doc = NULL;
        viewer_info_destroy(info);
        free(info);
        return NULL;
    }

    return info;
}

ViewerInfo *viewer_info_new_from_gfile(GFile *file)
{
    ViewerInfo *info;
    GError *error = NULL;
    PopplerDocument *doc = poppler_document_new_from_gfile(file, NULL, NULL, &error);

    if (error) {
        g_printerr("Error opening document: %s\n", error->message);
        g_error_free(error);
        return NULL;
    }

    info = viewer_info_new(doc);
    if (info == NULL) {
        g_object_unref(doc);
        return NULL;
    }

    return info;
}

bool viewer_info_init(ViewerInfo *info, PopplerDocument *doc)
{
    info->doc = doc;
    info->n_pages = 0;
    info->pages = NULL;
    g_mutex_init(&info->poppler_lock);

    const int n_pages = poppler_document_get_n_pages(doc);
    if (n_pages <= 0) {
        g_printerr("Document has no pages\n");
        return false;
    }

    info->pages = calloc(n_pages, sizeof(PopplerPage *));
    if (info->pages == NULL) {
        return false;
    }

    for (int i = 0; i < n_pages; i++) {
        PopplerPage *page = poppler_document_get_page(doc, i);
        if (page == NULL) {
            g_printerr("Could not open %i'th page of document\n", i);
            return false;
        }

        info->pages[i] = page;
        info->n_pages = i + 1;
    }

    return true;
}

void viewer_info_destroy(ViewerInfo *info)
{
    if (info->doc) {
        g_object_unref(info->doc);
        info->doc = NULL;
    }

    if (info->pages) {
        for (int i = 0; i < info->n_pages; i++) {
            g_object_unref(info->pages[i]);
            info->pages[i] = NULL;
        }

        free(info->pages);
        info->pages = NULL;
    }

    g_mutex_clear(&info->poppler_lock);
}

void viewer_info_lock(ViewerInfo *info)
{
    g_mutex_lock(&info->poppler_lock);
}

void viewer_info_unlock(ViewerInfo *info)
{
    g_mutex_unlock(&info->poppler_lock);
}

PopplerDest *viewer_info_get_dest(ViewerInfo *info, PopplerDest *dest)
{
    PopplerDest *actual_dest = NULL;

    if (dest->type == POPPLER_DEST_NAMED) {
        viewer_info_lock(info);
        actual_dest = poppler_document_find_dest(info->doc, dest->named_dest);
        viewer_info_unlock(info);
    } else {
        actual_dest = dest;
    }

    return actual_dest;
}

PopplerPage *viewer_info_get_poppler_page(ViewerInfo *info, int page_num)
{
    if (page_num < 0 || page_num >= info->n_pages) {
        return NULL;
    }

    return info->pages[page_num];
}