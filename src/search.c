#include "search.h"

#include <string.h>

/* The main thread's record of one page for the current query */
typedef struct {
    gboolean scanned;
    /* search_page_matches returned FALSE for it, so `changed` is owed */
    gboolean reported_unknown;
    SearchRect *rects;
    guint n_rects;
    /* Fewer than n_rects when a match spans a line break */
    guint n_matches;
} SearchPage;

/* A finished scan, handed from the worker to the main thread */
typedef struct {
    guint64 generation;
    int page;
    SearchRect *rects;
    guint n_rects;
    guint n_matches;
} SearchResult;

typedef struct {
    gboolean active;
    int from;
    /* Matching pages to move: forward when positive, back when negative */
    int total;
    /* Started by search_set_query: moving forward counts `from` itself */
    gboolean counts_from;
} SearchSeek;

struct Search {
    ViewerInfo *info;
    int n_pages;
    SearchCallbacks callbacks;
    gpointer user_data;
    GSource *source;
    GThread *thread;

    /* Main thread only */
    char *query;
    guint64 generation;
    SearchPage *pages;
    int n_scanned;
    guint n_matches;
    SearchSeek seek;
    /* search_seeking as `changed` last reported it */
    gboolean notified_seeking;
    /* search_tally may have changed since the last `tallied` */
    gboolean tally_changed;

    /* Shared with the worker, under `mutex` */
    GMutex mutex;
    GCond cond;
    gboolean quit;
    char *work_query;
    guint64 work_generation;
    /* Per page: scanned, or being scanned, for work_generation */
    gboolean *claimed;
    /* Pages search_page_matches reported as unknown; the latest is last */
    GArray *requested;
    /* Next page to try in the pending seek's direction, or -1 */
    int seek_next;
    int seek_direction;
    /* Where scanning outward from the query's from_page has got to */
    int origin;
    int outward_step;
    GPtrArray *results;
};

static void search_start_query(Search *search, const char *query,
    int from_page, gboolean seek);
static gpointer search_worker(gpointer data);
static int search_next_page_locked(Search *search);
static PopplerFindFlags search_find_flags(const char *query);
static SearchResult *search_scan(ViewerInfo *info, const char *query,
    guint64 generation, int page);
static gboolean search_deliver(gpointer data);
static gboolean search_seek_resolve(const Search *search, int *to);
static int search_seek_start(const SearchSeek *seek, int *direction);
static void search_update_seek_hint(Search *search);
static void search_wake_main(Search *search);
static void search_clear_pages(Search *search);
static void search_result_free(gpointer data);
static int search_clamp_page(const Search *search, int page);

static gboolean search_source_dispatch(GSource *source, GSourceFunc callback,
    gpointer user_data)
{
    g_source_set_ready_time(source, -1);
    return callback(user_data);
}

static GSourceFuncs search_source_funcs = {
    .dispatch = search_source_dispatch,
};

Search *search_new(ViewerInfo *info, const SearchCallbacks *callbacks,
    gpointer user_data)
{
    Search *search = g_new0(Search, 1);

    search->info = info;
    search->n_pages = info->n_pages;
    if (callbacks != NULL) {
        search->callbacks = *callbacks;
    }
    search->user_data = user_data;
    search->pages = g_new0(SearchPage, search->n_pages);

    g_mutex_init(&search->mutex);
    g_cond_init(&search->cond);
    search->claimed = g_new0(gboolean, search->n_pages);
    search->requested = g_array_new(FALSE, FALSE, sizeof(int));
    search->seek_next = -1;
    search->results = g_ptr_array_new_with_free_func(search_result_free);

    GMainContext *context = g_main_context_ref_thread_default();
    search->source = g_source_new(&search_source_funcs, sizeof(GSource));
    g_source_set_callback(search->source, search_deliver, search, NULL);
    g_source_set_ready_time(search->source, -1);
    g_source_attach(search->source, context);
    g_main_context_unref(context);

    search->thread = g_thread_new("search", search_worker, search);

    return search;
}

void search_free(Search *search)
{
    if (search == NULL) {
        return;
    }

    g_mutex_lock(&search->mutex);
    search->quit = TRUE;
    g_cond_signal(&search->cond);
    g_mutex_unlock(&search->mutex);
    g_thread_join(search->thread);

    g_source_destroy(search->source);
    g_source_unref(search->source);

    search_clear_pages(search);
    g_free(search->pages);
    g_free(search->query);

    g_ptr_array_free(search->results, TRUE);
    g_array_free(search->requested, TRUE);
    g_free(search->claimed);
    g_free(search->work_query);
    g_cond_clear(&search->cond);
    g_mutex_clear(&search->mutex);
    g_free(search);
}

void search_set_query(Search *search, const char *query, int from_page)
{
    search_start_query(search, query, from_page, TRUE);
}

void search_restore_query(Search *search, const char *query, int from_page)
{
    search_start_query(search, query, from_page, FALSE);
}

static void search_start_query(Search *search, const char *query,
    int from_page, gboolean seek)
{
    from_page = search_clamp_page(search, from_page);

    search_clear_pages(search);
    search->n_scanned = 0;
    search->n_matches = 0;
    search->tally_changed = TRUE;
    g_free(search->query);
    search->query = query != NULL && query[0] != '\0' ? g_strdup(query) : NULL;
    search->generation++;
    search->seek = (SearchSeek){
        .active = seek && search->query != NULL,
        .from = from_page,
        .total = 1,
        .counts_from = TRUE,
    };

    g_mutex_lock(&search->mutex);
    g_free(search->work_query);
    search->work_query = g_strdup(search->query);
    search->work_generation = search->generation;
    memset(search->claimed, 0, sizeof(gboolean) * search->n_pages);
    g_array_set_size(search->requested, 0);
    g_ptr_array_set_size(search->results, 0);
    search->origin = from_page;
    search->outward_step = 0;
    search_update_seek_hint(search);
    g_cond_signal(&search->cond);
    g_mutex_unlock(&search->mutex);

    search_wake_main(search);
}

const char *search_query(const Search *search)
{
    return search->query;
}

void search_seek(Search *search, int from_page, int pages)
{
    if (search->query == NULL || pages == 0) {
        return;
    }

    from_page = search_clamp_page(search, from_page);
    if (search->seek.active && search->seek.from == from_page) {
        search->seek.total += pages;
    } else {
        search->seek = (SearchSeek){
            .active = TRUE,
            .from = from_page,
            .total = pages,
            .counts_from = FALSE,
        };
    }

    g_mutex_lock(&search->mutex);
    search_update_seek_hint(search);
    g_cond_signal(&search->cond);
    g_mutex_unlock(&search->mutex);

    search_wake_main(search);
}

gboolean search_seeking(const Search *search)
{
    return search->seek.active;
}

SearchTally search_tally(const Search *search, int page)
{
    SearchTally tally = {
        .position = 0,
        .total = search->n_matches,
        .complete = search->query == NULL ||
            search->n_scanned == search->n_pages,
    };

    if (search->query == NULL) {
        return tally;
    }

    for (int p = 0; p <= page && p < search->n_pages; p++) {
        const SearchPage *entry = &search->pages[p];

        if (!entry->scanned) {
            tally.position = -1;
            break;
        }
        if (p < page) {
            tally.position += entry->n_matches;
        } else if (entry->n_matches > 0) {
            tally.position++;
        }
    }

    return tally;
}

gboolean search_page_matches(Search *search, int page,
    const SearchRect **rects, guint *n_rects)
{
    *rects = NULL;
    *n_rects = 0;

    if (search->query == NULL || page < 0 || page >= search->n_pages) {
        return TRUE;
    }

    SearchPage *entry = &search->pages[page];
    if (entry->scanned) {
        *rects = entry->rects;
        *n_rects = entry->n_rects;
        return TRUE;
    }

    entry->reported_unknown = TRUE;

    g_mutex_lock(&search->mutex);
    for (guint i = 0; i < search->requested->len; i++) {
        if (g_array_index(search->requested, int, i) == page) {
            g_array_remove_index(search->requested, i);
            break;
        }
    }
    g_array_append_val(search->requested, page);
    g_cond_signal(&search->cond);
    g_mutex_unlock(&search->mutex);

    return FALSE;
}

static gpointer search_worker(gpointer data)
{
    Search *search = data;

    g_mutex_lock(&search->mutex);
    while (!search->quit) {
        int page = search_next_page_locked(search);
        if (page < 0) {
            g_cond_wait(&search->cond, &search->mutex);
            continue;
        }

        search->claimed[page] = TRUE;
        guint64 generation = search->work_generation;
        char *query = g_strdup(search->work_query);
        g_mutex_unlock(&search->mutex);

        SearchResult *result = search_scan(search->info, query, generation,
            page);
        g_free(query);

        g_mutex_lock(&search->mutex);
        if (generation == search->work_generation) {
            g_ptr_array_add(search->results, result);
            search_wake_main(search);
        } else {
            search_result_free(result);
        }
    }
    g_mutex_unlock(&search->mutex);

    return NULL;
}

/*
 * Requested pages first, latest first; then the pending seek's direction;
 * then outward from the query's from_page. -1 when every page is claimed.
 */
static int search_next_page_locked(Search *search)
{
    if (search->work_query == NULL) {
        return -1;
    }

    while (search->requested->len > 0) {
        guint last = search->requested->len - 1;
        int page = g_array_index(search->requested, int, last);
        g_array_remove_index(search->requested, last);
        if (!search->claimed[page]) {
            return page;
        }
    }

    while (search->seek_next >= 0 && search->seek_next < search->n_pages) {
        int page = search->seek_next;
        search->seek_next += search->seek_direction;
        if (!search->claimed[page]) {
            return page;
        }
    }

    while (search->outward_step <= 2 * search->n_pages) {
        /* origin, origin + 1, origin - 1, origin + 2, origin - 2... */
        int step = search->outward_step++;
        int distance = (step + 1) / 2;
        int page = search->origin + (step % 2 == 1 ? distance : -distance);
        if (page >= 0 && page < search->n_pages && !search->claimed[page]) {
            return page;
        }
    }

    return -1;
}

/*
 * Across line breaks, and smart-case: ignores case unless `query` has an
 * uppercase letter
 */
static PopplerFindFlags search_find_flags(const char *query)
{
    for (const char *p = query; p != NULL && *p != '\0';
         p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);

        if (g_unichar_isupper(c) || g_unichar_istitle(c)) {
            return POPPLER_FIND_MULTILINE | POPPLER_FIND_CASE_SENSITIVE;
        }
    }

    return POPPLER_FIND_MULTILINE;
}

/* Scans on a short-lived page so Poppler's text cache is freed with it */
static SearchResult *search_scan(ViewerInfo *info, const char *query,
    guint64 generation, int page)
{
    SearchResult *result = g_new0(SearchResult, 1);
    const PopplerFindFlags flags = search_find_flags(query);
    GList *matches = NULL;

    result->generation = generation;
    result->page = page;

    viewer_info_lock(info);
    PopplerPage *poppler_page = poppler_document_get_page(info->doc, page);
    if (poppler_page != NULL) {
        matches = poppler_page_find_text_with_options(poppler_page, query,
            flags);
        g_object_unref(poppler_page);
    }
    viewer_info_unlock(info);

    result->n_rects = g_list_length(matches);
    if (result->n_rects > 0) {
        result->rects = g_new(SearchRect, result->n_rects);
    }
    guint i = 0;
    for (GList *l = matches; l != NULL; l = l->next, i++) {
        const PopplerRectangle *match = l->data;
        if (!poppler_rectangle_find_get_match_continued(match)) {
            result->n_matches++;
        }
        result->rects[i] = (SearchRect){
            .x1 = match->x1,
            .y1 = match->y1,
            .x2 = match->x2,
            .y2 = match->y2,
        };
    }
    g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);

    return result;
}

/*
 * Takes the worker's results into the cache, moves the seek forward, then
 * runs the callbacks
 */
static gboolean search_deliver(gpointer data)
{
    Search *search = data;
    gboolean changed = FALSE;
    int found_from = -1;
    int found_to = -1;

    g_mutex_lock(&search->mutex);
    GPtrArray *results = search->results;
    search->results = g_ptr_array_new_with_free_func(search_result_free);
    g_mutex_unlock(&search->mutex);

    for (guint i = 0; i < results->len; i++) {
        SearchResult *result = g_ptr_array_index(results, i);
        SearchPage *entry = &search->pages[result->page];

        if (result->generation != search->generation || entry->scanned) {
            continue;
        }

        entry->scanned = TRUE;
        entry->rects = g_steal_pointer(&result->rects);
        entry->n_rects = result->n_rects;
        entry->n_matches = result->n_matches;
        search->n_scanned++;
        search->n_matches += entry->n_matches;
        search->tally_changed = TRUE;
        changed |= entry->reported_unknown;
    }
    g_ptr_array_free(results, TRUE);

    int to;
    if (search->seek.active && search_seek_resolve(search, &to)) {
        if (to >= 0) {
            found_from = search->seek.from;
            found_to = to;
        }
        search->seek.active = FALSE;
        changed = TRUE;

        g_mutex_lock(&search->mutex);
        search_update_seek_hint(search);
        g_mutex_unlock(&search->mutex);
    }

    changed |= search->seek.active != search->notified_seeking;
    search->notified_seeking = search->seek.active;

    if (found_to >= 0 && search->callbacks.found != NULL) {
        search->callbacks.found(found_from, found_to, search->user_data);
    }
    if (changed && search->callbacks.changed != NULL) {
        search->callbacks.changed(search->user_data);
    }
    if (search->tally_changed) {
        search->tally_changed = FALSE;
        if (search->callbacks.tallied != NULL) {
            search->callbacks.tallied(search->user_data);
        }
    }

    return G_SOURCE_CONTINUE;
}

/*
 * Whether the pending seek's end is known from the cache. Sets *to to the
 * page it reached, or -1 when it ends where it started.
 */
static gboolean search_seek_resolve(const Search *search, int *to)
{
    const SearchSeek *seek = &search->seek;
    int remaining = ABS(seek->total);
    int last = -1;
    int direction;

    *to = -1;
    if (remaining == 0) {
        return TRUE;
    }

    for (int page = search_seek_start(seek, &direction);
         page >= 0 && page < search->n_pages; page += direction) {
        const SearchPage *entry = &search->pages[page];
        if (!entry->scanned) {
            return FALSE;
        }
        if (entry->n_rects > 0) {
            last = page;
            if (--remaining == 0) {
                break;
            }
        }
    }

    *to = last != seek->from ? last : -1;
    return TRUE;
}

/*
 * The first page a seek with a non-zero total looks at. Sets *direction to 1
 * or -1.
 */
static int search_seek_start(const SearchSeek *seek, int *direction)
{
    *direction = seek->total > 0 ? 1 : -1;

    return seek->counts_from && *direction > 0
        ? seek->from : seek->from + *direction;
}

/* Points the worker at the pending seek's pages. Call with `mutex` held. */
static void search_update_seek_hint(Search *search)
{
    const SearchSeek *seek = &search->seek;

    if (!seek->active || seek->total == 0) {
        search->seek_next = -1;
        search->seek_direction = 0;
        return;
    }

    search->seek_next = search_seek_start(seek, &search->seek_direction);
}

static void search_wake_main(Search *search)
{
    g_source_set_ready_time(search->source, 0);
}

static void search_clear_pages(Search *search)
{
    for (int i = 0; i < search->n_pages; i++) {
        g_free(search->pages[i].rects);
    }
    memset(search->pages, 0, sizeof(SearchPage) * search->n_pages);
}

static void search_result_free(gpointer data)
{
    SearchResult *result = data;

    g_free(result->rects);
    g_free(result);
}

static int search_clamp_page(const Search *search, int page)
{
    return CLAMP(page, 0, search->n_pages - 1);
}
