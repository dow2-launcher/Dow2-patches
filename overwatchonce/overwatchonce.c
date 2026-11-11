// OverwatchOnce.dll - an overwatch order (right-click on a unit's build button) builds ONE squad and switches itself off
// (Dawn of War II: Retribution 3.19, Elite and vanilla).
//
// How the game does overwatch (DOW2.exe 3.19.10320, 11.11.2026): it is the HUD's, not the simulation's. A right-click on a
// unit button of a building (BindingBuildTech click handler 0x00AEA450, the "overwatch" widgets of the tech tree) toggles an
// entry { building entity id -> squad blueprint } in a map of the HUD's overwatch object (*(*(0x01335720)+8)+0xCC; +0x0C the
// map, +0x10 its size; +0x00 the auto-reinforce list of squads). Every HUD update (0x00ACBFB6 calls it) the overwatch tick
// 0x00B26890(obj, ctx) walks the map: for a building that is alive and has no command of ours still on its way
// (CommandProxy +0x7C), if it can build the squad now (CommandProxy +0x88 - requisition, power, population, the queue) it
// sends the normal build order, CommandProxy +0x0C (0x00AF4380: WorldDoCommandEntity CMD_BuildSquad = 3, the very order of a
// left-click). The entry stays, so the next time the money is there it builds again - forever.
// There is no overwatch in the simulation and no overwatch command: the other players and the replay only ever see plain
// build orders. Removing an entry is what the game itself does on a second right-click (0x00B26BD0, erase by building id).
//
// OverwatchOnce: when the overwatch tick sends its build order (return address 0x00B26AD8), the building and blueprint are
// remembered with the number of that blueprint in the building's production queue (EntityController::QI(7) +0x54..+0x58,
// items of 0x48 bytes, +0 the blueprint). As soon as the queue has one more of it (the squad started), the entry is erased
// exactly as the second right-click does, before the tick runs again, and the button's overwatch mark is redrawn. If the
// order didn't make it into the queue (the money went elsewhere first), the entry stays and the tick tries again by itself;
// if the tick wants to send again while the first squad is in the queue already (its count hid it: another one of the same
// finished meanwhile), the second order is not sent and the entry erased. Nothing is written into the simulation, no command
// is added: one build order less at most, client side, so there is no desync and the other players don't need the DLL.
// Replays and observers have no overwatch entries, so nothing happens there.
//
// OverwatchOnce.ini [OverwatchOnce]: Enabled=1 (re-read every 2 s: 0 = the game's endless overwatch again), Verbose=0.
// OverwatchOnce.log: version, each hook with its checked bytes, every squad started by overwatch and switched off.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define OO_VERSION    1
#define CALL_TICK     0x00ACBFB6u                    // E8 -> overwatch tick (HUD update), preceded by push ebx; push edi
#define OW_TICK       0x00B26890u                    // void __stdcall tick(OverwatchObj*, ctx*)
#define OW_ERASE      0x00B26BD0u                    // map erase by key: EAX = &entity id, [esp+4] = &obj->map, ret 4
#define RET_SEND      0x00B26AD8u                    // return address of the tick's build order (call eax at 0x00B26AD6)
#define VT_PROXY      0x0110E514u                    // CommandProxy vftable
#define VT_EVPROXY    0x01117704u                    // EventGeneratorCommandProxy (derived; the HUD's)
#define SLOT_BUILD    0x0C                           // BuildSquad(Entity*, int, PropertyBagGroup* squad blueprint), ret 0xC
#define ORIG_BUILD    0x00AF4380u
#define VT_BUILDTECH  0x0110DFCCu                    // BindingBuildTech vftable (the tech tree of a selected building)
#define SLOT_BTUPD    0x18                           // update(ctx), ret 4: redraws the buttons when this+0x1ED is set
#define ORIG_BTUPD    0x00AEA150u
#define QI_PRODQ      7                              // EntityController::QI(7) = the production queue extension
#define PQ_BEGIN      0x54
#define PQ_END        0x58
#define PQ_ITEM       0x48
#define PQ_ADDED      0x60

typedef void (__stdcall *Tick_t)(void *obj, void *ctx);
typedef void (__fastcall *Build_t)(void *self, void *edx, void *ent, int flag, void *bp);
typedef void (__fastcall *BtUpd_t)(void *self, void *edx, void *ctx);
typedef void *(__fastcall *QI_t)(void *ctl, void *edx, unsigned id);
typedef void *(__fastcall *Lookup_t)(void *obj, void *edx, DWORD id);
typedef const char *(__fastcall *Name_t)(void *pbg, void *edx);
typedef void *(__fastcall *Get_t)(void *obj, void *edx);

static Tick_t o_tick; static Build_t o_build, o_build2; static BtUpd_t o_btupd; static QI_t p_qi; static Name_t p_fname;
static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH];
static int g_enabled = 1, g_verbose = 0, g_redraw, g_installed;
static DWORD g_iniAt;
static void *g_ctx;                                   // the tick's ctx while it runs (its CommandProxy is ctx+0x0C)

#define MAXP 16
typedef struct { int used; DWORD ent, t; void *bp; int n0, added0; } Pend;   // a build order sent by overwatch, not seen in the queue yet
static Pend g_p[MAXP];
static DWORD g_drop[MAXP]; static void *g_dropbp[MAXP]; static int g_ndrop;   // erase after the tick (it walks the map)

static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsOverwatchOnce.log", g_dir);
    FILE *f = _wfopen(p, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static void load_ini(void) {
    g_enabled = GetPrivateProfileIntW(L"OverwatchOnce", L"Enabled", 1, g_ini);
    g_verbose = GetPrivateProfileIntW(L"OverwatchOnce", L"Verbose", 0, g_ini);
}

static int rd(const void *p, unsigned n) { return p && !IsBadReadPtr(p, n); }
static DWORD bpid(void *bp) { return rd((char *)bp + 0x50, 4) ? *(DWORD *)((char *)bp + 0x50) : 0xFFFFFFFF; }
static const char *bpname(void *bp) {                // the blueprint's file name, for the log
    const char *s = (bp && p_fname) ? p_fname(bp, NULL) : NULL;
    return rd(s, 1) ? s : "?";
}

// the building by its id, as the tick finds it: ((ctx+4)->+0xF8)->vftable[4](id); NULL when gone or not spawned
static void *entity(void *ctx, DWORD id) {
    if (!rd((char *)ctx + 4, 4)) return NULL;
    void *w = *(void **)((char *)ctx + 4); if (!rd((char *)w + 0xF8, 4)) return NULL;
    void *m = *(void **)((char *)w + 0xF8); if (!rd(m, 4)) return NULL;
    void *e = ((Lookup_t)(*(void ***)m)[4])(m, NULL, id);
    if (!e || !rd((char *)e + 0x54, 4) || (*(DWORD *)((char *)e + 0x50) & 0x18) != 0x18) return NULL;
    return e;
}
// the building's production queue (EntityController::QI(7)): items +0x54..+0x58 (0x48 bytes, +0 the item object whose
// vftable +8 gives its blueprint - the sim's own lookup 0x00871080), +0x60 a counter raised by every item added (0x00871790).
// Returns how many of this blueprint it holds, -1: no queue.
typedef struct { int size, back, added; } QInfo;
static int queued(void *ent, void *bp, QInfo *q) {
    memset(q, 0, sizeof *q); q->added = -1;
    void *ctl = rd((char *)ent + 0x3C, 4) ? *(void **)((char *)ent + 0x3C) : NULL; if (!ctl) return -1;
    char *x = (char *)p_qi(ctl, NULL, QI_PRODQ); if (!x || !rd(x + PQ_BEGIN, PQ_ADDED + 4 - PQ_BEGIN)) return -1;
    char *b = *(char **)(x + PQ_BEGIN), *e = *(char **)(x + PQ_END);
    if (e == b) { q->added = *(int *)(x + PQ_ADDED); return 0; }
    if (!b || e < b || (e - b) % PQ_ITEM || (e - b) > PQ_ITEM * 64 || !rd(b, (unsigned)(e - b))) return -1;
    int n = 0;
    for (char *it = b; it < e; it += PQ_ITEM) {
        void *o = *(void **)it, *ib = NULL;
        if (rd(o, 4) && rd(*(void **)o, 12)) ib = ((Get_t)(*(void ***)o)[2])(o, NULL);
        if (ib == bp || (ib && bpid(ib) == bpid(bp) && bpid(bp) != 0xFFFFFFFF)) { n++; if (it + PQ_ITEM == e) q->back = 1; }
    }
    q->size = (int)((e - b) / PQ_ITEM); q->added = *(int *)(x + PQ_ADDED);
    return n;
}

// the overwatch map: obj+0x0C -> STLport header node (+4 root, +8 leftmost); node +0x08 left, +0x0C right, +0x10 key, +0x14 value
static void *ow_find(void *obj, DWORD id) {
    char *h = *(char **)((char *)obj + 0x0C); if (!rd(h, 0x10)) return NULL;
    char *n = *(char **)(h + 4);
    for (int k = 0; n && k < 64; k++) {
        DWORD key = *(DWORD *)(n + 0x10);
        if (key == id) return *(void **)(n + 0x14);
        n = id < key ? *(char **)(n + 8) : *(char **)(n + 0x0C);
    }
    return NULL;
}
static void ow_erase(void *obj, DWORD id) {          // the game's own erase (the second right-click's)
    DWORD key = id, *pk = &key, r; void *map = (char *)obj + 0x0C;
    __asm__ volatile ("pushl %2\n\tcall *%3" : "=a"(r) : "a"(pk), "r"(map), "r"((DWORD)OW_ERASE) : "ecx", "edx", "memory", "cc");
    (void)r;
    g_redraw = 1;
}

static void pend_drop(int i) { g_p[i].used = 0; }
static int pend_find(DWORD ent) { for (int i = 0; i < MAXP; i++) if (g_p[i].used && g_p[i].ent == ent) return i; return -1; }

// ---- the build order of the overwatch tick (both CommandProxy vftables)
static void build_common(Build_t orig, void *self, void *ent, int flag, void *bp, void *ret) {
    if (ret != (void *)RET_SEND || !g_enabled || !g_ctx || !ent || !bp || !rd((char *)ent + 0x2C, 4)) { orig(self, NULL, ent, flag, bp); return; }
    DWORD id = *(DWORD *)((char *)ent + 0x2C);
    QInfo q; int n = queued(ent, bp, &q), i = pend_find(id);
    if (i >= 0 && g_p[i].bp == bp && n >= 1 && q.added != g_p[i].added0) {
        // sent before, an item was added since and this squad is in the queue: it started (a finished one of the same kept
        // the count) - don't send the next one
        logmsg("building %lu: %s already in the queue (%d of %d) - second order not sent, overwatch off", (unsigned long)id, bpname(bp), n, q.size);
        pend_drop(i);
        if (g_ndrop < MAXP) { g_drop[g_ndrop] = id; g_dropbp[g_ndrop] = bp; g_ndrop++; }
        return;
    }
    orig(self, NULL, ent, flag, bp);
    if (n < 0) {                                     // no production queue to watch: switch it off right after this order
        logmsg("building %lu: %s ordered, no queue to watch - overwatch off now", (unsigned long)id, bpname(bp));
        if (i >= 0) pend_drop(i);
        if (g_ndrop < MAXP) { g_drop[g_ndrop] = id; g_dropbp[g_ndrop] = bp; g_ndrop++; }
        return;
    }
    if (i < 0) { for (int k = 0; k < MAXP && i < 0; k++) if (!g_p[k].used) i = k; if (i < 0) i = 0; }
    if (g_p[i].used && g_verbose) logmsg("building %lu: order not seen in the queue - sent again", (unsigned long)id);
    g_p[i].used = 1; g_p[i].ent = id; g_p[i].bp = bp; g_p[i].n0 = n; g_p[i].added0 = q.added; g_p[i].t = GetTickCount();
    if (g_verbose) logmsg("building %lu: overwatch ordered %s (bp %lu), queue %d with %d of it, added %d", (unsigned long)id, bpname(bp), (unsigned long)bpid(bp), q.size, n, q.added);
}
static void __fastcall hk_build(void *self, void *edx, void *ent, int flag, void *bp) { (void)edx; build_common(o_build, self, ent, flag, bp, __builtin_return_address(0)); }
static void __fastcall hk_build2(void *self, void *edx, void *ent, int flag, void *bp) { (void)edx; build_common(o_build2, self, ent, flag, bp, __builtin_return_address(0)); }

// ---- the overwatch tick (called from the HUD update only, the game's main thread)
static void __stdcall hk_tick(void *obj, void *ctx) {
    DWORD now = GetTickCount();
    if (now - g_iniAt > 2000) {
        int was = g_enabled; load_ini(); g_iniAt = now;
        if (was != g_enabled) logmsg("%s in OverwatchOnce.ini", g_enabled ? "enabled" : "disabled - the game's own overwatch");
    }
    if (g_enabled && obj && ctx) {
        // the orders sent before: started now? (before the tick, so it can't send the next one first)
        for (int i = 0; i < MAXP; i++) {
            Pend *p = &g_p[i]; if (!p->used) continue;
            void *cur = ow_find(obj, p->ent);
            if (cur != p->bp) { if (g_verbose) logmsg("building %lu: overwatch changed or switched off by the player", (unsigned long)p->ent); pend_drop(i); continue; }
            void *e = entity(ctx, p->ent); if (!e) { logmsg("building %lu: gone", (unsigned long)p->ent); pend_drop(i); continue; }
            QInfo q; int n = queued(e, p->bp, &q);
            // started: one more of it in the queue, or an item was added and the last one is it (one of the same finished meanwhile)
            if (n > p->n0 || (q.back && q.added != p->added0)) {
                logmsg("building %lu: %s started %lu ms after the overwatch order (queue %d, %d of it) - overwatch off", (unsigned long)p->ent,
                       bpname(p->bp), (unsigned long)(now - p->t), q.size, n);
                ow_erase(obj, p->ent); pend_drop(i); continue;
            }
            if (now - p->t > 15000) { logmsg("building %lu: %s not in the queue 15 s after the order - overwatch stays", (unsigned long)p->ent, bpname(p->bp)); pend_drop(i); }
        }
    }
    g_ctx = ctx; g_ndrop = 0;
    o_tick(obj, ctx);
    g_ctx = NULL;
    for (int k = 0; k < g_ndrop; k++) if (obj && ow_find(obj, g_drop[k]) == g_dropbp[k]) ow_erase(obj, g_drop[k]);
    g_ndrop = 0;
}

// ---- the tech tree: redraw its buttons (the overwatch mark) after an erase, as the game does after a right-click
static void __fastcall hk_btupd(void *self, void *edx, void *ctx) {
    if (g_redraw && self && rd((char *)self + 0x1ED, 1)) { *((char *)self + 0x1ED) = 1; g_redraw = 0; }
    o_btupd(self, edx, ctx);
}

// ---- install (checked bytes / slots; nothing is hooked when one doesn't match)
static int bytes_ok(DWORD at, const char *hex, const char *what) {
    unsigned char want[32]; int n = 0;
    for (const char *s = hex; s[0] && s[1] && n < 32; s += 2) { unsigned v; sscanf(s, "%2x", &v); want[n++] = (unsigned char)v; }
    if (IsBadReadPtr((void *)at, n)) { logmsg("%s: %08lX unreadable - not hooking", what, (unsigned long)at); return 0; }
    if (memcmp((void *)at, want, n)) {
        char got[80]; for (int i = 0; i < n; i++) sprintf(got + 2 * i, "%02x", ((unsigned char *)at)[i]);
        logmsg("%s: %08lX holds %s, not %s (not 3.19?) - not hooking", what, (unsigned long)at, got, hex); return 0;
    }
    logmsg("%s: %08lX = %s ok", what, (unsigned long)at, hex);
    return 1;
}
static int slot_ok(DWORD at, DWORD expect, const char *what) {
    if (IsBadReadPtr((void *)at, 4)) { logmsg("%s: %08lX unreadable - not hooking", what, (unsigned long)at); return 0; }
    if (*(DWORD *)at != expect) { logmsg("%s: %08lX holds %08lX, not %08lX (hooked by another add-on or not 3.19) - not hooking", what, (unsigned long)at, (unsigned long)*(DWORD *)at, (unsigned long)expect); return 0; }
    logmsg("%s: %08lX -> %08lX ok", what, (unsigned long)at, (unsigned long)expect);
    return 1;
}
static void put32(DWORD at, DWORD v) { DWORD o; VirtualProtect((void *)at, 4, PAGE_EXECUTE_READWRITE, &o); *(DWORD *)at = v; VirtualProtect((void *)at, 4, o, &o); FlushInstructionCache(GetCurrentProcess(), (void *)at, 4); }
static int install(void) {
    HMODULE sm = GetModuleHandleA("SimEngine.dll");
    p_qi = sm ? (QI_t)GetProcAddress(sm, "?QI@EntityController@@QBEPBVEntityExtension@@I@Z") : NULL;
    p_fname = sm ? (Name_t)GetProcAddress(sm, "?GetFilename@PropertyBagGroup@@QBEPBDXZ") : NULL;
    if (!p_qi) { logmsg("SimEngine.dll EntityController::QI not found - not hooking"); return 0; }
    DWORD rel = OW_TICK - (CALL_TICK + 5);
    char call[32]; sprintf(call, "5357e8%02x%02x%02x%02x", (unsigned)(rel & 0xFF), (unsigned)((rel >> 8) & 0xFF), (unsigned)((rel >> 16) & 0xFF), (unsigned)(rel >> 24));
    if (!bytes_ok(CALL_TICK - 2, call, "HUD update -> overwatch tick call") ||
        !bytes_ok(OW_TICK, "6aff689b15ed00", "overwatch tick") ||
        !bytes_ok(OW_ERASE, "8b4c240483ec08", "overwatch erase") ||
        !bytes_ok(RET_SEND - 5, "6a0157ffd0", "overwatch tick build order") ||
        !slot_ok(VT_PROXY + SLOT_BUILD, ORIG_BUILD, "CommandProxy build squad") ||
        !slot_ok(VT_EVPROXY + SLOT_BUILD, ORIG_BUILD, "EventGeneratorCommandProxy build squad") ||
        !slot_ok(VT_BUILDTECH + SLOT_BTUPD, ORIG_BTUPD, "BindingBuildTech update")) return 0;
    o_build = o_build2 = (Build_t)ORIG_BUILD; o_btupd = (BtUpd_t)ORIG_BTUPD; o_tick = (Tick_t)OW_TICK;
    put32(VT_PROXY + SLOT_BUILD, (DWORD)hk_build);
    put32(VT_EVPROXY + SLOT_BUILD, (DWORD)hk_build2);
    put32(VT_BUILDTECH + SLOT_BTUPD, (DWORD)hk_btupd);
    put32(CALL_TICK + 1, (DWORD)hk_tick - (CALL_TICK + 5));   // last: the tick goes through us from now on
    return 1;
}

static DWORD WINAPI worker(LPVOID _) {
    (void)_;
    for (int i = 0; i < 120 && !GetModuleHandleA("SimEngine.dll"); i++) Sleep(500);
    g_installed = install();
    logmsg("OverwatchOnce %d start: an overwatch order builds one squad, then overwatch switches off - %s%s", OO_VERSION,
           g_installed ? "installed" : "NOT installed (the game's own overwatch)", g_enabled ? "" : " (Enabled=0 in the ini for now)");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0; else g_dir[0] = 0;
    _snwprintf(g_ini, MAX_PATH, L"%lsOverwatchOnce.ini", g_dir);
    // one copy per game process: it can come both from Elite_DLLs (launcher) and from dow2-patches\ (winmm loader)
    char mx[64]; wsprintfA(mx, "Local\\dow2patch_OverwatchOnce_%lu", GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, mx);
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("another OverwatchOnce copy is already active in this game, this one stays idle"); return TRUE; }
    load_ini(); g_iniAt = GetTickCount();
    if (!g_enabled) logmsg("OverwatchOnce %d: Enabled=0 in OverwatchOnce.ini - hooks installed, idle until enabled", OO_VERSION);
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
