#include <errno.h>
#include <stdlib.h>

#include "viewer_info.h"

static ViewerInfo *viewer_info_open(PopplerDocument *doc, GError **error);
static ViewerTarget viewer_info_resolve_explicit(const ViewerInfo *info,
    const PopplerDest *dest);

ViewerInfo *viewer_info_new(PopplerDocument *doc)
{
    GError *error = NULL;
    ViewerInfo *info = viewer_info_open(doc, &error);

    if (info == NULL) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
    }

    return info;
}

ViewerInfo *viewer_info_new_from_bytes(GBytes *bytes, GError **error)
{
    PopplerDocument *doc = poppler_document_new_from_bytes(bytes, NULL, error);
    ViewerInfo *info;

    if (doc == NULL) {
        return NULL;
    }

    info = viewer_info_open(doc, error);
    if (info == NULL) {
        g_object_unref(doc);
    }

    return info;
}

bool viewer_info_init(ViewerInfo *info, PopplerDocument *doc, GError **error)
{
    info->doc = doc;
    info->n_pages = 0;
    info->pages = NULL;
    g_mutex_init(&info->poppler_lock);

    const int n_pages = poppler_document_get_n_pages(doc);
    if (n_pages <= 0) {
        g_set_error_literal(error, POPPLER_ERROR, POPPLER_ERROR_INVALID,
            "Document has no pages");
        return false;
    }

    info->pages = calloc(n_pages, sizeof(PopplerPage *));
    if (info->pages == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
            g_strerror(ENOMEM));
        return false;
    }

    for (int i = 0; i < n_pages; i++) {
        PopplerPage *page = poppler_document_get_page(doc, i);
        if (page == NULL) {
            g_set_error(error, POPPLER_ERROR, POPPLER_ERROR_DAMAGED,
                "Could not open %i'th page of document", i);
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

ViewerTarget viewer_info_resolve_dest(ViewerInfo *info, const PopplerDest *dest)
{
    const ViewerTarget nowhere = {.page = -1, .has_top = false, .top = 0};
    PopplerDest *found;
    ViewerTarget target;

    if (dest->type != POPPLER_DEST_NAMED) {
        return viewer_info_resolve_explicit(info, dest);
    }

    if (dest->named_dest == NULL) {
        return nowhere;
    }

    viewer_info_lock(info);
    found = poppler_document_find_dest(info->doc, dest->named_dest);
    viewer_info_unlock(info);

    if (found == NULL || found->type == POPPLER_DEST_NAMED) {
        if (found != NULL) {
            poppler_dest_free(found);
        }
        return nowhere;
    }

    target = viewer_info_resolve_explicit(info, found);
    poppler_dest_free(found);

    return target;
}

PopplerPage *viewer_info_get_poppler_page(ViewerInfo *info, int page_num)
{
    if (page_num < 0 || page_num >= info->n_pages) {
        return NULL;
    }

    return info->pages[page_num];
}

static ViewerTarget viewer_info_resolve_explicit(const ViewerInfo *info,
    const PopplerDest *dest)
{
    const ViewerTarget nowhere = {.page = -1, .has_top = false, .top = 0};
    /* Poppler numbers pages from 1, and gives 0 when it can't find the page */
    const int page = dest->page_num - 1;

    if (dest->type == POPPLER_DEST_UNKNOWN || page < 0
        || page >= info->n_pages) {
        return nowhere;
    }

    return (ViewerTarget){
        .page = page,
        .has_top = dest->change_top != 0,
        .top = dest->top,
    };
}

static ViewerInfo *viewer_info_open(PopplerDocument *doc, GError **error)
{
    ViewerInfo *info = malloc(sizeof(ViewerInfo));
    if (info == NULL) {
        g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_NOMEM,
            g_strerror(ENOMEM));
        return NULL;
    }

    if (!viewer_info_init(info, doc, error)) {
        info->doc = NULL;
        viewer_info_destroy(info);
        free(info);
        return NULL;
    }

    return info;
}
