// heapfix.c - crossplay_fix v9, part 7: the pathfinder's open list orders equal nodes the way Windows does.
// Why: every A* of the simulation (PathPrecise*: 11 environments, PathHigh, AIAStar) keeps its open nodes in a binary heap
// keyed by f = g + h. Windows (STLport, VS2005) uses std::push_heap / __adjust_heap with no tie-breaking: on pop the hole
// goes to the right child unless the left one is strictly smaller, then the last element sifts up while strictly smaller.
// The Mac port uses boost::heap::d_ary_heap<arity 2, stable, mutable>: equal keys are ordered by an insertion counter and
// pop swaps top/last and sifts down comparing with the counter. Same keys, different order of equal-f nodes -> A* expands
// other cells -> PathfinderQuery::GiveClosestFreePosition returns another free cell. At frame 0 this put the Mac player's
// starting squad (6 Slugga Boys) one cell away and turned it 180 degrees (heading = free cell - marker) - sync error at once
// (seen 02.11.2026, match_1102_v8: w0/m0 dumps, entities 13193-13198; the single-entity hero, which skips the search, matched).
// What: the 13 sift-down and 12 update (decrease-key) instantiations jump to two functions that do the Windows algorithm on
// the Mac heap's own layout. Push stays the port's: a new node has the largest counter, so its stable sift-up is exactly
// STLport's strict sift-up. Checked in heapfix_test.c against a transcription of DOW2.exe 0xa22e30 / 0xa22e80 with the
// port's real machine code. DOW2_NOHEAPFIX=1: off.
#include "patch_util.h"
#include "heapfix_impl.h"
#define hlog pu_log
#define jump_to pu_jump

static const uint64_t SIFT[] = { 0x101ad1470, 0x101e9a040, 0x101ea4ee0, 0x101ea6230, 0x101ea7450, 0x101ea8660, 0x101ea99a0,
    0x101eaa970, 0x101eab940, 0x101eac910, 0x101eadb30, 0x101eaed50, 0x101eaff60 };
static const uint64_t UPD[] = { 0x101e9a2c0, 0x101ea5160, 0x101ea64b0, 0x101ea76d0, 0x101ea88e0, 0x101ea9c20, 0x101eaabf0,
    0x101eabbc0, 0x101eacb90, 0x101eaddb0, 0x101eaefd0, 0x101eb01e0 };
static const unsigned char SIFT_WANT[36] = { 0x55,0x48,0x89,0xe5,0x48,0x8d,0x54,0x36,0x01,0x4c,0x8b,0x47,0x08,0x4c,0x8b,0x4f,0x10,
    0x4c,0x89,0xc8,0x4c,0x29,0xc0,0x48,0xc1,0xf8,0x04,0x48,0x39,0xc2,0x0f,0x83,0x24,0x01,0x00,0x00 };
static const unsigned char UPD_WANT[36] = { 0x55,0x48,0x89,0xe5,0x48,0x85,0xf6,0x0f,0x84,0xc7,0x00,0x00,0x00,0x48,0x8d,0x4e,0xff,
    0x48,0xd1,0xe9,0x4c,0x8b,0x57,0x08,0x48,0xc1,0xe1,0x04,0x48,0x89,0xf2,0x48,0xc1,0xe2,0x04,0x4d };

__attribute__((constructor)) static void heapfix_init(void) {
    if (getenv("DOW2_NOHEAPFIX")) { hlog("heapfix: DOW2_NOHEAPFIX set - the port's own (boost stable) pathfinder heap%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    size_t ns = sizeof SIFT / sizeof SIFT[0], nu = sizeof UPD / sizeof UPD[0];
    for (size_t i = 0; i < ns; i++)   // all or nothing: another game build is left alone
        if (memcmp((void *)(SIFT[i] + slide), SIFT_WANT, sizeof SIFT_WANT)) {
            hlog("heapfix: unexpected bytes at sift-down %#lx (another game build?) - NOT patched%.0lu", (unsigned long)SIFT[i], 0); return; }
    for (size_t i = 0; i < nu; i++)
        if (memcmp((void *)(UPD[i] + slide), UPD_WANT, sizeof UPD_WANT)) {
            hlog("heapfix: unexpected bytes at update %#lx (another game build?) - NOT patched%.0lu", (unsigned long)UPD[i], 0); return; }
    int bad = heapfix_selftest();
    if (bad) { hlog("heapfix self-test: %lu sequences differ from Windows - NOT patched, report this%.0lu", (unsigned long)bad, 0); return; }
    size_t done = 0;
    for (size_t i = 0; i < ns; i++) done += jump_to((unsigned char *)(SIFT[i] + slide), (void *)heapfix_siftdown);
    for (size_t i = 0; i < nu; i++) done += jump_to((unsigned char *)(UPD[i] + slide), (void *)heapfix_update);
    if (done == ns + nu) hlog("heapfix: OK - %lu pathfinder heap functions order equal nodes as Windows (STLport)%.0lu", (unsigned long)done, 0);
    else hlog("heapfix: only %lu of %lu functions patched (vm_protect failed)", (unsigned long)done, (unsigned long)(ns + nu));
}
