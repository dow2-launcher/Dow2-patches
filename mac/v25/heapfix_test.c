// heapfix_test.c - Linux check of heapfix: the Mac port's REAL push / sift-down / update machine code (mapped from the game
// binary at its own addresses) vs DOW2.exe's heap (0xa22e30 push_heap, 0xa22e80 adjust_heap, 0xa1f170 pop/decrease, transcribed).
//   gcc -O1 -o heapfix_test heapfix_test.c && ./heapfix_test /mnt/dow2bench/dev/ghidra_proj_mac/dow2mac.bin
//   ./heapfix_test dow2mac.bin crossplay_fix.dylib <heapfix_siftdown off> <heapfix_update off>   (the shipped code)
// Expect: "port as shipped" differs from Windows, "port + heapfix" matches Windows on every step for all 13 instantiations.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "heapfix_impl.h"
#include "dylib_map.h"

static const uint64_t PUSH[13] = { 0x101ad0e20, 0x101e99ba0, 0x101ea4a40, 0x101ea5d90, 0x101ea6fb0, 0x101ea81c0, 0x101ea9500,
    0x101eaa4d0, 0x101eab4a0, 0x101eac470, 0x101ead690, 0x101eae8b0, 0x101eafac0 };
static const uint64_t SIFT[13] = { 0x101ad1470, 0x101e9a040, 0x101ea4ee0, 0x101ea6230, 0x101ea7450, 0x101ea8660, 0x101ea99a0,
    0x101eaa970, 0x101eab940, 0x101eac910, 0x101eadb30, 0x101eaed50, 0x101eaff60 };
static const uint64_t UPD[13] = { 0, 0x101e9a2c0, 0x101ea5160, 0x101ea64b0, 0x101ea76d0, 0x101ea88e0, 0x101ea9c20, 0x101eaabf0,
    0x101eabbc0, 0x101eacb90, 0x101eaddb0, 0x101eaefd0, 0x101eb01e0 };   // AIAStar (the 1st) never updates

// ---- DOW2.exe (STLport) on {node, key} entries ----
typedef struct { int node; float key; } went;
static void w_push_heap(went *a, int hole, int top, went v) {                // 0xa22e30
    while (hole > top) { int p = (hole - 1) / 2; if (!(v.key < a[p].key)) break; a[hole] = a[p]; hole = p; }
    a[hole] = v;
}
static void w_adjust_heap(went *a, int hole, int len, went v) {              // 0xa22e80
    int top = hole, sc = 2 * hole + 2;
    while (sc < len) { if (a[sc - 1].key < a[sc].key) sc--; a[hole] = a[sc]; hole = sc; sc = 2 * sc + 2; }
    if (sc == len) { a[hole] = a[sc - 1]; hole = sc - 1; }
    w_push_heap(a, hole, top, v);
}

typedef void (*push_fn)(hf_heap *, char **);
typedef void (*idx_fn)(hf_heap *, uint64_t);
static idx_fn FIX_SIFT = heapfix_siftdown, FIX_UPD = heapfix_update;   // or the built dylib's code (argv 2..4)

enum { N = 3000 };
static char nodes[N][0x28];
static hf_entry store[N];
static went win[N];

static int run(int inst, int fixed, uint32_t seed, int nkeys, long *steps) {
    push_fn push = (push_fn)PUSH[inst];
    idx_fn sift = fixed ? FIX_SIFT : (idx_fn)SIFT[inst];
    idx_fn upd = fixed ? FIX_UPD : (idx_fn)UPD[inst];
    hf_heap h = { 0, store, store, store + N }; int wn = 0, made = 0; uint32_t r = seed;
    for (int step = 0; step < 4000; step++) {
        r = r * 1103515245u + 12345u; int op = (r >> 16) % 10;
        if (op < 5 && made < N) {
            int id = made++; float k = nkeys ? (float)((r >> 5) % nkeys) * 0.5f : (float)(r >> 9) / 1024.0f;
            memset(nodes[id], 0, sizeof nodes[id]); memcpy(nodes[id] + 0x18, &k, 4);
            char *it = nodes[id]; push(&h, &it);
            went v = { id, k }; w_push_heap(win, wn, 0, v); wn++;
        } else if (op < 8 && h.e > h.b) {
            hf_entry t = h.b[0]; h.e--; int64_t n = h.e - h.b;                   // the port's inlined pop
            if (n) { h.b[0] = h.b[n]; h.b[n] = t; *(uint64_t *)(h.b[0].ln + 0x20) = 0; sift(&h, 0); }
            went v = win[--wn]; win[wn] = win[0]; w_adjust_heap(win, 0, wn, v);   // 0xa1f170 pop
            if ((int)((t.ln - nodes[0]) / 0x28) != win[wn].node) return step;
        } else if (h.e > h.b && UPD[inst]) {
            int64_t i = (r >> 4) % (h.e - h.b); int id = (int)((h.b[i].ln - nodes[0]) / 0x28);
            float k = HF_KEY(h.b[i]) - (nkeys ? 0.5f * (float)(1 + (r >> 12) % 3) : (float)((r >> 12) % 64) / 1024.0f);
            memcpy(nodes[id] + 0x18, &k, 4);
            upd(&h, *(uint64_t *)(nodes[id] + 0x20));
            int j = 0; while (win[j].node != id) j++;                           // 0x6bf990 linear find, then push_heap
            win[j].key = k; w_push_heap(win, j, 0, win[j]);
        }
        if (wn != h.e - h.b) return step;
        for (int i = 0; i < wn; i++)
            if ((int)((h.b[i].ln - nodes[0]) / 0x28) != win[i].node || *(uint64_t *)(h.b[i].ln + 0x20) != (uint64_t)i) return step;
        (*steps)++;
    }
    return -1;
}

int main(int argc, char **argv) {
    FILE *f = fopen(argc > 1 ? argv[1] : "/mnt/dow2bench/dev/ghidra_proj_mac/dow2mac.bin", "rb"); if (!f) { perror("bin"); return 2; }
    static const uint64_t R[2][2] = { { 0x101ad0000, 0x101ad2000 }, { 0x101e99000, 0x101eb1000 } };
    for (int i = 0; i < 2; i++) {
        size_t len = R[i][1] - R[i][0];
        void *m = mmap((void *)R[i][0], len, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (m != (void *)R[i][0]) { perror("mmap"); return 2; }
        fseek(f, (long)(R[i][0] - 0x100000000), SEEK_SET); if (fread(m, 1, len, f) != len) { perror("read"); return 2; }
    }
    if (argc > 4) { char *d = dylib_map(argv[2]); FIX_SIFT = (idx_fn)(d + strtoul(argv[3], 0, 16)); FIX_UPD = (idx_fn)(d + strtoul(argv[4], 0, 16));
        printf("checking the built dylib's code: %s\n", argv[2]); }
    int fails_fixed = 0, diverged_orig = 0, runs = 0; long steps = 0;
    for (int inst = 0; inst < 13; inst++)
        for (int nk = 0; nk <= 9; nk += 3)
            for (uint32_t s = 1; s <= 25; s++) {
                long st = 0; int a = run(inst, 1, s * 7919u + inst, nk, &st); steps += st; runs++;
                if (a >= 0) { if (fails_fixed++ < 5) printf("FIXED differs: inst %d keys %d seed %u at step %d\n", inst, nk, s, a); }
                long d; if (run(inst, 0, s * 7919u + inst, nk, &d) >= 0) diverged_orig++;
            }
    printf("runs %d, steps checked %ld\n", runs, steps);
    printf("port as shipped: %d of %d runs differ from Windows\n", diverged_orig, runs);
    printf("port + heapfix:  %d of %d runs differ from Windows\n", fails_fixed, runs);
    printf("load-time self-test (heapfix_selftest): %d bad\n", heapfix_selftest());
    return fails_fixed ? 1 : 0;
}
