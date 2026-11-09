// artfix.c - crossplay_fix v24, part 29: OffMapArtilleryActionState::Update's scatter points as DOW2.exe's x87 code.
// Why (difftest step 9, calderisrefinery 06-01 High H6928 eld_plasma_grenade_autarch_pvp axis; step traces s9_fly / s9_ce*): the
// grenade is spawned by an off-map-artillery action (Autarch ability): start and target are random points around centres,
//   DOW2.exe 0x9d92da (helper 0x9d9250, x87 under the game's PC_24):  x = fcos(a) * r + c.x;  z = fsin(a) * r + c.z
//     - fcos / fsin of the float angle in EXTENDED precision, then ONE rounding of the product to 24 bits
//   Mac 0x101daff2f.. (inlined twice):  x = cosf(a) [rounded to float first] * r + c.x  (SSE) - double rounding, 1 ulp apart in
//     1.2 % of the cases (t24 1M); the grenade's start z 40e85282 / 40e85281 -> its heading z (20 ulp) -> every later step.
// What: 4 spans (29 bytes each: movss a; call cosf|sinf; mulss r; addss c) jump to x87 stubs doing DOW2.exe's instruction sequence
// under a temporary control word PC_24 / round-to-nearest (fnstcw / fldcw around it), result in xmm0, back to the store. Self-test
// at load: AF_NST cases from DOW2.exe (t24/w24 artst: its own bytes run under PC_24) through the PATCHED spans in place (fake frame
// in rbp, temporary landing jumps after each span). DOW2_NOARTFIX=1: off.
#include "patch_util.h"
#include "artfix_st.inc"
static const uint64_t AF_AT[4] = { 0x101daff2fUL, 0x101daff65UL, 0x101db005bUL, 0x101db0091UL };   // cos, sin (start), cos, sin (target)
static const uint64_t AF_FN[4] = { 0x1009333a0UL, 0x1009312a0UL, 0x1009333a0UL, 0x1009312a0UL };   // call targets: cosf, sinf
// movss xmm0,[rbp-0x3c0]; call rel32; mulss xmm0,[rbp-0x3c8]; addss xmm0,[rbp-0x3cc]
static const unsigned char AF_HEAD[8] = { 0xf3,0x0f,0x10,0x85,0x40,0xfc,0xff,0xff };
static const unsigned char AF_TAIL[16] = { 0xf3,0x0f,0x59,0x85,0x38,0xfc,0xff,0xff, 0xf3,0x0f,0x58,0x85,0x34,0xfc,0xff,0xff };
uint64_t af_back[4];
void af_s0(void), af_s1(void), af_s2(void), af_s3(void);
// entry: push the way back, common part: rax/flags are dead here (the original span calls cosf / sinf)
__asm__(".text\n.globl _af_s0\n_af_s0:\n pushq _af_back(%rip)\n jmp af_cos\n"
        ".globl _af_s1\n_af_s1:\n pushq _af_back+8(%rip)\n jmp af_sin\n"
        ".globl _af_s2\n_af_s2:\n pushq _af_back+16(%rip)\n jmp af_cos\n"
        ".globl _af_s3\n_af_s3:\n pushq _af_back+24(%rip)\n jmp af_sin\n"
        "af_cos:\n sub $16, %rsp\n fnstcw 8(%rsp)\n movzwl 8(%rsp), %eax\n andl $0xf0ff, %eax\n movw %ax, 10(%rsp)\n fldcw 10(%rsp)\n"
        " flds -0x3c0(%rbp)\n fcos\n fmuls -0x3c8(%rbp)\n fadds -0x3cc(%rbp)\n fstps 12(%rsp)\n fldcw 8(%rsp)\n movss 12(%rsp), %xmm0\n add $16, %rsp\n ret\n"
        "af_sin:\n sub $16, %rsp\n fnstcw 8(%rsp)\n movzwl 8(%rsp), %eax\n andl $0xf0ff, %eax\n movw %ax, 10(%rsp)\n fldcw 10(%rsp)\n"
        " flds -0x3c0(%rbp)\n fsin\n fmuls -0x3c8(%rbp)\n fadds -0x3cc(%rbp)\n fstps 12(%rsp)\n fldcw 8(%rsp)\n movss 12(%rsp), %xmm0\n add $16, %rsp\n ret\n");
static void (*const AF_STUB[4])(void) = { af_s0, af_s1, af_s2, af_s3 };
// af_run(rbp, entry): rbp = fake frame, jump into the span; the landing jump after it comes back to af_land with xmm0
uint64_t af_saved_rsp;
float af_run(char *rbpv, uint64_t entry);
void af_land(void);
__asm__(".text\n.globl _af_run\n_af_run:\n push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " mov %rsp, _af_saved_rsp(%rip)\n sub $0x108, %rsp\n mov %rdi, %rbp\n jmp *%rsi\n"
        ".globl _af_land\n_af_land:\n mov _af_saved_rsp(%rip), %rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n");
static int af_wr(unsigned char *p, const void *b, size_t n) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, n); vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE); return 1;
}
static int af_site_ok(const unsigned char *p, uint64_t at, uint64_t fn) {
    return !memcmp(p, AF_HEAD, 8) && p[8] == 0xe8 && at + 13 + *(const int32_t *)(p + 9) == fn && !memcmp(p + 13, AF_TAIL, 16);
}
static char af_fr[0x1000];
float artfix_test_case(int site, const uint32_t *c) {     // t24 (landing put by the harness): site 0 cos with cx, 1 sin with cz
    char *rb = af_fr + 0x800; memcpy(rb - 0x3c0, c, 4); memcpy(rb - 0x3c8, c + 1, 4); memcpy(rb - 0x3cc, c + (site & 1 ? 3 : 2), 4);
    return af_run(rb, AF_AT[site]);
}
static unsigned long af_selftest(uintptr_t slide, int *ok) {
    unsigned long bad = 0; unsigned char sv[4][14]; *ok = 0;
    for (int s = 0; s < 4; s++) memcpy(sv[s], (unsigned char *)(AF_AT[s] + 29 + slide), 14);
    for (int s = 0; s < 4; s++) if (!pu_jump((unsigned char *)(AF_AT[s] + 29 + slide), (void *)af_land)) {
        for (int t = 0; t < 4; t++) af_wr((unsigned char *)(AF_AT[t] + 29 + slide), sv[t], 14); return 0; }
    char *rb = af_fr + 0x800;
    for (unsigned long q = 0; q < AF_NST; q++) for (int s = 0; s < 4; s++) {
        memcpy(rb - 0x3c0, AF_ST[q], 4); memcpy(rb - 0x3c8, AF_ST[q] + 1, 4); memcpy(rb - 0x3cc, AF_ST[q] + (s & 1 ? 3 : 2), 4);
        float v = af_run(rb, AF_AT[s] + slide); uint32_t vb; memcpy(&vb, &v, 4); if (vb != AF_ST[q][4 + (s & 1)]) bad++;
    }
    for (int s = 0; s < 4; s++) if (!af_wr((unsigned char *)(AF_AT[s] + 29 + slide), sv[s], 14)) return bad + 1;
    *ok = 1; return bad;
}
__attribute__((constructor)) static void artfix_init(void) {
    if (getenv("DOW2_NOARTFIX")) { pu_log("artfix: DOW2_NOARTFIX set - the port's own artillery scatter points (cosf / sinf rounded first)%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0); unsigned char old[4][29];
    for (int s = 0; s < 4; s++) { unsigned char *p = (unsigned char *)(AF_AT[s] + slide);
        if (!af_site_ok(p, AF_AT[s] + slide, AF_FN[s] + slide)) { pu_log("artfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", AF_AT[s], 0); return; }
        memcpy(old[s], p, 29); af_back[s] = AF_AT[s] + 29 + slide; }
    for (int s = 0; s < 4; s++) if (!pu_jump((unsigned char *)(AF_AT[s] + slide), (void *)AF_STUB[s])) {
        for (int t = 0; t < 4; t++) af_wr((unsigned char *)(AF_AT[t] + slide), old[t], 29); pu_log("artfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    int ok; unsigned long bad = af_selftest(slide, &ok);
    if (!ok || bad) { for (int t = 0; t < 4; t++) af_wr((unsigned char *)(AF_AT[t] + slide), old[t], 29);
        pu_log(ok ? "artfix self-test: %lu of %lu differ from DOW2.exe - NOT patched" : "artfix self-test could not run (%lu/%lu) - NOT patched", bad, AF_NST * 4); return; }
    pu_log("artfix: OK - OffMapArtilleryActionState scatter points cos/sin(a) * r + c as DOW2.exe x87 (PC_24, one rounding), 4 spans, self-test %lu (patched code run)%.0lu", AF_NST * 4, 0);
}
