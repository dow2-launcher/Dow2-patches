// walkfix.c - crossplay_fix v18, part 20: the height of a walkable surface (bridges, ramps, building tops) as SimEngine.dll.
// Why (difftest step 2, replay 6.rec frame 496): ig_sentinel 31465 snapped to a walkable surface: GroundDynamics::Drive ->
// ground-snap helper (DOW2.exe 0x912680) -> CollisionSystem::QueryTerrainHeight -> WalkableSurfaceSystem::QueryHeight ->
// CollisionModel::QueryHeight -> CollisionModel::GetTriangleHeightNormal (Mac 0x102185cf0 = SimEngine.dll 0x10030f40).
// position.y Windows 0x434816e5, Mac 0x434816e6 (the -sync_high state of frame 495 equal).
// Cause: /fp:fast sum order. With n = e1 x e2 (same products on both), SimEngine adds the plane offset d = v0.n as
// (v0.y*ny + v0.z*nz) + v0.x*nx, the port (v0.x*nx + v0.y*ny) + v0.z*nz; the normal's |n|^2 SimEngine (nz^2 + ny^2) + nx^2,
// the port (nx^2 + ny^2) + nz^2. The rest (2D point-in-triangle MathCollision::Intersection 0x101939960 = 0x10050aa0, the
// height -((nx*px + nz*pz) - d) / ny, n * (1 / |n|)) is the same on both.
// Side by side (t18: w18 = SimEngine.dll's code, m18 = the port's, 1M cases): 10.44% differ (height 8.55%, normal only 1.89%,
// hit/miss 0); after the two edits 0.
// What: both sums rewritten in place, same length, same registers out (xmm0; xmm1 is dead after both). Self-test at load:
// 48 cases (the replay-6 triangle first) through the patched function, results from SimEngine.dll 0x10030f40 (t18/w18 st);
// on any difference the original bytes are put back. DOW2_NOWALKFIX=1: off.
// Same class, already fixed earlier (combatfix, v12): CollisionModel::Query |hit - start|^2 and |normal|, its segment /
// triangle FindIntersection, SetTransform's InverseSelf and Mesh::TransformMesh. Read and equal: CollisionModel::QueryHeight
// (first hit, no math), WalkableSurfaceSystem::QueryHeight / QueryCollision and CollisionSystem::QueryTerrainHeight(Normal)
// (compares / calls only).
#include "patch_util.h"
#include "walkfix_sites.h"
#include "walkfix_st.inc"
static int wf_write(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
static unsigned long wf_selftest(uintptr_t slide) {
    char (*gt)(void *, const float *, const float *, float *, float *) =
        (char (*)(void *, const float *, const float *, float *, float *))(0x102185cf0UL + slide);
    unsigned long bad = 0;
    for (unsigned long q = 0; q < WF_NST; q++) {
        float tri[9], p[2], h = 7, n[3] = { 7, 7, 7 }; uint32_t got[5];
        memcpy(tri, WF_ST[q], 36); memcpy(p, WF_ST[q] + 9, 8);
        got[0] = (unsigned char)gt(0, p, tri, &h, n); memcpy(got + 1, &h, 4); memcpy(got + 2, n, 12);
        if (memcmp(got, WF_ST[q] + 11, sizeof got)) bad++;
    }
    return bad;
}
__attribute__((constructor)) static void walkfix_init(void) {
    if (getenv("DOW2_NOWALKFIX")) { pu_log("walkfix: DOW2_NOWALKFIX set - the port's own walkable-surface height%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    for (int i = 0; i < WF_N; i++) if (memcmp((void *)(WF_SITES[i].addr + slide), WF_SITES[i].want, WF_SITES[i].len)) {
        pu_log("walkfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)WF_SITES[i].addr, 0); return; }
    unsigned long before = wf_selftest(slide);
    int done = 0;
    for (int i = 0; i < WF_N; i++) done += wf_write((unsigned char *)(WF_SITES[i].addr + slide), WF_SITES[i].put, WF_SITES[i].len);
    if (done != WF_N) {
        for (int i = 0; i < WF_N; i++) wf_write((unsigned char *)(WF_SITES[i].addr + slide), WF_SITES[i].want, WF_SITES[i].len);
        pu_log("walkfix: vm_protect failed (%lu of 2 edits) - NOT patched%.0lu", (unsigned long)done, 0); return;
    }
    unsigned long bad = wf_selftest(slide);
    if (bad) {
        for (int i = 0; i < WF_N; i++) wf_write((unsigned char *)(WF_SITES[i].addr + slide), WF_SITES[i].want, WF_SITES[i].len);
        pu_log("walkfix self-test: %lu of %lu differ - NOT patched (original code put back)", bad, WF_NST); return;
    }
    pu_log("walkfix: OK - walkable-surface height / normal (GetTriangleHeightNormal plane offset, |n|) as SimEngine.dll, 2 edits, self-test %lu (port before: %lu differ)",
           WF_NST, before);
}
