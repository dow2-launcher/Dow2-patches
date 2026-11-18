// OverwatchOnce.dylib - Overwatch once for the native Mac game (Feral 1.0.3): an overwatch order (right-click on a unit's build
// button) builds ONE squad and then switches itself off. The same as the Windows OverwatchOnce.dll (overwatchonce/overwatchonce.c).
//
// How the Mac game does overwatch (the 1.0.3 symbol map; the same design as DOW2.exe): it is the HUD's, not the simulation's.
// OverwatchManager (ModUI impl +0x190) keeps a std::map { building entity id -> squad blueprint } (+0x18 begin, +0x20 root,
// +0x28 size; node +0x20 key, +0x28 value). A right-click on a build button calls OverwatchManager::ToggleSquadProductionOverwatch
// (mgr, blueprint, id) - add, or erase when the same blueprint is there - and sets BindingBuildTech +0x36d (redraw the buttons).
// ModUI::OnSyncPoint (vtable slot 0x102df60a0) calls OverwatchManager::Update(mgr, BindingContext) -> UpdateSquadProductionOverwatch:
// for every entry whose building is alive and whose production queue is empty (CommandProxy::GetProductionQueue = the real queue
// plus our orders still on the way) and CommandProxy::CanProduce says yes, it sends CommandProxy::DoBuildSquad(proxy, building, 1,
// blueprint) through the proxy's vtable (+0x18; call rbx at 0x101fce612, return address 0x101fce614) - the very order of a left
// click (WorldDoCommandEntity CMD_BuildSquad). The entry stays, so it builds again whenever the queue is empty - forever.
// No overwatch in the simulation, no overwatch command: the other players and the replay only ever see plain build orders.
//
// OverwatchOnce (as on Windows): when the overwatch tick sends its build order (DoBuildSquad hooked in both CommandProxy vtables,
// only for that return address), the building and blueprint are remembered. Before each later tick (ModUI::OnSyncPoint hooked in
// its vtable slot) the building's real production queue is looked at with the game's own functions (UIGroupUtil::ConvertIDToEntity,
// ProductionUtil::FindProductionQueue, ProductionQueue::GetQueuedItemCount(queue, 1 = squad, blueprint)): as soon as the squad is
// in it, the entry is erased exactly as the second right-click does (ToggleSquadProductionOverwatch with the same blueprint, only
// when IsSquadProductionOverwatched says it is still that one) and the tech tree buttons are redrawn (BindingBuildTech::OnUpdate
// hooked in its vtable slot: +0x36d set, as after a right-click). An order that didn't make it into the queue (the money went
// elsewhere first) keeps the entry: the tick tries again by itself; after 15 s without it the watch is dropped, overwatch stays.
// Nothing is written into the simulation, no command is added or changed: client side only, no desync, the other players don't
// need it. Replays and observers have no overwatch entries, so nothing happens there.
//
// OverwatchOnce.ini [OverwatchOnce]: Enabled=1 (0 at the start: nothing hooked; re-read every 2 s: 0 = the game's endless overwatch
// again), Verbose=0. OverwatchOnce.log next to the dylib: start, every squad started by overwatch and switched off.
#include "../common/machook.h"
#include <mach/mach_time.h>

#define OO_VERSION 1
#define VT_SYNC        0x102df60a0UL     // ModUI vtable: OnSyncPoint
#define FN_SYNC        0x101fc52e0UL     // ModUI::OnSyncPoint(this, ?, bool ui)
#define AT_SYNC_OW     0x101fc543dUL     // ...; mov rsi,[rax+0x178]; mov rdi,[rax+0x190]; call OverwatchManager::Update
#define VT_BUILD1      0x102df57e8UL     // CommandProxy vtable +0x18: DoBuildSquad
#define VT_BUILD2      0x102df5b48UL     // EventGeneratorCommandProxy vtable +0x18 (the HUD's proxy)
#define FN_BUILD       0x101faa720UL     // CommandProxy::DoBuildSquad(this, Entity*, int, PropertyBagGroup*)
#define AT_TICK_SEND   0x101fce5efUL     // the tick's build order: GetCommandProxy; mov rbx,[vt+0x18]; ...; call rbx
#define RET_SEND       0x101fce614UL     // return address of that call
#define FN_TICK        0x101fce490UL     // OverwatchManager::UpdateSquadProductionOverwatch (bytes checked only)
#define FN_UPDATE      0x101fcdee0UL     // OverwatchManager::Update (bytes checked only)
#define FN_TOGGLE      0x101fcead0UL     // OverwatchManager::ToggleSquadProductionOverwatch(mgr, bp, id)
#define FN_ISOW        0x101fcebd0UL     // OverwatchManager::IsSquadProductionOverwatched(mgr, bp, id) -> bool
#define FN_ID2ENT      0x102032b00UL     // UIGroupUtil::ConvertIDToEntity(ModWorld*, id, 0x18) -> Entity*
#define FN_FINDQ       0x101e04420UL     // ProductionUtil::FindProductionQueue(const ConstOwnerPointer&) -> ProductionQueue*
#define FN_QCOUNT      0x101dff6a0UL     // ProductionQueue::GetQueuedItemCount(queue, int type, PropertyBagGroup*)
#define VT_BTUPD       0x102df99b8UL     // BindingBuildTech vtable +0x38: OnUpdate(this, ctx)
#define FN_BTUPD       0x10208c280UL
#define AT_BTREDRAW    0x10208c2eeUL     // cmp byte [rbx+0x36d],0 in OnUpdate (the redraw flag)
#define OFF_IMPL       0x10              // ModUI -> impl
#define OFF_CTX        0x178             // impl -> BindingContext
#define OFF_OWMGR      0x190             // impl -> OverwatchManager
#define OFF_ENT_ID     0x58              // Entity -> its id (UIGroupUtil::ConvertGroupToIDs)
#define OFF_BT_REDRAW  0x36d
#define ITEM_SQUAD     1                 // SpawnProductionItem's type (its vtable +0x10)

typedef void (*Sync_t)(void *ui, void *a, uint64_t b, uint64_t c);
typedef uint64_t (*Build_t)(void *proxy, void *ent, uint64_t flag, void *bp);
typedef void (*BtUpd_t)(void *self, void *ctx);
typedef void (*Toggle_t)(void *mgr, void *bp, uint32_t id);
typedef char (*IsOw_t)(void *mgr, void *bp, uint32_t id);
typedef void *(*Id2Ent_t)(void *world, uint32_t id, uint32_t flags);
typedef void *(*FindQ_t)(const void *cop);
typedef uint64_t (*QCount_t)(void *queue, uint32_t type, void *bp);

static Sync_t o_sync; static Build_t o_build; static BtUpd_t o_btupd;
static Toggle_t p_toggle; static IsOw_t p_isow; static Id2Ent_t p_id2ent; static FindQ_t p_findq; static QCount_t p_qcount;
static volatile int g_enabled = 1, g_verbose, g_redraw;
static uint64_t g_iniAt, g_started;
static void *g_mgr;                                   // the OverwatchManager while its tick runs (OnSyncPoint)

#define MAXP 16
typedef struct { int used; uint32_t ent; void *bp; long n0; uint64_t t; } Pend;   // a build order sent by overwatch
static Pend g_p[MAXP];
static uint32_t g_drop[MAXP]; static void *g_dropbp[MAXP]; static int g_ndrop;       // erase after the tick (it walks the map)

static uint64_t now_ms(void) {                       // mach_absolute_time: there on every macOS (clock_gettime only from 10.12)
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    return mach_absolute_time() / 1000000 * tb.numer / tb.denom;
}
static void load_ini(void) { g_enabled = mh_ini_int("OverwatchOnce", "Enabled", 1); g_verbose = mh_ini_int("OverwatchOnce", "Verbose", 0); }
static uint32_t bpid(void *bp) { return bp ? *(uint32_t *)((char *)bp + 0x9c) : 0; }   // what DoBuildSquad puts into the order

// the building's real production queue (not the proxy's guesses): how many of this blueprint it holds, -1 = no queue
static long queued(void *ent, void *bp) {
    struct { int32_t type, pad; void *ent, *squad, *player; } cop = { 0, 0, ent, 0, 0 };   // ConstOwnerPointer(Entity*)
    void *q = ent ? p_findq(&cop) : 0;
    return q ? (long)p_qcount(q, ITEM_SQUAD, bp) : -1;
}
static void *entity(void *ctx, uint32_t id) {
    void *world = ctx ? *(void **)((char *)ctx + 8) : 0;     // BindingContext::GetModWorld
    return world ? p_id2ent(world, id, 0x18) : 0;
}
static void ow_erase(void *mgr, uint32_t id, void *bp) {     // the second right-click's erase, only while it is that entry
    if (!mgr || !p_isow(mgr, bp, id)) return;
    p_toggle(mgr, bp, id);
    g_redraw = 1;
}
static int pend_find(uint32_t id) { for (int i = 0; i < MAXP; i++) if (g_p[i].used && g_p[i].ent == id) return i; return -1; }
static void drop_later(uint32_t id, void *bp) { if (g_ndrop < MAXP) { g_drop[g_ndrop] = id; g_dropbp[g_ndrop] = bp; g_ndrop++; } }

// ---- the build order (both CommandProxy vtables); only the overwatch tick's call is looked at
static uint64_t hk_build(void *proxy, void *ent, uint64_t flag, void *bp) {
    void *ret = __builtin_return_address(0);
    if (ret != mh_at(RET_SEND) || !g_enabled || !g_mgr || !ent || !bp) return o_build(proxy, ent, flag, bp);
    uint32_t id = *(uint32_t *)((char *)ent + OFF_ENT_ID);
    long n = queued(ent, bp);
    uint64_t r = o_build(proxy, ent, flag, bp);
    int i = pend_find(id);
    if (n < 0) {                                      // no production queue to watch: switch it off right after this order
        mh_log("building %u: squad %#x ordered by overwatch, no queue to watch - overwatch off now", id, bpid(bp));
        if (i >= 0) g_p[i].used = 0;
        drop_later(id, bp);
        return r;
    }
    if (i < 0) { for (int k = 0; k < MAXP && i < 0; k++) if (!g_p[k].used) i = k; if (i < 0) i = 0; }
    else if (g_verbose) mh_log("building %u: the last order was not seen in the queue - sent again", id);
    g_p[i].used = 1; g_p[i].ent = id; g_p[i].bp = bp; g_p[i].n0 = n; g_p[i].t = now_ms();
    if (g_verbose) mh_log("building %u: overwatch ordered squad %#x (bp %p), %ld of it in the queue", id, bpid(bp), bp, n);
    return r;
}

// ---- ModUI::OnSyncPoint: the overwatch tick runs inside it (only when its third argument is set)
static void hk_sync(void *ui, void *a, uint64_t b, uint64_t c) {
    uint64_t now = now_ms();
    if (now - g_iniAt > 2000) {
        int was = g_enabled; load_ini(); g_iniAt = now;
        if (was != g_enabled) mh_log("%s in OverwatchOnce.ini", g_enabled ? "enabled" : "disabled - the game's own overwatch");
    }
    char *impl = ui ? *(char **)((char *)ui + OFF_IMPL) : 0;
    void *mgr = impl ? *(void **)(impl + OFF_OWMGR) : 0, *ctx = impl ? *(void **)(impl + OFF_CTX) : 0;
    if ((b & 0xFF) && g_enabled && mgr && ctx) {
        // the orders sent before: in the queue now? (before the tick, so it can't send the next one first)
        for (int i = 0; i < MAXP; i++) {
            Pend *p = &g_p[i]; if (!p->used) continue;
            if (!p_isow(mgr, p->bp, p->ent)) { if (g_verbose) mh_log("building %u: overwatch changed or switched off by the player", p->ent); p->used = 0; continue; }
            void *e = entity(ctx, p->ent);
            if (!e) { mh_log("building %u: gone", p->ent); p->used = 0; continue; }
            long n = queued(e, p->bp);
            if (n > p->n0) {
                ow_erase(mgr, p->ent, p->bp); p->used = 0; g_started++;
                mh_log("building %u: squad %#x started %llu ms after the overwatch order (%ld in the queue) - overwatch off (%llu so far)",
                       p->ent, bpid(p->bp), (unsigned long long)(now - p->t), n, (unsigned long long)g_started);
                continue;
            }
            if (now - p->t > 15000) { mh_log("building %u: squad %#x not in the queue 15 s after the order - overwatch stays", p->ent, bpid(p->bp)); p->used = 0; }
        }
    }
    g_mgr = (b & 0xFF) ? mgr : 0; g_ndrop = 0;
    o_sync(ui, a, b, c);
    g_mgr = 0;
    for (int k = 0; k < g_ndrop; k++) ow_erase(mgr, g_drop[k], g_dropbp[k]);
    g_ndrop = 0;
}

// ---- the tech tree: redraw its buttons (the overwatch mark) after an erase, as the game does after a right-click
static void hk_btupd(void *self, void *ctx) {
    if (g_redraw && self) { ((uint8_t *)self)[OFF_BT_REDRAW] = 1; g_redraw = 0; }
    o_btupd(self, ctx);
}

static int slot_is(uintptr_t slot, uintptr_t fn) { return *(uint64_t *)mh_at(slot) == fn + mh_slide; }
static int put_slot(uintptr_t slot, void *hk) { uint64_t v = (uint64_t)hk; return mh_write(mh_at(slot), &v, 8, MH_DATA); }

__attribute__((constructor)) static void overwatchonce_init(void) {
    if (!mh_start("OverwatchOnce", OO_VERSION)) return;
    load_ini(); g_iniAt = now_ms();
    if (*(uint64_t *)mh_at(VT_SYNC) == (uint64_t)hk_sync) { mh_log("already hooked"); return; }
    static const uint8_t SYNC[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50, 0x89, 0xD3, 0x49, 0x89, 0xFF, 0x49 };
    // mov rax,[r15+0x10]; mov rdi,[rax+0x60]; call NewModGhostManager::UpdateST; mov rax,[r15+0x10]; mov rsi,[rax+0x178];
    // mov rdi,[rax+0x190]; call OverwatchManager::Update
    static const uint8_t SYNC_OW[32] = { 0x49, 0x8B, 0x47, 0x10, 0x48, 0x8B, 0x78, 0x60, 0xE8, 0xB6, 0x44, 0x00, 0x00, 0x49, 0x8B, 0x47, 0x10,
                                         0x48, 0x8B, 0xB0, 0x78, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xB8, 0x90, 0x01, 0x00, 0x00, 0xE8 };
    // mov rdi,[rbp-0x108]; call BindingContext::GetCommandProxy; mov rcx,[rax]; mov rbx,[rcx+0x18]; mov rcx,[r12+0x28];
    // mov edx,1; mov rdi,rax; mov rsi,r15; call rbx
    static const uint8_t SEND[37] = { 0x48, 0x8B, 0xBD, 0xF8, 0xFE, 0xFF, 0xFF, 0xE8, 0x15, 0x27, 0x11, 0x00, 0x48, 0x8B, 0x08, 0x48, 0x8B, 0x59,
                                      0x18, 0x49, 0x8B, 0x4C, 0x24, 0x28, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x48, 0x89, 0xC7, 0x4C, 0x89, 0xFE, 0xFF, 0xD3 };
    static const uint8_t TICK[20] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x08, 0x01, 0x00, 0x00 };
    static const uint8_t UPDATE[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x49, 0x89, 0xF6, 0x48, 0x89, 0xFB, 0x48, 0x8B, 0x03 };
    static const uint8_t TOGGLE[16] = { 0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x4C, 0x8B, 0x47, 0x20, 0x4D, 0x85, 0xC0, 0x74, 0x41, 0x4C };
    static const uint8_t ISOW[16] = { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x47, 0x20, 0x48, 0x85, 0xC0, 0x74, 0x45, 0x48, 0x83, 0xC7 };
    static const uint8_t ID2ENT[16] = { 0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x89, 0xD3, 0x48, 0x8D, 0x0D, 0xC1, 0x12, 0x54, 0x00, 0x31 };
    static const uint8_t FINDQ[16] = { 0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x48, 0x89, 0xFB, 0xE8, 0x32, 0x47, 0x36, 0x00, 0x89, 0xC1 };
    static const uint8_t QCOUNT[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC };
    static const uint8_t BUILD[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC };
    static const uint8_t BTUPD[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x50, 0x49, 0x89, 0xF6, 0x48, 0x89, 0xFB };
    static const uint8_t BTREDRAW[8] = { 0x80, 0xBB, 0x6D, 0x03, 0x00, 0x00, 0x00, 0x75 };
    int ok = 1;
    if (!slot_is(VT_SYNC, FN_SYNC) || !mh_same(FN_SYNC, SYNC, 16) || !mh_same(AT_SYNC_OW, SYNC_OW, 32)) { mh_log("ModUI::OnSyncPoint is not as in 1.0.3 (or hooked by another add-on)"); ok = 0; }
    if (!slot_is(VT_BUILD1, FN_BUILD) || !slot_is(VT_BUILD2, FN_BUILD) || !mh_same(FN_BUILD, BUILD, 16)) { mh_log("CommandProxy::DoBuildSquad / its vtable slots are not as in 1.0.3"); ok = 0; }
    if (!mh_same(AT_TICK_SEND, SEND, 37) || !mh_same(FN_TICK, TICK, 20) || !mh_same(FN_UPDATE, UPDATE, 16)) { mh_log("the overwatch tick is not as in 1.0.3"); ok = 0; }
    if (!mh_same(FN_TOGGLE, TOGGLE, 16) || !mh_same(FN_ISOW, ISOW, 16) || !mh_same(FN_ID2ENT, ID2ENT, 16) || !mh_same(FN_FINDQ, FINDQ, 16)
        || !mh_same(FN_QCOUNT, QCOUNT, 16)) { mh_log("overwatch / production queue functions are not as in 1.0.3"); ok = 0; }
    if (!slot_is(VT_BTUPD, FN_BTUPD) || !mh_same(FN_BTUPD, BTUPD, 16) || !mh_same(AT_BTREDRAW, BTREDRAW, 8)) { mh_log("BindingBuildTech::OnUpdate is not as in 1.0.3"); ok = 0; }
    if (!ok) { mh_log("OverwatchOnce %d: NOT installed (the game's own overwatch)", OO_VERSION); return; }
    o_sync = (Sync_t)mh_at(FN_SYNC); o_build = (Build_t)mh_at(FN_BUILD); o_btupd = (BtUpd_t)mh_at(FN_BTUPD);
    p_toggle = (Toggle_t)mh_at(FN_TOGGLE); p_isow = (IsOw_t)mh_at(FN_ISOW); p_id2ent = (Id2Ent_t)mh_at(FN_ID2ENT);
    p_findq = (FindQ_t)mh_at(FN_FINDQ); p_qcount = (QCount_t)mh_at(FN_QCOUNT);
    if (!put_slot(VT_BUILD1, hk_build) || !put_slot(VT_BUILD2, hk_build) || !put_slot(VT_BTUPD, hk_btupd) || !put_slot(VT_SYNC, hk_sync)) {
        mh_log("vm_protect failed - hooks may be partial"); return; }
    mh_log("OverwatchOnce %d: an overwatch order builds one squad, then overwatch switches off - installed (ModUI::OnSyncPoint %#lx, "
           "CommandProxy::DoBuildSquad %#lx/%#lx, BindingBuildTech::OnUpdate %#lx vtable slots)%s", OO_VERSION, VT_SYNC, VT_BUILD1, VT_BUILD2,
           VT_BTUPD, g_enabled ? "" : " - Enabled=0 for now");
}
