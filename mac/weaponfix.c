// weaponfix.c - crossplay_fix v14, part 16: the weapon state machine's counters as Windows keeps them.
// Why (match_1103_v13, desync at frame 3393, an Ork shoota boy's weapon 713): after a burst, ShootingWeapon::
// UpdateState_WS_WindingDown counts the burst against the clip: Windows (DOW2.exe 0xa4b9f0) does
// n = max(n - 1, 0) on a signed int, the port (0x101c7c889) a plain "dec dword [rbx+0x218]" on an unsigned one. A weapon
// that enters a burst with "bursts until reload" = 0 (a fresh weapon after an instant setup, or a reload cut short by
// hold-fire) reloads on both sides, but Windows keeps 0 and the port wraps to 4294967295: the sync dump differs at once and
// from then on the port's weapon never reloads again (EnterPostFireState reloads only at n <= 1).
// The same audit (t14/w14.c vs m14.c: every place that writes burstsuntilreload, reloadendtime, cooldownendtime,
// winddownendtime, burstticks, burstfireremainder, roundstofire or the weapon state - ShootingWeapon (infantry, vehicle and
// turret weapons, setup teams, garrisons: one class) and MeleeWeapon, the blueprint loader - run side by side with Windows'
// code) found two more signed/unsigned differences of the same kind, fixed here too:
//   UpdateState_WS_WindingUp 0x101c7d2d5: burst ticks = max(ticks, 1): Windows signed (cmp/jg), port unsigned cmova
//   UpdateState_WS_Firing    0x101c7d566: manual fire: shots = min(shots, rounds to fire): Windows signed cmovl, port cmovb
// Every float -> int conversion there is a truncation on both sides (cvttss2si / fistp with RC=chop / ftol2) and equal.
// What: the WindingDown decrement jumps to a 6-instruction stub in a page near the game code (n - 1, 0 if <= 0), the two
// cmovs become their signed forms in place. All or nothing on unexpected bytes; self-test at load: the stub and the two
// patched instruction runs are executed on cases whose results were taken from DOW2.exe's own code; DOW2_NOWEAPONFIX=1: off.
#include "patch_util.h"
static const struct { uint64_t addr; int len; unsigned char want[16], put[16]; } WF_SITES[] = {
    { 0x101c7d2d5, 3, { 0x0f, 0x47, 0xc8 }, { 0x0f, 0x4f, 0xc8 } },           // cmova ecx, eax -> cmovg ecx, eax
    { 0x101c7d566, 3, { 0x0f, 0x42, 0xde }, { 0x0f, 0x4c, 0xde } },           // cmovb ebx, esi -> cmovl ebx, esi
};
#define WF_DEC 0x101c7c889UL                                                  // dec dword [rbx+0x218] (6 bytes)
static const unsigned char WF_DEC_WANT[6] = { 0xff, 0x8b, 0x18, 0x02, 0x00, 0x00 };
static const unsigned char WF_STUB[] = {
    0x8b, 0x83, 0x18, 0x02, 0x00, 0x00,     // mov eax, [rbx+0x218]
    0xff, 0xc8, 0x85, 0xc0,                 // dec eax ; test eax, eax
    0x7f, 0x02, 0x31, 0xc0,                 // jg 1f ; xor eax, eax
    0x89, 0x83, 0x18, 0x02, 0x00, 0x00,     // 1: mov [rbx+0x218], eax
    0xff, 0x25, 0, 0, 0, 0 };               // jmp [rip+0] ; .quad back (eax and the flags are dead there)
// self-test cases from DOW2.exe (t14/w14 st): WindingDown n -> n'; WindingUp GetRandRange value -> burst ticks;
// Firing (shots, rounds to fire) -> (shots, rounds left)
static const uint32_t ST_DOWN[][2] = {
    { 0x00000000, 0x00000000 }, { 0x00000001, 0x00000000 }, { 0x00000002, 0x00000001 }, { 0x00000003, 0x00000002 },
    { 0x00000007, 0x00000006 }, { 0x7fffffff, 0x7ffffffe }, { 0x80000000, 0x7fffffff }, { 0x80000001, 0x00000000 },
    { 0xfffffffe, 0x00000000 }, { 0xffffffff, 0x00000000 } };
static const uint32_t ST_UP[][2] = {
    { 0x00000000, 0x00000001 }, { 0x3f800000, 0x00000001 }, { 0x3fc00000, 0x00000001 }, { 0x40400000, 0x00000003 },
    { 0x41f00000, 0x0000001e }, { 0xbf800000, 0x00000001 }, { 0xc2c80000, 0x00000001 }, { 0x4f000000, 0x00000001 },
    { 0x4f800000, 0x00000001 }, { 0xcf000000, 0x00000001 } };
static const uint32_t ST_MAN[][4] = {
    { 0x00000003, 0x00000005, 0x00000003, 0x00000002 }, { 0x00000005, 0x00000003, 0x00000003, 0x00000000 },
    { 0x00000000, 0x00000004, 0x00000000, 0x00000004 }, { 0x00000004, 0x00000000, 0x00000000, 0x00000000 },
    { 0xffffffff, 0x00000002, 0xffffffff, 0x00000003 }, { 0x00000002, 0xffffffff, 0xffffffff, 0x00000000 },
    { 0x80000000, 0x00000001, 0x80000000, 0x80000001 }, { 0x00000007, 0x00000007, 0x00000007, 0x00000000 } };
// wf_call(code, rbx, r15, eax, xmm0): run a code copy with those registers; returns rax:rdx
struct wf_rd { uint64_t rax, rdx; };
struct wf_rd wf_call(const void *code, uint64_t rbx, uint64_t r15, uint32_t eax, float x0);
void wf_ret(void);
__asm__(".text\n.globl _wf_call\n_wf_call:\n push %rbp\n mov %rsp, %rbp\n push %rbx\n push %r15\n"
        " mov %rsi, %rbx\n mov %rdx, %r15\n mov %ecx, %eax\n xor %edx, %edx\n call *%rdi\n pop %r15\n pop %rbx\n pop %rbp\n ret\n"
        ".globl _wf_ret\n_wf_ret:\n ret\n");
static int wf_write(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
// the near page: 0x000 the stub (back into the game), 0x080 its test copy (back to wf_ret), 0x100 / 0x180 copies of the two
// patched instruction runs (game bytes with the edit) + "return the registers"
static unsigned long wf_selftest(unsigned char *near, unsigned long *n) {
    unsigned long bad = 0; static uint32_t o[0x90];
    for (size_t k = 0; k < sizeof ST_DOWN / sizeof ST_DOWN[0]; k++) {
        o[0x218 / 4] = ST_DOWN[k][0]; wf_call(near + 0x80, (uint64_t)(uintptr_t)o, 0, 0x12345678, 0);
        bad += o[0x218 / 4] != ST_DOWN[k][1];
    }
    for (size_t k = 0; k < sizeof ST_UP / sizeof ST_UP[0]; k++) {
        float r; memcpy(&r, &ST_UP[k][0], 4);
        struct wf_rd v = wf_call(near + 0x100, 0, 0, 0, r); bad += (uint32_t)v.rax != ST_UP[k][1];
    }
    for (size_t k = 0; k < sizeof ST_MAN / sizeof ST_MAN[0]; k++) {
        o[0x140 / 4] = ST_MAN[k][1];
        struct wf_rd v = wf_call(near + 0x180, ST_MAN[k][0], (uint64_t)(uintptr_t)o, 0, 0);
        bad += (uint32_t)v.rax != ST_MAN[k][2] || (uint32_t)v.rdx != ST_MAN[k][3];
    }
    *n = sizeof ST_DOWN / sizeof ST_DOWN[0] + sizeof ST_UP / sizeof ST_UP[0] + sizeof ST_MAN / sizeof ST_MAN[0];
    return bad;
}
__attribute__((constructor)) static void weaponfix_init(void) {
    if (getenv("DOW2_NOWEAPONFIX")) { pu_log("weaponfix: DOW2_NOWEAPONFIX set - the port's own weapon counters%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *dec = (unsigned char *)(WF_DEC + slide);
    if (memcmp(dec, WF_DEC_WANT, 6)) { pu_log("weaponfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", WF_DEC, 0); return; }
    for (size_t i = 0; i < sizeof WF_SITES / sizeof WF_SITES[0]; i++)
        if (memcmp((void *)(WF_SITES[i].addr + slide), WF_SITES[i].want, WF_SITES[i].len)) {
            pu_log("weaponfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)WF_SITES[i].addr, 0); return; }
    vm_address_t near = 0;
    for (vm_address_t want = 0x104300000UL + slide; want < 0x140000000UL + slide; want += 0x100000) {
        near = want;
        if (vm_allocate(mach_task_self(), &near, vm_page_size, 0) == KERN_SUCCESS) break;
        near = 0;
    }
    long long d = near ? (long long)near - (long long)(dec + 5) : 0;
    if (!near || d > 0x7fffffffLL || d < -0x80000000LL) { pu_log("weaponfix: no page near the game code (%#lx) - NOT patched%.0lu", (unsigned long)near, 0); return; }
    unsigned char *N = (unsigned char *)near;
    uint64_t back = (uint64_t)(dec + 6), tback = (uint64_t)(uintptr_t)wf_ret;
    memcpy(N, WF_STUB, sizeof WF_STUB); memcpy(N + sizeof WF_STUB, &back, 8);
    memcpy(N + 0x80, WF_STUB, sizeof WF_STUB); memcpy(N + 0x80 + sizeof WF_STUB, &tback, 8);
    // WindingUp 0x101c7d2c8..0x101c7d2d8 (cvttss2si rax, xmm0; cmp eax, 1; mov ecx, 1; cmovX ecx, eax) + mov eax, ecx; ret
    memcpy(N + 0x100, (void *)(0x101c7d2c8 + slide), 16); memcpy(N + 0x100 + 13, WF_SITES[0].put, 3);
    static const unsigned char t1[] = { 0x89, 0xc8, 0xc3 }; memcpy(N + 0x110, t1, sizeof t1);
    // Firing 0x101c7d55d..0x101c7d56b (mov esi, [r15+0x140]; cmp esi, ebx; cmovX ebx, esi; sub esi, ebx) + mov eax, ebx; mov edx, esi; ret
    memcpy(N + 0x180, (void *)(0x101c7d55d + slide), 14); memcpy(N + 0x180 + 9, WF_SITES[1].put, 3);
    static const unsigned char t2[] = { 0x89, 0xd8, 0x89, 0xf2, 0xc3 }; memcpy(N + 0x18e, t2, sizeof t2);
    vm_protect(mach_task_self(), near, vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    unsigned long n, bad = wf_selftest(N, &n);
    if (bad) { pu_log("weaponfix self-test: %lu of %lu cases differ from Windows - NOT patched, report this", bad, n); return; }
    int done = 0;
    for (size_t i = 0; i < sizeof WF_SITES / sizeof WF_SITES[0]; i++)
        done += wf_write((unsigned char *)(WF_SITES[i].addr + slide), WF_SITES[i].put, WF_SITES[i].len);
    unsigned char j[6] = { 0xE9, 0, 0, 0, 0, 0x90 }; int32_t rel = (int32_t)d; memcpy(j + 1, &rel, 4);
    done += wf_write(dec, j, 6);
    // the game now runs exactly the instructions the self-test ran
    if (memcmp((void *)(0x101c7d2c8 + slide), N + 0x100, 16) || memcmp((void *)(0x101c7d55d + slide), N + 0x180, 14)) done = -100;
    if (done != 3) { pu_log("weaponfix: only %lu of %lu edits made (vm_protect failed)", (unsigned long)(done < 0 ? 0 : done), 3); return; }
    pu_log("weaponfix: OK - 3 edits: bursts until reload stop at 0, burst ticks and manual-fire rounds compared signed (as Windows), self-test %lu", n, 0);
}
