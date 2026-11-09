// meleefix.c - crossplay_fix v16, part 18: melee - the special attack chance, the accuracy power, the sync-kill transform.
// Why (match 1107 on v15, desync at frame 491, the first melee swing of the match): the Ork boyz' melee weapon (Ork Choppa)
// "specialattackchanceaccumulator" 0 -> Mac 0x4095cf6a (4.681569099), Windows 0x4095cf6e (4.681571007). The chance of a
// special attack is split over the attacker's squad: 100 * (1 - (1 - chance/100)^(1/n)), here chance 25 over n = 6 boyz.
// Windows (DOW2.exe 0xa2815b, x87 at 24 bits) calls msvcr80's _CIpow - its SSE2 pow on doubles - and keeps the DOUBLE result
// on the x87 stack for 1 - p; the port (0x101c8f655, in WeaponFire::ComputeWeaponFireAssessment) calls powf: the result rounded
// to a float BEFORE 1 - p, which then loses up to 8 ulp. 70.6% of chances differ (t16, 1M cases); the random draw is the same
// on both sides (randomstreamindex equal), so it is the accumulator that desyncs, at once.
// The same audit of the melee / fire assessment code (t16/w16.c = DOW2.exe's own code + the real msvcr80.dll, m16.c = the
// port) found two more of the class "Windows library / x87 rounds differently", fixed here too:
//   accuracy *= multiplier ^ (n - 1) (0x101c8efcf; DOW2.exe 0xa27d78 -> 0xa298c0): MSVC's std::pow(float, int) is _Pow_int
//     (squaring in float, one rounding per multiplication); the port: (float)pow((double)x, (double)(n - 1))  48.3% differ
//   sync kill (0x101bc9bdb, StateMelee::SyncKillState's transform helper; DOW2.exe 0x97bfa8 in 0x97bf30): local transform *
//     the entity's: MSVC sums the first row (x + z) + y, the port's vector code otherwise                          8.3% differ
// What: the three code runs are replaced in place by calls of meleefix_impl.h's transcriptions (an absolute "call [rip+2];
// jmp +8; .quad fn" inside the region, no page near the game code needed) + the register moves around them. All or nothing on
// unexpected bytes; self-test at load against DOW2.exe's results (t16/w16 st); DOW2_NOMELEEFIX=1: off.
#include "patch_util.h"
#include "meleefix_impl.h"
#include "meleefix_sites.h"
#include "meleefix_st.inc"
static int mf_write(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    vm_size_t sz = (((vm_address_t)p + len + vm_page_size - 1) & ~(vm_address_t)(vm_page_size - 1)) - page;
    if (vm_protect(mach_task_self(), page, sz, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, sz, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
// SysV entry points for the patched code (xmm0 / xmm1 / edi / rdi, rsi, rdx as the call sites set them)
__attribute__((used, noinline)) float meleefix_chance(float base, float y) { return mf_chance(base, y); }
__attribute__((used, noinline)) float meleefix_powi(float x, int32_t n) { return mf_powi(x, n); }
__attribute__((used, noinline)) void meleefix_synckill(float *m, const float *l, const float *e) { mf_synckill(m, l, e); }
static unsigned long mf_selftest(unsigned long *n) {
    unsigned long bad = 0; *n = 0;
    for (size_t k = 0; k < sizeof MF_ST_CH / sizeof MF_ST_CH[0]; k++, ++*n) {
        float b, y; memcpy(&b, &MF_ST_CH[k][0], 4); memcpy(&y, &MF_ST_CH[k][1], 4);
        float r = meleefix_chance(b, y); bad += memcmp(&r, &MF_ST_CH[k][2], 4) != 0;
    }
    for (size_t k = 0; k < sizeof MF_ST_PW / sizeof MF_ST_PW[0]; k++, ++*n) {
        float x; memcpy(&x, &MF_ST_PW[k][0], 4);
        float r = meleefix_powi(x, (int32_t)MF_ST_PW[k][1]); bad += memcmp(&r, &MF_ST_PW[k][2], 4) != 0;
    }
    for (size_t k = 0; k < sizeof MF_ST_SK / sizeof MF_ST_SK[0]; k++, ++*n) {
        float m[12]; memcpy(m, MF_ST_SK[k], 48);                 // in place, as the game calls it
        meleefix_synckill(m, m, (const float *)(MF_ST_SK[k] + 12)); bad += memcmp(m, MF_ST_SK[k] + 24, 48) != 0;
    }
    return bad;
}
static void put_call(unsigned char *c, void *fn) {         // call [rip+2]; jmp +8; .quad fn  (16 bytes)
    static const unsigned char h[8] = { 0xFF, 0x15, 0x02, 0, 0, 0, 0xEB, 0x08 }; uint64_t a = (uint64_t)(uintptr_t)fn;
    memcpy(c, h, 8); memcpy(c + 8, &a, 8);
}
static void put_nops(unsigned char *c, int n) {           // multi-byte nops
    static const unsigned char N[9][9] = { { 0 }, { 0x90 }, { 0x66, 0x90 }, { 0x0F, 0x1F, 0x00 }, { 0x0F, 0x1F, 0x40, 0 },
        { 0x0F, 0x1F, 0x44, 0, 0 }, { 0x66, 0x0F, 0x1F, 0x44, 0, 0 }, { 0x0F, 0x1F, 0x80, 0, 0, 0, 0 },
        { 0x0F, 0x1F, 0x84, 0, 0, 0, 0, 0 } };
    while (n > 0) { int k = n > 8 ? 8 : n; memcpy(c, N[k], k); c += k; n -= k; }
}
__attribute__((constructor)) static void meleefix_init(void) {
    if (getenv("DOW2_NOMELEEFIX")) { pu_log("meleefix: DOW2_NOMELEEFIX set - the port's own melee chance / accuracy power / sync-kill sums%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *A = (unsigned char *)(MF_A_ADDR + slide), *B = (unsigned char *)(MF_B_ADDR + slide), *C = (unsigned char *)(MF_C_ADDR + slide);
    if (memcmp(A, MF_A_WANT, MF_A_LEN) || memcmp(B, MF_B_WANT, MF_B_LEN) || memcmp(C, MF_C_WANT, MF_C_LEN)) {
        pu_log("meleefix: unexpected bytes at %#lx / %#lx / 0x101bc9bdb (another game build?) - NOT patched", MF_A_ADDR, MF_B_ADDR); return; }
    unsigned long n, bad = mf_selftest(&n);
    if (bad) { pu_log("meleefix self-test: %lu of %lu cases differ from Windows - NOT patched, report this", bad, n); return; }
    // A 0x101c8f655 (25): call powf; movss xmm1, 1.0; subss xmm1, xmm0; mulss xmm1, 100.0
    //   -> call meleefix_chance(xmm0 = base, xmm1 = 1/n); movaps xmm1, xmm0; nops
    unsigned char a[MF_A_LEN]; put_call(a, (void *)meleefix_chance); a[16] = 0x0F; a[17] = 0x28; a[18] = 0xC8; put_nops(a + 19, MF_A_LEN - 19);
    // B 0x101c8efcc (21): dec r13d; cvtss2sd xmm0, xmm0; cvtsi2sd xmm1, r13d; call pow; cvtsd2ss xmm0, xmm0
    //   -> lea edi, [r13 - 1]; call meleefix_powi(xmm0 = x, edi = n - 1); nop   (r13 is reloaded right after, at 0x101c8eff5)
    unsigned char b[MF_B_LEN]; b[0] = 0x41; b[1] = 0x8D; b[2] = 0x7D; b[3] = 0xFF; put_call(b + 4, (void *)meleefix_powi); put_nops(b + 20, MF_B_LEN - 20);
    // C 0x101bc9bdb (461) .. 0x101bc9da8 (call ClampToWorld with rdi = rbx + 0x24): the product, its result to [rbx]
    //   -> mov rdi, rbx; mov rsi, rbx; lea rdx, [r12 + 0x9c]; call meleefix_synckill; lea rdi, [rbx + 0x24]; jmp 0x101bc9da8
    unsigned char c[MF_C_LEN]; int o = 0;
    static const unsigned char c0[] = { 0x48, 0x89, 0xDF, 0x48, 0x89, 0xDE, 0x49, 0x8D, 0x94, 0x24, 0x9C, 0x00, 0x00, 0x00 };
    memcpy(c, c0, sizeof c0); o = sizeof c0; put_call(c + o, (void *)meleefix_synckill); o += 16;
    c[o++] = 0x48; c[o++] = 0x8D; c[o++] = 0x7B; c[o++] = 0x24;
    int32_t rel = MF_C_LEN - (o + 5); c[o++] = 0xE9; memcpy(c + o, &rel, 4); o += 4; put_nops(c + o, MF_C_LEN - o);
    int done = mf_write(A, a, MF_A_LEN) + mf_write(B, b, MF_B_LEN) + mf_write(C, c, MF_C_LEN);
    if (memcmp(A, a, MF_A_LEN) || memcmp(B, b, MF_B_LEN) || memcmp(C, c, MF_C_LEN)) done = -100;
    if (done != 3) { pu_log("meleefix: only %lu of %lu edits made (vm_protect failed)", (unsigned long)(done < 0 ? 0 : done), 3); return; }
    pu_log("meleefix: OK - 3 edits: special attack chance (1 - pow in double, as msvcr80 _CIpow), accuracy ^ n (MSVC _Pow_int), "
           "sync-kill transform (MSVC sum order) as DOW2.exe, self-test %lu%.0lu", n, 0);
}
