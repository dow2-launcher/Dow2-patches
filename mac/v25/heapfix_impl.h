// heapfix_impl.h - the Windows (STLport) heap algorithms on the Mac port's boost::heap layout. Shared by heapfix.c (the game)
// and heapfix_test.c (Linux check against the port's real machine code and DOW2.exe's code transcribed).
// Mac layout (boost 1.51 priority_queue_mutable_wrapper<d_ary_heap<..., stable<true>, mutable_<true>, arity<2>>>):
//   heap: [0] insertion counter, [8] vector begin, [0x10] end, [0x18] capacity; entry = {list node *, counter} (16 bytes);
//   list node: +0x18 key (float f of the OpenNode), +0x20 the entry's index in the heap (the mutable handle).
// The counter is only read by the port's push (see heapfix.c) - here it just travels with its entry.
#include <stdint.h>
#include <string.h>

typedef struct { char *ln; uint64_t cnt; } hf_entry;
typedef struct { uint64_t counter; hf_entry *b, *e, *cap; } hf_heap;
#define HF_KEY(x) (*(const float *)((x).ln + 0x18))
#define HF_PUT(a, i, v) do { (a)[i] = (v); *(uint64_t *)((a)[i].ln + 0x20) = (uint64_t)(i); } while (0)

// STLport __push_heap(first, hole, top, value), less-is-better: move up while the parent's key is strictly greater (DOW2.exe 0xa22e30)
static inline void hf_push_hole(hf_entry *a, int64_t hole, int64_t top, hf_entry v) {
    int64_t p = (hole - 1) / 2;
    while (hole > top && HF_KEY(v) < HF_KEY(a[p])) { HF_PUT(a, hole, a[p]); hole = p; p = (hole - 1) / 2; }
    HF_PUT(a, hole, v);
}

// replaces the port's sift-down: STLport __adjust_heap(first, idx, len, first[idx]) (DOW2.exe 0xa22e80; pop = adjust from 0
// with the former last element, which the port has already swapped to the top)
__attribute__((used, visibility("default"))) void heapfix_siftdown(hf_heap *h, uint64_t idx) {
    hf_entry *a = h->b; int64_t len = h->e - h->b, top = (int64_t)idx, hole = top;
    if (hole >= len) return;
    hf_entry v = a[hole];
    int64_t second = 2 * hole + 2;
    while (second < len) {
        if (HF_KEY(a[second - 1]) < HF_KEY(a[second])) second--;    // ties go to the right child
        HF_PUT(a, hole, a[second]); hole = second; second = 2 * second + 2;
    }
    if (second == len) { HF_PUT(a, hole, a[second - 1]); hole = second - 1; }
    hf_push_hole(a, hole, top, v);
}

// replaces the port's update(handle) after a better path to an open node: Windows only sifts it up (0xa1f170 -> 0xa22e30)
__attribute__((used, visibility("default"))) void heapfix_update(hf_heap *h, uint64_t idx) {
    hf_entry *a = h->b; int64_t len = h->e - h->b;
    if ((int64_t)idx >= len) return;
    hf_push_hole(a, (int64_t)idx, 0, a[idx]);
}

// a quick check at load: pushes/pops/decreases with many equal keys vs the STLport algorithms on a plain {node, key} array.
// Push here is a transcription of the port's (stable: counter breaks ties, a new node carries the largest counter).
static int heapfix_selftest(void) {
    enum { N = 400 };
    static char nodes[N][0x28]; static hf_entry store[N]; static struct { int node; float key; } win[N];
    int bad = 0; uint32_t r = 12345u;
    for (int round = 0; round < 40; round++) {
        hf_heap h = { 0, store, store, store + N }; int wn = 0, nl = 0;
        for (int step = 0; step < 300; step++) {
            r = r * 1103515245u + 12345u; int op = (r >> 16) % 10;
            if (op < 5 && nl < N) {                                              // push
                int id = nl; float k = (float)((r >> 8) % 7);
                memset(nodes[id], 0, sizeof nodes[id]); memcpy(nodes[id] + 0x18, &k, 4); nl++;
                hf_entry v = { nodes[id], ++h.counter }; int64_t i = h.e - h.b; h.e++;   // port: append, stable sift-up
                HF_PUT(h.b, i, v);
                while (i > 0) { int64_t p = (i - 1) / 2;
                    if (HF_KEY(h.b[p]) < HF_KEY(h.b[i]) || (HF_KEY(h.b[p]) == HF_KEY(h.b[i]) && h.b[p].cnt <= h.b[i].cnt)) break;
                    hf_entry t = h.b[p]; HF_PUT(h.b, p, h.b[i]); HF_PUT(h.b, i, t); i = p; }
                int j = wn++; while (j > 0 && k < win[(j - 1) / 2].key) { win[j] = win[(j - 1) / 2]; j = (j - 1) / 2; }   // STLport
                win[j].node = id; win[j].key = k;
            } else if (op < 8 && h.e > h.b) {                                    // pop
                hf_entry t = h.b[0]; h.e--; int64_t n = h.e - h.b;
                if (n) { HF_PUT(h.b, 0, h.b[n]); h.b[n] = t; heapfix_siftdown(&h, 0); }
                int got = (int)((t.ln - nodes[0]) / 0x28);
                int want = win[0].node; int len = --wn; float vk = win[len].key; int vn = win[len].node;
                int hole = 0, sc = 2;
                while (sc < len) { if (win[sc - 1].key < win[sc].key) sc--; win[hole] = win[sc]; hole = sc; sc = 2 * sc + 2; }
                if (sc == len) { win[hole] = win[sc - 1]; hole = sc - 1; }
                while (hole > 0 && vk < win[(hole - 1) / 2].key) { win[hole] = win[(hole - 1) / 2]; hole = (hole - 1) / 2; }
                win[hole].node = vn; win[hole].key = vk;
                if (got != want) bad++;
            } else if (h.e > h.b) {                                              // decrease a random open node's key
                int64_t i = (r >> 4) % (h.e - h.b); int id = (int)((h.b[i].ln - nodes[0]) / 0x28);
                float k = HF_KEY(h.b[i]) - (float)(1 + (r >> 12) % 2); memcpy(nodes[id] + 0x18, &k, 4);
                heapfix_update(&h, *(uint64_t *)(nodes[id] + 0x20));
                int j = 0; while (win[j].node != id) j++;
                while (j > 0 && k < win[(j - 1) / 2].key) { win[j] = win[(j - 1) / 2]; j = (j - 1) / 2; }
                win[j].node = id; win[j].key = k;
            }
            for (int64_t i = 0; i < h.e - h.b; i++)
                if ((int)((h.b[i].ln - nodes[0]) / 0x28) != win[i].node || *(uint64_t *)(h.b[i].ln + 0x20) != (uint64_t)i) { bad++; break; }
        }
    }
    return bad;
}
