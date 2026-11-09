// costfix.c - crossplay_fix v21, part 23: the high-level pathfinder's step cost (PathHighEnvironment::ActualCost) as DOW2.exe.
// Why (difftest step 6, replays lohmistral F324 and 48787 F3415, gpr:hlr:numsearched 57/58 and 69/70): A* expansion traces of
// both sides (macrun MR_ASTAR / DiffTrace.dll DT_ASTAR) pop the same nodes in the same order; the first difference is one
// neighbour's g one ulp apart (lohmistral: sector 1010 g Mac 0x423d540e, Windows 0x423d540f; 48787: 2021 0x415f6e13 / ..14),
// so a later f compares the other way and one search expands one node more.
// Cause: ActualCost (Mac 0x101e9a950 = DOW2.exe 0xa7a7f0) with d = sqrt(dx^2 + dy^2) (sector centres, or the start point),
// P(pos) = the path-cost object's operator()(pos, d/2) (BasicPathCost returns d/2) and L = the level penalty:
//   DOW2.exe (x87 at 24 bits = float): L = (diff * d) * 0.1f, cost = (P(to) + P(from)) + L
//   the port (SSE):                    L = 0 + (diff * 0.1f) * d, cost = (P(from) + L) + P(to)
// Both the product order of L and the sum order round differently when a level penalty applies (diff = limit - level > 0).
// What: the function entry jumps to hc_cost, the DOW2.exe order, calling the same path-cost object (from first, as both do).
// Self-test at load: CF_NST cases through hc_cost, results from DOW2.exe 0xa7a7f0 itself (t21/w21 st, BasicPathCost 0x91bfd0).
// Side by side t21 (1M random sectors / levels): port 0x101e9a950 vs DOW2.exe, dylib vs DOW2.exe. DOW2_NOCOSTFIX=1: off.
// The heuristic (PathHighEnvironment::Heuristic 0x101e9a710 = 0xa7a3d0: max + min * (sqrt2f - 1)), the neighbour order
// (0xa7a490), the exit test and the open list (heapfix) were read / traced equal.
#include "patch_util.h"
typedef float (*cf_pcost)(void *self, const int32_t *pos, float half);
float hc_cost(char *env, char *from, char *to, uint32_t lvl) {
    int32_t dx, dy;
    if (*(uint16_t *)(from + 0x328) == *(uint16_t *)(env + 0x90)) { dx = *(int32_t *)(to + 0x320) - *(int32_t *)(env + 0x28); dy = *(int32_t *)(to + 0x324) - *(int32_t *)(env + 0x2c); }
    else { dx = *(int32_t *)(to + 0x320) - *(int32_t *)(from + 0x320); dy = *(int32_t *)(to + 0x324) - *(int32_t *)(from + 0x324); }
    int32_t d2 = (int32_t)((uint32_t)dy * (uint32_t)dy + (uint32_t)dx * (uint32_t)dx);
    float d = __builtin_sqrtf((float)d2);
    volatile float L = 0.0f;
    int32_t diff = *(int32_t *)(env + 0x74) - (int32_t)(lvl & 0xffff);
    if (diff > 0) { volatile float t = (float)diff * d; L = t * 0.1f; }
    volatile float half = d * 0.5f;
    void *pc = *(void **)(env + 0x88); cf_pcost fn = **(cf_pcost **)pc;
    int32_t pf[2] = { *(int32_t *)(from + 0x320), *(int32_t *)(from + 0x324) }, pt[2] = { *(int32_t *)(to + 0x320), *(int32_t *)(to + 0x324) };
    volatile float a = fn(pc, pf, half);
    volatile float b = fn(pc, pt, half);
    volatile float s = b + a;
    return s + L;
}
#include "costfix_st.inc"
// self-test: env / sectors / BasicPathCost-like object (returns its scale: the DOW2.exe side used 0x91bfd0)
static float cf_basic(void *self, const int32_t *pos, float half) { return half; }
static unsigned long cf_selftest(float (*f)(char *, char *, char *, uint32_t)) {
    static char env[0x100], s1[0x330], s2[0x330]; static void *vt[1] = { (void *)cf_basic }; static void *obj[1] = { vt };
    unsigned long bad = 0;
    for (unsigned long q = 0; q < CF_NST; q++) {
        const int32_t *c = CF_ST[q];          // start x y, limit, lvl, from x y id, to x y id, start id, want (float bits)
        *(int32_t *)(env + 0x28) = c[0]; *(int32_t *)(env + 0x2c) = c[1]; *(int32_t *)(env + 0x74) = c[2];
        *(uint16_t *)(env + 0x90) = (uint16_t)c[10]; *(void **)(env + 0x88) = obj;
        *(int32_t *)(s1 + 0x320) = c[4]; *(int32_t *)(s1 + 0x324) = c[5]; *(uint16_t *)(s1 + 0x328) = (uint16_t)c[6];
        *(int32_t *)(s2 + 0x320) = c[7]; *(int32_t *)(s2 + 0x324) = c[8]; *(uint16_t *)(s2 + 0x328) = (uint16_t)c[9];
        float r = f(env, s1, s2, (uint32_t)c[3]); uint32_t rb; memcpy(&rb, &r, 4);
        if (rb != (uint32_t)c[11]) bad++;
    }
    return bad;
}
static const unsigned char CF_WANT[17] = { 0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xec,0x20,0x41,0x89 };
__attribute__((constructor)) static void costfix_init(void) {
    if (getenv("DOW2_NOCOSTFIX")) { pu_log("costfix: DOW2_NOCOSTFIX set - the port's own high-level path step cost%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(0x101e9a950UL + slide);
    if (memcmp(p, CF_WANT, sizeof CF_WANT)) { pu_log("costfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", 0x101e9a950UL, 0); return; }
    unsigned long bad = cf_selftest(hc_cost);
    if (bad) { pu_log("costfix self-test: %lu of %lu differ from DOW2.exe - NOT patched", bad, CF_NST); return; }
    if (!pu_jump(p, (void *)hc_cost)) { pu_log("costfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    pu_log("costfix: OK - PathHighEnvironment::ActualCost in DOW2.exe order ((P(to) + P(from)) + (diff * d) * 0.1), self-test %lu%.0lu", CF_NST, 0);
}
