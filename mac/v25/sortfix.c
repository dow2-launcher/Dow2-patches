// sortfix.c - crossplay_fix v19, part 21: WeaponUtil::FindWeaponTarget sorts its candidates the way DOW2.exe does.
// Why (difftest step 4, 08.11.2026): in 5 of 8 replays the first High-sync difference was a weapon heading (activeweapon
// horizon/verticalrotation) - Mac and Windows took different targets in the same frame from the same state. FindWeaponTarget
// (Mac 0x101c96b20 = DOW2.exe 0x98cde0) collects the enemies in range (CollisionSystem::QueryEntities / GetCloseEnemies, same
// list and order on both, priorities equal bit for bit), sorts them by priority, highest first, and takes the first one that
// passes the checks. Most candidates have priority 0. DOW2.exe sorts with STLport std::sort (0x98c7c0 -> 0x98f0a0: introsort,
// not stable: for more than 16 candidates the partition reorders equal ones - 20 zeros come out reversed); the Mac port calls
// libc++ std::stable_sort (0x101c9a570, keeps the input order). Seen: calderis 05-29 frame 882 eld_warp_spider 30448, 20
// candidates all 0: Windows first 30528 (ork), Mac 30464 (hive tyrant); 6.rec 1344 csm_cultist 31301 (23 candidates, all 0)
// 31331 / 31332; outskirts 05-30 759 csm_tactical_marine 31366 (21, all 0) 31331 / 31329. Lists with <= 16 candidates: same.
// What: the call at 0x101c96eff (stable_sort(first, last, comp, len, buf, bufsize) on the 16-byte {float prio; Entity *}
// entries) goes through a page near the game code to sortfix_sort = STLport's std::sort transcribed (sortfix_impl.h); the
// temporary buffer the caller allocates is simply unused. Side by side (t19: w19 = DOW2.exe 0x98f0a0 / 0x98f0e0 + 0x98f1b0
// itself, m19 = the port / sortfix, 1M lists, mostly ties, n up to 600, forced heap-sort fallback): port 58.5 % differ,
// sortfix 0. Self-test at load: 48 lists (the replay-calderis one first) through the patched call path, orders from DOW2.exe;
// on any difference the call is put back. DOW2_NOSORTFIX=1: off.
#include "patch_util.h"
typedef struct { float k; uint32_t pad; void *p; } sf_el16;
#define SF_EL sf_el16
#include "sortfix_impl.h"
#include "sortfix_st.inc"
#define SO_CALL 0x101c96effUL
#define SO_STABLE 0x101c9a570UL
void sortfix_sort(sf_el16 *first, sf_el16 *last) { sf_sort(first, last); }
static int so_write(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
// fn(first, last, comp, len, buf, bufsize) over the self-test lists: how many orders differ from DOW2.exe's
static unsigned long so_selftest(void (*fn)(sf_el16 *, sf_el16 *, void *, long, void *, long)) {
    static sf_el16 a[64]; char comp = 0; unsigned long bad = 0; const uint32_t *q = SO_ST;
    for (unsigned long c = 0; c < SO_NST; c++) {
        long n = (long)*q++;
        for (long i = 0; i < n; i++) { memcpy(&a[i].k, q + i, 4); a[i].pad = 0; a[i].p = (void *)(uintptr_t)q[n + i]; }
        void *buf = malloc(16 * n); fn(a, a + n, &comp, n, buf, buf ? n : 0); free(buf);
        for (long i = 0; i < n; i++) if ((uint32_t)(uintptr_t)a[i].p != q[2 * n + i]) { bad++; break; }
        q += 3 * n;
    }
    return bad;
}
__attribute__((constructor)) static void sortfix_init(void) {
    if (getenv("DOW2_NOSORTFIX")) { pu_log("sortfix: DOW2_NOSORTFIX set - the port's own stable_sort of weapon targets%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *c = (unsigned char *)(SO_CALL + slide), want[5] = { 0xe8 }; int32_t rel = (int32_t)(SO_STABLE - (SO_CALL + 5));
    memcpy(want + 1, &rel, 4);
    if (memcmp(c, want, 5)) { pu_log("sortfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)SO_CALL, 0); return; }
    unsigned long before = so_selftest((void (*)(sf_el16 *, sf_el16 *, void *, long, void *, long))(SO_STABLE + slide));
    vm_address_t near = 0;
    for (vm_address_t w = 0x104200000UL + slide; w < 0x140000000UL + slide; w += 0x100000) {
        near = w;
        if (vm_allocate(mach_task_self(), &near, vm_page_size, 0) == KERN_SUCCESS) break;
        near = 0;
    }
    if (!near) { pu_log("sortfix: no page near the game code - NOT patched%.0lu%.0lu", 0, 0); return; }
    long long d = (long long)near - (long long)(c + 5);
    if (d > 0x7fffffffLL || d < -0x80000000LL) { pu_log("sortfix: page too far (%#lx) - NOT patched%.0lu", (unsigned long)near, 0); return; }
    unsigned char *t = (unsigned char *)near; void *h = (void *)sortfix_sort;
    t[0] = 0xFF; t[1] = 0x25; memset(t + 2, 0, 4); memcpy(t + 6, &h, 8);
    vm_protect(mach_task_self(), near, vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    unsigned long bad = so_selftest((void (*)(sf_el16 *, sf_el16 *, void *, long, void *, long))near);   // the patched path
    if (bad) { pu_log("sortfix self-test: %lu of %lu differ - NOT patched", bad, SO_NST); return; }
    unsigned char put[5] = { 0xe8 }; int32_t r2 = (int32_t)d; memcpy(put + 1, &r2, 4);
    if (!so_write(c, put, 5)) { pu_log("sortfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    pu_log("sortfix: OK - weapon target candidates sorted like DOW2.exe (STLport std::sort, FindWeaponTarget call 0x101c96eff), self-test %lu (port before: %lu differ)",
           SO_NST, before);
}
