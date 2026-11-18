#include <stdlib.h>

#include "viewer.h"
#include "page_links.h"

/* A Link number and the link it stands for */
typedef struct {
    ViewerLinkNumber number;
    const PageLink *link;
} NumberedLink;

static GArray *number_links(Viewer *viewer);
static void execute_action(Viewer *viewer, const PopplerAction *action);
static Viewport *viewport_new_for_document(ViewerInfo *info,
    const ViewportRules *rules);

Viewer *viewer_new(ViewerInfo *info, GroupSelection *selection, Search *search,
    const ViewportRules *rules)
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
    viewer->viewport = viewport;
    viewer->page_links = page_links_new(info);

    return viewer;
}

void viewer_destroy(Viewer *viewer)
{
    if (viewer == NULL) {
        return;
    }

    viewport_destroy(viewer->viewport);

    search_free(viewer->search);

    page_links_free(viewer->page_links);

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

GArray *viewer_link_numbers(Viewer *viewer)
{
    GArray *numbered = number_links(viewer);
    GArray *numbers = g_array_sized_new(FALSE, FALSE,
        sizeof(ViewerLinkNumber), numbered->len);

    for (guint i = 0; i < numbered->len; i++) {
        g_array_append_val(numbers,
            g_array_index(numbered, NumberedLink, i).number);
    }
    g_array_unref(numbered);

    return numbers;
}

void viewer_follow_link(Viewer *viewer, unsigned int number)
{
    GArray *numbered = number_links(viewer);
    PopplerAction *action = NULL;

    if (number >= 1 && number <= numbered->len) {
        action = g_array_index(numbered, NumberedLink, number - 1).link->action;
    }
    g_array_unref(numbered);

    if (action != NULL) {
        execute_action(viewer, action);
    }
}

/*
 * The numbering of the links in the view. Never NULL; its links are
 * valid until page_links_free.
 */
static GArray *number_links(Viewer *viewer)
{
    GArray *numbered = g_array_new(FALSE, FALSE, sizeof(NumberedLink));
    const ViewportSize view = viewport_get_view_size(viewer->viewport);

    if (view.width <= 0 || view.height <= 0) {
        return numbered;
    }

    const ViewportLayout layout = viewport_layout(viewer->viewport,
        viewer_current_mark(viewer));
    const double scale = layout.scale;

    for (int page = layout.from; page <= layout.to; page++) {
        const ViewportRect rect = viewport_layout_page_rect(viewer->viewport,
            &layout, page);
        const double height = viewport_get_page_size(viewer->viewport,
            page).height;
        guint n_links;
        const PageLink *links = page_links_get(viewer->page_links, page,
            &n_links);

        for (guint i = 0; i < n_links; i++) {
            const PopplerRectangle *area = &links[i].area;
            const ViewportRect in_view = {
                .x = rect.x + area->x1 * scale,
                .y = rect.y + (height - area->y2) * scale,
                .width = (area->x2 - area->x1) * scale,
                .height = (area->y2 - area->y1) * scale,
            };

            if (in_view.x < view.width && in_view.x + in_view.width > 0 &&
                in_view.y < view.height && in_view.y + in_view.height > 0) {
                const NumberedLink link = {
                    .number = {.area = in_view, .number = numbered->len + 1},
                    .link = &links[i],
                };
                g_array_append_val(numbered, link);
            }
        }
    }

    return numbered;
}

static void execute_action(Viewer *viewer, const PopplerAction *action)
{
    GError *error = NULL;

    switch (action->type) {
    case POPPLER_ACTION_URI:
        g_app_info_launch_default_for_uri(action->uri.uri, NULL, &error);
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
