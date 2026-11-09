// rangefix.c - crossplay_fix v13, part 14: range checks between units (EntityUtil::IsWithinDist and the OBB2 distance)
// as Windows computes them.
// Why: "is the target within range / within melee / at the destination" is decided by the distance between the units'
// 2D boxes. The port's MathCollision::DistanceSquared(OBB2, OBB2) differs from SimEngine.dll's in 24.6% of random cases
// (last bits - the closest-corner / edge math in another order), and the quick bound adds the 5 terms
// (two half sizes each + range) in another order (Windows ((((A.y + A.x) + B.y) + B.x) + d)). A unit 1 ulp in range on one
// side and out of range on the other = desync. (t13/wobb.c vs mobb.c)
// What: the port's DistanceSquared(OBB2, OBB2), IsWithinDist(OBB2, OBB2, d), IsWithinDistShifted(OBB2, at, OBB2, d) jump to
// SimEngine.dll's own code translated to C (lift/lift_obb.c; DOW2.exe's statically linked copy 0x67e880 is the same
// instruction stream, so the port's callers from both sides get Windows' numbers). After: 0 of 1M differ.
// Also Math::Matrix43f::InverseSelf (the port's one copy for all callers): SimEngine.dll's 0x1004e3a0 and DOW2.exe's 0x6746b0 are
// the same instruction stream (v12 sent only the SimEngine-side callers to it); the port's own differs in 52% of cases. DOW2.exe
// side: ModProjectileDynamics::RequestMove (target in the target entity's space, 20% differ with the product below), wall
// splines, StateProjectile, AreaInfo::CheckPie, the AI threat map ...
// All or nothing on unexpected bytes; self-test at load (8 + 4 cases); DOW2_NORANGEFIX=1: off.
#include "patch_util.h"
#include "rangefix_sites.h"
#include "obb_glue.h"
float *combatfix_inverse_se(float *m);           // combatfix.c: SimEngine.dll's 4x3 inverse (0x1004e3a0) lifted to C
#include "combatfix_selftest.inc"
static const uint32_t RF_ST[][24] = {
#include "rangefix_st.inc"
};
static unsigned long rf_selftest(void) {
    unsigned long bad = 0;
    for (size_t k = 0; k < sizeof RF_ST / sizeof RF_ST[0]; k++) {
        const uint32_t *in = RF_ST[k], *want = in + 21;     // a[8], b[8], p[2], q[2], d
        float d; memcpy(&d, in + 20, 4);
        float d1 = rangefix_distsq((const float *)in, (const float *)(in + 8));
        uint32_t o[3]; memcpy(o, &d1, 4);
        o[1] = (uint8_t)rangefix_iswithin((const float *)in, (const float *)(in + 8), d);
        o[2] = (uint8_t)rangefix_iswithinshifted((const float *)in, (const float *)(in + 16), (const float *)(in + 8), d);
        if (memcmp(o, want, sizeof o)) bad++;
    }
    for (size_t k = 0; k < sizeof CF_ST_INV / sizeof CF_ST_INV[0]; k++) {
        float m[12]; memcpy(m, CF_ST_INV[k], 48);
        if (combatfix_inverse_se(m) != m || memcmp(m, CF_ST_INV[k] + 12, 48)) bad++;
    }
    return bad;
}
__attribute__((constructor)) static void rangefix_init(void) {
    if (getenv("DOW2_NORANGEFIX")) { pu_log("rangefix: DOW2_NORANGEFIX set - the port's own OBB2 range math%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    for (int i = 0; i < RF_NSITES; i++)
        if (memcmp((void *)(RF_SITES[i].addr + slide), RF_SITES[i].want, 24)) {
            pu_log("rangefix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)RF_SITES[i].addr, 0); return; }
    unsigned long bad = rf_selftest(), n = sizeof RF_ST / sizeof RF_ST[0] + sizeof CF_ST_INV / sizeof CF_ST_INV[0];
    if (bad) { pu_log("rangefix self-test: %lu of %lu cases differ from SimEngine.dll - NOT patched, report this", bad, n); return; }
    static void *const G[] = { (void *)rangefix_distsq, (void *)rangefix_iswithin, (void *)rangefix_iswithinshifted, (void *)combatfix_inverse_se };
    int done = 0;
    for (int i = 0; i < RF_NSITES; i++) done += pu_jump((unsigned char *)(RF_SITES[i].addr + slide), G[RF_SITES[i].glue]);
    if (done != RF_NSITES) { pu_log("rangefix: only %lu of %lu edits made (vm_protect failed)", (unsigned long)done, (unsigned long)RF_NSITES); return; }
    pu_log("rangefix: OK - %lu edits: OBB2 distance, IsWithinDist(Shifted), 4x3 inverse as Windows (SimEngine.dll code), self-test %lu", (unsigned long)done, n);
}
