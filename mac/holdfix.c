// holdfix.c - crossplay_fix v22, part 26: the combat-hold slot priority of an entity (CombatSlotManager::OnAddEntity) in
// DOW2.exe's product order.
// Why (difftest step 7, replay calderis_outskirts 05-30 H1072: sm_scout_marine 31319 holdableext priority Mac 41500000 (13.0),
// Windows 41500001): the watch (macrun MR_WATCH kind x on HoldableExt +0x34) shows the value written by PlaceInCombatSlot from
// PlaceEntities' request list, whose priority OnAddEntity computes per weapon: t = mult(armour) * damage; if the weapon has a
// scale flag: Mac 0x101ceed70  t = t * (scale * 10.0f),   DOW2.exe 0xa586ed  t = (scale * t) * 10.0f;   max over the weapons.
// What: 20 bytes at 0x101ceed70 -> mulss xmm0, [rax+0x1d8]; mulss xmm0, [rip -> the same 10.0f]; nop4  (= (t * scale) * 10).
// Self-test at load: HP_NST cases from DOW2.exe 0xa58650 (t22/w22 priost) run through the PATCHED machine code itself (the loop
// 0x101ceed13..0x101ceeda0 entered with fake weapons, a landing jump put on 0x101ceeda0 for the test and removed after).
// DOW2_NOHOLDFIX=1: off.
#include "patch_util.h"
#include "holdfix_st.inc"
static const unsigned char HF_OLD[20] = { 0xf3,0x0f,0x10,0x88,0xd8,0x01,0x00,0x00, 0xf3,0x0f,0x59,0x0d,0xf4,0x41,0x77,0x00, 0xf3,0x0f,0x59,0xc1 };
static const unsigned char HF_NEW[20] = { 0xf3,0x0f,0x59,0x80,0xd8,0x01,0x00,0x00, 0xf3,0x0f,0x59,0x05,0xf4,0x41,0x77,0x00, 0x0f,0x1f,0x40,0x00 };
// v23 (difftest step 8, 48787 H7689 csm_havoc holdableext priority 42480001 / 42480000, outskirts 05-24 H9690): the same loop is
// inlined a second time in CombatSlotManager::OnModEvent (0x101cf1740, weapons changed by upgrades/modifiers; DOW2.exe calls the
// one function 0xa58650 from both) - same 20 bytes except the rel32 of the 10.0f constant; patched the same way (+ constant check).
static const unsigned char HF_OLD2[20] = { 0xf3,0x0f,0x10,0x88,0xd8,0x01,0x00,0x00, 0xf3,0x0f,0x59,0x0d,0x24,0x18,0x77,0x00, 0xf3,0x0f,0x59,0xc1 };
static const unsigned char HF_NEW2[20] = { 0xf3,0x0f,0x59,0x80,0xd8,0x01,0x00,0x00, 0xf3,0x0f,0x59,0x05,0x24,0x18,0x77,0x00, 0x0f,0x1f,0x40,0x00 };
static const unsigned char HF_LAND_OLD[14] = { 0x4d,0x89,0xe7, 0x4c,0x89,0x7d,0xa8, 0x89,0x5d,0xb0, 0xf3,0x0f,0x11,0x45 };
// hf_run(cext, armour, entry): enter the loop at entry (0x101ceed13: rdi = combat extension, ebx = armour, [rbp-0x60] scratch);
// the landing jump at 0x101ceeda0 comes back to hf_land with the priority in xmm0.
uint64_t hf_saved_rsp;
float hf_run(void *cext, uint32_t armour, uint64_t entry);
void hf_land(void);
__asm__(".text\n.globl _hf_run\n_hf_run:\n push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " mov %rsp, _hf_saved_rsp(%rip)\n mov %esi, %ebx\n sub $0x108, %rsp\n lea 0xc0(%rsp), %rbp\n jmp *%rdx\n"
        ".globl _hf_land\n_hf_land:\n mov _hf_saved_rsp(%rip), %rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n");
// fake objects in the Mac layout: combat ext +0x110 vector<Weapon*>; weapon +0x88 info; info +0x58 family bag (+0x10 damage bag:
// +8/+0x10 vector<float> armour multipliers), +0x88 damage, +0x1c0 scale flag, +0x1d8 scale
static char hf_ext[0x200], hf_w[4][0x100], hf_wi[4][0x200], hf_fb[4][0x40], hf_db[4][0x40]; static float hf_mv[4][8]; static void *hf_vec[4];
static float hf_case(const uint32_t *c, uintptr_t slide) {
    int nw = (int)c[1];
    for (int i = 0; i < nw; i++) {
        const uint32_t *w = c + 3 + 13 * i;
        hf_vec[i] = w[0] ? 0 : hf_w[i]; *(void **)(hf_w[i] + 0x88) = hf_wi[i]; *(void **)(hf_wi[i] + 0x58) = hf_fb[i]; *(void **)(hf_fb[i] + 0x10) = hf_db[i];
        memcpy(hf_mv[i], w + 5, 32); *(float **)(hf_db[i] + 8) = hf_mv[i]; *(float **)(hf_db[i] + 0x10) = hf_mv[i] + w[4];
        hf_wi[i][0x1c0] = (char)w[1]; memcpy(hf_wi[i] + 0x88, w + 2, 4); memcpy(hf_wi[i] + 0x1d8, w + 3, 4);
    }
    *(void ***)(hf_ext + 0x110) = hf_vec; *(void ***)(hf_ext + 0x118) = hf_vec + nw;
    return hf_run(hf_ext, c[0], 0x101ceed13UL + slide);
}
static unsigned long hf_selftest(uintptr_t slide, int *ok) {
    unsigned char *land = (unsigned char *)(0x101ceeda0UL + slide); unsigned long bad = 0; *ok = 0;
    if (memcmp(land, HF_LAND_OLD, sizeof HF_LAND_OLD)) return 0;
    if (!pu_jump(land, (void *)hf_land)) return 0;
    for (unsigned long q = 0; q < HP_NST; q++) { float r = hf_case(HP_ST[q], slide); uint32_t rb; memcpy(&rb, &r, 4); if (rb != HP_ST[q][2]) bad++; }
    vm_address_t page = (vm_address_t)land & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return bad + 1;
    memcpy(land, HF_LAND_OLD, sizeof HF_LAND_OLD); vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    *ok = 1; return bad;
}
static int hf_write(unsigned char *p, const unsigned char *b) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, 20); vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE); return 1;
}
unsigned long holdfix_test_case(const uint32_t *c) { float r = hf_case(c, 0); uint32_t rb; memcpy(&rb, &r, 4); return rb; }   // t22 (landing put by the harness)
__attribute__((constructor)) static void holdfix_init(void) {
    if (getenv("DOW2_NOHOLDFIX")) { pu_log("holdfix: DOW2_NOHOLDFIX set - the port's own combat slot priority order%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(0x101ceed70UL + slide);
    if (memcmp(p, HF_OLD, sizeof HF_OLD)) { pu_log("holdfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", 0x101ceed70UL, 0); return; }
    if (!hf_write(p, HF_NEW)) { pu_log("holdfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    int ok; unsigned long bad = hf_selftest(slide, &ok);
    if (!ok || bad) { hf_write(p, HF_OLD); pu_log(ok ? "holdfix self-test: %lu of %lu differ from DOW2.exe - NOT patched" : "holdfix self-test could not run (%lu/%lu) - NOT patched", bad, HP_NST); return; }
    unsigned char *p2 = (unsigned char *)(0x101cf1740UL + slide); float k1, k2;
    memcpy(&k1, (char *)(0x101ceed80UL + slide) + *(int32_t *)(p + 12), 4); memcpy(&k2, (char *)(0x101cf1750UL + slide) + *(int32_t *)(HF_OLD2 + 12), 4);
    if (memcmp(p2, HF_OLD2, sizeof HF_OLD2) || k1 != 10.0f || k2 != 10.0f) { pu_log("holdfix: OnModEvent copy at %#lx unexpected (another game build?) - only OnAddEntity patched%.0lu", 0x101cf1740UL, 0); }
    else if (!hf_write(p2, HF_NEW2)) pu_log("holdfix: vm_protect failed at %#lx - only OnAddEntity patched%.0lu", 0x101cf1740UL, 0);
    else { pu_log("holdfix: OK - CombatSlotManager::OnAddEntity + OnModEvent weapon priority (scale * t) * 10 as DOW2.exe, self-test %lu (patched code run)%.0lu", HP_NST, 0); return; }
    pu_log("holdfix: OK - CombatSlotManager::OnAddEntity weapon priority (scale * t) * 10 as DOW2.exe, self-test %lu (patched code run)%.0lu", HP_NST, 0);
}
