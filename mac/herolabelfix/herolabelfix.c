// HeroLabelFix.dylib - Hero bar fix for the native Mac game (Feral 1.0.3): the bar over a hero (icon, health, energy) comes
// back after the hero is downed and revived. The same as the Windows HeroLabelFix.dll (herolabelfix/herolabelfix.c).
//
// The Mac port has the same path as DOW2.exe (pairs by vtable): SFMovieManager::Update (0x102140c90) walks the movies under its
// mutex (manager + 0x69) and calls Update (vtable +0x10) only for SHOWN ones (SFMovie::GetVisible = byte +0x38); that is what
// calls SquadLeaderDecorator::OnUpdate (0x1021040a0, Windows 0xB25070 "draw"). SquadLeaderDecorator::OnSimUpdate
// (0x102102c80, Windows 0xB24370 "update", vtable slot at 0x102dfee58) clears "valid" (+0x140, Windows +0x9C) when the hero is
// down and sets it back after the revive; OnUpdate hides the movie (SFMovie::SetVisible 0) while not valid. A hidden movie is
// never asked again, so OnUpdate never shows it again: no bar for the rest of the match.
// Fix: after the decorator's own OnSimUpdate, a decorator that is valid but hidden, and already taken in by the manager
// (+0x44/+0x48, the viewport divisors of SFMovie::UpdateViewport, Windows +0x28/+0x2C), gets the game's own OnUpdate once,
// under the manager's mutex (TryEnter: busy -> again on the next update). Interface only - nothing in the simulation changes.
// HeroLabelFix.ini [HeroLabelFix]: Enabled=1 (as on Windows).
#include "../common/machook.h"

#define HL_VERSION 1
#define VT_SLOT_SIMUPD 0x102dfee58UL     // SquadLeaderDecorator vtable: OnSimUpdate
#define VT_SLOT_UPD    0x102dfee18UL     // same vtable: OnUpdate
#define FN_SIMUPD      0x102102c80UL     // SquadLeaderDecorator::OnSimUpdate(this, a, b, c)
#define FN_UPD         0x1021040a0UL     // SquadLeaderDecorator::OnUpdate(this, float)
#define FN_MGR         0x10213f3f0UL     // SFMovieManager::Instance()
#define FN_TRYENTER    0x1018423c0UL     // Plat::RecursiveMutex::TryEnter -> bool
#define FN_LEAVE       0x1018423d0UL     // Plat::RecursiveMutex::Leave
#define OFF_VALID 0x140
#define OFF_SHOWN 0x38
#define OFF_DIVX  0x44
#define OFF_DIVY  0x48
#define OFF_MGR_MUTEX 0x69

typedef void (*SimUpd_t)(void *self, void *a, void *b, void *c);
typedef void (*Upd_t)(void *self, float dt);
typedef void *(*Mgr_t)(void);
typedef char (*Try_t)(void *mx);
typedef void (*Leave_t)(void *mx);
static SimUpd_t o_simupd; static Upd_t p_upd; static Mgr_t p_mgr; static Try_t p_try; static Leave_t p_leave;
static volatile long g_restored;

static void hk_simupd(void *self, void *a, void *b, void *c) {
    o_simupd(self, a, b, c);
    uint8_t *d = self;
    if (!d[OFF_VALID] || d[OFF_SHOWN] || !*(int32_t *)(d + OFF_DIVX) || !*(int32_t *)(d + OFF_DIVY)) return;
    char *mgr = p_mgr(); if (!mgr) return;
    void *mx = mgr + OFF_MGR_MUTEX;
    if (!p_try(mx)) return;                       // the manager is busy on another thread: again on the next update
    if (!d[OFF_SHOWN]) p_upd(self, 0.0f);
    p_leave(mx);
    long n = __sync_add_and_fetch(&g_restored, 1);
    if (n <= 50 || n % 100 == 0) mh_log("bar shown again (decorator %p, total %ld)", self, n);
}

__attribute__((constructor)) static void herolabelfix_init(void) {
    if (!mh_start("HeroLabelFix", HL_VERSION)) return;
    static const uint8_t SIMUPD[24] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53,
                                        0x48, 0x81, 0xEC, 0xE8, 0x02, 0x00, 0x00, 0x48, 0x89, 0xCB, 0x48 };
    static const uint8_t UPD[16] = { 0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x48, 0x89, 0xFB, 0x80, 0xBB, 0x40, 0x01, 0x00, 0x00, 0x00 };   // cmp byte [rbx+0x140],0
    static const uint8_t MGR[11] = { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x05, 0x4D, 0x65, 0x7B, 0x01 };
    static const uint8_t TRY[9] = { 0x55, 0x48, 0x89, 0xE5, 0xE8, 0x77, 0x2E, 0xE4, 0xFE };     // call _TryEnterCriticalSection
    static const uint8_t LEAVE[9] = { 0x55, 0x48, 0x89, 0xE5, 0x5D, 0xE9, 0xD6, 0x2E, 0xE4 };   // jmp _LeaveCriticalSection
    uint64_t *slot = mh_at(VT_SLOT_SIMUPD), *uslot = mh_at(VT_SLOT_UPD);
    if (*slot == (uint64_t)hk_simupd) { mh_log("already hooked"); return; }
    if (*slot != FN_SIMUPD + mh_slide) {
        if (*uslot == FN_UPD + mh_slide && (*slot < MH_IMAGE_LO + mh_slide || *slot >= MH_IMAGE_HI + mh_slide)) { mh_log("another HeroLabelFix copy is already active in this game, this one stays idle"); return; }
        mh_log("SquadLeaderDecorator vtable is not as in 1.0.3 (slot %#lx = %#llx) - not patching", VT_SLOT_SIMUPD, (unsigned long long)*slot); return;
    }
    if (*uslot != FN_UPD + mh_slide || !mh_same(FN_SIMUPD, SIMUPD, sizeof SIMUPD) || !mh_same(FN_UPD, UPD, sizeof UPD) || !mh_same(FN_MGR, MGR, sizeof MGR)
        || !mh_same(FN_TRYENTER, TRY, sizeof TRY) || !mh_same(FN_LEAVE, LEAVE, sizeof LEAVE)) {
        mh_log("SquadLeaderDecorator / SFMovieManager code is not as in 1.0.3 - not patching"); return;
    }
    o_simupd = (SimUpd_t)mh_at(FN_SIMUPD); p_upd = (Upd_t)mh_at(FN_UPD); p_mgr = (Mgr_t)mh_at(FN_MGR);
    p_try = (Try_t)mh_at(FN_TRYENTER); p_leave = (Leave_t)mh_at(FN_LEAVE);
    uint64_t hk = (uint64_t)hk_simupd;
    if (!mh_write(slot, &hk, 8, MH_DATA)) { mh_log("vm_protect failed - not patched"); return; }
    mh_log("HeroLabelFix %d: hero bars come back after a revive (SquadLeaderDecorator::OnSimUpdate %#lx hooked in its vtable slot %#lx)", HL_VERSION, FN_SIMUPD, VT_SLOT_SIMUPD);
}
