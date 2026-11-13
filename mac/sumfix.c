// sumfix.c - crossplay_fix v13, part 15: float sums inside larger functions put in Windows' order, in place.
// Why: MSVC /fp:fast adds 3-term sums (|v|^2, dot products, every inlined vector * matrix column) in an order of its own
// choosing at each place; the port adds in source order and differs in the last bit. Where a whole function can't be
// replaced (many calls, object state), the few differing sums are fixed where they are. Found by running Windows' own code
// and the port's side by side: whole functions (t13/w*.c, m*.c) or fragments of them (t13/wfrag.h, mfrag.h):
//   WeaponUtil::CanAutoTarget       the off-axis angle of the target: |v| (y^2 + z^2) + x^2   4.3% of cases differ
//   WeaponUtil::GetHitPositionOnEntity  the random point in the target's box (x column (Y+Z)+X) and the aim ray's length
//                                   (z^2 + y^2) + x^2 (twice)                                   9.3%
// What (sumfix_sites.inc from gen_sum.py): same-length rewrites of the instructions, and calls of the port's
// Vector3f::operator*= redirected (through a page near the game code) to helpers with Windows' column orders.
// Also divisions by a constant (sumfix_divs.inc): MSVC /fp:fast multiplies by the float reciprocal instead (all of DOW2.exe has
// 1 divss by a constant; SimEngine's PercentageToProbability is x * 0.01f, which the port inlines as x / 100 - percentages of
// the blueprints: refunds, chances, suppression resistance, heal amount ...; weapon cooldowns, knockback, formation catch-up,
// entity order, driver info, ability ticks ...). x / c and x * (1 / c) differ in the last bit for a good share of x.
// And the fast inverse square root (0x5f3759df + two Newton steps) where Windows multiplies (y*y)*half and the port
// (half*y)*y: projectile fire from a hold, squad combat/unload states, melee, AI avoidance (stubs in the near page).
// All or nothing on unexpected bytes; self-test of the helpers against Windows' results; DOW2_NOSUMFIX=1: off.
#include "patch_util.h"
#include "sumfix_sites.h"
static const uint32_t SF_ST_VM[][18] = {        // x, y, z, rotation[9], center[3] -> center + (x, y, z) * rotation (Windows)
#include "sumfix_st_vm.inc"
};
static int sf_write(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
static unsigned long sf_selftest(void) {
    unsigned long bad = 0;
    for (size_t k = 0; k < sizeof SF_ST_VM / sizeof SF_ST_VM[0]; k++) {
        const uint32_t *in = SF_ST_VM[k]; float v[3], c[3], m[9]; memcpy(v, in, 12); memcpy(m, in + 3, 36); memcpy(c, in + 12, 12);
        sumfix_vm3_yz_xy_xy(v, m);
        float r[3] = { c[0] + v[0], c[1] + v[1], c[2] + v[2] };
        if (memcmp(r, in + 15, 12)) bad++;
    }
    return bad;
}
__attribute__((constructor)) static void sumfix_init(void) {
    if (getenv("DOW2_NOSUMFIX")) { pu_log("sumfix: DOW2_NOSUMFIX set - the port's own sum order%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    for (int i = 0; i < SF_NSITES; i++)
        if (memcmp((void *)(SF_SITES[i].addr + slide), SF_SITES[i].want, SF_SITES[i].len)) {
            pu_log("sumfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)SF_SITES[i].addr, 0); return; }
    for (int i = 0; i < SF_NCALLS; i++) {
        unsigned char *c = (unsigned char *)(SF_CALLS[i].at + slide); int32_t rel; memcpy(&rel, c + 1, 4);
        if (c[0] != 0xE8 || (uint64_t)(c + 5 + rel) != SF_CALLS[i].target + slide) {
            pu_log("sumfix: unexpected call at %#lx - NOT patched%.0lu", (unsigned long)SF_CALLS[i].at, 0); return; }
    }
    for (int i = 0; i < SF_NSTUBS; i++)
        if (memcmp((void *)(SF_STUBS[i].addr + slide), SF_STUBS[i].want, SF_STUBS[i].len)) {
            pu_log("sumfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)SF_STUBS[i].addr, 0); return; }
    for (int i = 0; i < SF_NDIVS; i++)
        if (memcmp((void *)(SF_DIVS[i].addr + slide), SF_DIVS[i].want, SF_DIVS[i].len)) {
            pu_log("sumfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)SF_DIVS[i].addr, 0); return; }
    unsigned long bad = sf_selftest(), n = sizeof SF_ST_VM / sizeof SF_ST_VM[0];
    if (bad) { pu_log("sumfix self-test: %lu of %lu cases differ from Windows - NOT patched, report this", bad, n); return; }
    vm_address_t near = 0;
    for (vm_address_t want = 0x104200000UL + slide; want < 0x140000000UL + slide; want += 0x100000) {
        near = want;
        if (vm_allocate(mach_task_self(), &near, vm_page_size, 0) == KERN_SUCCESS) break;
        near = 0;
    }
    if (!near) { pu_log("sumfix: no page near the game code - NOT patched%.0lu%.0lu", 0, 0); return; }
    for (int i = 0; i < SF_NCALLS; i++) {
        unsigned char *t = (unsigned char *)near + 16 * i; t[0] = 0xFF; t[1] = 0x25; memset(t + 2, 0, 4);
        void *h = SF_HELPERS[SF_CALLS[i].helper]; memcpy(t + 6, &h, 8);
    }
    for (int i = 0; i < SF_NDIVS; i++) memcpy((char *)near + 0x800 + 4 * i, &SF_DIVS[i].recip, 4);
    for (int i = 0; i < SF_NSTUBS; i++) {
        unsigned char *b = (unsigned char *)near + 0x400 + 0x60 * i; uint64_t back = SF_STUBS[i].addr + slide + SF_STUBS[i].len;
        memcpy(b, SF_STUBS[i].blob, SF_STUBS[i].bloblen); memcpy(b + SF_STUBS[i].backoff, &back, 8);
    }
    vm_protect(mach_task_self(), near, vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    for (int i = 0; i < SF_NCALLS; i++) {
        unsigned char *c = (unsigned char *)(SF_CALLS[i].at + slide);
        long long d = (long long)(near + 16 * i) - (long long)(c + 5);
        if (d > 0x7fffffffLL || d < -0x80000000LL) { pu_log("sumfix: helper page too far (%#lx) - NOT patched%.0lu", (unsigned long)near, 0); return; }
    }
    for (int i = 0; i < SF_NSTUBS; i++) {
        long long d = (long long)(near + 0x400 + 0x60 * i) - (long long)(SF_STUBS[i].addr + slide + 5);
        if (d > 0x7fffffffLL || d < -0x80000000LL) { pu_log("sumfix: stub page too far (%#lx) - NOT patched%.0lu", (unsigned long)near, 0); return; }
    }
    for (int i = 0; i < SF_NDIVS; i++) {
        long long d = (long long)(near + 0x800 + 4 * i) - (long long)(SF_DIVS[i].addr + slide + SF_DIVS[i].len);
        if (d > 0x7fffffffLL || d < -0x80000000LL) { pu_log("sumfix: constant page too far (%#lx) - NOT patched%.0lu", (unsigned long)near, 0); return; }
    }
    int done = 0;
    for (int i = 0; i < SF_NSITES; i++) done += sf_write((unsigned char *)(SF_SITES[i].addr + slide), SF_SITES[i].put, SF_SITES[i].len);
    for (int i = 0; i < SF_NCALLS; i++) {
        unsigned char *c = (unsigned char *)(SF_CALLS[i].at + slide);
        int32_t rel = (int32_t)((long long)(near + 16 * i) - (long long)(c + 5));
        done += sf_write(c + 1, &rel, 4);
    }
    for (int i = 0; i < SF_NDIVS; i++) {
        unsigned char ins[10]; memcpy(ins, SF_DIVS[i].want, SF_DIVS[i].len);
        int32_t d = (int32_t)((long long)(near + 0x800 + 4 * i) - (long long)(SF_DIVS[i].addr + slide + SF_DIVS[i].len));
        ins[SF_DIVS[i].opo] = 0x59; memcpy(ins + SF_DIVS[i].dispo, &d, 4);
        done += sf_write((unsigned char *)(SF_DIVS[i].addr + slide), ins, SF_DIVS[i].len);
    }
    for (int i = 0; i < SF_NSTUBS; i++) {
        unsigned char j[64]; memset(j, 0x90, sizeof j); j[0] = 0xE9;
        int32_t d = (int32_t)((long long)(near + 0x400 + 0x60 * i) - (long long)(SF_STUBS[i].addr + slide + 5)); memcpy(j + 1, &d, 4);
        done += sf_write((unsigned char *)(SF_STUBS[i].addr + slide), j, SF_STUBS[i].len);
    }
    int total = SF_NSITES + SF_NCALLS + SF_NDIVS + SF_NSTUBS;
    if (done != total) { pu_log("sumfix: only %lu of %lu edits made (vm_protect failed)", (unsigned long)done, (unsigned long)total); return; }
    pu_log("sumfix: OK - %lu edits: sums in Windows' order (weapon target angle, hit point ...), x / const as x * (1 / const), fast 1/sqrt steps, self-test %lu", (unsigned long)done, n);
}
