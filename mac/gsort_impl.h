// gsort_impl.h - STLport (VS2005 build of DOW2.exe / SimEngine.dll) std::sort for any element size and comparator:
// the same algorithm as sortfix_impl.h (transcribed from DOW2.exe 0x98f0a0 / 0x98f0e0 / 0x98f200 / 0x98f290 / 0x98f1b0 ...),
// written over raw bytes. Elements are moved with memcpy (STLport copies values the same way for the POD-like element types
// of the simulation; ordering depends only on the comparator and the index sequence, not on the element size).
// less(a, b, ctx) = the site's own comparator ("a before b"). Introsort: median of 3, unguarded partition, depth 2*floor(log2 n),
// heap sort fallback (partial_sort), final insertion sort with threshold 16. Not stable - like Windows.
// Also gs_stable(): a plain stable merge/insertion sort (any stable sort gives the same order for a strict weak ordering).
#ifndef GSORT_IMPL_H
#define GSORT_IMPL_H
#include <string.h>
#include <stdlib.h>
typedef int (*gs_less_fn)(const void *a, const void *b, void *ctx);
typedef struct { size_t sz; gs_less_fn lt; void *ctx; } gs_t;
#define GS_AT(g, p, i) ((char *)(p) + (long)(i) * (long)(g)->sz)
#define GS_LT(g, a, b) ((g)->lt((a), (b), (g)->ctx))
#define GS_CP(g, d, s) memcpy((d), (s), (g)->sz)
#define GS_MAXEL 512
static void gs_unguarded_linear_insert(const gs_t *g, char *last, const char *val) {
    char *next = last - g->sz;
    while (GS_LT(g, val, next)) { GS_CP(g, last, next); last = next; next -= g->sz; }
    GS_CP(g, last, val);
}
static void gs_insertion_sort(const gs_t *g, char *first, char *last) {
    char val[GS_MAXEL];
    if (first == last) return;
    for (char *i = first + g->sz; i != last; i += g->sz) {
        GS_CP(g, val, i);
        if (GS_LT(g, val, first)) { memmove(first + g->sz, first, (size_t)(i - first)); GS_CP(g, first, val); }
        else gs_unguarded_linear_insert(g, i, val);
    }
}
static void gs_final_insertion_sort(const gs_t *g, char *first, char *last) {
    char val[GS_MAXEL];
    if ((last - first) / (long)g->sz > 16) {
        char *m = GS_AT(g, first, 16);
        gs_insertion_sort(g, first, m);
        for (char *i = m; i != last; i += g->sz) { GS_CP(g, val, i); gs_unguarded_linear_insert(g, i, val); }
    } else gs_insertion_sort(g, first, last);
}
static void gs_push_heap(const gs_t *g, char *first, long hole, long top, const char *val) {
    long parent = (hole - 1) / 2;
    while (hole > top && GS_LT(g, GS_AT(g, first, parent), val)) { GS_CP(g, GS_AT(g, first, hole), GS_AT(g, first, parent)); hole = parent; parent = (hole - 1) / 2; }
    GS_CP(g, GS_AT(g, first, hole), val);
}
static void gs_adjust_heap(const gs_t *g, char *first, long hole, long len, const char *val0) {
    char val[GS_MAXEL]; GS_CP(g, val, val0);
    long top = hole, second = 2 * hole + 2;
    while (second < len) {
        if (GS_LT(g, GS_AT(g, first, second), GS_AT(g, first, second - 1))) second--;
        GS_CP(g, GS_AT(g, first, hole), GS_AT(g, first, second)); hole = second; second = 2 * (second + 1);
    }
    if (second == len) { GS_CP(g, GS_AT(g, first, hole), GS_AT(g, first, second - 1)); hole = second - 1; }
    gs_push_heap(g, first, hole, top, val);
}
static void gs_make_heap(const gs_t *g, char *first, char *last) {
    long len = (last - first) / (long)g->sz; if (len < 2) return;
    for (long parent = (len - 2) / 2; ; parent--) { gs_adjust_heap(g, first, parent, len, GS_AT(g, first, parent)); if (parent == 0) return; }
}
static void gs_pop_heap(const gs_t *g, char *first, char *last, char *result, const char *val0) {
    char val[GS_MAXEL]; GS_CP(g, val, val0);
    GS_CP(g, result, first); gs_adjust_heap(g, first, 0, (last - first) / (long)g->sz, val);
}
static void gs_partial_sort(const gs_t *g, char *first, char *middle, char *last) {
    gs_make_heap(g, first, middle);
    for (char *i = middle; i < last; i += g->sz) if (GS_LT(g, i, first)) gs_pop_heap(g, first, middle, i, i);
    while ((middle - first) / (long)g->sz > 1) { middle -= g->sz; gs_pop_heap(g, first, middle, middle, middle); }   // sort_heap
}
static char *gs_unguarded_partition(const gs_t *g, char *first, char *last, const char *pivot) {
    char t[GS_MAXEL];
    for (;;) {
        while (GS_LT(g, first, pivot)) first += g->sz;
        last -= g->sz;
        while (GS_LT(g, pivot, last)) last -= g->sz;
        if (!(first < last)) return first;
        GS_CP(g, t, first); GS_CP(g, first, last); GS_CP(g, last, t);
        first += g->sz;
    }
}
static const char *gs_median(const gs_t *g, const char *a, const char *b, const char *c) {
    if (GS_LT(g, a, b)) { if (GS_LT(g, b, c)) return b; else if (GS_LT(g, a, c)) return c; else return a; }
    else if (GS_LT(g, a, c)) return a; else if (GS_LT(g, b, c)) return c; else return b;
}
static void gs_introsort_loop(const gs_t *g, char *first, char *last, long depth) {
    char pv[GS_MAXEL];
    while ((last - first) / (long)g->sz > 16) {
        if (depth == 0) { gs_partial_sort(g, first, last, last); return; }
        --depth;
        long n = (last - first) / (long)g->sz;
        GS_CP(g, pv, gs_median(g, first, GS_AT(g, first, n / 2), last - g->sz));     // the pivot is passed by value
        char *cut = gs_unguarded_partition(g, first, last, pv);
        gs_introsort_loop(g, cut, last, depth);
        last = cut;
    }
}
static void gs_sort(const gs_t *g, void *first_, void *last_) {
    char *first = first_, *last = last_;
    if (first == last || g->sz > GS_MAXEL) return;
    long depth = 0; for (long n = (last - first) / (long)g->sz; n != 1; n >>= 1) depth++;
    gs_introsort_loop(g, first, last, 2 * depth);
    gs_final_insertion_sort(g, first, last);
}
// stable: insertion sort (n small in the simulation) / bottom-up merge for larger n
static void gs_stable(const gs_t *g, void *first_, void *last_) {
    char *first = first_, *last = last_; long n = (last - first) / (long)g->sz;
    if (n < 2 || g->sz > GS_MAXEL) return;
    char *tmp = malloc((size_t)n * g->sz); if (!tmp) return;
    for (long w = 1; w < n; w *= 2) {
        for (long lo = 0; lo < n; lo += 2 * w) {
            long mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, i = lo, j = mid, k = lo;
            while (i < mid && j < hi) { if (GS_LT(g, GS_AT(g, first, j), GS_AT(g, first, i))) GS_CP(g, GS_AT(g, tmp, k++), GS_AT(g, first, j++)); else GS_CP(g, GS_AT(g, tmp, k++), GS_AT(g, first, i++)); }
            while (i < mid) GS_CP(g, GS_AT(g, tmp, k++), GS_AT(g, first, i++));
            while (j < hi) GS_CP(g, GS_AT(g, tmp, k++), GS_AT(g, first, j++));
        }
        memcpy(first, tmp, (size_t)n * g->sz);
    }
    free(tmp);
}
#endif
