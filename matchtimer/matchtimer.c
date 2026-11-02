// matchtimer.c - match clock (MM:SS of game time) in Dawn of War II: Retribution 3.19 (Elite and vanilla).
//
// Game time: the clock runs while the simulation ticks and stands still while it doesn't (pause, offline game
// minimized, "waiting for players"), so every player of a match sees the same time, the replay too. A minimized
// online match keeps ticking, so its clock keeps running. Shown in every game, offline vs AI and replays included;
// OnlineOnly=1 keeps it to live multiplayer (the session is online only there).
// Hooks through the DOW2.exe import table (SimEngine.dll), seen with SimProbe:
//   World::SimulatePre  - once per match, at its start (still on the loading screen): the clock is reset;
//   World::Simulate     - every sim tick: the time between two ticks is added, a gap over PAUSE_MS is not;
//   World::SimulatePost - once per match, at its end (victory, defeat or leaving through the menu): clock hidden.
// Shown (v5, 02.11.2026): as text of the game's own Menu button movie (Scaleform), set from the game's UI thread - see
// "the clock" below and common/gfxui.h. Nothing is drawn through Direct3D any more (v1-v4 hooked EndScene/Reset).
// Read-only: nothing in the simulation changes, so no desync; only the player with the DLL sees the clock.
//
// MatchTimer.dll is built from this file alone; Cooldowns.dll and LoadingRatings.dll carry it too (-DMT_EMBED) for the
// match state (see "one match core per game").
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include "../common/gfxui.h"

#define IAT_SIM_PRE     0x00F88154   // World::SimulatePre  (__thiscall, ecx = World), match start
#define IAT_SIM         0x00F885E4   // World::Simulate     (__thiscall, ecx = World), every sim tick (~8 per second)
#define IAT_SIM_POST    0x00F88100   // World::SimulatePost (__thiscall, ecx = World), match end
// SimulatePost comes late, at the end of the loading screen after the match, which shows the last frame with the clock;
// these come when the match itself ends: the game is decided, or the match scripts stop (leaving through the menu)
#define IAT_GAME_OVER   0x00F88498   // SimManager::SetGameOver (__thiscall)
#define IAT_END_SCRIPT  0x00F88170   // Scar::EndScript (__thiscall, returns bool)
// earlier still: the loading screen is built (0x4A08F3, game_load_screen.gfx) and drawn once before the game freezes on it
// to unload the match - built while a match runs, it is the way out, and its one frame must come without the clock
#define LOADSCREEN_ADDR 0x004A08F3
static const unsigned char LOADSCREEN_PRO[5] = {0xB8, 0x71, 0x69, 0xEF, 0x00};   // mov eax, 0xEF6971
#define G_SESSION       0x01335678   // *(*G_SESSION + 4) & 0x558: the session is online (live MP), see docs/OBSERVER_LAG.md
#define ONLINE_MASK     0x558
#define PAUSE_MS        1000         // a longer gap between two ticks is a pause or a lag (only logged now)
#define TICK_MS         100          // one sim tick = 100 ms of game time (measured 02.11.2026: 99.4-99.9 ms per tick)

static wchar_t g_log[MAX_PATH], g_ini[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---------- settings: MatchTimer.ini, re-read while the game runs when the file changes ----------
// v5: the clock is text of the Menu button movie, so the v1-v4 keys of the Direct3D clock (Style, Font, MenuX, Anchor...) are gone
static int g_enabled = 1, g_size = 22, g_shadow = 1, g_onlineOnly = 0;   // FontSize: in the movie's units (the "Menu" label is 20)
static int g_plate = 1, g_plateAlpha = 85;                              // a plate under the clock: a copy of the Menu button's own background
enum { PL_UNDER, PL_LEFT, PL_MENU, PL_TOP };
static int g_place = PL_UNDER, g_gap = 1, g_width = 100, g_height = 30, g_offX = 0, g_offY = 0, g_raise = 8, g_textY = 3;   // where the clock is (MatchTimer.ini Place=under|menu|left, Gap, Width, Height, OffsetX/Y, TextY: the digits down in the plate (a Flash field keeps room under the line), Raise: the Menu button up by this, room under it - the movie clips below its stage, y 75)
static DWORD g_color = 0xFFCAF9FF;                                      // the colour of the "Menu" label
static FILETIME g_iniTime;
static DWORD ini_color(const wchar_t *key, const wchar_t *def) {
    wchar_t s[16]; GetPrivateProfileStringW(L"MatchTimer", key, def, s, 16, g_ini);
    return 0xFF000000 | (wcstoul(*s ? s : def, NULL, 16) & 0xFFFFFF);
}
static int ini_int(const wchar_t *key, int def, int lo, int hi) {
    int v = GetPrivateProfileIntW(L"MatchTimer", key, def, g_ini);
    return v < lo ? lo : v > hi ? hi : v;
}
static void load_ini(void) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a)) g_iniTime = a.ftLastWriteTime;
    g_enabled    = ini_int(L"Enabled", 1, 0, 1);
    { wchar_t pl[16]; GetPrivateProfileStringW(L"MatchTimer", L"Place", L"under", pl, 16, g_ini); g_place = !_wcsicmp(pl, L"left") ? PL_LEFT : !_wcsicmp(pl, L"menu") ? PL_MENU : !_wcsicmp(pl, L"top") ? PL_TOP : PL_UNDER; }
    g_size       = ini_int(L"FontSize", g_place == PL_MENU ? 17 : g_place == PL_UNDER ? 18 : 20, 10, 40);
    g_shadow     = ini_int(L"Shadow", 1, 0, 1);
    g_onlineOnly = ini_int(L"OnlineOnly", 0, 0, 1);
    g_color      = ini_color(L"Color", g_place == PL_TOP ? L"FFFFFF" : L"CAF9FF");   // top: white like the score
    g_plate      = ini_int(L"Plate", 1, 0, 1);
    g_gap        = ini_int(L"Gap", g_place == PL_LEFT ? 6 : 2, -40, 200);
    g_width      = ini_int(L"Width", g_place == PL_LEFT ? 184 : 100, 40, 400);
    g_height     = ini_int(L"Height", g_place == PL_LEFT ? 48 : 34, 16, 100);
    g_offX       = ini_int(L"OffsetX", g_place == PL_TOP ? -2 : 0, -400, 400);
    g_offY       = ini_int(L"OffsetY", g_place == PL_TOP ? 3 : 0, -200, 200);
    g_raise      = ini_int(L"Raise", g_place == PL_UNDER ? 8 : 0, 0, 20);
    g_textY      = ini_int(L"TextY", 3, -30, 30);
    g_plateAlpha = ini_int(L"PlateAlpha", 85, 0, 100);
}
static void clock_restyle(void);
static void reload_ini(DWORD now) {
    static DWORD last;
    if (now - last < 1000) return;
    last = now;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a) || !CompareFileTime(&a.ftLastWriteTime, &g_iniTime)) return;
    load_ini();
    logmsg("MatchTimer.ini re-read: Enabled=%d FontSize=%d Color=%06lX Shadow=%d OnlineOnly=%d", g_enabled, g_size,
           (unsigned long)(g_color & 0xFFFFFF), g_shadow, g_onlineOnly);
    clock_restyle();
}

// ---------- match clock: sim ticks (sim thread) ----------
typedef void (__fastcall *ThisFn)(void *, void *);
static ThisFn o_pre, o_sim, o_post;
static void *volatile g_world;                  // World of the current match
static volatile int g_inMatch;                  // between SimulatePre and SimulatePost
static volatile DWORD g_play, g_lastTick;       // game time of the match in ms, GetTickCount() at the last sim tick
static volatile LONG g_ticks;                   // sim ticks in this match
static volatile DWORD g_wall0;                  // GetTickCount() at the first sim tick (the log at the end: ms per tick)
static volatile int g_over;                     // the match ended (SetGameOver / EndScript): no clock, no cooldown numbers

static DWORD session_flags(void) {
    DWORD *g = (DWORD *)G_SESSION;
    if (IsBadReadPtr(g, 4) || !*g || IsBadReadPtr((void *)(*g + 4), 4)) return 0;
    return *(DWORD *)(*g + 4);
}
static void match_start(void *world, const char *why) {
    logmsg("match start (%s): World %p, previous %p, session flags %lX", why, world, g_world, session_flags());
    g_world = world; g_play = 0; g_lastTick = 0; g_ticks = 0; g_inMatch = 1; g_over = 0; g_wall0 = 0;
}
// how long a sim tick is (02.11.2026): game time counted, ticks, wall time since the first tick - to see whether the game
// time can be ticks x a fixed tick (a replay played fast, lags) instead of the wall-clock gaps between ticks
static void log_tick_rate(const char *when) {
    DWORD wall = g_wall0 ? GetTickCount() - g_wall0 : 0;
    logmsg("tick rate (%s): %lu ms of game time in %ld ticks = %.1f ms per tick; %lu ms of wall time since the first tick = %.1f ms per tick",
           when, g_play, g_ticks, g_ticks ? (double)g_play / g_ticks : 0.0, wall, g_ticks ? (double)wall / g_ticks : 0.0);
}
static void __fastcall hk_pre(void *self, void *edx) {
    match_start(self, "SimulatePre");
    o_pre(self, edx);
}
static void __fastcall hk_post(void *self, void *edx) {
    logmsg("match end: %lu:%02lu of game time, %ld ticks", g_play / 60000, g_play / 1000 % 60, g_ticks);
    g_inMatch = 0;
    o_post(self, edx);
}
static ThisFn o_over; typedef char (__fastcall *BoolThisFn)(void *, void *); static BoolThisFn o_endscript;
static void __fastcall hk_over(void *self, void *edx) {
    if (!g_over && g_inMatch) { logmsg("game over (SetGameOver): %lu:%02lu of game time, clock off", g_play / 60000, g_play / 1000 % 60); log_tick_rate("game over"); }
    g_over = 1; o_over(self, edx);
}
static char __fastcall hk_endscript(void *self, void *edx) {
    if (!g_over && g_inMatch) { logmsg("match scripts end (EndScript): %lu:%02lu of game time, clock off", g_play / 60000, g_play / 1000 % 60); log_tick_rate("end of scripts"); }
    if (g_inMatch) g_over = 1;
    return o_endscript(self, edx);
}
void mt_load_screen(void) __asm__("mt_load_screen");
void mt_load_screen(void) {
    if (g_inMatch && g_ticks && !g_over) { g_over = 1; logmsg("loading screen out of the match: %lu:%02lu of game time, clock off", g_play / 60000, g_play / 1000 % 60); }
}
void *g_lsTramp __asm__("mt_ls_tramp");
__attribute__((naked)) static void hk_loadscreen(void) {
    __asm__ volatile("pushal\n\t call mt_load_screen\n\t popal\n\t jmp *mt_ls_tramp\n\t");
}
static void hook_loadscreen(void) {
    unsigned char *t = (unsigned char *)LOADSCREEN_ADDR;
    if (IsBadReadPtr(t, 5) || memcmp(t, LOADSCREEN_PRO, 5)) { logmsg("loading screen builder differs at %08X", LOADSCREEN_ADDR); return; }
    unsigned char *tr = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(tr, LOADSCREEN_PRO, 5); tr[5] = 0xE9; *(DWORD *)(tr + 6) = (LOADSCREEN_ADDR + 5) - (DWORD)(tr + 10);
    g_lsTramp = tr;
    DWORD old; VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old);
    t[0] = 0xE9; *(DWORD *)(t + 1) = (DWORD)hk_loadscreen - (DWORD)(t + 5);
    VirtualProtect(t, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 5);
}
static void __fastcall hk_sim(void *self, void *edx) {
    DWORD now = GetTickCount(), gap = now - g_lastTick;
    if (self != g_world) match_start(self, "first tick of a new World");    // e.g. the DLL came in mid-match
    else if (g_lastTick && gap > 3000) logmsg("sim stood still for %lu s (pause or lag), the clock waited", gap / 1000);
    if (!g_wall0) g_wall0 = now;
    // game time = sim ticks x TICK_MS (02.11.2026): the same for every player of a match, in a replay at any speed, with
    // lags - it was the wall-clock gaps between ticks (a replay played fast counted wall time, a lagging clock drifted)
    g_lastTick = now; g_ticks++; g_play = (DWORD)g_ticks * TICK_MS;
    if (g_ticks % 480 == 0) log_tick_rate("every 480 ticks");   // a minute or so: the rate also mid-match (and in a replay sped up)
    o_sim(self, edx);
}

// slot already hooked by another DLL (e.g. FrameProbe) - chain onto it if it points to executable code (as in FrameProbe)
// One import-table write at a time across all our DLLs (a mutex per game process): two threads both opening the page for
// writing and then putting the old protection back crashed the game at start when one closed it while the other still wrote
// (LobbyStats 0.9.x: its tooltip thread and the match clock hook the same page of DOW2.exe's imports). The write itself goes
// through WriteProcessMemory, which fails instead of crashing if somebody else closed the page anyway.
static int iat_write(DWORD slot, void *fn, void **orig) {
    char n[64]; wsprintfA(n, "Local\\dow2patch_iat_%lu", GetCurrentProcessId());
    HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx) WaitForSingleObject(mx, 5000);
    int ok = 0; DWORD old, cur = *(volatile DWORD *)slot;                  // read again under the lock: another hook may be in by now
    if (VirtualProtect((void *)slot, 4, PAGE_READWRITE, &old)) {
        SIZE_T w = 0;
        if (WriteProcessMemory(GetCurrentProcess(), (void *)slot, &fn, 4, &w) && w == 4) { *orig = (void *)cur; ok = 1; }
        VirtualProtect((void *)slot, 4, old, &old);
    }
    if (mx) { ReleaseMutex(mx); CloseHandle(mx); }
    return ok;
}
static int iat_hook(DWORD slot, const char *dll, void *fn, void **orig) {
    HMODULE m = GetModuleHandleA(dll); MODULEINFO mi;
    if (!m || !GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof mi) || IsBadReadPtr((void *)slot, 4)) return 0;
    DWORD cur = *(DWORD *)slot;
    if (cur < (DWORD)mi.lpBaseOfDll || cur >= (DWORD)mi.lpBaseOfDll + mi.SizeOfImage) {
        MEMORY_BASIC_INFORMATION mb;
        if (!VirtualQuery((void *)cur, &mb, sizeof mb) || !(mb.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return 0;
        HMODULE exe = GetModuleHandleA(NULL); MODULEINFO me;               // but only inside the game itself (DOW2.exe)
        if (!GetModuleInformation(GetCurrentProcess(), exe, &me, sizeof me) || slot < (DWORD)me.lpBaseOfDll || slot >= (DWORD)me.lpBaseOfDll + me.SizeOfImage) return 0;
        char nm[MAX_PATH]; GetModuleFileNameA(exe, nm, MAX_PATH); if (!strstr(nm, "DOW2") && !strstr(nm, "dow2")) return 0;
    }
    return iat_write(slot, fn, orig);
}

// ---------- the clock: a text field of our own in the game's Menu button movie (v5, 02.11.2026) ----------
// No Direct3D any more (owner: no drawing over the game through the video driver, use the game's own interface).
// The Menu button is a form of its own (vtable 0x0110C54C, made in 0xAD7560 with the movie ui\movies\hud\menu_btn):
// its Update (0x682000) calls vtable slot 4, OnUpdate(float dt), every frame on the UI thread, and that slot is an empty
// "ret 4" in the game. Ours goes there: the form comes as this, form + 4 is its movie (common/gfxui.h).
// In that movie (stage 210 x 75, the button at 12,-10 and 184 x 48, the movie in the top right corner and scaled with the
// screen height, so the same on every resolution) "_root.createTextField" makes "mt_clock" under the button, left of the
// hero portraits: the movie shows nothing much left of its stage, so not beside the button. Its htmlText is in the game's
// own $TitleFont and the colour of the "Menu" label; "mt_clock_sh" under it, black and a little down-right, is the shadow.
// Shown and hidden with _visible; created again if the HUD comes with a new movie (a new match).
#define MENU_VT_UPD   0x0110C55C       // menu button form vtable 0x0110C54C, slot 4
#define NULLSUB_RET4  0x009AB7D0       // what the game has there: ret 4
// The clock as a button of its own next to Menu (02.11.2026, siritos: as big as the Menu button, its padding and text size,
// left of it; left of it the movie clips it - its stage starts at x 0 - so under it, 02.11). The Menu button's visible part on the stage: the menu_btn clip at 12.7,-9.6, its hit area at 15.25,5.6 in it,
// 184 x 48 -> 27.95,-4 .. 211.95,44; its label (size 20) at 25,23 in the clip. MatchTimer.ini: Place=under|menu|left, Gap, Height,
// Width (of the visible plate, 184 = the Menu button), OffsetX / OffsetY - read again while the game runs.
#define MB_X0 27.95                    // the Menu button's visible rectangle on the stage
#define MB_Y0 -4.0
#define MB_W  184.0
#define MB_H  48.0
#define UP_PADX 15.25                  // where the visible part starts inside "up" (208 x 72) and menu_btn
#define UP_PADY 5.6
static int clock_visible(void); DWORD mt_game_ms(void);
typedef void (__thiscall *MenuUpd_t)(char *form, float dt);
static MenuUpd_t o_menuUpd;
static void *g_clkMovie; static DWORD g_clkCheck; static int g_clkShown = -1; static char g_clkText[24];
// Place=menu (siritos 02.11: "Menu (00:01)"): the time in brackets right after the button's own label. The label is an
// unnamed text field of menu_btn (depth 7, 162 x 31 at 25,23, its text centred) - Flash names it "instanceN"; found by
// asking for the names. It goes LABEL_SHIFT to the left so label and time together stay in the middle of the button.
#define LABEL_X     25.0
#define LABEL_SHIFT 30.0
static char g_lbl[32];                 // "menu_btn.instanceN" of the current movie, "" = not found
static void label_find(void *mv) {
    g_lbl[0] = 0;
    for (int n = 1; n <= 300; n++) {
        char p[32]; snprintf(p, sizeof p, "menu_btn.instance%d", n);
        if (gfx_exists(mv, p)) { strcpy(g_lbl, p); break; }
    }
    logmsg("Menu label: %s", g_lbl[0] ? g_lbl : "not found - the time goes right of it");
}
static void label_shift(void *mv, double x) {
    if (!g_lbl[0]) return;
    char p[64]; snprintf(p, sizeof p, "_root.%s._x", g_lbl); gfx_setvar(mv, p, gv_num(x));
}
static void clock_make(void *mv) {
    // the plate's visible rectangle on the stage: under Menu (default) or left of it
    double W = g_width, H = g_height;
    // the Menu button (menu_btn at 12.7,-9.6) up by Raise: everything of ours goes with it
    double R = g_place == PL_UNDER ? g_raise : 0, mby = -9.6 - R;
    gfx_setvar(mv, "_root.menu_btn._y", gv_num(mby));
    double px = (g_place == PL_LEFT ? MB_X0 - g_gap - W : MB_X0) + g_offX, py = (g_place == PL_LEFT ? MB_Y0 : MB_Y0 + MB_H + g_gap) - R + g_offY;
    // the text like the Menu label: its field 31 high for size 20 sits 17.4 below the top of the button
    double fh = 31.0 * g_size / 20.0, fy = py + (H - fh) / 2 + g_textY, h = fh;
    label_find(mv);
    if (g_place == PL_MENU) {                        // in the button, on the label's line, left-aligned after it
        label_shift(mv, LABEL_X - LABEL_SHIFT);
        // the label's centre (clip 25 + 81) moved, plus half of "Menu" and a space; without the label found: after it
        px = 12.7 + (g_lbl[0] ? LABEL_X + 81 - LABEL_SHIFT + 25 : LABEL_X + 81 + 25) + g_offX;
        W = 90; fy = mby + 23 + (31.0 - fh) / 2 + g_offY; h = fh;   // on the label's middle line
    } else label_shift(mv, LABEL_X);
    for (int k = 0; k < 2; k++) {                    // the shadow first: the text over it
        const char *nm = k ? "mt_clock" : "mt_clock_sh";
        char p[64];
        GfxVal a[6] = { gv_str(nm), gv_num(16000 + k), gv_num(px + (k ? 0 : 1.5)), gv_num(fy + (k ? 0 : 2)), gv_num(W), gv_num(h) };
        int ok = gfx_invoke(mv, "_root.createTextField", NULL, a, 6);
        snprintf(p, sizeof p, "_root.%s.html", nm); gfx_setvar(mv, p, gv_bool(1));
        snprintf(p, sizeof p, "_root.%s.selectable", nm); gfx_setvar(mv, p, gv_bool(0));
        if (!k) { snprintf(p, sizeof p, "_root.%s._alpha", nm); gfx_setvar(mv, p, gv_num(g_shadow ? 70 : 0)); }
        if (!ok) logmsg("Menu button movie %p: createTextField %s failed", mv, nm);
    }
    logmsg("clock made in the Menu button movie %p: %s", mv, gfx_exists(mv, "mt_clock") ? "ok" : "NOT THERE - no clock");
    // the plate (02.11.2026, siritos: the game's own interface under the digits): the movie's "menu_btn" clip (stage 12.7,-9.6)
    // holds "up" / "over" / "down", the button's own background (trans_btn_up, 208 x 72). Its copy "mt_plate" in the same clip
    // (Receive_DuplicateMovieClip, as the cooldown numbers copy the button's count) is scaled to the clock field and put under
    // it; the clock fields are on _root at depth 16000+, over the whole menu_btn clip. No mouse handlers: takes no clicks.
    if (g_plate && g_place != PL_MENU) {
        if (!gfx_exists(mv, "menu_btn.mt_plate")) {
            GfxVal a[2] = { gv_str("/menu_btn/up"), gv_str("mt_plate") }, r; memset(&r, 0, sizeof r);
            gfx_invoke(mv, "Receive_DuplicateMovieClip", &r, a, 2);
            logmsg("plate: copy of the Menu button's background %s", r.type == GV_BOOL && r.v.b ? "made" : "NOT made");
        }
        // scaled so its visible part is W x H, placed so that part starts at px,py (menu_btn's own coordinates)
        double sx = W / MB_W, sy = H / MB_H, x = px - 12.7 - UP_PADX * sx, y = py - mby - UP_PADY * sy;
        GfxVal pa[3] = { gv_str("menu_btn.mt_plate"), gv_num(x), gv_num(y) }, sa[3] = { gv_str("menu_btn.mt_plate"), gv_num(sx), gv_num(sy) };
        logmsg("clock at %.1f,%.1f %gx%g on the stage (%s the Menu button), text size %d", px, py, W, H, g_place == PL_LEFT ? "left of" : "under", g_size);
        gfx_invoke(mv, "Receive_SetMovieClipPosition", NULL, pa, 3);
        gfx_invoke(mv, "Receive_SetMovieClipScale", NULL, sa, 3);
        gfx_setvar(mv, "_root.menu_btn.mt_plate._alpha", gv_num(g_plateAlpha));
    } else if (gfx_exists(mv, "menu_btn.mt_plate")) gfx_set_visible(mv, "menu_btn.mt_plate", 0);   // Plate=0 / menu while the game runs
}
// Place=top (experimental, siritos 02.11): under the victory point counter at the top centre. That panel is a form of its own
// too (vtable 0x0111174C, made in 0xB18850 with ui\movies\hud\victory_point_panel; slot 4 the same empty "ret 4"); its stage
// is 436 x 115, the bar and the stars in its upper part. The fields are centred on the stage at x 218; OffsetX / OffsetY move
// them (read again while the game runs). No plate: the counter has none either.
#define VP_VT_UPD 0x0111175C
static void clock_make_top(void *mv) {
    double W = g_width, fh = 31.0 * g_size / 20.0, px = 218 - W / 2 + g_offX, fy = 115 - fh - 2 + g_offY;
    for (int k = 0; k < 2; k++) {
        const char *nm = k ? "mt_clock" : "mt_clock_sh";
        char p[64];
        GfxVal a[6] = { gv_str(nm), gv_num(16000 + k), gv_num(px + (k ? 0 : 1.5)), gv_num(fy + (k ? 0 : 2)), gv_num(W), gv_num(fh) };
        int ok = gfx_invoke(mv, "_root.createTextField", NULL, a, 6);
        snprintf(p, sizeof p, "_root.%s.html", nm); gfx_setvar(mv, p, gv_bool(1));
        snprintf(p, sizeof p, "_root.%s.selectable", nm); gfx_setvar(mv, p, gv_bool(0));
        if (!k) { snprintf(p, sizeof p, "_root.%s._alpha", nm); gfx_setvar(mv, p, gv_num(g_shadow ? 70 : 0)); }
        if (!ok) logmsg("victory point movie %p: createTextField %s failed", mv, nm);
    }
    logmsg("clock made in the victory point movie %p at %.1f,%.1f %gx%.1f: %s", mv, px, fy, W, fh, gfx_exists(mv, "mt_clock") ? "ok" : "NOT THERE - no clock");
}
static int plate_on(void) { return g_plate && g_place != PL_MENU && g_place != PL_TOP; }
static void clock_restyle(void) { g_clkMovie = NULL; }   // the next frame makes the fields again (same names and depths: replaced)
static void clock_text(void *mv, const char *t0) {
    char h[256], t[32]; const char *al = g_place == PL_MENU ? "left" : "center";
    if (g_place == PL_MENU) snprintf(t, sizeof t, "(%s)", t0); else snprintf(t, sizeof t, "%s", t0);
    snprintf(h, sizeof h, "<p align=\"%s\"><font face=\"$TitleFont\" size=\"%d\" color=\"#000000\">%s</font></p>", al, g_size, t);
    gfx_setvar(mv, "_root.mt_clock_sh.htmlText", gv_str(h));
    snprintf(h, sizeof h, "<p align=\"%s\"><font face=\"$TitleFont\" size=\"%d\" color=\"#%06lX\">%s</font></p>", al, g_size,
             (unsigned long)(g_color & 0xFFFFFF), t);
    gfx_setvar(mv, "_root.mt_clock.htmlText", gv_str(h));
}
// one clock: in the Menu button movie (host 0) or the victory point movie (host 1, Place=top); the other host takes its
// own off when the place changes while the game runs
static void clock_frame(char *form, int host) {
    void *mv = gfx_movie_of_form(form);
    if (!mv) return;
    DWORD now = GetTickCount();
    reload_ini(now);
    if (host != (g_place == PL_TOP)) {
        if (mv == g_clkMovie) {                      // the clock was here: off, the Menu button as the game has it
            gfx_setvar(mv, "_root.mt_clock._visible", gv_bool(0));
            gfx_setvar(mv, "_root.mt_clock_sh._visible", gv_bool(0));
            if (!host) {
                if (gfx_exists(mv, "menu_btn.mt_plate")) gfx_set_visible(mv, "menu_btn.mt_plate", 0);
                gfx_setvar(mv, "_root.menu_btn._y", gv_num(-9.6));
                label_shift(mv, LABEL_X);
            }
            g_clkMovie = NULL;
        }
        return;
    }
    int vis = g_enabled && clock_visible();
    if (mv != g_clkMovie || (now - g_clkCheck > 3000 && (g_clkCheck = now, !gfx_exists(mv, "mt_clock")))) {
        g_clkMovie = mv; g_clkCheck = now; g_clkShown = -1; g_clkText[0] = 0;
        if (host) clock_make_top(mv); else clock_make(mv);
    }
    if (vis != g_clkShown) {
        DWORD run = mt_game_ms();
        if (vis) logmsg("clock shown");
        else if (g_clkShown == 1) logmsg("clock hidden: match %lu:%02lu of game time", run / 60000, run / 1000 % 60);
        g_clkShown = vis;
        gfx_setvar(mv, "_root.mt_clock._visible", gv_bool(vis));
        gfx_setvar(mv, "_root.mt_clock_sh._visible", gv_bool(vis && g_shadow));
        if (plate_on()) gfx_set_visible(mv, "menu_btn.mt_plate", vis);
        if (g_place == PL_MENU) label_shift(mv, vis ? LABEL_X - LABEL_SHIFT : LABEL_X);   // no time: the label back in the middle
    }
    if (!vis) return;
    DWORD s = mt_game_ms() / 1000; char t[24];
    if (s >= 3600) snprintf(t, sizeof t, "%lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
    else snprintf(t, sizeof t, "%02lu:%02lu", s / 60, s % 60);
    if (strcmp(t, g_clkText)) { strcpy(g_clkText, t); clock_text(mv, t); }
}
static void __thiscall hk_menu_upd(char *form, float dt) { o_menuUpd(form, dt); clock_frame(form, 0); }
static MenuUpd_t o_vpUpd;
static void __thiscall hk_vp_upd(char *form, float dt) { o_vpUpd(form, dt); clock_frame(form, 1); }
static void clock_install(void) {
    static volatile LONG done;
    if (InterlockedExchange(&done, 1)) return;
    o_menuUpd = (MenuUpd_t)NULLSUB_RET4;
    if (gfx_patch_slot(MENU_VT_UPD, NULLSUB_RET4, (void *)hk_menu_upd)) logmsg("clock: in the game's own Menu button movie (its form's per-frame update)");
    else logmsg("Menu button form not as in DOW2.exe 3.19 (slot %08X = %08lX) - no clock", MENU_VT_UPD,
                IsBadReadPtr((void *)MENU_VT_UPD, 4) ? 0UL : (unsigned long)*(DWORD *)MENU_VT_UPD);
    o_vpUpd = (MenuUpd_t)NULLSUB_RET4;
    if (gfx_patch_slot(VP_VT_UPD, NULLSUB_RET4, (void *)hk_vp_upd)) logmsg("clock: Place=top ready (the victory point panel's per-frame update)");
    else logmsg("victory point panel form not as in DOW2.exe 3.19 - no Place=top");
}

// ---------- one match core per game (01.11.2026) ----------
// MatchTimer.dll, Cooldowns.dll and LoadingRatings.dll all carry this file. The first of them to start runs the sim hooks and
// puts the address of its core into the process environment (DOW2_MTCORE_<pid>); the others hook nothing of the simulation and
// read the match state from it. Before, two of them hooked the same game functions at once (every match event twice in a log).
// v2 of the core (02.11.2026): no Direct3D. An older add-on of ours that still asks the core to draw (add_draw) gets nothing
// drawn; an old MatchTimer (want_clock) gets the clock in the Menu button movie.
typedef struct {
    int ver;
    int (*loading)(void); int (*online)(void); int (*ticking)(void); DWORD (*game_ms)(void); long (*ticks)(void); float (*ms_per_tick)(void);
    void (*add_draw)(void *draw, void *lost, void *reset);           // v1: drawing through the core's Direct3D hook - gone
    void (*want_clock)(const wchar_t *ini, const wchar_t *log);     // MatchTimer.dll v4: the core shows the clock with these settings
} mt_core;
static mt_core *g_remote;                        // the core of another of our DLLs (NULL: this one runs the hooks)
static int l_loading(void) { return g_inMatch && !g_ticks; }   // SimulatePre came, no sim tick yet: the loading screen
static int l_online(void) { return (session_flags() & ONLINE_MASK) != 0; }
static int l_ticking(void) { return g_inMatch && g_ticks && !g_over; }                  // the match runs (past its loading screen)
static DWORD l_game_ms(void) { return g_play; }
static long l_ticks(void) { return g_ticks; }
static float l_ms_per_tick(void) { return (float)TICK_MS; }   // exact: game time is ticks x TICK_MS
static void l_add_draw(void *d, void *l, void *r) {
    (void)l; (void)r;
    if (d) logmsg("an older add-on of ours wants to draw through Direct3D: not any more, update it in the launcher");
}
static void l_want_clock(const wchar_t *ini, const wchar_t *log) {
    lstrcpynW(g_ini, ini, MAX_PATH); lstrcpynW(g_log, log, MAX_PATH);
    load_ini();
    logmsg("the clock of an older MatchTimer.dll, shown by the match core of another add-on");
    if (g_enabled) clock_install();
}
// match state for the clock, cooldowns.c and lobbystats.c (LS_LOADING): from the core, wherever it runs
int mt_loading(void) { return g_remote ? g_remote->loading() : l_loading(); }
int mt_online(void) { return g_remote ? g_remote->online() : l_online(); }
int mt_ticking(void) { return g_remote ? g_remote->ticking() : l_ticking(); }
DWORD mt_game_ms(void) { return g_remote ? g_remote->game_ms() : l_game_ms(); }
long mt_ticks(void) { return g_remote ? g_remote->ticks() : l_ticks(); }
float mt_ms_per_tick(void) { return g_remote ? g_remote->ms_per_tick() : l_ms_per_tick(); }
static int clock_visible(void) {                 // menus, loading and post-game screens: no; replays and offline games have no online session
    return mt_ticking() && (!g_onlineOnly || mt_online());
}
// 1 = this DLL runs the core (and has published it); 0 = g_remote points at the one already running
static int core_claim(void) {
    char n[64], var[40], v[24]; DWORD pid = GetCurrentProcessId();
    wsprintfA(n, "Local\\dow2patch_mtcore_%lu", pid); wsprintfA(var, "DOW2_MTCORE_%lu", pid);
    HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx) WaitForSingleObject(mx, 5000);
    int mine = 0;
    if (GetEnvironmentVariableA(var, v, sizeof v) && v[0]) g_remote = (mt_core *)(UINT_PTR)strtoul(v, NULL, 16);
    else {
        static mt_core me = { 2, l_loading, l_online, l_ticking, l_game_ms, l_ticks, l_ms_per_tick, l_add_draw, l_want_clock };
        wsprintfA(v, "%08lX", (unsigned long)(UINT_PTR)&me); SetEnvironmentVariableA(var, v); mine = 1;
    }
    if (mx) { ReleaseMutex(mx); CloseHandle(mx); }
    return mine;
}
static DWORD WINAPI worker(LPVOID _) {          // the core: sim imports (match state)
    int sim = 0;
    for (int i = 0; i < 120 && !sim; i++) {
        if ((sim = iat_hook(IAT_SIM, "SimEngine.dll", hk_sim, (void **)&o_sim))) {
            int pre = iat_hook(IAT_SIM_PRE, "SimEngine.dll", hk_pre, (void **)&o_pre);
            int post = iat_hook(IAT_SIM_POST, "SimEngine.dll", hk_post, (void **)&o_post);
            int over = iat_hook(IAT_GAME_OVER, "SimEngine.dll", hk_over, (void **)&o_over);
            int scr = iat_hook(IAT_END_SCRIPT, "SimEngine.dll", hk_endscript, (void **)&o_endscript);
            logmsg("match end hooks: game over %s, end of scripts %s", over ? "yes" : "NO", scr ? "yes" : "NO");
            hook_loadscreen();
            logmsg("sim hooks installed: tick yes, match start %s, match end %s%s", pre ? "yes" : "NO", post ? "yes" : "NO",
                   post ? "" : " - the clock will stay on the post-game screen");
        } else Sleep(1000);
    }
    if (!sim) logmsg("World::Simulate import not found at %08X - not DOW2.exe 3.19? The clock stays off", IAT_SIM);
    return 0;
}

// one copy per game process: a patch can come both from Elite_DLLs (launcher) and from dow2-patches\ (winmm loader)
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
#ifdef MT_EMBED
// Cooldowns.dll / LoadingRatings.dll: the match state only (no clock here). dir = the host's folder (trailing backslash).
static void core_start(const wchar_t *dir, const wchar_t *ini, const wchar_t *logName, const char *what) {
    _snwprintf(g_log, MAX_PATH, L"%ls%ls", dir, logName); lstrcpynW(g_ini, ini, MAX_PATH);
    if (!core_claim()) { logmsg("match state from the match core of another add-on (core v%d, no second set of game hooks)", g_remote->ver); return; }
    logmsg("match core v2: match state for %s (no clock, no Direct3D)", what);
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
}
void mt_start(const wchar_t *dir, const wchar_t *ini) { core_start(dir, ini, L"MatchState.log", "the cooldown numbers"); }
void mt_start_nodevice(const wchar_t *dir, const wchar_t *ini) { core_start(dir, ini, L"MatchState.log", "the loading screen ratings"); }
#else
// MatchTimer.dll on its own (30.10.2026, owner: every feature its own add-on and launcher tick)
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wcscpy(g_ini, g_log); wcscat(g_ini, L"MatchTimer.ini"); wcscat(g_log, L"MatchTimer.log");
    load_ini();
    logmsg("MatchTimer v5 (own add-on: the clock in the game's own Menu button movie, no Direct3D)");
    if (already_loaded("MatchTimer")) { logmsg("another MatchTimer copy is already active in this game, this one stays idle"); return TRUE; }
    if (!g_enabled) { logmsg("disabled in MatchTimer.ini"); return TRUE; }
    clock_install();
    if (!core_claim()) { logmsg("match state from the match core of another add-on (core v%d)", g_remote->ver); return TRUE; }
    logmsg("match core v2: the clock");
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
#endif
