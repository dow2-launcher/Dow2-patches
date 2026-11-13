// knockfix.c - crossplay_fix v17, part 19: knockback - where a thrown model lands and for how long it flies, as Windows.
// Why (live match 1107 on v16, Green Tooth Gorge: sync to frame 891, desync at 892): an Eldar guardian (entity 13197) entering
// StateKnockback: the fly target x Windows 0x4145f3fe / Mac 0x4145f3ff, the fly duration 5 / 6 ticks (end tick 897 / 898);
// the facing (normalize(pos - fly target), Update_STATE_KNOCKBACK) follows from the fly target - checked: Windows' fly target
// gives exactly Windows' axes, the Mac's the Mac's. RNG equal: a float difference.
// StateKnockback::PlotThrow (Mac 0x101bc5040 / DOW2.exe 0x977130) pulls the fly target toward the slide target the pathfinder
// gave and rescales the duration. MSVC /fp:fast:
//   |fly - pos|, |slide - pos|, |fly - pos| again, |slide - fly|: (z^2 + y^2) + x^2   the port (y^2 + x^2) + z^2
//   fly duration = (int)(((float)dur / d0) * d1)                                    the port (dur * d1) / d0
// The same audit of the rest of the knockback entry (DoKnockback Mac 0x101bc35b0 / DOW2.exe 0x976a10 + 0x976ca0, run side by
// side, t17) found two more:
//   KnockbackFlightPathInfo::ComputeNiceDuration (0x101e4d700): (s + s) / 29.4 - Windows (s * 2) * 0.0340136f   45.9% of the
//     flight durations differ in the last bit (the int ticks less often)
//   the knock target (0x101bc36b2..0x101bc3835, inlined; Windows 0x976ca0): ((speed * t) * t) * 0.25 - Windows ((t * t) * speed)
//     (the speed / 15 there is sumfix's since v13)                                                            0.24%
//   LevitateDynamicsOverride::StepMotionCalculation (0x101e4e900 / DOW2.exe 0xa35aa0): (rate * 2.45) * p - Windows
//     (rate * p) * 2.45                                                                                      1.09%
// What: PlotThrow - 5 same-length rewrites in place (the four sums, the duration's order); ComputeNiceDuration - its entry jumps
// to kb_nice; levitate - the two multiplications swapped in place (Windows' formula); the knock target - the region becomes a call of knockfix_impl.h's kb_fly (DOW2.exe 0x976ca0
// transcribed) + ClampToWorld as before. All or nothing on unexpected bytes (the sumfix edit inside the region is accepted);
// self-test at load against DOW2.exe's results (t17/w17 st), through the patched game functions where they are callable;
// DOW2_NOKNOCKFIX=1: off. Must run after sumfix (link order).
#include "patch_util.h"
#include "knockfix_impl.h"
#include "knockfix_sites.h"
#include "knockfix_st.inc"
static int kb_write(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    vm_size_t sz = (((vm_address_t)p + len + vm_page_size - 1) & ~(vm_address_t)(vm_page_size - 1)) - page;
    if (vm_protect(mach_task_self(), page, sz, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, sz, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
// KnockbackFlightPathInfo::ComputeNiceDuration(this): DOW2.exe 0x976ad5..0x976afa (inlined there): s = [this+8]
__attribute__((used, noinline)) float knockfix_nice(const float *info) {
    float x = (info[2] * 2.0f) * 0.0340136066f;               // [0x1085040] = 2, [0x113cd98] = 0x3d0b51da
    if (0.0f > x) return 0.0f;
    return sqrtf(x) * 2.0f;
}
__attribute__((used, noinline)) int32_t knockfix_fly(const float *tgt, const float *pos, float nice, float *out) { return kb_fly(tgt, pos, nice, out); }
// the four PlotThrow sums: (z^2 + y^2) + x^2 with x in xmm2 (S1, S3, S4) or xmm1 (S2), y in xmm1 / xmm3, z in xmm0 / xmm2
static const unsigned char KB_S134_PUT[20] = { 0xf3,0x0f,0x59,0xd2, 0xf3,0x0f,0x59,0xc9, 0xf3,0x0f,0x59,0xc0, 0xf3,0x0f,0x58,0xc1, 0xf3,0x0f,0x58,0xc2 };
static const unsigned char KB_S2_PUT[23] = { 0xf3,0x0f,0x59,0xc9, 0xf3,0x0f,0x59,0xdb, 0x0f,0x28,0xc2, 0xf3,0x0f,0x59,0xc0, 0xf3,0x0f,0x58,0xc3, 0xf3,0x0f,0x58,0xc1 };
// LevitateDynamicsOverride::StepMotionCalculation 0x101e4e96e: y += (rate * p) * 2.45 as DOW2.exe 0xa35b17 (the port:
// (rate * 2.45) * p): mulss xmm1, xmm0; mulss xmm1, [rip -> 2.45] (the same constant, displacement moved by 4)
static const unsigned char KB_LV_PUT[12] = { 0xf3,0x0f,0x59,0xc8, 0xf3,0x0f,0x59,0x0d,0xb6,0xa4,0x71,0x00 };
// fly duration: divss xmm1, [rbp-0x80]; mulss xmm1, xmm0  (was mulss; divss)
static const unsigned char KB_D_PUT[9] = { 0xf3,0x0f,0x5e,0x4d,0x80, 0xf3,0x0f,0x59,0xc8 };
#define KB_FLY_SF 0xfe                                         // sumfix's divss -> mulss (0x101bc37b0) inside the region
// kb_call(code, x0, x1, x2, x3) -> xmm0: runs a copy of a rewritten sum (+ ret) for the self-test
float kb_call(const void *code, float a, float b, float c, float d);
__asm__(".text\n.globl _kb_call\n_kb_call:\n jmp *%rdi\n");
static unsigned long kb_selftest(unsigned char *page, unsigned long *n) {
    unsigned long bad = 0; *n = 0;
    for (size_t k = 0; k < sizeof KB_ST_FLY / sizeof KB_ST_FLY[0]; k++, ++*n) {
        const uint32_t *r = KB_ST_FLY[k]; float t[3], p[3], nice, o[3]; memcpy(t, r, 12); memcpy(p, r + 3, 12); memcpy(&nice, r + 6, 4);
        memcpy(o, t, 12); int32_t tk = knockfix_fly(t, p, nice, o); if (tk == -1) tk = 0;
        bad += memcmp(o, r + 7, 12) != 0 || (uint32_t)tk != r[10];
    }
    for (size_t k = 0; k < sizeof KB_ST_SUM / sizeof KB_ST_SUM[0]; k++, ++*n) {   // copies of the rewritten sums
        const uint32_t *r = KB_ST_SUM[k]; float x, y, z; memcpy(&x, r, 4); memcpy(&y, r + 1, 4); memcpy(&z, r + 2, 4);
        float a = kb_call(page, z, y, x, 0), b = kb_call(page + 0x40, 0, x, z, y);
        bad += memcmp(&a, r + 3, 4) != 0 || memcmp(&b, r + 3, 4) != 0;
    }
    return bad;
}
__attribute__((constructor)) static void knockfix_init(void) {
    if (getenv("DOW2_NOKNOCKFIX")) { pu_log("knockfix: DOW2_NOKNOCKFIX set - the port's own knockback sums%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
#define AT(a) ((unsigned char *)((a) + slide))
    int ok = !memcmp(AT(KB_S1_ADDR), KB_S1_WANT, KB_S1_LEN) && !memcmp(AT(KB_S2_ADDR), KB_S2_WANT, KB_S2_LEN) &&
             !memcmp(AT(KB_S3_ADDR), KB_S3_WANT, KB_S3_LEN) && !memcmp(AT(KB_S4_ADDR), KB_S4_WANT, KB_S4_LEN) &&
             !memcmp(AT(KB_D_ADDR), KB_D_WANT, KB_D_LEN) && !memcmp(AT(KB_LV_ADDR), KB_LV_WANT, KB_LV_LEN) && !memcmp(AT(KB_ND_ADDR), KB_ND_WANT, KB_ND_LEN);
    const unsigned char *F = AT(KB_FLY_ADDR);
    static const unsigned char sfput[4] = { 0xf3, 0x0f, 0x59, 0x15 };
    if (ok) ok = !memcmp(F, KB_FLY_WANT, KB_FLY_SF) && !memcmp(F + KB_FLY_SF + 8, KB_FLY_WANT + KB_FLY_SF + 8, KB_FLY_LEN - KB_FLY_SF - 8) &&
                 (!memcmp(F + KB_FLY_SF, KB_FLY_WANT + KB_FLY_SF, 8) || !memcmp(F + KB_FLY_SF, sfput, 4));
    if (!ok) { pu_log("knockfix: unexpected bytes in PlotThrow / DoKnockback / ComputeNiceDuration (another game build?) - NOT patched%.0lu%.0lu", 0, 0); return; }
    // a scratch page for the self-test copies of the sums (S1 form at +0, S2 form at +0x40, each + ret)
    vm_address_t pg = 0;
    if (vm_allocate(mach_task_self(), &pg, vm_page_size, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) { pu_log("knockfix: no scratch page - NOT patched%.0lu%.0lu", 0, 0); return; }
    unsigned char *P = (unsigned char *)pg;
    memcpy(P, KB_S134_PUT, 20); P[20] = 0xC3; memcpy(P + 0x40, KB_S2_PUT, 23); P[0x40 + 23] = 0xC3;
    vm_protect(mach_task_self(), pg, vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    unsigned long n, bad = kb_selftest(P, &n);
    vm_deallocate(mach_task_self(), pg, vm_page_size);
    if (bad) { pu_log("knockfix self-test: %lu of %lu cases differ from Windows - NOT patched, report this", bad, n); return; }
    // the knock target region: r15d = (int)nice (kept for KnockbackData), call knockfix_fly, r12d = ticks, ClampToWorld unless -1
    unsigned char f[KB_FLY_LEN]; int o = 0;
    static const unsigned char f0[] = {
        0xF3, 0x44, 0x0F, 0x2C, 0xBD, 0xA4, 0xFC, 0xFF, 0xFF,          // cvttss2si r15d, [rbp-0x35c]
        0x48, 0x8D, 0xBD, 0x98, 0xFD, 0xFF, 0xFF,                      // lea rdi, [rbp-0x268]   (target)
        0x48, 0x8D, 0xB0, 0xC0, 0x00, 0x00, 0x00,                      // lea rsi, [rax+0xc0]    (entity position)
        0x48, 0x8D, 0x95, 0x78, 0xFD, 0xFF, 0xFF,                      // lea rdx, [rbp-0x288]   (out = copy of the target)
        0xF3, 0x0F, 0x10, 0x85, 0xA4, 0xFC, 0xFF, 0xFF,                // movss xmm0, [rbp-0x35c]
        0xFF, 0x15, 0x02, 0x00, 0x00, 0x00, 0xEB, 0x08 };              // call [rip+2]; jmp +8; .quad knockfix_fly
    memcpy(f, f0, sizeof f0); o = sizeof f0; uint64_t fn = (uint64_t)(uintptr_t)knockfix_fly; memcpy(f + o, &fn, 8); o += 8;
    static const unsigned char f1[] = {
        0x45, 0x31, 0xE4,                                              // xor r12d, r12d
        0x83, 0xF8, 0xFF, 0x74, 0x0F,                                  // cmp eax, -1; je end
        0x41, 0x89, 0xC4,                                              // mov r12d, eax
        0x48, 0x8D, 0xBD, 0x78, 0xFD, 0xFF, 0xFF, 0xE8 };              // lea rdi, [rbp-0x288]; call ClampToWorld
    memcpy(f + o, f1, sizeof f1); o += sizeof f1;
    int32_t rel = (int32_t)((long long)(0x101e826b0UL + slide) - (long long)(KB_FLY_ADDR + slide + o + 4)); memcpy(f + o, &rel, 4); o += 4;
    f[o] = 0xE9; rel = KB_FLY_LEN - (o + 5); memcpy(f + o + 1, &rel, 4); o += 5;  // end: jmp 0x101bc3835
    memset(f + o, 0xCC, KB_FLY_LEN - o);
    unsigned char nd[14] = { 0xFF, 0x25, 0, 0, 0, 0 }; fn = (uint64_t)(uintptr_t)knockfix_nice; memcpy(nd + 6, &fn, 8);
    unsigned char ndsave[14]; memcpy(ndsave, AT(KB_ND_ADDR), 14);
    int done = kb_write(AT(KB_S1_ADDR), KB_S134_PUT, 20) + kb_write(AT(KB_S2_ADDR), KB_S2_PUT, 23) + kb_write(AT(KB_S3_ADDR), KB_S134_PUT, 20) +
               kb_write(AT(KB_S4_ADDR), KB_S134_PUT, 20) + kb_write(AT(KB_D_ADDR), KB_D_PUT, 9) + kb_write(AT(KB_FLY_ADDR), f, KB_FLY_LEN) +
               kb_write(AT(KB_ND_ADDR), nd, 14) + kb_write(AT(KB_LV_ADDR), KB_LV_PUT, 12);
    if (done != 8) { pu_log("knockfix: only %lu of %lu edits made (vm_protect failed)", (unsigned long)done, 8); return; }
    // the flight path through the game's own functions now: KnockbackFlightPathInfo::Compute (0x101e4d690) + the patched
    // ComputeNiceDuration, * 10 as DoKnockback does - against DOW2.exe 0x976a91..0x976b06
    unsigned long bad2 = 0;
    for (size_t k = 0; k < sizeof KB_ST_ND / sizeof KB_ST_ND[0]; k++) {
        const uint32_t *r = KB_ST_ND[k]; float y0, y1, a, info[3] = { 0, 0, 0 }; memcpy(&y0, r, 4); memcpy(&y1, r + 1, 4); memcpy(&a, r + 2, 4);
        ((void (*)(float *, float, float, float))(0x101e4d690UL + slide))(info, y0, y1, a);
        float v = ((float (*)(const float *))(0x101e4d700UL + slide))(info) * 10.0f;
        bad2 += memcmp(&v, r + 3, 4) != 0;
    }
    if (bad2) {                                                 // put ComputeNiceDuration back, keep the rest (independent)
        kb_write(AT(KB_ND_ADDR), ndsave, 14);
        pu_log("knockfix: flight duration self-test %lu of %lu differ - ComputeNiceDuration NOT patched, report this", bad2, sizeof KB_ST_ND / sizeof KB_ST_ND[0]);
    }
    if (!bad2) pu_log("knockfix: OK - 8 edits: knockback fly target / durations (PlotThrow sums and order, knock target, flight duration), levitate height as DOW2.exe, self-test %lu%.0lu",
                      n + sizeof KB_ST_ND / sizeof KB_ST_ND[0], 0);
#undef AT
}
