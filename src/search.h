#pragma once

#include <glib.h>

#include "viewer_info.h"

/*
 * A window's Search: its query and the pages that contain it.
 *
 * One worker thread scans pages for the current query, each under the
 * document lock (viewer_info_lock) on a short-lived PopplerPage, and keeps
 * only the match rectangles. Call every function from the main thread.
 * Callbacks run on the main context, never from inside a Search function,
 * and never after search_free returns. They must not call search_free.
 */
typedef struct Search Search;

/* y1 is the bottom edge and y2 the top. */
typedef struct {
    double x1;
    double y1;
    double x2;
    double y2;
} SearchRect;

typedef struct {
    /*
     * A page that search_page_matches reported as unknown has been scanned,
     * or search_seeking changed. Coalesced: at most one call per main-loop
     * iteration.
     */
    void (*changed)(gpointer user_data);
    /*
     * A seek that started on page `from` (after clamping) reached page `to`,
     * which is never `from`. The seek is over.
     */
    void (*found)(int from, int to, gpointer user_data);
    /*
     * search_tally may have changed: pages have been scanned, or the query
     * changed. Coalesced as `changed`.
     */
    void (*tallied)(gpointer user_data);
} SearchCallbacks;

/* `callbacks` is copied. It may be NULL, and so may any member. */
Search *search_new(ViewerInfo *info, const SearchCallbacks *callbacks,
    gpointer user_data);
/* Waits for the page being scanned, if any. Call before `info` is destroyed. */
void search_free(Search *search);

/*
 * Copies `query`; NULL or "" clears it. Forgets every match, ends any seek
 * without `found`, and scans outward from `from_page`.
 *
 * Matching ignores case unless `query` has an uppercase letter, and a line
 * break in the text matches a space in `query`. A match can span two lines,
 * dropping a hyphen at the end of the first.
 *
 * A non-empty query also starts a seek from `from_page` that counts
 * `from_page` itself: it finds the first matching page at or after
 * `from_page`, so it ends without `found` when `from_page` matches.
 */
void search_set_query(Search *search, const char *query, int from_page);
/* NULL when there is none. Valid until the next search_set_query. */
const char *search_query(const Search *search);

/*
 * Seeks `pages` matching pages on from `from_page`: forward when positive,
 * back when negative, not counting `from_page`. With fewer matching pages in
 * that direction it finds the last of them; with none it ends without
 * `found`.
 *
 * While a seek from the same `from_page` is pending, adds `pages` to it, and
 * a total of 0 ends it. Otherwise replaces any pending seek. Does nothing
 * without a query or when `pages` is 0.
 */
void search_seek(Search *search, int from_page, int pages);
/*
 * TRUE from search_seek (or search_set_query) until the main loop delivers
 * the seek's end, so calls between those always add to the same seek.
 */
gboolean search_seeking(const Search *search);

typedef struct {
    /*
     * Matches on pages before the given page, plus 1 when that page matches:
     * the first match on it, or the last one before it. -1 until all of
     * those pages have been scanned.
     */
    int position;
    /* Matches on the pages scanned so far */
    guint total;
    /* Every page has been scanned, so `total` is final */
    gboolean complete;
} SearchTally;

/*
 * Tallies the current query's matches, as seen from `page`. A match across
 * a line break is one match. Without a query, every page counts as scanned
 * with no matches.
 */
SearchTally search_tally(const Search *search, int page);

/*
 * When `page` has been scanned for the current query, sets *rects (valid
 * until the next search_set_query) and *n_rects and returns TRUE. Without a
 * query, every page counts as scanned with no matches. Otherwise returns
 * FALSE and scans `page` next. Never waits for Poppler.
 */
gboolean search_page_matches(Search *search, int page,
    const SearchRect **rects, guint *n_rects);
