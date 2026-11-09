// combatfix.c - crossplay_fix v12, part 12: combat-time collision math as Windows does it.
// Why: match_1102_v11 (v11 on both sides) desynced at frame 573 when the Eldar started shooting, with the RNG logs still
// identical: target scanning / line-of-sight / projectile hits differ in the last bits. Found by running Windows' and the
// port's code side by side on random cases (test/w*.c vs test/m*.c):
//   ModProjectileDynamics::doCollision (DOW2.exe)             0.29% of cases differ
//   MathCollision::FindIntersection(OBBf, Segmentf)           18%   (QueryEntities: line of sight, shots)
//   segment / triangle (CollisionModel::Query)                13%   (0.7% hit vs miss)
//   4x3 inverse (CollisionModel::SetTransform, GetOBB2f, QueryEntities)  52% (translation)
//   Mesh::TransformMesh (collision meshes, moved at every SetTransform)  11% of the coordinates
// What (combatfix_sites.h): 9 in-place reorders of the same instructions; 10 calls redirected (through a page near the
// game code) to helpers with Windows' order or to SimEngine.dll's own code lifted to C (lift/). All or nothing on unexpected
// bytes. Self-test at load: the lifted functions on 12 cases against SimEngine.dll's results; on a mismatch nothing is
// patched. DOW2_NOCOMBATFIX=1: off.
#include "patch_util.h"
#include "combatfix_sites.h"
#include "combatfix_helpers.h"
#include "combatfix_selftest.inc"

static int cf_write(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}

static unsigned long cf_selftest(void) {
    unsigned long bad = 0;
    for (size_t k = 0; k < sizeof CF_ST_FI / sizeof CF_ST_FI[0]; k++) {
        float obb[15], seg[6]; memcpy(obb, CF_ST_FI[k], 60); memcpy(seg, CF_ST_FI[k] + 15, 24);
        uint32_t cp[32]; int32_t n = -7; memset(cp, 0x55, sizeof cp);
        uint32_t r = (uint32_t)(combatfix_findintersection(obb, seg, &n, cp, 1) & 0xff);
        if (r != CF_ST_FI[k][21] || (r && ((uint32_t)n != CF_ST_FI[k][22] || memcmp(cp, CF_ST_FI[k] + 23, 64)))) bad++;
    }
    for (size_t k = 0; k < sizeof CF_ST_TR / sizeof CF_ST_TR[0]; k++) {
        float seg[6], tri[9]; memcpy(seg, CF_ST_TR[k], 24); memcpy(tri, CF_ST_TR[k] + 6, 36);
        uint32_t pt[3] = { 0, 0, 0 }, t = 0;
        uint32_t r = (uint32_t)(combatfix_segtri(seg, tri, pt, &t) & 0xff);
        if (r != CF_ST_TR[k][15] || (r && (memcmp(pt, CF_ST_TR[k] + 16, 12) || t != CF_ST_TR[k][19]))) bad++;
    }
    for (size_t k = 0; k < sizeof CF_ST_INV / sizeof CF_ST_INV[0]; k++) {
        float m[12]; memcpy(m, CF_ST_INV[k], 48);
        combatfix_inverse_se(m);
        if (memcmp(m, CF_ST_INV[k] + 12, 48)) bad++;
    }
    return bad;
}

__attribute__((constructor)) static void combatfix_init(void) {
    if (getenv("DOW2_NOCOMBATFIX")) { pu_log("combatfix: DOW2_NOCOMBATFIX set - the port's own collision math%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    for (int i = 0; i < CF_NSITES; i++)
        if (memcmp((void *)(CF_SITES[i].addr + slide), CF_SITES[i].want, CF_SITES[i].len)) {
            pu_log("combatfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)CF_SITES[i].addr, 0); return; }
    for (int i = 0; i < CF_NCALLS; i++) {
        unsigned char *c = (unsigned char *)(CF_CALLS[i].at + slide); int32_t rel; memcpy(&rel, c + 1, 4);
        if (c[0] != 0xE8 || (uint64_t)(c + 5 + rel) != CF_CALLS[i].target + slide) {
            pu_log("combatfix: unexpected call at %#lx - NOT patched%.0lu", (unsigned long)CF_CALLS[i].at, 0); return; }
    }
    unsigned long bad = cf_selftest();
    if (bad) { pu_log("combatfix self-test: %lu of 12 cases differ from SimEngine.dll - NOT patched, report this%.0lu", bad, 0); return; }
    // the helpers live in this dylib (too far for call rel32): a page within 2 GB of the game holds "jmp [rip+0]; .quad helper"
    vm_address_t near = 0;
    for (vm_address_t want = 0x104100000UL + slide; want < 0x140000000UL + slide; want += 0x100000) {
        near = want;
        if (vm_allocate(mach_task_self(), &near, vm_page_size, 0) == KERN_SUCCESS) break;
        near = 0;
    }
    if (!near) { pu_log("combatfix: no page near the game code - NOT patched%.0lu%.0lu", 0, 0); return; }
    for (int i = 0; i < CF_NCALLS; i++) {
        unsigned char *t = (unsigned char *)near + 16 * i; t[0] = 0xFF; t[1] = 0x25; memset(t + 2, 0, 4);
        void *h = CF_HELPERS[CF_CALLS[i].helper]; memcpy(t + 6, &h, 8);
    }
    vm_protect(mach_task_self(), near, vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    for (int i = 0; i < CF_NCALLS; i++) {
        unsigned char *c = (unsigned char *)(CF_CALLS[i].at + slide);
        long long d = (long long)(near + 16 * i) - (long long)(c + 5);
        if (d > 0x7fffffffLL || d < -0x80000000LL) { pu_log("combatfix: helper page too far (%#lx) - NOT patched%.0lu", (unsigned long)near, 0); return; }
    }
    int done = 0;
    for (int i = 0; i < CF_NSITES; i++) done += cf_write((unsigned char *)(CF_SITES[i].addr + slide), CF_SITES[i].put, CF_SITES[i].len);
    for (int i = 0; i < CF_NCALLS; i++) {
        unsigned char *c = (unsigned char *)(CF_CALLS[i].at + slide);
        int32_t rel = (int32_t)((long long)(near + 16 * i) - (long long)(c + 5));
        done += cf_write(c + 1, &rel, 4);
    }
    if (done != CF_NSITES + CF_NCALLS) { pu_log("combatfix: only %lu of %lu edits made (vm_protect failed)", (unsigned long)done, (unsigned long)(CF_NSITES + CF_NCALLS)); return; }
    pu_log("combatfix: OK - %lu edits: doCollision, collision queries/meshes/inverse as Windows (SimEngine.dll), self-test %lu/12",
           (unsigned long)done, 12);
}
