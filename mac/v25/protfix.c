// protfix.c - crossplay_fix v24, part 28: Feral's anti-modification timers are never armed (long matches no longer end at 13-16 min).
// Why (owner's live match 09.11.2026 on v22: launch 02:12:31, crash 02:28:39 = 968 s, no desync): the crash report's thread 0 is in
// exit() <- CFeralGame::RezLib_Callback_Zoomed <- NRunLoop::PerformWork (a timer), running the C++ static destructors while the game
// thread (WinMain) still renders: dca::Context::ActionUpdater::operator new -> Plat::RecursiveMutex::Enter -> EnterCriticalSection ->
// NMutex::Lock(NULL) (the allocator's static mutex already destroyed) -> EXC_BAD_ACCESS at 0x8.
// The mechanism (Mac binary): CFeralGame::Execute (0x10089082f) adds a repeating 120 s timer -> CFeralGame::DoAMTask (0x100898450):
//   if (CDynProfUtils::CollectInfo() && !game->amArmed /* +0x130 */) { game->amArmed = 1; RezLib_Callback_ShowHide(game); }
// CollectInfo is obfuscated (opaque TLS arithmetic, an integrity / code-signing check). It reports "modified" for the game with the
// crossplay dylib (DYLD_INSERT_LIBRARIES + patched code) - on the owner's Mac and in macrun alike. ShowHide (0x10089afe0) then arms
//   NTimer 780 + rand%180 s  -> RezLib_Callback_Zoomed: syslog "localhost kernel: internal ICX failure"; exit(1)
//   NTimer 1080 + rand%180 s -> call 0xcafebabe (SIGSEGV)
// i.e. every patched game quits / crashes 15-18 min after start (first check at 120 s; the exit races with the game thread -> the
// crash above: 968 s). macrun (MR_KEEP_TAMPER=1, v24p): "flagged the patched game 120 s after start" - CollectInfo says modified.
// What: DoAMTask's entry jumps to pf_doam - the same logic without ShowHide: CollectInfo is still called (the obfuscated code keeps
// its own state), the flag is still set once, a log line says the check fired; no timer is armed. Whole binary scanned: the only
// reference to ShowHide is DoAMTask's jmp, the only one to Zoomed / 0xcafebabe is inside ShowHide.
// Self-test at load: the original 55 bytes of DoAMTask checked, then the PATCHED entry is called with a fake game and a fake
// CollectInfo (true, true, false): flag set once, nothing armed. DOW2_NOPROTFIX=1: off (the game's own timers armed as shipped).
#include "patch_util.h"
#include <time.h>
static const unsigned char PF_OLD[55] = { 0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x89,0xfb,0xe8,0xf2,0x16,0xac,0xff,0x84,0xc0,0x74,0x09,0x80,0xbb,
  0x30,0x01,0x00,0x00,0x00,0x74,0x07,0x48,0x83,0xc4,0x08,0x5b,0x5d,0xc3,0xc6,0x83,0x30,0x01,0x00,0x00,0x01,0x48,0x89,0xdf,0x48,0x83,0xc4,
  0x08,0x5b,0x5d,0xe9,0x59,0x2b,0x00,0x00 };   // 0x100898450: call CollectInfo (0x100359b50) ... jmp ShowHide (0x10089afe0)
typedef char (*pf_collect_t)(void *);
static pf_collect_t pf_collect; static time_t pf_t0; static int pf_testing, pf_fired, pf_calls;
static void pf_doam(void *game) {
    pf_calls++;
    if (!pf_collect(game) || ((unsigned char *)game)[0x130]) return;
    ((unsigned char *)game)[0x130] = 1; pf_fired++;
    if (!pf_testing) pu_log("protfix: Feral anti-modification check (CFeralGame::DoAMTask) flagged the patched game %lu s after start - its exit / 0xcafebabe timers were NOT armed%.0lu", (unsigned long)(time(0) - pf_t0), 0);
}
static int pf_fake_n; static char pf_fake(void *g) { (void)g; return pf_fake_n++ < 2; }
__attribute__((constructor)) static void protfix_init(void) {
    pf_t0 = time(0);
    if (getenv("DOW2_NOPROTFIX")) { pu_log("protfix: DOW2_NOPROTFIX set - Feral's anti-modification timers armed as shipped (the patched game quits 15-18 min after start)%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(0x100898450UL + slide);
    if (memcmp(p, PF_OLD, sizeof PF_OLD)) { pu_log("protfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", 0x100898450UL, 0); return; }
    pf_collect = (pf_collect_t)(0x100359b50UL + slide);
    if (!pu_jump(p, (void *)pf_doam)) { pu_log("protfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    // self-test through the patched entry: fake CollectInfo true, true, false -> flag set once, fired once, 3 calls
    static unsigned char game[0x200]; pf_testing = 1; pf_collect = pf_fake;
    void (*doam)(void *) = (void (*)(void *))p;
    doam(game); doam(game); doam(game);
    int ok = game[0x130] == 1 && pf_fired == 1 && pf_calls == 3 && pf_fake_n == 3 && p[0] == 0xff && p[1] == 0x25 && *(uint64_t *)(p + 6) == (uint64_t)pf_doam;
    unsigned long tf = game[0x130], tn = (unsigned long)pf_fired; pf_testing = 0; pf_fired = 0; pf_calls = 0; pf_collect = (pf_collect_t)(0x100359b50UL + slide);
    if (!ok) {
        vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
        if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS) {
            memcpy(p, PF_OLD, sizeof PF_OLD); vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE); }
        pu_log("protfix self-test failed (flag %lu, fired %lu) - NOT patched", tf, tn); return;
    }
    pu_log("protfix: OK - Feral anti-modification timers (exit(1) 'internal ICX failure' 15-18 min, call 0xcafebabe 20-23 min after start) never armed, CFeralGame::DoAMTask without ShowHide, self-test 3 (patched entry run)%.0lu%.0lu", 0, 0);
}
