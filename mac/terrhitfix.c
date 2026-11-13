// terrhitfix.c - crossplay_fix v15, part 17: where a segment hits the terrain, as Windows computes it.
// Why (match_1103_v14, desync at frame 506): an Ork stun grenade (ebps\projectiles\race_ork\ork_stun_grenade_hero_pvp,
// entity 13215, artillery dynamics) landed this frame: ModProjectileDynamics::doRealCollision -> CollisionSystem::QueryAll
// -> QueryTerrain -> Dirt::Terrain::Intersect -> HeightCollisionImp::Collide -> HeightMapCollision::Intersect (per chunk)
// -> GenericIntersect -> the per-cell test. Collided position y: Windows 0x4245fc1f, Mac 0x4245fc20 (x, z equal) - one ulp,
// and from there the entity position and the -sync_high CRC (frame 505 equal everywhere).
// The per-cell test (Mac 0x101997050 = DOW2.exe 0xcb4050 -> 0xcb3ae0) builds the cell's two triangles and calls
// MathCollision::FindIntersection(Segmentf, Trianglef) (one-sided, Mac 0x10193cb30 + FindIntersectionNN 0x10193cc50 =
// DOW2.exe 0xcb3a10 + 0x67a940). Windows (MSVC /fp:fast) adds every dot product of the plane / barycentric test as
// (z + y) + x, the port in source order (x + y) + z; so t, and the hit point start + t*dir, differ in the last bit. The hit
// normal: Windows |n|^2 = (nz^2 + ny^2) + nx^2, the port (nx^2 + ny^2) + nz^2.
// Side by side (t15: w15 = DOW2.exe's code, m15 = the port's, 1M cases): HeightMapCollision::Intersect differs in 16.1%
// (point 10.8%, normal only 5.3%), FindIntersection alone in 6.1%.
// What: the port's per-cell function jumps to DOW2.exe's own 0xcb3ae0 translated to C (lift/lift15.py -> lift/lift_th.c,
// with 0x6ccc40 Triangle ctor, 0xcb3a10 / 0x67a940 FindIntersection, 0x673660 Segment ctor); its four height-sampler
// virtual calls go to the port's sampler (vtable +0x10, same (i, j)). Self-test at load: 32 segments over a 9x9 field run
// through the port's HeightMapCollision::Intersect after the jump, results from DOW2.exe 0xcb4660 (t15/w15 st); on any
// difference the original bytes are put back. DOW2_NOTERRHITFIX=1: off.
// Same class next to it, CollisionSystem::QueryTerrain (Mac 0x1021a2c90 = SimEngine.dll 0x1004a740), which turns that hit
// into the query result doRealCollision sorts and uses: the hit's distance^2 from the start (Windows (z^2 + y^2) + x^2), its
// fraction along the segment (h - s).d / d.d (Windows (y + z) + x over (y^2 + z^2) + x^2) and the same fraction on the
// vertical-segment path - the port adds all three in source order (t15 qt, 1M: 17.4%, 26.7%, 26.7% differ). Edits: the
// distance in place (same length), the vertical fraction rewritten in place (93 bytes, scalar, Windows' order), the other
// fraction calls terrhitfix_frac (below).
#include "patch_util.h"
#include "terrhitfix_glue.h"
#include "terrhitfix_st.inc"

// QueryTerrain's hit fraction as SimEngine.dll 0x1004ab72: seg = start, delta
__attribute__((used)) float terrhitfix_frac(const float *seg, float hx, float hy, float hz) {
    float x = hx - seg[0], y = hy - seg[1], z = hz - seg[2];
    float num = (seg[4] * y + seg[5] * z) + seg[3] * x;
    float den = (seg[4] * seg[4] + seg[5] * seg[5]) + seg[3] * seg[3];
    return num / den;
}
#define QT_P2_AT 0x1021a3063UL
static const struct { const char *name; uint64_t addr; int len; unsigned char want[96], put[96]; } QT_SITES[] = {
    { "QueryTerrain hit distance^2", 0x1021a2e19, 20,
      // mulss xmm0,xmm0; mulss xmm1,xmm1; addss xmm1,xmm0; mulss xmm5,xmm5; addss xmm5,xmm1
      { 0xf3,0x0f,0x59,0xc0,0xf3,0x0f,0x59,0xc9,0xf3,0x0f,0x58,0xc8,0xf3,0x0f,0x59,0xed,0xf3,0x0f,0x58,0xe9 },
      // mulss xmm0,xmm0; mulss xmm1,xmm1; mulss xmm5,xmm5; addss xmm5,xmm1; addss xmm5,xmm0
      { 0xf3,0x0f,0x59,0xc0,0xf3,0x0f,0x59,0xc9,0xf3,0x0f,0x59,0xed,0xf3,0x0f,0x58,0xe9,0xf3,0x0f,0x58,0xe8 } },
    { "QueryTerrain hit fraction", QT_P2_AT, 73,
      // subss xmm4,[rbx] ... packed (dx*hx' + dy*hy') + dz*hz' / (dx^2 + dy^2) + dz^2 ... divss xmm0,xmm1
      { 0xf3,0x0f,0x5c,0x23,0xf3,0x0f,0x5c,0x5b,0x04,0xf3,0x0f,0x5c,0x53,0x08,0xf3,0x0f,0x10,0x43,0x0c,0xf3,0x0f,0x10,0x4b,0x10,
        0x0f,0x14,0xe0,0xf3,0x0f,0x12,0xc0,0x0f,0x59,0xc4,0x0f,0x14,0xd9,0xf3,0x0f,0x12,0xc9,0x0f,0x59,0xcb,0x0f,0x58,0xc8,0xf3,
        0x0f,0x10,0x43,0x14,0x0f,0x14,0xd0,0xf3,0x0f,0x12,0xc0,0x0f,0x59,0xc2,0x0f,0x58,0xc1,0xf3,0x0f,0x16,0xc8,0xf3,0x0f,0x5e,
        0xc1 },
      // mov rdi,rbx; movaps xmm0,xmm4; movaps xmm1,xmm3; movabs rax,terrhitfix_frac (filled in); call rax; jmp +0x1e; nops
      { 0x48,0x89,0xdf,0x0f,0x28,0xc4,0x0f,0x28,0xcb,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0xeb,0x1e,
        0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
        0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
        0x90,0x90 } },
    { "QueryTerrain vertical-path fraction", 0x1021a31ab, 93,
      { 0xf3,0x0f,0x10,0x45,0x90,0xf3,0x0f,0x10,0x4d,0x94,0xf3,0x0f,0x5c,0x03,0xf3,0x0f,0x5c,0x4b,0x04,0xf3,0x0f,0x10,0x55,0x98,
        0xf3,0x0f,0x5c,0x53,0x08,0xf3,0x0f,0x10,0x5b,0x0c,0xf3,0x0f,0x10,0x63,0x10,0xf3,0x0f,0x59,0xc3,0xf3,0x0f,0x59,0xcc,0xf3,
        0x0f,0x58,0xc8,0xf3,0x0f,0x10,0x43,0x14,0xf3,0x0f,0x59,0xd0,0xf3,0x0f,0x58,0xd1,0xf3,0x0f,0x59,0xdb,0xf3,0x0f,0x59,0xe4,
        0xf3,0x0f,0x58,0xe3,0xf3,0x0f,0x59,0xc0,0xf3,0x0f,0x58,0xc4,0xf3,0x0f,0x5e,0xd0,0xf3,0x0f,0x11,0x55,0xac },
      // h - s per axis, d loaded once, num = (dy*y + dz*z) + dx*x, den = (dy^2 + dz^2) + dx^2, divss, store [rbp-0x54]
      { 0xf3,0x0f,0x10,0x45,0x90,0xf3,0x0f,0x5c,0x03,0xf3,0x0f,0x10,0x4d,0x94,0xf3,0x0f,0x5c,0x4b,0x04,0xf3,0x0f,0x10,0x55,0x98,
        0xf3,0x0f,0x5c,0x53,0x08,0xf3,0x0f,0x10,0x5b,0x0c,0xf3,0x0f,0x10,0x63,0x10,0xf3,0x0f,0x10,0x6b,0x14,0xf3,0x0f,0x59,0xc3,
        0xf3,0x0f,0x59,0xcc,0xf3,0x0f,0x59,0xd5,0xf3,0x0f,0x58,0xca,0xf3,0x0f,0x58,0xc8,0xf3,0x0f,0x59,0xdb,0xf3,0x0f,0x59,0xe4,
        0xf3,0x0f,0x59,0xed,0xf3,0x0f,0x58,0xe5,0xf3,0x0f,0x58,0xe3,0xf3,0x0f,0x5e,0xcc,0xf3,0x0f,0x11,0x4d,0xac } },
};
#define QT_N 3
// terrhitfix_frac self-test: seg (6), hit (3) -> SimEngine.dll's fraction (t15 w15 qt)
#include "terrhitfix_qt_st.inc"
static int qt_write(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
static void qt_init(uintptr_t slide) {
    for (int i = 0; i < QT_N; i++) if (memcmp((void *)(QT_SITES[i].addr + slide), QT_SITES[i].want, QT_SITES[i].len)) {
        pu_log("terrhitfix: unexpected bytes at %#lx (QueryTerrain) - those 3 edits NOT made%.0lu", (unsigned long)QT_SITES[i].addr, 0); return; }
    unsigned long bad = 0;
    for (int q = 0; q < QT_NST; q++) {
        float seg[6], h[3], r; memcpy(seg, QT_ST[q], 24); memcpy(h, QT_ST[q] + 6, 12);
        r = terrhitfix_frac(seg, h[0], h[1], h[2]); if (memcmp(&r, &QT_ST[q][9], 4)) bad++;
    }
    if (bad) { pu_log("terrhitfix self-test (QueryTerrain fraction): %lu of %lu differ - those 3 edits NOT made", bad, QT_NST); return; }
    int done = 0;
    for (int i = 0; i < QT_N; i++) {
        unsigned char put[96]; memcpy(put, QT_SITES[i].put, QT_SITES[i].len);
        if (QT_SITES[i].addr == QT_P2_AT) { uint64_t f = (uint64_t)terrhitfix_frac; memcpy(put + 11, &f, 8); }
        done += qt_write((unsigned char *)(QT_SITES[i].addr + slide), put, QT_SITES[i].len);
    }
    pu_log(done == QT_N ? "terrhitfix: OK - QueryTerrain hit distance / fraction as SimEngine.dll, %lu edits, self-test %lu"
                        : "terrhitfix: QueryTerrain: only %lu edits made (vm_protect), self-test %lu", (unsigned long)done, QT_NST);
}

static const uint64_t TH_SITE = 0x101997050;               // the per-cell functor's operator (GenericIntersect calls it)
static const unsigned char TH_WANT[20] = { 0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53,
    0x48, 0x81, 0xec, 0xf8, 0x00, 0x00, 0x00 };
static float th_st_sample(void *s, long i, long j) { (void)s; float f; memcpy(&f, &TH_HF[j * 9 + i], 4); return f; }
static void *th_st_vt[3] = { 0, 0, (void *)th_st_sample };
static int th_restore(unsigned char *p) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, TH_WANT, sizeof TH_WANT);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}

__attribute__((constructor)) static void terrhitfix_init(void) {
    if (getenv("DOW2_NOTERRHITFIX")) { pu_log("terrhitfix: DOW2_NOTERRHITFIX set - the port's own terrain hit test%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    qt_init(slide);
    unsigned char *p = (unsigned char *)(TH_SITE + slide);
    if (memcmp(p, TH_WANT, sizeof TH_WANT)) { pu_log("terrhitfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)TH_SITE, 0); return; }
    if (!pu_jump(p, (void *)terrhitfix_cell)) { pu_log("terrhitfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    // self-test through the port's own HeightMapCollision::Intersect (0x101996150): GenericIntersect -> the jump -> lifted code
    char (*hmi)(void *, unsigned long, unsigned long, float, const float *, const float *, float *, float *) =
        (char (*)(void *, unsigned long, unsigned long, float, const float *, const float *, float *, float *))(0x101996150UL + slide);
    void *smp = th_st_vt; unsigned long bad = 0, hits = 0;
    for (int q = 0; q < 32; q++) {
        float s[3], d[3], pt[3] = { 7, 7, 7 }, n[3] = { 7, 7, 7 }; uint32_t got[7];
        memcpy(s, TH_ST[q], 12); memcpy(d, TH_ST[q] + 3, 12);
        got[0] = (unsigned char)hmi(&smp, 9, 9, 1.5f, s, d, pt, n); memcpy(got + 1, pt, 12); memcpy(got + 4, n, 12);
        if (memcmp(got, TH_ST[q] + 6, sizeof got)) bad++;
        hits += got[0];
    }
    if (bad) {
        int r = th_restore(p);
        pu_log(r ? "terrhitfix self-test: %lu of 32 differ - NOT patched (original code put back)%.0lu"
                 : "terrhitfix self-test: %lu of 32 differ - and restoring failed%.0lu", bad, 0);
        return;
    }
    pu_log("terrhitfix: OK - terrain segment hit (per-cell triangles, FindIntersection, normal) as DOW2.exe, self-test 32 (%lu hits)%.0lu", hits, 0);
}
