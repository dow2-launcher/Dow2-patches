// sortfix_impl.h - STLport (VS2005 build of DOW2.exe) std::sort, transcribed from DOW2.exe 0x98f0a0 (sort), 0x98f0e0
// (__introsort_loop), 0x98f200 (__unguarded_partition), 0x98f290 (partial_sort = heap sort when the depth limit runs out),
// 0x98f1b0 / 0x98f250 / 0x98f310 / 0x98f380 (__final_insertion_sort, __insertion_sort, __linear_insert,
// __unguarded_insertion_sort). Comparator: a.k > b.k (FindWeaponTarget's candidates, highest priority first).
// The element type SF_EL (a struct with float k) is defined by the includer: the Mac port's 16-byte {float k; pad; Entity *p}.
// Not stable: for n > 16 equal keys are reordered by the partition steps (that is the whole point - Windows does it).
#ifndef SF_EL
#error define SF_EL
#endif
#define SF_GT(a, b) ((a).k > (b).k)
static void sf_unguarded_linear_insert(SF_EL *last, SF_EL val) {
    SF_EL *next = last - 1;
    while (SF_GT(val, *next)) { *last = *next; last = next; --next; }
    *last = val;
}
static void sf_insertion_sort(SF_EL *first, SF_EL *last) {
    if (first == last) return;
    for (SF_EL *i = first + 1; i != last; ++i) {
        SF_EL val = *i;
        if (SF_GT(val, *first)) { for (SF_EL *p = i; p != first; --p) *p = p[-1]; *first = val; }
        else sf_unguarded_linear_insert(i, val);
    }
}
static void sf_final_insertion_sort(SF_EL *first, SF_EL *last) {
    if (last - first > 16) {
        sf_insertion_sort(first, first + 16);
        for (SF_EL *i = first + 16; i != last; ++i) sf_unguarded_linear_insert(i, *i);
    } else sf_insertion_sort(first, last);
}
static void sf_push_heap(SF_EL *first, long hole, long top, SF_EL val) {
    long parent = (hole - 1) / 2;
    while (hole > top && SF_GT(first[parent], val)) { first[hole] = first[parent]; hole = parent; parent = (hole - 1) / 2; }
    first[hole] = val;
}
static void sf_adjust_heap(SF_EL *first, long hole, long len, SF_EL val) {
    long top = hole, second = 2 * hole + 2;
    while (second < len) {
        if (SF_GT(first[second], first[second - 1])) second--;
        first[hole] = first[second]; hole = second; second = 2 * (second + 1);
    }
    if (second == len) { first[hole] = first[second - 1]; hole = second - 1; }
    sf_push_heap(first, hole, top, val);
}
static void sf_make_heap(SF_EL *first, SF_EL *last) {
    long len = last - first; if (len < 2) return;
    for (long parent = (len - 2) / 2; ; parent--) { sf_adjust_heap(first, parent, len, first[parent]); if (parent == 0) return; }
}
static void sf_pop_heap(SF_EL *first, SF_EL *last, SF_EL *result, SF_EL val) { *result = *first; sf_adjust_heap(first, 0, last - first, val); }
static void sf_partial_sort(SF_EL *first, SF_EL *middle, SF_EL *last) {
    sf_make_heap(first, middle);
    for (SF_EL *i = middle; i < last; ++i) if (SF_GT(*i, *first)) sf_pop_heap(first, middle, i, *i);
    while (middle - first > 1) { --middle; sf_pop_heap(first, middle, middle, *middle); }      // sort_heap
}
static SF_EL *sf_unguarded_partition(SF_EL *first, SF_EL *last, SF_EL pivot) {
    for (;;) {
        while (SF_GT(*first, pivot)) ++first;
        --last;
        while (SF_GT(pivot, *last)) --last;
        if (!(first < last)) return first;
        SF_EL t = *first; *first = *last; *last = t;
        ++first;
    }
}
static const SF_EL *sf_median(const SF_EL *a, const SF_EL *b, const SF_EL *c) {
    if (SF_GT(*a, *b)) { if (SF_GT(*b, *c)) return b; else if (SF_GT(*a, *c)) return c; else return a; }
    else if (SF_GT(*a, *c)) return a; else if (SF_GT(*b, *c)) return c; else return b;
}
static void sf_introsort_loop(SF_EL *first, SF_EL *last, long depth) {
    while (last - first > 16) {
        if (depth == 0) { sf_partial_sort(first, last, last); return; }
        --depth;
        SF_EL *cut = sf_unguarded_partition(first, last, *sf_median(first, first + (last - first) / 2, last - 1));
        sf_introsort_loop(cut, last, depth);
        last = cut;
    }
}
static void sf_sort(SF_EL *first, SF_EL *last) {
    if (first == last) return;
    long depth = 0; for (long n = last - first; n != 1; n >>= 1) depth++;
    sf_introsort_loop(first, last, 2 * depth);
    sf_final_insertion_sort(first, last);
}
