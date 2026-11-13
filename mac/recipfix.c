// recipfix.c - crossplay_fix v23, part 27: WeaponBag::PostLoadValidate scales by a reciprocal like DOW2.exe.
// Why (difftest step 8, lohmistral H5719 ammocase health 2 ulp, 48787 H7342 ammocase health 2 ulp, the health class of 4 of 8
// replays): two-sided single-step trace of the fire assessment (macrun MR_STEP / DiffTrace DT_STEP): the first damage already
// differs (8.333333 41055555 Mac / 41055556 Windows) because GetModifiedValue's base, WeaponBag damage_hp, differs. The attribute
// binary holds the same value for both; PostLoadValidate normalises it:
//   Mac 0x101c876ed   d = x > 0 ? x * 10 : 1;  hp = hp / d;            courage = courage / d
//   DOW2.exe 0x821f8b r = 1.0f / d;            hp = r * hp;            courage = courage * r      (25 * (1/3) = 41055556)
// and the same function's weight pair (0x101c87916 / 0x82216e): s = a + b; if s > 1: Mac a / s, b / s; DOW2.exe r = 1/s, r*a, r*b.
// What: both spans jump to stubs here doing DOW2.exe's arithmetic (SSE scalar on both sides, so bit-exact by construction), back
// to 0x101c87713 / 0x101c87932. Self-test at load: RF_NST cases from DOW2.exe (t23/w23 bagst) through the PATCHED code in place
// (entered at 0x101c876ad / 0x101c878f2 with a fake bag in r12, temporary landing jumps on 0x101c87713 / 0x101c87940).
// DOW2_NORECIPFIX=1: off.
// v24 (difftest step 9, same-divisor scan of the Mac sim, o23/mac_samediv_sites.txt): site 3 = DamageAction::PostLoadValidate
// (Mac 0x101da0038 / DOW2.exe 0x94a38b): s = a + b; if s > 1: Mac a / s, b / s; DOW2.exe r = 1/s, r*a, r*b; then b += a. Same stub
// as site 2 (rbx = action, +0x38 / +0x3c), back to 0x101da004a. Self-test RF_NST3 cases from DOW2.exe (t24/w24 dactst) through the
// patched code (entered at 0x101da001e with a fake action in rbx, temporary landing on 0x101da004e). t24 1M side by side.
#include "patch_util.h"
#include "recipfix_st.inc"
#include "recipfix_st3.inc"
#define RF_S1 0x101c876edUL                 // divss xmm0,xmm2 .. movss [r12+0x8c],xmm0   (38 bytes, ends 0x101c87713)
#define RF_S2 0x101c87916UL                 // divss xmm1,xmm2 .. movss [r12+0x338],xmm0  (28 bytes, ends 0x101c87932)
static const unsigned char RF_OLD1[38] = { 0xf3,0x0f,0x5e,0xc2, 0xf3,0x41,0x0f,0x11,0x84,0x24,0x88,0x00,0x00,0x00,
    0xf3,0x41,0x0f,0x10,0x84,0x24,0x8c,0x00,0x00,0x00, 0xf3,0x0f,0x5e,0xc2, 0xf3,0x41,0x0f,0x11,0x84,0x24,0x8c,0x00,0x00,0x00 };
static const unsigned char RF_OLD2[28] = { 0xf3,0x0f,0x5e,0xca, 0xf3,0x41,0x0f,0x11,0x8c,0x24,0x34,0x03,0x00,0x00,
    0xf3,0x0f,0x5e,0xc2, 0xf3,0x41,0x0f,0x11,0x84,0x24,0x38,0x03,0x00,0x00 };
#define RF_S3 0x101da0038UL                 // divss xmm1,xmm2 .. movss [rbx+0x3c],xmm0   (18 bytes, ends 0x101da004a)
static const unsigned char RF_OLD3[18] = { 0xf3,0x0f,0x5e,0xca, 0xf3,0x0f,0x11,0x4b,0x38, 0xf3,0x0f,0x5e,0xc2, 0xf3,0x0f,0x11,0x43,0x3c };
static const unsigned char RF_LAND3_OLD[14] = { 0xf3,0x0f,0x11,0x43,0x3c, 0xb0,0x01, 0x5b, 0x41,0x5e, 0x5d, 0xc3, 0x66,0x0f };
uint64_t rf_back1, rf_back2, rf_back3; const float rf_one = 1.0f;
void rf_stub1(void), rf_stub2(void), rf_stub3(void);
// stub1: xmm2 = d, r12 = bag; r = 1/d; hp = hp * r; courage = courage * r  (xmm0, xmm3 scratch: a call follows at 0x101c87713)
// stub2: xmm1 = a, xmm0 = b, xmm2 = s; r = 1/s; a *= r; b *= r; stored; 0x101c87932 then does b += a  (xmm3 scratch: dead after)
__asm__(".text\n.globl _rf_stub1\n_rf_stub1:\n movss _rf_one(%rip), %xmm3\n divss %xmm2, %xmm3\n"
        " movss 0x88(%r12), %xmm0\n mulss %xmm3, %xmm0\n movss %xmm0, 0x88(%r12)\n"
        " movss 0x8c(%r12), %xmm0\n mulss %xmm3, %xmm0\n movss %xmm0, 0x8c(%r12)\n jmp *_rf_back1(%rip)\n"
        ".globl _rf_stub2\n_rf_stub2:\n movss _rf_one(%rip), %xmm3\n divss %xmm2, %xmm3\n mulss %xmm3, %xmm1\n mulss %xmm3, %xmm0\n"
        " movss %xmm1, 0x334(%r12)\n movss %xmm0, 0x338(%r12)\n jmp *_rf_back2(%rip)\n"
        ".globl _rf_stub3\n_rf_stub3:\n movss _rf_one(%rip), %xmm3\n divss %xmm2, %xmm3\n mulss %xmm3, %xmm1\n mulss %xmm3, %xmm0\n"
        " movss %xmm1, 0x38(%rbx)\n movss %xmm0, 0x3c(%rbx)\n jmp *_rf_back3(%rip)\n");
// stub3: DamageAction (rbx), xmm1 = a, xmm0 = b, xmm2 = s; as stub2; 0x101da004a then does b += a (xmm3 dead: ret follows)
// rf_run(bag, entry): r12 = bag, jump into the game code; a landing jump brings it back to rf_land
uint64_t rf_saved_rsp;
void rf_run(void *bag, uint64_t entry);
void rf_land(void);
__asm__(".text\n.globl _rf_run\n_rf_run:\n push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " mov %rsp, _rf_saved_rsp(%rip)\n sub $0x108, %rsp\n lea 0xc0(%rsp), %rbp\n mov %rdi, %r12\n jmp *%rsi\n"
        ".globl _rf_land\n_rf_land:\n mov _rf_saved_rsp(%rip), %rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n");
static char rf_bag[0x400];
// rf_run3(act, entry): rbx = act; landing rf_land3 (on 0x101da004e) does that address' store movss [rbx+0x3c], xmm0 itself
void rf_run3(void *act, uint64_t entry);
void rf_land3(void);
__asm__(".text\n.globl _rf_run3\n_rf_run3:\n push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " mov %rsp, _rf_saved_rsp(%rip)\n sub $0x108, %rsp\n lea 0xc0(%rsp), %rbp\n mov %rdi, %rbx\n jmp *%rsi\n"
        ".globl _rf_land3\n_rf_land3:\n movss %xmm0, 0x3c(%rbx)\n mov _rf_saved_rsp(%rip), %rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n");
static int rf_wr(unsigned char *p, const void *b, size_t n) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, n); vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE); return 1;
}
// one case through the game code (Mac layout: flag +0x1c0, x +0x1d8, hp +0x88, courage +0x8c, weights +0x334 +0x338); out 4 dwords
static void rf_case(const uint32_t *c, uintptr_t slide, uint32_t *o) {
    memset(rf_bag, 0, sizeof rf_bag); rf_bag[0x1c0] = (char)c[0]; memcpy(rf_bag + 0x1d8, c + 1, 4); memcpy(rf_bag + 0x88, c + 2, 8);
    memcpy(rf_bag + 0x334, c + 4, 8);
    rf_run(rf_bag, 0x101c876adUL + slide); rf_run(rf_bag, 0x101c878f2UL + slide);
    memcpy(o, rf_bag + 0x88, 8); memcpy(o + 2, rf_bag + 0x334, 8);
}
unsigned long recipfix_test_case(const uint32_t *c, uint32_t *o) { rf_case(c, 0, o); return 0; }   // t23 (landings put by the harness)
static char rf_act[0x100];
static void rf_case3(const uint32_t *c, uintptr_t slide, uint32_t *o) {   // Mac layout a +0x38, b +0x3c
    memset(rf_act, 0, sizeof rf_act); memcpy(rf_act + 0x38, c, 8); rf_run3(rf_act, 0x101da001eUL + slide); memcpy(o, rf_act + 0x38, 8);
}
unsigned long recipfix_test_case3(const uint32_t *c, uint32_t *o) { rf_case3(c, 0, o); return 0; }   // t24 (landing put by the harness)
static unsigned long rf_selftest3(uintptr_t slide, int *ok) {
    unsigned char *l3 = (unsigned char *)(0x101da004eUL + slide); unsigned long bad = 0; *ok = 0;
    if (memcmp(l3, RF_LAND3_OLD, 14) || !pu_jump(l3, (void *)rf_land3)) return 0;
    for (unsigned long q = 0; q < RF_NST3; q++) { uint32_t o[2]; rf_case3(RF_ST3[q], slide, o); if (memcmp(o, RF_ST3[q] + 2, 8)) bad++; }
    if (!rf_wr(l3, RF_LAND3_OLD, 14)) return bad + 1;
    *ok = 1; return bad;
}
static unsigned long rf_selftest(uintptr_t slide, int *ok) {
    unsigned char *l1 = (unsigned char *)(0x101c87713UL + slide), *l2 = (unsigned char *)(0x101c87940UL + slide), s1[14], s2[14];
    unsigned long bad = 0; *ok = 0; memcpy(s1, l1, 14); memcpy(s2, l2, 14);
    if (!pu_jump(l1, (void *)rf_land) || !pu_jump(l2, (void *)rf_land)) { rf_wr(l1, s1, 14); rf_wr(l2, s2, 14); return 0; }
    for (unsigned long q = 0; q < RF_NST; q++) { uint32_t o[4]; rf_case(RF_ST[q], slide, o); if (memcmp(o, RF_ST[q] + 6, 16)) bad++; }
    if (!rf_wr(l1, s1, 14) || !rf_wr(l2, s2, 14)) return bad + 1;
    *ok = 1; return bad;
}
__attribute__((constructor)) static void recipfix_init(void) {
    if (getenv("DOW2_NORECIPFIX")) { pu_log("recipfix: DOW2_NORECIPFIX set - the port's own PostLoadValidate divisions%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p1 = (unsigned char *)(RF_S1 + slide), *p2 = (unsigned char *)(RF_S2 + slide);
    if (memcmp(p1, RF_OLD1, sizeof RF_OLD1) || memcmp(p2, RF_OLD2, sizeof RF_OLD2)) { pu_log("recipfix: unexpected bytes at %#lx / %#lx (another game build?) - NOT patched", RF_S1, RF_S2); return; }
    rf_back1 = 0x101c87713UL + slide; rf_back2 = 0x101c87932UL + slide;
    if (!pu_jump(p1, (void *)rf_stub1) || !pu_jump(p2, (void *)rf_stub2)) { rf_wr(p1, RF_OLD1, 38); rf_wr(p2, RF_OLD2, 28); pu_log("recipfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    int ok; unsigned long bad = rf_selftest(slide, &ok);
    if (!ok || bad) { rf_wr(p1, RF_OLD1, 38); rf_wr(p2, RF_OLD2, 28);
        pu_log(ok ? "recipfix self-test: %lu of %lu differ from DOW2.exe - NOT patched" : "recipfix self-test could not run (%lu/%lu) - NOT patched", bad, RF_NST); return; }
    unsigned char *p3 = (unsigned char *)(RF_S3 + slide); rf_back3 = 0x101da004aUL + slide;
    if (memcmp(p3, RF_OLD3, sizeof RF_OLD3)) { pu_log("recipfix: OK - WeaponBag::PostLoadValidate scaling by 1/d as DOW2.exe, self-test %lu; DamageAction site: unexpected bytes at %#lx - not patched", RF_NST, RF_S3); return; }
    if (!pu_jump(p3, (void *)rf_stub3)) { rf_wr(p3, RF_OLD3, 18); pu_log("recipfix: OK - WeaponBag sites (self-test %lu); DamageAction site: vm_protect failed - not patched%.0lu", RF_NST, 0); return; }
    unsigned long bad3 = rf_selftest3(slide, &ok);
    if (!ok || bad3) { rf_wr(p3, RF_OLD3, 18); pu_log("recipfix: OK - WeaponBag sites (self-test %lu); DamageAction self-test: %lu differ (or could not run) - DamageAction not patched", RF_NST, bad3); return; }
    pu_log("recipfix: OK - WeaponBag::PostLoadValidate damage / weights and DamageAction::PostLoadValidate weights scaled by 1/x as DOW2.exe, self-test %lu + %lu (patched code run)", RF_NST, RF_NST3);
}
