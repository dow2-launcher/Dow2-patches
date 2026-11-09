// projfix.c - crossplay_fix v11, part 11: projectiles and entity facings, 3-term sums added in Windows' order.
// Why: match_1102_v10 (v10 on both sides, -sync_high) agreed for 185 frames; at frame 186 an Ork stun grenade's transform
// differed (x axis .y b4080001 Win / b4100001 Mac). ModProjectileDynamics::setProjectileTransform builds the projectile's
// basis from heading h and its x axis V: a = normalize(h x V), b = normalize(a x h). Windows' DOW2.exe (MSVC /fp:fast) adds
// the squared lengths as (a2^2 + a1^2) + a0^2, the port in source order (a0^2 + a1^2) + a2^2: 1-2 ulp apart in 14% of calls.
// At frame 185 (the grenade's first step - not yet in the checksum) the result already differed, at 186 it showed. Reproduced
// bit for bit by running both games' code (test/wproj.c, test/mproj.c, cases 0 and 1 are that grenade).
// The same kind of difference, found by running both games' code side by side on 1M random cases each:
//   getNextPosFlyingStraight (missiles, 30% of steps differed), getNextPosHoming (21%), Entity::SetPositionAndHeading
//   (Windows SimEngine.dll; 1.6%), EntityUtil::MoveEntityCloserToPoint (SimEngine.dll; 4.7%). Equal already: the artillery
//   step (grenades), initArtillery, ProjectileWeapon::Fire's launch matrix.
// What: 13 edits (projfix_sites.h): 11 reorder the same instructions in place, 2 jump to stubs (projfix_stub.h). All or
// nothing on unexpected bytes. Self-test at load: SetPositionAndHeading and MoveEntityCloserToPoint (the stubs) on 16 cases
// against SimEngine.dll's results; on a mismatch every edit is undone. DOW2_NOPROJFIX=1: off.
#include "patch_util.h"
#include "projfix_sites.h"
#include "projfix_stub.h"
#include "projfix_selftest.inc"

static int pj_write(unsigned char *p, const unsigned char *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
static void pj_vret(void *self) { (void)self; }

static unsigned long pj_selftest(uintptr_t slide) {
    static void *vt[32]; for (int k = 0; k < 32; k++) vt[k] = (void *)pj_vret;   // the entity's virtuals: nothing to do
    static char ent[0x200]; unsigned long bad = 0;
    void (*set_pos_heading)(void *, const float *, const float *, int, float) =
        (void (*)(void *, const float *, const float *, int, float))(0x102157d90ULL + slide);
    char (*closer)(void *, const float *, float) = (char (*)(void *, const float *, float))(0x102168330ULL + slide);
    for (size_t k = 0; k < sizeof PJ_ST_HEAD / sizeof PJ_ST_HEAD[0]; k++) {
        float pos[3], h[3]; memcpy(pos, PJ_ST_HEAD[k], 12); memcpy(h, PJ_ST_HEAD[k] + 3, 12);
        memset(ent, 0, sizeof ent); *(void ***)ent = vt;
        set_pos_heading(ent, pos, h, 1, 1.0f);
        if (memcmp(ent + 0x9c, PJ_ST_HEAD[k] + 6, 48)) bad++;
    }
    for (size_t k = 0; k < sizeof PJ_ST_MOVE / sizeof PJ_ST_MOVE[0]; k++) {
        float pt[3], d; memcpy(pt, PJ_ST_MOVE[k] + 12, 12); memcpy(&d, PJ_ST_MOVE[k] + 15, 4);
        memset(ent, 0, sizeof ent); *(void ***)ent = vt; memcpy(ent + 0x9c, PJ_ST_MOVE[k], 48);
        int r = closer(ent, pt, d) & 0xff;
        if ((uint32_t)r != PJ_ST_MOVE[k][16] || memcmp(ent + 0x9c, PJ_ST_MOVE[k] + 17, 48)) bad++;
    }
    return bad;
}

__attribute__((constructor)) static void projfix_init(void) {
    if (getenv("DOW2_NOPROJFIX")) { pu_log("projfix: DOW2_NOPROJFIX set - the port's own projectile/facing sums%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    for (int i = 0; i < PJ_NSITES; i++)
        if (memcmp((void *)(PJ_SITES[i].addr + slide), PJ_SITES[i].want, PJ_SITES[i].len)) {
            pu_log("projfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)PJ_SITES[i].addr, 0); return; }
    projfix_back_heading = PJ_HEADING_BACK + slide; projfix_back_closer = PJ_CLOSER_BACK + slide;
    projfix_back_closer_setpos = PJ_CLOSER_SETPOS + slide;
    int done = 0;
    for (int i = 0; i < PJ_NSITES; i++) {
        unsigned char put[40]; memcpy(put, PJ_SITES[i].put, sizeof put);
        if (PJ_SITES[i].stub) {                                           // jmp qword [rip+0]; .quad stub; nops
            void (*stub)(void) = PJ_SITES[i].stub == 1 ? projfix_stub_heading : projfix_stub_closer;
            memset(put, 0x90, sizeof put); put[0] = 0xFF; put[1] = 0x25; memset(put + 2, 0, 4);
            uint64_t to = (uint64_t)stub; memcpy(put + 6, &to, 8);
        }
        done += pj_write((unsigned char *)(PJ_SITES[i].addr + slide), put, PJ_SITES[i].len);
    }
    if (done != PJ_NSITES) { pu_log("projfix: only %lu of %lu edits made (vm_protect failed)", (unsigned long)done, (unsigned long)PJ_NSITES); return; }
    unsigned long bad = pj_selftest(slide);
    if (bad) {
        for (int i = 0; i < PJ_NSITES; i++) pj_write((unsigned char *)(PJ_SITES[i].addr + slide), PJ_SITES[i].want, PJ_SITES[i].len);
        pu_log("projfix self-test: %lu of 16 cases differ from Windows - edits undone, NOT patched, report this%.0lu", bad, 0); return; }
    pu_log("projfix: OK - %lu edits: projectile transform/flight and entity facing sums as Windows (MSVC)%.0lu", (unsigned long)done, 0);
}
