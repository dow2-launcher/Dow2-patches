// rowfix.c - crossplay_fix v25, part 31: RightOfWay::CollectLowerPriorityEntitiesOnLine's line direction as DOW2.exe (r = 1/len).
// Why (difftest step 10, calderis_outskirts 2026-03-15 Low F18771 / High H18746, entity 32465 sm_terminator_assault): on the Mac
// RightOfWay asks the unit to step aside (Evacuate, command 14) and on Windows it stays idle at (-35.5, 39.5). The Mac chain is
// MoveToTargetState -> EvacuateFor -> DoEvacuateForMovingUnit -> EvacuatePath (Mac 0x101cd8180 / DOW2.exe 0xa38610) ->
// CollectLowerPriorityEntitiesOnLine (Mac 0x101cd78b0 / DOW2.exe 0xa38140) -> IssueEvacuateOrder (Mac 0x101cd73c0 / DOW2.exe
// 0xa37cf0). CollectLowerPriorityEntitiesOnLine walks the grid cells along each path segment (DDA: step x or y by comparing
// |(boundary - start) / dir|); its direction:
//   Mac 0x101cd7a27     dirx = dx / len;            diry = dy / len
//   DOW2.exe 0xa38233   r = 1.0f / len; dirx = r * dx; diry = r * dy     (both SSE scalar; 46 % of t25 cases 1 ulp apart)
// Path points sit on cell centres / corners, so the x/y step comparisons tie and a 1-ulp direction picks another cell -> another
// set of "lower priority" units on the line -> an Evacuate order on one side only.
// What: the span 0x101cd7a27..0x101cd7a39 (movss xmm3,[rbp-0x48]; divss xmm3,xmm0; movss xmm2,[rbp-0x34]; divss xmm2,xmm0) jumps
// to a stub doing DOW2.exe's arithmetic (xmm0 = len kept; xmm2 / xmm3 upper lanes zero as after the original movss loads), back to
// 0x101cd7a39. Self-test at load: RW_NST cases from DOW2.exe (t25/w25 rowst: its own bytes 0xa381d6.. run) through the PATCHED code
// in place (entered at 0x101cd79cb with a fake frame in rbp, temporary landing jump on 0x101cd7a39). DOW2_NOROWFIX=1: off.
#include "patch_util.h"
#include "rowfix_st.inc"
#define RW_AT 0x101cd7a27UL
#define RW_BACK 0x101cd7a39UL
#define RW_ENTRY 0x101cd79cbUL
static const unsigned char RW_OLD[18] = { 0xf3,0x0f,0x10,0x5d,0xb8, 0xf3,0x0f,0x5e,0xd8, 0xf3,0x0f,0x10,0x55,0xcc, 0xf3,0x0f,0x5e,0xd0 };
static const unsigned char RW_LAND_OLD[14] = { 0x0f,0x2e,0xd9, 0x48,0x8b,0x45,0xd0, 0xba,0x01,0x00,0x00,0x00, 0x41,0xbd };
uint64_t rw_back; const float rw_one = 1.0f; uint32_t rw_out[2];
void rw_stub(void);
__asm__(".text\n.globl _rw_stub\n_rw_stub:\n movss _rw_one(%rip), %xmm2\n divss %xmm0, %xmm2\n"
        " movss -0x48(%rbp), %xmm3\n mulss %xmm2, %xmm3\n mulss -0x34(%rbp), %xmm2\n jmp *_rw_back(%rip)\n");
// rw_run(rbp, entry): rbp = fake frame, jump into the game code; the landing jump on 0x101cd7a39 comes back to rw_land (xmm3, xmm2 out)
uint64_t rw_saved_rsp;
void rw_run(char *rbpv, uint64_t entry);
void rw_land(void);
__asm__(".text\n.globl _rw_run\n_rw_run:\n push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " mov %rsp, _rw_saved_rsp(%rip)\n sub $0x108, %rsp\n mov %rdi, %rbp\n jmp *%rsi\n"
        ".globl _rw_land\n_rw_land:\n movss %xmm3, _rw_out(%rip)\n movss %xmm2, _rw_out+4(%rip)\n"
        " mov _rw_saved_rsp(%rip), %rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n");
static int rw_wr(unsigned char *p, const void *b, size_t n) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, n); vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE); return 1;
}
static char rw_fr[0x400];
static void rw_case(const uint32_t *c, uintptr_t slide, uint32_t *o) {   // Mac frame: sx -0x40, sy -0x3c, ex -0x38, ey -0x44
    char *rb = rw_fr + 0x200; memcpy(rb - 0x40, c, 4); memcpy(rb - 0x3c, c + 1, 4); memcpy(rb - 0x38, c + 2, 4); memcpy(rb - 0x44, c + 3, 4);
    rw_out[0] = rw_out[1] = 0xdeadbeef; rw_run(rb, RW_ENTRY + slide); o[0] = rw_out[0]; o[1] = rw_out[1];
}
unsigned long rowfix_test_case(const uint32_t *c, uint32_t *o) { rw_case(c, 0, o); return 0; }   // t25 (landing put by the harness)
static unsigned long rw_selftest(uintptr_t slide, int *ok) {
    unsigned char *l = (unsigned char *)(RW_BACK + slide); unsigned long bad = 0; *ok = 0;
    if (memcmp(l, RW_LAND_OLD, 14) || !pu_jump(l, (void *)rw_land)) return 0;
    for (unsigned long q = 0; q < RW_NST; q++) { uint32_t o[2]; rw_case(RW_ST[q], slide, o); if (memcmp(o, RW_ST[q] + 4, 8)) bad++; }
    if (!rw_wr(l, RW_LAND_OLD, 14)) return bad + 1;
    *ok = 1; return bad;
}
__attribute__((constructor)) static void rowfix_init(void) {
    if (getenv("DOW2_NOROWFIX")) { pu_log("rowfix: DOW2_NOROWFIX set - the port's own RightOfWay line direction (dx / len)%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(RW_AT + slide);
    if (memcmp(p, RW_OLD, sizeof RW_OLD)) { pu_log("rowfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", RW_AT, 0); return; }
    rw_back = RW_BACK + slide;
    if (!pu_jump(p, (void *)rw_stub)) { rw_wr(p, RW_OLD, sizeof RW_OLD); pu_log("rowfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    int ok; unsigned long bad = rw_selftest(slide, &ok);
    if (!ok || bad) { rw_wr(p, RW_OLD, sizeof RW_OLD);
        pu_log(ok ? "rowfix self-test: %lu of %lu differ from DOW2.exe - NOT patched" : "rowfix self-test could not run (%lu/%lu) - NOT patched", bad, RW_NST); return; }
    pu_log("rowfix: OK - RightOfWay::CollectLowerPriorityEntitiesOnLine direction r = 1/len, r*dx, r*dy as DOW2.exe, self-test %lu (patched code run)%.0lu", RW_NST, 0);
}
