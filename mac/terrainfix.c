// terrainfix.c - crossplay_fix v9, part 8: terrain height and normal summed in Windows' order.
// Why: Dirt::HeightMapCollision::GetHeightNormal (Mac 0x101995e30, DOW2.exe 0xcb40c0) interpolates the height field over a
// triangle. Windows adds ((1-f1)*dA + f2*dB) + hc, the port (1-f1)*dA + (hc + f2*dB); the normal: Windows (c*c + a*a) + 1 and
// Newton steps (y*y)*half, the port c*c + (a*a + 1) and (half*y)*y. Different float rounding: an ulp off in ~20% of points.
// Every entity snapped to the terrain gets its y from here: at frame 0 of match_1102_v8, 13 map objects (walls, plants, the
// victory and strategic points) stood an ulp higher/lower on the Mac -> -sync_high CRC differs at once.
// Checked in terrainfix_test.c: the port's real code reproduces the Mac y of all 13, this one the Windows y (and 372 stored
// heights of the map). DOW2_NOTERRAINFIX=1: off.
#include "patch_util.h"
#include "terrainfix_impl.h"

static const uint64_t SITE = 0x101995e30;
static const unsigned char WANT[36] = { 0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18,0x49,
    0x89,0xce,0x0f,0x28,0xda,0x49,0x89,0xd4,0x48,0x89,0xf3,0x49,0x89,0xff,0xf3,0x0f,0x10,0x15 };

static const float G[16] = { 37.519184f, 37.07898f, 38.25f, 36.9017f, 37.8125f, 37.2511f, 39.0039f, 36.5f, 37.123f, 38.777f,
    37.0001f, 36.75f, 38.1f, 37.6f, 37.3f, 36.95f };
static float st_sample(const void *s, long i, long j) { (void)s; return G[(j & 3) * 4 + (i & 3)]; }
static void *st_vt[3] = { 0, 0, (void *)st_sample };
static const struct { float x, z; uint32_t h, n0, n1, n2; } T[] = {   // values of the Linux build checked against Windows heights
    {0x1.333334p-2f, 0x1.666666p-1f, 0x4216396eu, 0x3ef2d2fdu, 0x3f58441fu, 0xbe7dbcb5u},
    {0x1.666666p-1f, 0x1.333334p-2f, 0x42150cfbu, 0x3ecbc505u, 0x3f6772e1u, 0xbe1f58d7u},
    {-0x1p-2f, 0x1.333334p-1f, 0x421629c8u, 0xbf027f65u, 0x3f53565bu, 0xbe77f452u},
    {0x1.666666p+0f, -0x1.e66666p+0f, 0x4218628au, 0x3f5cb8d6u, 0x3ef86f4cu, 0xbe1502deu},
    {0x1p-1f, 0x1p-1f, 0x42158a62u, 0x3ef2d2fdu, 0x3f58441fu, 0xbe7dbcb5u},
    {-0x1.19999ap+0f, -0x1.99999ap-5f, 0x4213f4c4u, 0x3f32f2f0u, 0x3f04b8cau, 0xbefc2bf3u},
    {0x1.f7cedap-4f, 0x1.fcac08p+0f, 0x42155775u, 0xbf4e5623u, 0x3ef98004u, 0x3eac07afu},
    {0x1.cp+0f, 0x1.cp-1f, 0x421a2d3eu, 0xbf5d8d82u, 0x3efccc77u, 0xbdae0bc5u} };

__attribute__((constructor)) static void terrainfix_init(void) {
    if (getenv("DOW2_NOTERRAINFIX")) { pu_log("terrainfix: DOW2_NOTERRAINFIX set - the port's own terrain height%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(SITE + slide);
    if (memcmp(p, WANT, sizeof WANT)) { pu_log("terrainfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)SITE, 0); return; }
    struct { void **vt; } smp = { st_vt }; unsigned long bad = 0;
    for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) {
        float n[3]; float h = terrainfix_height_normal(&smp, 8, 8, 1.0f, T[k].x, T[k].z, n); uint32_t b[4];
        memcpy(b, &h, 4); memcpy(b + 1, n, 12);
        if (b[0] != T[k].h || b[1] != T[k].n0 || b[2] != T[k].n1 || b[3] != T[k].n2) bad++;
    }
    if (bad) { pu_log("terrainfix self-test: %lu values differ - NOT patched, report this%.0lu", bad, 0); return; }
    pu_log(pu_jump(p, (void *)terrainfix_height_normal) ? "terrainfix: OK - terrain height/normal summed as Windows%.0lu%.0lu"
                                                         : "terrainfix: vm_protect failed%.0lu%.0lu", 0, 0);
}
