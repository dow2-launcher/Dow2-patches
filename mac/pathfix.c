// pathfix.c - crossplay_fix v10, part 10: ground units' turns and the line walk through the cells, rounded as Windows rounds.
// Why: the Windows DOW2.exe was built by MSVC with /fp:fast. Where a function has 1/L at hand it multiplies (x*(1/L)) instead of
// dividing (x/L), and it reassociates (angle/speed)*r. The port (clang) computes exactly what the source says. Same maths,
// 1 ulp apart in 10-30% of calls. match_1102_v9 (v9 on both sides, -sync_high): the whole state agreed for 199 frames, then
// one Ork's path had a waypoint direction.y bb4f2758 on the Mac, bb4f2750 on Windows -> sync error at frame 200. That value is
// TurningBehaviorUtil::DoCurveTurn's S-turn: cosine c = 2r/L on the Mac, (1/L)*(2r) on Windows (reproduced bit for bit, r =
// speed/turn rate = 5/17.4533). The same kind of difference sits in the turn costs (which turn is taken) and in
// GroundDynamicsUtils::DoLineDraw (every "is this line passable" test).
// What: 9 edits in the game's code (pathfix_sites.h): 6 in place (same bytes reordered or swapped), 3 jumps to the small
// stubs in pathfix_stub.h. All or nothing: any unexpected byte -> nothing is patched.
// Checked (v10/test, Linux): DOW2.exe's own machine code (mapped in a 32-bit process, msvcr80 math from msmath.c) against the
// port's own machine code with these edits, on random cases: DoCurveTurn 100k, its S-turn 200k (incl. the match_1102_v9 case),
// DoThreePointTurn 100k, DoCurveTurnSmoothed 100k, DoConstantRadiusTurn 100k, CalcTurnCost (7 kinds) 1.4M, DoLineDraw 300k -
// all bit-exact; as shipped the port differs in 4-33% of them. Self-test at load: the curve-turn cost on 14 such cases.
// DOW2_NOPATHFIX=1: off.
#include "patch_util.h"
#include "pathfix_sites.h"
#include "pathfix_stub.h"

static const struct { uint32_t in[11]; int type; uint32_t cost; } ST[] = {   // ax ay adx ady bx by bdx bdy r speed turn
#include "pathfix_selftest.inc"
};
typedef char (*curve_cost_fn)(float r, const void *A, const void *B, const void *data, float *cost, int *type);

static int pf_write(unsigned char *p, const unsigned char *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}

__attribute__((constructor)) static void pathfix_init(void) {
    if (getenv("DOW2_NOPATHFIX")) { pu_log("pathfix: DOW2_NOPATHFIX set - the port's own turn and line-walk rounding%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    for (int i = 0; i < PF_NSITES; i++)
        if (memcmp((void *)(PF_SITES[i].addr + slide), PF_SITES[i].want, PF_SITES[i].len)) {
            pu_log("pathfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)PF_SITES[i].addr, 0); return; }
    pathfix_back_xcost = PF_XCOST_BACK + slide; pathfix_back_tpcost = PF_TPCOST_BACK + slide; pathfix_back_line = PF_LINE_BACK + slide;
    int done = 0;
    for (int i = 0; i < PF_NSITES; i++) {
        unsigned char put[48]; memcpy(put, PF_SITES[i].put, sizeof put);
        if (PF_SITES[i].stub) {                                           // jmp qword [rip+0]; .quad stub; nops
            void (*stub)(void) = PF_SITES[i].stub == 1 ? pathfix_stub_xcost : PF_SITES[i].stub == 2 ? pathfix_stub_tpcost : pathfix_stub_line;
            memset(put, 0x90, sizeof put); put[0] = 0xFF; put[1] = 0x25; memset(put + 2, 0, 4);
            uint64_t to = (uint64_t)stub; memcpy(put + 6, &to, 8);
        }
        done += pf_write((unsigned char *)(PF_SITES[i].addr + slide), put, PF_SITES[i].len);
    }
    if (done != PF_NSITES) { pu_log("pathfix: only %lu of %lu edits made (vm_protect failed)", (unsigned long)done, (unsigned long)PF_NSITES); return; }
    // self-test: the game's own curve-turn cost (0x101e8b250, uses edits 2-7) against DOW2.exe's results
    unsigned long bad = 0;
    for (size_t k = 0; k < sizeof ST / sizeof ST[0]; k++) {
        float v[11]; memcpy(v, ST[k].in, sizeof v);
        float A[7] = { v[0], v[1], v[2], v[3], 0, 0, 0 }, B[7] = { v[4], v[5], v[6], v[7], 0, 0, 0 };
        char data[0x100]; memset(data, 0, sizeof data); memcpy(data + 0x30, &v[9], 4); memcpy(data + 0x38, &v[10], 4);
        float cost = -1; int type = -1;
        ((curve_cost_fn)(0x101e8b250ULL + slide))(v[8], A, B, data, &cost, &type);
        uint32_t cb; memcpy(&cb, &cost, 4);
        if (type != ST[k].type || cb != ST[k].cost) bad++;
    }
    if (bad) {
        for (int i = 0; i < PF_NSITES; i++) pf_write((unsigned char *)(PF_SITES[i].addr + slide), PF_SITES[i].want, PF_SITES[i].len);
        pu_log("pathfix self-test: %lu of 14 turn costs differ from Windows - edits undone, NOT patched, report this%.0lu", bad, 0); return; }
    pu_log("pathfix: OK - %lu edits: turns (S-turn cosine, turn costs) and the cell line walk round as Windows (MSVC)%.0lu", (unsigned long)done, 0);
}
