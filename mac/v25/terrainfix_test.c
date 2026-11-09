// terrainfix_test.c - Linux check of terrainfix: the port's REAL GetHeightNormal (0x101995e30, with its floorf 0x100934170,
// mapped from the game binary) and terrainfix_height_normal() on the 1v1typhonlowlands height field (513 x 545 floats from
// the scenario's HMAN/HITE/HVAL chunk), against entity heights from the frame-0 sync dumps of match_1102_v8 (Windows w0 / Mac m0).
//   gcc -O2 -ffp-contract=off -o terrainfix_test terrainfix_test.c && ./terrainfix_test dow2mac.bin heights.bin ents.txt
//   ... ents.txt crossplay_fix.dylib <terrainfix_height_normal off>   (the shipped code)
// ents.txt: id x z y_windows y_mac (float bits, hex). Expect: port code == y_mac and terrainfix == y_windows wherever the
// entity was snapped to the terrain on that side.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "terrainfix_impl.h"
#include "dylib_map.h"

enum { W = 513, H = 545 };
static float hts[W * H];
static float sample(const void *s, long i, long j) { (void)s; return hts[j * W + i]; }
static void *vt[4] = { 0, 0, (void *)sample, 0 };
static struct { void **vt; } smp = { vt };
typedef float (*hn_fn)(const void *, unsigned long, unsigned long, float, float, float, float *);

static float fb(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t bf(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s dow2mac.bin heights.bin ents.txt\n", argv[0]); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror("bin"); return 2; }
    static const uint64_t R[5][2] = { { 0x100934000, 0x100935000 }, { 0x101995000, 0x101997000 }, { 0x102462000, 0x102464000 },
                                     { 0x1024d4000, 0x1024d5000 }, { 0x102544000, 0x102545000 } };   // code + its float constants
    for (int k = 0; k < 5; k++) {
        size_t len = R[k][1] - R[k][0];
        void *m = mmap((void *)R[k][0], len, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (m != (void *)R[k][0]) { perror("mmap"); return 2; }
        fseek(f, (long)(R[k][0] - 0x100000000), SEEK_SET); if (fread(m, 1, len, f) != len) { perror("read"); return 2; }
    }
    FILE *h = fopen(argv[2], "rb"); if (!h || fread(hts, 4, W * H, h) != W * H) { perror("heights"); return 2; }
    hn_fn port = (hn_fn)0x101995e30, fix = terrainfix_height_normal;
    if (argc > 5) { fix = (hn_fn)(dylib_map(argv[4]) + strtoul(argv[5], 0, 16)); printf("checking the built dylib's code: %s\n", argv[4]); }
    FILE *e = fopen(argv[3], "r"); if (!e) { perror("ents"); return 2; }
    int id, n = 0, port_mac = 0, fix_win = 0, ydiff = 0, ydiff_ok = 0, fix_nwin_but_port = 0; unsigned x, z, yw, ym;
    while (fscanf(e, "%d %x %x %x %x", &id, &x, &z, &yw, &ym) == 5) {
        float nP[3], nF[3];
        uint32_t p = bf(port(&smp, W, H, 1.0f, fb(x), fb(z), nP)), q = bf(fix(&smp, W, H, 1.0f, fb(x), fb(z), nF));
        n++; port_mac += p == ym; fix_win += q == yw;
        if (p == ym && q != yw) fix_nwin_but_port++;          // snapped on the Mac but our Windows formula misses the Windows y
        if (yw != ym) { ydiff++; ydiff_ok += p == ym && q == yw;
            printf("entity %d: y Windows %08x Mac %08x | port code %08x, terrainfix %08x\n", id, yw, ym, p, q); }
    }
    printf("entities %d: port code == Mac y: %d, terrainfix == Windows y: %d\n", n, port_mac, fix_win);
    printf("entities whose y differs Win/Mac: %d, explained (port == Mac and fix == Windows): %d\n", ydiff, ydiff_ok);
    printf("snapped on Mac (port == Mac y) but terrainfix != Windows y: %d\n", fix_nwin_but_port);
    // random points: normals and heights of the port code and terrainfix, how often they differ (association only)
    srand(1); int dh = 0, dn = 0, tot = 200000;
    for (int k = 0; k < tot; k++) {
        float px = (rand() / (float)RAND_MAX - 0.5f) * 250.0f, pz = (rand() / (float)RAND_MAX - 0.5f) * 270.0f, a[3], b[3];
        float hp = port(&smp, W, H, 1.0f, px, pz, a), hf = fix(&smp, W, H, 1.0f, px, pz, b);
        dh += bf(hp) != bf(hf); dn += memcmp(a, b, 12) != 0;
    }
    printf("random %d points: height differs port vs fix %d, normal differs %d\n", tot, dh, dn);
    return ydiff_ok == ydiff ? 0 : 1;
}
