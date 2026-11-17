#include <stdlib.h>

#include "viewer.h"

static Viewport *viewport_new_for_document(ViewerInfo *info,
    const ViewportRules *rules);

Viewer *viewer_new(ViewerInfo *info, GroupSelection *selection, Search *search,
    ViewerLinks *links, const ViewportRules *rules)
{
    Viewport *viewport = viewport_new_for_document(info, rules);
    if (viewport == NULL) {
        return NULL;
    }

    Viewer *viewer = malloc(sizeof(Viewer));
    if (viewer == NULL) {
        viewport_destroy(viewport);
        return NULL;
    }

    viewer->info = info;
    viewer->selection = selection;
    viewer->search = search;
    viewer->links = links;
    viewer->viewport = viewport;

    return viewer;
}

void viewer_destroy(Viewer *viewer)
{
    if (viewer == NULL) {
        return;
    }

    viewport_destroy(viewer->viewport);

    search_free(viewer->search);

    viewer_links_destroy(viewer->links);
    free(viewer->links);

    viewer_info_destroy(viewer->info);
    free(viewer->info);

    free(viewer);
}

ViewerCursor *viewer_current_mark(const Viewer *viewer)
{
    return group_selection_current_mark(viewer->selection);
}

gboolean viewer_goto_found_page(Viewer *viewer, int from, int to)
{
    ViewerCursor *mark = viewer_current_mark(viewer);

    if (mark->current_page != from) {
        return FALSE;
    }

    viewport_goto_dest(viewer->viewport, mark, to, false, 0);
    return TRUE;
}

void viewer_goto_target(Viewer *viewer, ViewerTarget target)
{
    if (target.page < 0) {
        return;
    }

    viewport_goto_dest(viewer->viewport, viewer_current_mark(viewer),
        target.page, target.has_top, target.top);
}

void viewer_execute_action(Viewer *viewer, PopplerAction *action)
{
    PopplerActionUri *action_uri;
    GError *error = NULL;

    switch (action->type) {
    case POPPLER_ACTION_URI:
        action_uri = (PopplerActionUri *)action;
        g_app_info_launch_default_for_uri(action_uri->uri, NULL, &error);
        if (error != NULL) {
            g_printerr("Poppler: Error launching URI: %s\n", error->message);
            g_error_free(error);
        }
        break;
    case POPPLER_ACTION_GOTO_DEST:
        if (action->goto_dest.dest != NULL) {
            viewer_goto_target(viewer, viewer_info_resolve_dest(viewer->info,
                action->goto_dest.dest));
        }
        break;
    default:
        g_printerr("Poppler: Unsupported link type\n");
        break;
    }
}

static Viewport *viewport_new_for_document(ViewerInfo *info,
    const ViewportRules *rules)
{
    ViewportSize *sizes = g_new(ViewportSize, info->n_pages);
    for (int i = 0; i < info->n_pages; i++) {
        poppler_page_get_size(viewer_info_get_poppler_page(info, i),
            &sizes[i].width, &sizes[i].height);
    }

    Viewport *viewport = viewport_new(sizes, info->n_pages, rules);
    g_free(sizes);

    return viewport;
}
