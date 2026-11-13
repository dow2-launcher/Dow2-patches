// fowcellfix.c - crossplay_fix v21, part 24: the fog-of-war cell of a world position rounded as DOW2.exe.
// Why (difftest step 6, replays angelgate 05-29 F3023 tyr_hormagaunt 16733 and calderisrefinery 06-01 F1053 ig_commissar_lord
// 30453: sightext lastfow.cx Mac 48 / Windows 47 and 136 / 135, everything else equal): the cell is computed by
// DOW2.exe 0x868330 (PlayerFOW, 8 callers) as  v = (W + clamp(x)) * 0.5;  cx = fistp(v + 0.50001f) - 1  (x87 at 24 bits: a
// float sum, then round to nearest EVEN); the port inlines it 8 times (16 roundings) as
//   cx = (int)roundf((float)((double)v + 0.50001)) - 1  (a double sum with the double constant, then round half AWAY from 0).
// When v + 0.50001 lands on k + 0.5 (positions on a cell edge) the two give k + 1 vs k (even k), Mac one cell higher - as seen.
// What: at each of the 16 roundings (PlayerFOW::UpdateEntity, UpdateTaggedEntities, UpdateRevealAreas, IsChannelSet, IsVisible
// x3, IsExplored) the float -> double conversion, the double add (or the movsd / addsd pair) and the double -> float conversion
// become nops (v stays a float in xmm0, a movaps when it was in another register) and the call to roundf goes to fc_round
// (v + 0.50001f in float, cvtss2si = round to nearest even) via a stub on a page near the game code; the code around (clamp,
// (int) and -1) is unchanged. Self-test at load: FC_NST cases (grid half sizes, x, z) through the port's clamp order + fc_round,
// results from DOW2.exe 0x868330 itself (t21/w21 cellst). Side by side t21: 1M cases. DOW2_NOFOWCELLFIX=1: off.
#include "patch_util.h"
#include <xmmintrin.h>
#include "fowcellfix_sites.h"
#include "fowcellfix_st.inc"
float fc_round(float v) {
    volatile float s = v + 0.50001001358032227f;                 // DOW2.exe 0x113cd10 (float 0.50001)
    return (float)_mm_cvtss_si32(_mm_set_ss(s));                  // fistp: round to nearest even (MXCSR / x87 default)
}
// the port's clamp and v (unchanged code), then the rounding under test; returns cx, cz as DOW2.exe 0x868330 would
static void fc_cell(const float *g, const float *p, int32_t *o, float (*rnd)(float)) {
    for (int i = 0; i < 2; i++) {
        float h = g[i], x = p[i];
        if (h < x) x = h + -1.0f;
        if (x < -h) x = 1.0f - h;
        volatile float v = (x + h) * 0.5f;
        o[i] = (int32_t)rnd(v) - 1;
    }
}
static unsigned long fc_selftest(void) {
    unsigned long bad = 0;
    for (unsigned long q = 0; q < FC_NST; q++) {
        float g[2], p[2]; int32_t o[2]; memcpy(g, FC_ST[q], 8); memcpy(p, FC_ST[q] + 2, 8);
        fc_cell(g, p, o, fc_round);
        if (o[0] != (int32_t)FC_ST[q][4] || o[1] != (int32_t)FC_ST[q][5]) bad++;
    }
    return bad;
}
unsigned long fowcellfix_test(const float *g, const float *p, int32_t *o) { fc_cell(g, p, o, fc_round); return 0; }
static int fc_write(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
__attribute__((constructor)) static void fowcellfix_init(void) {
    if (getenv("DOW2_NOFOWCELLFIX")) { pu_log("fowcellfix: DOW2_NOFOWCELLFIX set - the port's own FOW cell rounding%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    for (int i = 0; i < FC_NE; i++) if (memcmp((void *)(FC_EDITS[i].addr + slide), FC_EDITS[i].want, FC_EDITS[i].len)) {
        pu_log("fowcellfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)FC_EDITS[i].addr, 0); return; }
    unsigned char want[16][5];
    for (int i = 0; i < FC_NC; i++) {
        unsigned char *c = (unsigned char *)(FC_CALLS[i] + slide); int32_t rel = (int32_t)(0x100932d80L - (long)(FC_CALLS[i] + 5));
        want[i][0] = 0xe8; memcpy(want[i] + 1, &rel, 4);
        if (memcmp(c, want[i], 5)) { pu_log("fowcellfix: unexpected call at %#lx - NOT patched%.0lu", (unsigned long)FC_CALLS[i], 0); return; }
    }
    unsigned long bad = fc_selftest();
    if (bad) { pu_log("fowcellfix self-test: %lu of %lu differ from DOW2.exe - NOT patched", bad, FC_NST); return; }
    vm_address_t near = 0;
    for (vm_address_t w = 0x104400000UL + slide; w < 0x140000000UL + slide; w += 0x100000) {
        near = w;
        if (vm_allocate(mach_task_self(), &near, vm_page_size, 0) == KERN_SUCCESS) break;
        near = 0;
    }
    if (!near) { pu_log("fowcellfix: no page near the game code - NOT patched%.0lu%.0lu", 0, 0); return; }
    unsigned char *st = (unsigned char *)near; void *e = (void *)fc_round;
    st[0] = 0xff; st[1] = 0x25; memset(st + 2, 0, 4); memcpy(st + 6, &e, 8);
    vm_protect(mach_task_self(), near, vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    unsigned char put[16][5];
    for (int i = 0; i < FC_NC; i++) {
        long long d = (long long)(uintptr_t)st - (long long)(FC_CALLS[i] + slide + 5);
        if (d > 0x7fffffffLL || d < -0x80000000LL) { pu_log("fowcellfix: stub out of call range - NOT patched%.0lu%.0lu", 0, 0); return; }
        int32_t r2 = (int32_t)d; put[i][0] = 0xe8; memcpy(put[i] + 1, &r2, 4);
    }
    int done = 0;
    for (int i = 0; i < FC_NE; i++) done += fc_write((unsigned char *)(FC_EDITS[i].addr + slide), FC_EDITS[i].put, FC_EDITS[i].len);
    for (int i = 0; i < FC_NC; i++) done += fc_write((unsigned char *)(FC_CALLS[i] + slide), put[i], 5);
    if (done != FC_NE + FC_NC) {
        for (int i = 0; i < FC_NE; i++) fc_write((unsigned char *)(FC_EDITS[i].addr + slide), FC_EDITS[i].want, FC_EDITS[i].len);
        for (int i = 0; i < FC_NC; i++) fc_write((unsigned char *)(FC_CALLS[i] + slide), want[i], 5);
        pu_log("fowcellfix: vm_protect failed (%lu edits) - NOT patched (original code put back)%.0lu", (unsigned long)done, 0); return;
    }
    pu_log("fowcellfix: OK - %lu FOW cell roundings as DOW2.exe 0x868330 (float + 0.50001f, round half to even), self-test %lu", (unsigned long)FC_NC, FC_NST);
}
