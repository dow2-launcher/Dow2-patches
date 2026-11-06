// ReplayTakeover.dll - continue a replay as a normal game from any moment, as any of its players (DoW2: Retribution 3.19,
// Elite; experiment).
//
// Owner 06.11.2026: "реплей запускаем и с любой минуты/тика можем его продолжить - за любого игрока".
// How the player uses it: while watching a replay, select any unit of a player (click it, or his hero in the player list
// at the bottom), open the menu (Menu / Esc) and press "Continue as <name>". The replay turns into an ordinary skirmish
// from that very tick: the same world, the selected player is yours, the others are played by the computer (or stand,
// ReplayTakeover.ini [ReplayTakeover] Others=idle).
//
// Why it works: a replay IS the live simulation of the match, fed with the recorded orders. DOW2.exe runs every game
// through a SimulationController (GameState+0x154) that takes each tick's orders from a SynchronizationInterface (+4):
// P2PNetworkSynchronization online, EchoSynchronization (vtable 0x01087A54) in a game against the computer - it gives back
// next tick exactly the local orders it got - and PlaybackSynchronization (0x010B9E2C), which reads them from the .rec.
// The takeover swaps the playback source for a new EchoSynchronization (ctor 0x404B74, the one 0x407947 makes for a
// skirmish) at a tick boundary, so from the next tick on the simulation runs on the orders of this machine, and makes the
// rest of the game see a skirmish with a local player:
//   - game type settings+0x118: 0x800 (playback) -> 4 (SP skirmish); the computer players think again (0x424496 skips
//     them only in a replay), Relic gets no stats (ReportMatchStats: "not a network game");
//   - the slots of GameInfo (settings+0x118+0x810, 0xA8 each; +0 type, +0x9C player id): the chosen one -> 0 (local),
//     the other players -> 1 (computer, local) with an AI made by the game's own Misc_AIControlLocalPlayer path (0x426875,
//     for the player in GameState+0x6C, which is set to each of them in turn), or 3 (human, remote: they stand);
//   - GameState+0x6C = the chosen player (the local player of GameObj, as the dev command switchplayer 0x426627 sets it);
//   - world+0x174 (the playback length the replay panels read) = 0, sim rate back to 10 ticks/s;
//   - the HUD is rebuilt (GameUI vtbl+0x68 / +0x64, the same calls switchplayer makes through 0x423E9B / 0x423B98):
//     a player's HUD - resources, build menus, abilities - for the new local player;
//   - the minimap: a new HUD gets the minimap texture only when MiniMapScene re-creates it, so the minimap manager's
//     "enabled" byte (*(0x013351A0)+0xAEC) is off for 3 frames and back on (otherwise the minimap stays black).
// The switch runs only between two ticks: with the threaded simulation (GameState+0x1AC) at the frame's call 0x4242AA
// when the sim job of the last tick is done, otherwise at the start of the frame (call 0x40E53C).
//
// The button: the pause menu (ui/movies/hud/menu_panel.gfx) has "Concede match" (BindingConcedeMatchButton, vtable
// 0x0110C16C), hidden outside online matches. In a replay its IsActive says yes, its caption becomes "Continue as
// <name>" and its click (the delegate 0xAD2EB0 set by its Init at 0xAD2E63) does the takeover; in every other game the
// button is the game's own, untouched. No overlay, no synthetic input, no files read or written during the game except
// this add-on's log.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../common/gfxui.h"

#define VERSION "1"

#define G_SETTINGS   0x01335A78   // +0x110 GameObj, +0x118 game type (0x800 playback), +0x118+0x810 slots, +0x814 count
#define G_WORLD      0x013356A4   // +0xF8 entities (vtbl+0x10 = by id), +0x194 PlayerManager, +0x174 playback length, +0x178 tick
#define G_HUD        0x01335720   // +8: +0x20 selection, +0x68 sim rate (vtbl+0x28 set, +0x2C get)
#define G_UIMGR      0x013356A8   // +0x14 GameUI (vtbl+0x18 = unpause), +0x34 HUD (vtbl+0x34(1) after a rebuild)
#define G_MINIMAP    0x013351A0   // +0xAEC enabled
#define AT_FRAME     0x0040E53C   // call GameObj::Frame (stdcall GameObj, int, char, float)
#define FN_FRAME     0x004240F5
#define AT_SIMTHR    0x004242AA   // call 0x42DA47 (eax = threaded sim manager, +0x10 the sim job)
#define FN_SIMTHR    0x0042DA47
#define FN_AI_LOCAL  0x00426875   // stdcall(GameObj): an AI for the player in GameState+0x6C
#define FN_NEW       0x00DAA9B6   // operator new (cdecl)
#define FN_ECHO_CTOR 0x00404B74   // EchoSynchronization ctor, stdcall(this), 0x34 bytes
#define FN_SELECTION 0x00ADEE60   // thiscall(selection, vector<EntityID> *): the selected entities
#define VT_PLAYBACK  0x010B9E2C
#define VT_CONCEDE   0x0110C16C   // BindingConcedeMatchButton
#define FN_CC_ACTIVE 0x00AD2D80   // slot 3: IsActive (thiscall, 1 arg)
#define FN_CC_CLICK  0x00AD2EB0   // the click delegate (thiscall widget, event)
#define AT_CC_CLICK  0x00AD2E63   // mov ecx, 0xAD2EB0 in its Init
#define FN_NOP4      0x009AB7D0   // slot 6 (Update): ret 4
#define T_PLAYBACK   0x800
#define T_SKIRMISH   4

typedef int (__stdcall *Frame_t)(void *, int, int, float);
typedef char (__stdcall *AiLocal_t)(void *);
typedef void *(__cdecl *New_t)(unsigned);
typedef void *(__stdcall *Ctor_t)(void *);
typedef void *(__fastcall *GetPl_t)(void *pm, void *edx, DWORD id);
typedef void *(__fastcall *Owner_t)(void *entity, void *edx);
typedef const wchar_t *(__fastcall *CStr_t)(void *ls, void *edx);
typedef char (__fastcall *IsDone_t)(void *job, void *edx);
typedef void (__fastcall *Fast1_t)(void *);
typedef void (__fastcall *SetF_t)(void *o, void *edx, float f);
typedef void (__fastcall *V1_t)(void *o, void *edx, int a);
typedef void *(__fastcall *ById_t)(void *em, void *edx, DWORD id);
typedef void (__fastcall *Sel_t)(void *sel, void *edx, void *vec);
typedef void (__stdcall *Dealloc_t)(void *, unsigned);
typedef char (__fastcall *Active_t)(void *self, void *edx, void *ctx);
typedef void (__fastcall *Click_t)(void *self, void *edx, void *widget, int ev);

static wchar_t g_log[MAX_PATH]; static CRITICAL_SECTION g_cs;
static void logmsg(const char *fmt, ...) {
    EnterCriticalSection(&g_cs);
    FILE *f = _wfopen(g_log, L"a");
    if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_cs);
}
static int ok(const void *p, int n) { return p && !IsBadReadPtr(p, n); }
#define U8(p, o)  (*(unsigned char *)((char *)(p) + (o)))
#define I32(p, o) (*(int *)((char *)(p) + (o)))
#define P32(p, o) (*(char **)((char *)(p) + (o)))

static GetPl_t g_getpl; static Owner_t g_owner; static CStr_t g_cstr; static IsDone_t g_isdone; static Dealloc_t g_dealloc;
static int g_others_ai = 1, g_ru;

static char *settings(void) { return ok((void *)G_SETTINGS, 4) && ok(*(char **)G_SETTINGS, 0x1000) ? *(char **)G_SETTINGS : NULL; }
static char *world(void) { return ok((void *)G_WORLD, 4) && ok(*(char **)G_WORLD, 0x200) ? *(char **)G_WORLD : NULL; }
static char *gamestate(char *st) { char *go = st ? P32(st, 0x110) : NULL; return ok(go, 8) && ok(P32(go, 4), 0x200) ? P32(go, 4) : NULL; }
static int is_replay(void) { char *st = settings(); return st && I32(st, 0x118) == T_PLAYBACK; }
static void *player_by_id(DWORD id) { char *w = world(); return w && g_getpl ? g_getpl(w + 0x194, 0, id) : NULL; }
static DWORD player_id(void *p) { return ((DWORD (__fastcall *)(void *, void *))(*(void ***)p)[2])(p, 0); }
static const wchar_t *player_name(void *p) {
    const wchar_t *s = g_cstr && ok((char *)p + 0xC, 4) ? g_cstr((char *)p + 0xC, 0) : NULL;
    return s && ok(s, 2) ? s : L"?";
}
static const char *u8(const wchar_t *w) {           // for the log (main thread only)
    static char b[4][128]; static int k; k = (k + 1) & 3;
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, b[k], sizeof b[k], NULL, NULL)) strcpy(b[k], "?");
    return b[k];
}
static int slot_of(DWORD id) {                     // index of the GameInfo slot of this player id, -1 = none
    char *st = settings(); if (!st) return -1;
    char *arr = P32(st, 0x118 + 0x810); int n = I32(st, 0x118 + 0x814);
    for (int i = 0; i < n && i < 16; i++) if (ok(arr + i * 0xA8, 0xA8) && (DWORD)I32(arr + i * 0xA8, 0x9C) == id) return i;
    return -1;
}

// ---- the player of the selection: the owner of the first selected entity that has one (main thread) ----
static void *selected_player(void) {
    char *hud = ok((void *)G_HUD, 4) ? *(char **)G_HUD : NULL, *h = ok(hud, 12) ? P32(hud, 8) : NULL;
    char *sel = ok(h, 0x24) ? P32(h, 0x20) : NULL, *w = world();
    if (!ok(sel, 0x3A0) || !w || !g_owner) return NULL;
    DWORD *vec[3] = {0, 0, 0};                     // STLport vector<EntityID>: begin, end, end of storage
    ((Sel_t)FN_SELECTION)(sel, 0, vec);
    void *found = NULL, *em = P32(w, 0xF8);
    for (DWORD *p = vec[0]; p && p < vec[1] && !found && ok(em, 4); p++) {
        void *e = ((ById_t)(*(void ***)em)[0x10 / 4])(em, 0, *p);
        if (!ok(e, 0x54) || (I32(e, 0x50) & 0x18) != 0x18) continue;
        void *o = g_owner(e, 0);
        if (o && slot_of(player_id(o)) >= 0) found = o;
    }
    if (vec[0] && g_dealloc) g_dealloc(vec[0], (unsigned)((char *)vec[2] - (char *)vec[0]));
    return found;
}

// ---- the takeover itself (main thread, between two ticks) ----
static void hud_down(char *gs) { void *gu = P32(gs, 4); ((Fast1_t)(*(void ***)gu)[0x68 / 4])(gu); }
static void hud_up(char *gs) { void *gu = P32(gs, 4); ((Fast1_t)(*(void ***)gu)[0x64 / 4])(gu); }
static int g_mm_frames; static unsigned char g_mm_saved;
static void minimap_kick(void) {
    char *m = ok((void *)G_MINIMAP, 4) ? *(char **)G_MINIMAP : NULL; if (!ok(m, 0xB10)) return;
    g_mm_saved = U8(m, 0xAEC); U8(m, 0xAEC) = 0; g_mm_frames = 3;
}
static void minimap_tick(void) {
    if (!g_mm_frames || --g_mm_frames) return;
    char *m = ok((void *)G_MINIMAP, 4) ? *(char **)G_MINIMAP : NULL;
    if (ok(m, 0xB10)) U8(m, 0xAEC) = g_mm_saved ? g_mm_saved : 1;
}

static volatile LONG g_pending;                    // player id to take, 0 = none
static void *g_takenGame;                          // GameObj of the game we turned into a skirmish
static int sim_busy(char *gs) {
    char *m = P32(gs, 0x1A8); if (!ok(m, 0x14)) return 0;
    void *job = P32(m, 0x10); if (!job) return 0;
    return g_isdone ? !g_isdone(job, 0) : 1;
}
static void take(DWORD id) {
    char *st = settings(), *gs = gamestate(st), *w = world();
    char *go = st ? P32(st, 0x110) : NULL;
    if (!gs || !w || I32(st, 0x118) != T_PLAYBACK || I32(gs, 0x34) != 5) { logmsg("not in a running replay - nothing done"); return; }
    char *sc = P32(gs, 0x154), *sync = ok(sc, 0x78) ? P32(sc, 4) : NULL;
    void *tp = player_by_id(id); int ts = slot_of(id);
    if (!ok(sync, 8) || *(DWORD *)sync != VT_PLAYBACK || !tp || ts < 0) { logmsg("player %lu: no player / not a playback (sync %p) - nothing done", id, sync); return; }
    char *arr = P32(st, 0x118 + 0x810); int n = I32(st, 0x118 + 0x814);
    logmsg("continue as %s (player %lu, slot %d) at tick %d, others: %s", u8(player_name(tp)), id, ts, I32(w, 0x178), g_others_ai ? "computer" : "stand");
    { char *hud = *(char **)G_HUD, *h = ok(hud, 12) ? P32(hud, 8) : NULL, *sr = ok(h, 0x70) ? P32(h, 0x68) : NULL;
      if (ok(sr, 4)) ((SetF_t)(*(void ***)sr)[0x28 / 4])(sr, 0, 10.0f); }          // normal speed (a replay may run at 4x)
    hud_down(gs);
    I32(st, 0x118) = T_SKIRMISH;
    for (int i = 0; i < n; i++) {
        char *s = arr + i * 0xA8; int t = I32(s, 0), nt = t;
        if (i == ts) nt = 0;                               // local human
        else if (t == 0 || t == 1 || t == 3 || t == 4) nt = g_others_ai ? 1 : 3;   // computer here / a human who stands
        else if (t == 2) nt = 5;                           // the recording observer -> remote observer
        if (nt != t) I32(s, 0) = nt;
    }
    if (g_others_ai) for (int i = 0; i < n; i++) {
        char *s = arr + i * 0xA8; if (i == ts || I32(s, 0) != 1) continue;
        void *p = player_by_id(I32(s, 0x9C)); if (!p) continue;
        P32(gs, 0x6C) = p; char r = ((AiLocal_t)FN_AI_LOCAL)(go);
        logmsg("  computer for %s (player %d): %s", u8(player_name(p)), I32(s, 0x9C), r ? "on" : "already / failed");
    }
    P32(gs, 0x6C) = tp; U8(gs, 0x74) = 1;
    void *echo = ((New_t)FN_NEW)(0x34);
    if (echo) { ((Ctor_t)FN_ECHO_CTOR)(echo); P32(sc, 4) = echo; I32(sc, 0x74) = 3; }   // the playback source stays alive, unused
    I32(w, 0x174) = 0;
    hud_up(gs);
    char *um = *(char **)G_UIMGR; void *o = ok(um, 0x38) ? P32(um, 0x34) : NULL; if (ok(o, 4)) ((V1_t)(*(void ***)o)[0x34 / 4])(o, 0, 1);
    minimap_kick(); g_takenGame = go;
    logmsg("done: a skirmish now, %s is yours", u8(player_name(tp)));
}
static void try_take(char *gs) {
    LONG id = g_pending; if (!id || sim_busy(gs)) return;
    InterlockedExchange(&g_pending, 0); take((DWORD)id);
}

// threaded simulation: before the frame schedules the next sim tick
void __stdcall pre_simthr(char *mgr) {
    (void)mgr; char *gs = gamestate(settings()); if (g_pending && gs) try_take(gs);
}
void *g_simthr = (void *)FN_SIMTHR;
__asm__(".globl _simthr_stub\n_simthr_stub:\n  pushal\n  pushl %eax\n  call _pre_simthr@4\n  popal\n  jmp *_g_simthr\n");
void simthr_stub(void);

static int __stdcall my_frame(void *go, int a, int b, float dt) {
    minimap_tick();
    char *gs = ok(go, 8) ? P32(go, 4) : NULL;
    if (g_takenGame && (!ok(gs, 0x40) || I32(gs, 0x34) != 5 || is_replay())) g_takenGame = NULL;   // that game is over
    if (g_pending && ok(gs, 0x1B0) && !U8(gs, 0x1AC)) try_take(gs);   // simulation in this thread: idle here
    return ((Frame_t)FN_FRAME)(go, a, b, dt);
}

// ---- the menu button ("Concede match" in a replay) ----
static Active_t g_ccActive = (Active_t)FN_CC_ACTIVE;
static char __fastcall my_cc_active(void *self, void *edx, void *ctx) {
    if (is_replay() && !g_pending) return 1;
    return g_ccActive(self, edx, ctx);
}
static void *g_lastShown; static DWORD g_lastTick; static wchar_t g_caption[160];
static void set_caption(void *self, const wchar_t *text) {
    char *wdg = ok((char *)self + 0x24, 4) ? P32(self, 0x24) : NULL; void *mv = gfx_movie_of_widget(wdg);
    if (!mv) return;                               // set every time: the menu movie may put its own text back
    GfxVal a[2]; a[0] = gv_str("btn_concede_match.text_resume"); memset(&a[1], 0, sizeof a[1]); a[1].type = GV_WSTRING; a[1].v.ws = text;
    if (gfx_invoke(mv, "Receive_SetText", NULL, a, 2)) wcsncpy(g_caption, text, 159);
}
static int taken_here(void) { char *st = settings(); return st && g_takenGame && P32(st, 0x110) == g_takenGame && !is_replay(); }
static void *g_hiddenWidget;                       // the button we hid in a continued game (the menu outlives the game)
static void widget_show(char *wdg, int on) {
    if (U8(wdg, 0x209) == on) return;
    U8(wdg, 0x209) = (unsigned char)on; ((Fast1_t)(*(void ***)wdg)[0x34 / 4])(wdg);   // as the game's own widget code does
}
static void __fastcall my_cc_update(void *self, void *edx, void *arg) {
    (void)edx; (void)arg;
    char *wdg = ok((char *)self + 0x24, 4) ? P32(self, 0x24) : NULL;
    if (!ok(wdg, 0x20C) || !ok(*(void **)wdg, 0x38)) return;
    if (taken_here()) {                            // a continued game: no Concede (it is a skirmish now)
        if (U8(wdg, 0x209)) { widget_show(wdg, 0); g_hiddenWidget = wdg; g_caption[0] = 0; }
        return;
    }
    if (!is_replay()) return;
    if (g_hiddenWidget == wdg) { widget_show(wdg, 1); g_hiddenWidget = NULL; }
    DWORD now = GetTickCount(); if (now - g_lastTick < 300) return;
    g_lastTick = now;
    void *p = selected_player(); wchar_t t[160];
    if (p) _snwprintf(t, 159, g_ru ? L"Продолжить за: %ls" : L"Continue as %ls", player_name(p));
    else wcscpy(t, g_ru ? L"Продолжить: выделите юнит" : L"Continue: select a unit");   // the button fits ~27 letters
    t[159] = 0;
    if (wdg != g_lastShown) { g_lastShown = wdg; g_caption[0] = 0; }   // a new menu movie: its own caption again
    set_caption(self, t);
}
static void __fastcall my_cc_click(void *self, void *edx, void *widget, int ev) {
    if (taken_here()) return;
    if (!is_replay()) { ((Click_t)FN_CC_CLICK)(self, edx, widget, ev); return; }
    if (ev != 0 || !ok((char *)self + 0x24, 4) || widget != P32(self, 0x24)) return;
    void *p = selected_player();
    if (!p) { g_lastTick = 0; logmsg("menu button: nothing of a player selected"); return; }
    InterlockedExchange(&g_pending, (LONG)player_id(p));
    logmsg("menu button: continue as %s", u8(player_name(p)));
    char *um = *(char **)G_UIMGR; void *gu = ok(um, 0x18) ? P32(um, 0x14) : NULL;      // close the menu as Resume does
    if (ok(gu, 4)) ((V1_t)(*(void ***)gu)[0x18 / 4])(gu, 0, 0);
}

// ---- install ----
static int patch_call(DWORD at, DWORD expect, void *fn) {      // E8 rel32 at `at` calling `expect` -> fn
    unsigned char *t = (unsigned char *)at; DWORD old;
    if (IsBadReadPtr(t, 5) || t[0] != 0xE8 || (DWORD)(t + 5) + *(int *)(t + 1) != expect) return 0;
    VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old); *(int *)(t + 1) = (DWORD)fn - (DWORD)(t + 5);
    VirtualProtect(t, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 5);
    return 1;
}
static int patch_imm(DWORD at, unsigned char op, DWORD expect, void *fn) {   // op imm32 -> op fn
    unsigned char *t = (unsigned char *)at; DWORD old;
    if (IsBadReadPtr(t, 5) || t[0] != op || *(DWORD *)(t + 1) != expect) return 0;
    VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old); *(DWORD *)(t + 1) = (DWORD)fn;
    VirtualProtect(t, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 5);
    return 1;
}
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_cs);
    wchar_t dir[MAX_PATH], ini[MAX_PATH];
    GetModuleFileNameW(h, dir, MAX_PATH); wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0; else dir[0] = 0;
    wcscpy(g_log, dir); wcscat(g_log, L"ReplayTakeover.log"); wcscpy(ini, dir); wcscat(ini, L"ReplayTakeover.ini");
    if (already_loaded("ReplayTakeover")) { logmsg("another ReplayTakeover copy is already active in this game, this one stays idle"); return TRUE; }
    wchar_t v[32]; GetPrivateProfileStringW(L"ReplayTakeover", L"Others", L"ai", v, 32, ini);
    g_others_ai = _wcsicmp(v, L"idle") != 0;
    GetPrivateProfileStringW(L"ReplayTakeover", L"Language", L"auto", v, 32, ini);
    g_ru = !_wcsicmp(v, L"ru") || (!_wcsicmp(v, L"auto") && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
    HMODULE se = GetModuleHandleA("SimEngine.dll"), xt = GetModuleHandleA("XThread.dll"), lz = GetModuleHandleA("Localizer.dll"),
            sp = GetModuleHandleA("STLPort.dll");
    g_getpl = se ? (GetPl_t)GetProcAddress(se, "?GetPlayerFromID@PlayerManager@@QAEPAVPlayer@@K@Z") : NULL;
    g_owner = se ? (Owner_t)GetProcAddress(se, "?GetOwner@Entity@@QAEPAVPlayer@@XZ") : NULL;
    g_isdone = xt ? (IsDone_t)GetProcAddress(xt, "?IsDone@Job@XThread@@QBE_NXZ") : NULL;
    g_cstr = lz ? (CStr_t)GetProcAddress(lz, "?c_str@LocString@@QBEPB_WXZ") : NULL;
    g_dealloc = sp ? (Dealloc_t)GetProcAddress(sp, "?deallocate@__new_alloc@_STL@@SGXPAXI@Z") : NULL;
    // all or nothing: DOW2.exe 3.19 bytes where we patch, and what we call
    unsigned char *f = (unsigned char *)AT_FRAME, *s = (unsigned char *)AT_SIMTHR, *c = (unsigned char *)AT_CC_CLICK;
    DWORD *vt = (DWORD *)VT_CONCEDE;
    int good = g_getpl && g_owner && g_isdone && !IsBadReadPtr(f, 5) && f[0] == 0xE8 && !IsBadReadPtr(s, 5) && s[0] == 0xE8
        && !IsBadReadPtr(c, 5) && c[0] == 0xB9 && *(DWORD *)(c + 1) == FN_CC_CLICK && !IsBadReadPtr(vt, 0x1C)
        && vt[3] == FN_CC_ACTIVE && vt[6] == FN_NOP4 && *(DWORD *)VT_PLAYBACK == 0x0066AD20;
    if (!good) { logmsg("ReplayTakeover " VERSION ": not DOW2.exe 3.19 (or a module missing) - nothing changed"); return TRUE; }
    int r = patch_call(AT_FRAME, FN_FRAME, (void *)my_frame) & patch_call(AT_SIMTHR, FN_SIMTHR, (void *)simthr_stub);
    if (!r) { logmsg("ReplayTakeover " VERSION ": the frame calls are taken by something else - nothing changed"); return TRUE; }
    gfx_patch_slot((DWORD)&vt[3], FN_CC_ACTIVE, (void *)my_cc_active);
    gfx_patch_slot((DWORD)&vt[6], FN_NOP4, (void *)my_cc_update);
    patch_imm(AT_CC_CLICK, 0xB9, FN_CC_CLICK, (void *)my_cc_click);
    logmsg("ReplayTakeover " VERSION ": in a replay - select a unit of a player, Menu -> \"Continue as ...\"; others: %s, %s",
           g_others_ai ? "computer" : "stand", g_ru ? "ru" : "en");
    if (!g_cstr || !g_dealloc) logmsg("(names %s, selection buffer %s)", g_cstr ? "ok" : "missing", g_dealloc ? "freed" : "kept");
    return TRUE;
}
