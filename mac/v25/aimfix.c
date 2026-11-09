// aimfix.c - crossplay_fix v13, part 13: the weapons' aim and scatter as Windows computes them.
// Why: match_1102_v11 desynced when the Eldar started shooting; v12 fixed the hit/collision side. Run side by side
// (t13/waim.c, wscat.c: DOW2.exe's own code in a 32-bit process; t13/maim.c, mscat.c: the port's code) on 1M cases:
//   ShootingWeapon::GetWorldPositionHeading     9.6% of cases differ   (muzzle position / heading / right vector)
//   ShootingWeapon::GetAnglesToTarget          17.6%                   (yaw / pitch to the target)
//   ShootingWeapon::ConvertWorldSpacePoint...  17.5%                   (target in weapon space - CanAutoTarget)
//   WeaponUtil::CalculateScatterPosition       13.5%                   (where a shot lands)
//   ShootingWeapon::ComputeCoordinateSystem     8.9%                   (the hardpoint frame, made once per weapon, used
//                                                                       by all of the above)
//   MathCollision::FindIntersection(box, ray)   3.1%                   (where the shot meets the target's box; DOW2.exe's
//                                                                       copy 0x679e50 - the port's camera/UI use it too)
// MSVC /fp:fast folded Math::MakeRotateY / MakeRotateAboutAxis into the vector math with sin/cos in registers, adds each
// vector * matrix in its own order, and the scatter mixes in x87 at 24-bit precision; the port computes in source order.
// What: these six functions of the port jump to DOW2.exe's own code translated to C (lift/lift13.py -> lift/lift_aim.c;
// aim_glue.h maps the port's weapon / entity fields onto Windows' layout). After: 0 of 1M differ, each.
// All or nothing on unexpected bytes; self-test at load against Windows' results (28 cases); DOW2_NOAIMFIX=1: off.
#include "patch_util.h"
#include "aimfix_sites.h"
#include "aim_glue.h"
static const uint32_t AF_ST_AIM[][44] = {
#include "aimfix_st_aim.inc"
};
static const uint32_t AF_ST_SCAT[][34] = {
#include "aimfix_st_scat.inc"
};
static const uint32_t AF_ST_CS[][18] = {
#include "aimfix_st_cs.inc"
};
static const uint32_t AF_ST_RAY[][25] = {
#include "aimfix_st_ray.inc"
};
static uint32_t af_rs;
static float af_rand(uint32_t id) { af_rs = af_rs * 1664525u + 1013904223u + id; return (float)(af_rs >> 8) / 16777216.0f; }
static float af_height(float x, float z, uint32_t f) { return x * 0.03125f - z * 0.0625f + (float)f * 0.5f; }
static unsigned long af_selftest(void) {
    unsigned long bad = 0;
    static char w[0x300], obj[0x40], ent[0x200];
    *(void **)(w + 0x160) = obj; *(void **)(obj + 0x28) = ent;
    for (size_t k = 0; k < sizeof AF_ST_AIM / sizeof AF_ST_AIM[0]; k++) {
        const uint32_t *in = AF_ST_AIM[k], *want = in + 29;     // yaw, pitch, off[3], wm[9], em[12], tgt[3]
        memcpy(w + 0x1b8, in, 8); memcpy(w + 0x1c8, in + 2, 12); memcpy(w + 0x1d4, in + 5, 36); memcpy(ent + 0x9c, in + 14, 48);
        uint32_t o[15]; float a1 = 7, a2 = 7, out[3] = { 0, 0, 0 };
        aimfix_worldposheading(w, (float *)o, (float *)(o + 3), (float *)(o + 6));
        o[11] = (uint8_t)aimfix_anglestotarget(w, (const float *)(in + 26), &a1, &a2);
        aimfix_toweaponspace(w, (const float *)(in + 26), out);
        memcpy(o + 9, &a1, 4); memcpy(o + 10, &a2, 4); memcpy(o + 12, out, 12);
        if (memcmp(o, want, sizeof o)) bad++;
    }
    aim_rand_hook = af_rand; aim_height_hook = af_height;
    for (size_t k = 0; k < sizeof AF_ST_SCAT / sizeof AF_ST_SCAT[0]; k++) {
        const uint32_t *in = AF_ST_SCAT[k], *want = in + 27;    // info[16], from[3], tgt[3], counter, flag, b6, b7, rseed
        uint32_t info[16]; memcpy(info, in, 64); af_rs = in[26];
        float out[3] = { 9, 9, 9 }; int32_t cnt = (int32_t)in[22]; uint8_t of = 0x55;
        uint32_t o[7];
        o[0] = (uint8_t)aimfix_scatter((const char *)info, (char)in[23], (const float *)(in + 16), (const float *)(in + 19), &cnt,
                                       (char *)&of, out, (char)in[24], (char)in[25]);
        memcpy(o + 1, out, 12); o[4] = (uint32_t)cnt; o[5] = of; o[6] = af_rs;
        if (memcmp(o, want, sizeof o)) bad++;
    }
    aim_rand_hook = 0; aim_height_hook = 0;
    for (size_t k = 0; k < sizeof AF_ST_CS / sizeof AF_ST_CS[0]; k++) {
        const uint32_t *in = AF_ST_CS[k], *want = in + 6;       // hardpoint position, direction -> offset, rotation
        memset(w, 0, sizeof w); memcpy(w + 0x58, in, 24);
        aimfix_coordsys(w);
        if (memcmp(w + 0x1c8, want, 48)) bad++;
    }
    for (size_t k = 0; k < sizeof AF_ST_RAY / sizeof AF_ST_RAY[0]; k++) {
        const uint32_t *in = AF_ST_RAY[k], *want = in + 21;    // box[15], ray[6] -> hit?, point
        uint32_t o[4]; float hit[3] = { 9, 9, 9 };
        o[0] = (uint8_t)aimfix_rayobb((const float *)in, (const float *)(in + 15), hit); memcpy(o + 1, hit, 12);
        if (memcmp(o, want, sizeof o)) bad++;
    }
    return bad;
}
__attribute__((constructor)) static void aimfix_init(void) {
    if (getenv("DOW2_NOAIMFIX")) { pu_log("aimfix: DOW2_NOAIMFIX set - the port's own aim / scatter math%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    scat_slide = slide;
    for (int i = 0; i < AF_NSITES; i++)
        if (memcmp((void *)(AF_SITES[i].addr + slide), AF_SITES[i].want, 24)) {
            pu_log("aimfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)AF_SITES[i].addr, 0); return; }
    unsigned long bad = af_selftest(), n = sizeof AF_ST_AIM / sizeof AF_ST_AIM[0] + sizeof AF_ST_SCAT / sizeof AF_ST_SCAT[0] + sizeof AF_ST_CS / sizeof AF_ST_CS[0]
                      + sizeof AF_ST_RAY / sizeof AF_ST_RAY[0];
    if (bad) { pu_log("aimfix self-test: %lu of %lu cases differ from Windows - NOT patched, report this", bad, n); return; }
    static void *const G[] = { (void *)aimfix_worldposheading, (void *)aimfix_anglestotarget, (void *)aimfix_toweaponspace, (void *)aimfix_scatter,
                               (void *)aimfix_coordsys, (void *)aimfix_rayobb };
    int done = 0;
    for (int i = 0; i < AF_NSITES; i++) done += pu_jump((unsigned char *)(AF_SITES[i].addr + slide), G[AF_SITES[i].glue]);
    if (done != AF_NSITES) { pu_log("aimfix: only %lu of %lu edits made (vm_protect failed)", (unsigned long)done, (unsigned long)AF_NSITES); return; }
    pu_log("aimfix: OK - %lu edits: hardpoint frame, weapon muzzle/heading, angles to target, weapon space, scatter, shot / box hit point as Windows (DOW2.exe code), self-test %lu", (unsigned long)done, n);
}
