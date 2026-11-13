// homingfix.c - crossplay_fix v22, part 25: ModProjectileDynamics::initHoming draws its random numbers in DOW2.exe's order.
// Why (difftest step 7, replays 6 F5035 sm_krak_missile, 48787 F5427 krak missile, arena F5902 ork missile, refinery 06-01 F6381
// haywire grenade - the High state's first difference is the projectile's modprojectiledynamics offset x/y/z at F itself, whole
// different values; the High dumps' randomflow shows the same six calls in another order:
//   Mac:     PRJX PRX2 PRJY PRY2 PRJZ PRZ2        Windows: PRX2 PRJX PRY2 PRJY PRZ2 PRJZ
// Cause: the homing target offset is scattered per axis by  off.c = GetRand(PRJc) * off.c * (GetRand(PRc2) > 0.5 ? 0.5 : -0.5).
// MSVC evaluates the sign factor's GetRand first (DOW2.exe 0x9990cc..0x99920d), clang the left one first (Mac 0x101d43dc7..):
// both draws are taken from the same stream, so each axis gets the other's number and every later random call is shifted.
// Not covered so far: aimfix lifted CalculateScatterPosition (the other function with a different tag order, see the audit
// below), projfix the flight/homing steering, not this initialisation.
// What: the function entry (Mac 0x101d43d60) jumps to hm_init: the same fields, PRc2 drawn before PRJc, product (r * off) * s
// as DOW2.exe (s = +-0.5; for subnormal results the x87's single 24-bit rounding with its wide exponent is reproduced). Self-test at load: HM_NST cases from DOW2.exe 0x999090 (t22/w22 homst), with a fake
// generator that returns the n-th value for the n-th call: offsets, missile value and the order of the tags must match.
// Audit (t22 randorder.py): the FourCC tag sequence of every function calling GetRand / GetRandMax / GetRandRange, Mac vs
// DOW2.exe: 59 functions equal, 2 differ (this one; WeaponUtil::CalculateScatterPosition, replaced by aimfix's lift), 13 split
// by inlining (call order checked equal). DOW2_NOHOMINGFIX=1: off.
#include "patch_util.h"
typedef float (*hm_rand_fn)(void *ctx, uint32_t tag);
static void hm_core(char *self, hm_rand_fn rnd, void *ctx, uintptr_t slide) {
    char *md = ((char *(*)(void *))(0x101b966d0UL + slide))(*(void **)(self + 0x78));      // ProjectileExtInfo::GetMissileData
    uint32_t v = *(uint32_t *)(md + 0xc);                   // DOW2.exe copies it with fld / fstp: a signalling NaN comes out quiet
    if ((v & 0x7f800000u) == 0x7f800000u && (v & 0x3fffffu) && !(v & 0x400000u)) v |= 0x400000u;
    *(uint32_t *)(self + 0xe8) = v;
    char *t = *(char **)(*(char **)(self + 0x80) + 0x40);
    if (!t) return;
    *(uint32_t *)(self + 0x108) = *(uint32_t *)(t + 0x90);
    *(uint64_t *)(self + 0x100) = *(uint64_t *)(t + 0x88);
    static const uint32_t T2[3] = { 0x50525832, 0x50525932, 0x50525a32 }, T1[3] = { 0x50524a58, 0x50524a59, 0x50524a5a };   // PRc2, PRJc
    for (int i = 0; i < 3; i++) {
        volatile float b = rnd(ctx, T2[i]);
        volatile float s = b > 0.5f ? 0.5f : -0.5f;
        volatile float a = rnd(ctx, T1[i]);
        float *o = (float *)(self + 0x100 + 4 * i);
        // DOW2.exe: fmul at 24-bit precision with the x87's wide exponent, fmul by s, fstp: one 24-bit rounding of r * off without
        // the float exponent limit, the subnormal rounding only at the store (differs from two float products for tiny values)
        volatile double pd = (double)a * (double)*o; double pr;           // exact (24 x 24 bits)
        if (pd < 0x1p-120 && pd > -0x1p-120) { volatile float mf = (float)(pd * 0x1p100); pr = (double)mf * 0x1p-100; }
        else { volatile float mf = (float)pd; pr = mf; }
        *o = (float)(pr * (double)s);
    }
}
static uintptr_t hm_slide;
static float hm_game_rand(void *ctx, uint32_t tag) {
    char *sm = *(char **)(0x1038f5670UL + hm_slide) + 0x2b8;
    return ((float (*)(void *, uint32_t))(0x10216d780UL + hm_slide))(sm, tag);                // SimManager::GetRand (randfix'd)
}
void hm_init(char *self) { hm_core(self, hm_game_rand, 0, hm_slide); }
#include "homingfix_st.inc"
// self-test: the DOW2.exe cases through hm_core (fake ProjectileExtInfo: GetMissileData returns it + 0x4c, value at +0x58)
static uint32_t st_r[6], st_tags[16]; static int st_n;
static float st_rand(void *ctx, uint32_t tag) { if (st_n < 16) st_tags[st_n] = tag; float f; memcpy(&f, &st_r[st_n++ % 6], 4); return f; }
static uint32_t st_hash(const uint32_t *t, int n) { uint32_t h = 2166136261u; for (int i = 0; i < n; i++) h = (h ^ t[i]) * 16777619u; return h; }
static unsigned long hm_selftest(void) {
    static char self[0x200], pe[0x100], o80[0x100], tg[0x100]; unsigned long bad = 0;
    for (unsigned long q = 0; q < HM_NST; q++) {
        const uint32_t *c = HM_ST[q];
        memset(self, 0, sizeof self); memset(tg, 0, sizeof tg);
        *(void **)(self + 0x78) = pe; *(void **)(self + 0x80) = o80; *(uint32_t *)(pe + 0x58) = c[1];
        *(void **)(o80 + 0x40) = c[0] ? tg : 0; memcpy(tg + 0x88, c + 2, 12); memcpy(st_r, c + 5, 24); st_n = 0;
        hm_core(self, st_rand, 0, hm_slide);
        uint32_t o[6]; o[0] = *(uint32_t *)(self + 0xe8); memcpy(o + 1, self + 0x100, 12); o[4] = st_hash(st_tags, st_n < 16 ? st_n : 16); o[5] = st_n;
        if (!c[0]) o[1] = o[2] = o[3] = 0;
        if (memcmp(o, c + 11, 24)) bad++;
    }
    return bad;
}
static const unsigned char HM_WANT[17] = { 0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xec,0x38,0x49,0x89,0xfe,0x49 };
__attribute__((constructor)) static void homingfix_init(void) {
    if (getenv("DOW2_NOHOMINGFIX")) { pu_log("homingfix: DOW2_NOHOMINGFIX set - the port's own initHoming random order%.0lu%.0lu", 0, 0); return; }
    hm_slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(0x101d43d60UL + hm_slide);
    if (memcmp(p, HM_WANT, sizeof HM_WANT)) { pu_log("homingfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", 0x101d43d60UL, 0); return; }
    unsigned long bad = hm_selftest();
    if (bad) { pu_log("homingfix self-test: %lu of %lu differ from DOW2.exe - NOT patched", bad, HM_NST); return; }
    if (!pu_jump(p, (void *)hm_init)) { pu_log("homingfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    pu_log("homingfix: OK - ModProjectileDynamics::initHoming draws PRc2 before PRJc as DOW2.exe, self-test %lu%.0lu", HM_NST, 0);
}
