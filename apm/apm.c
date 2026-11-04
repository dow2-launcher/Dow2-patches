// apm.c - APM.dll: actions per minute of the player at the computer, in Dawn of War II: Retribution 3.19 (Elite and vanilla).
// An add-on of its own (docs/ADDON_RULES.md), built with the match core of matchtimer.c (-DMT_EMBED: match start / end,
// game time; no second set of sim hooks when MatchTimer or Cooldowns already run one).
//
// What is counted (03.11.2026): every press of a mouse button and of a key (not its auto-repeat) while a match runs and
// the game window is in front - clicks, orders, hotkeys, control groups, as StarCraft's APM counts them. Shift, Ctrl,
// Alt, Win, Caps / Num / Scroll Lock alone are not actions. Read with GetAsyncKeyState once a frame on the game's UI thread
// (edge: up -> down, plus the "pressed since the last call" bit for a tap shorter than a frame), so it works whatever
// input path the game uses. Nothing is sent to the game or changed in it: no desync, only this player sees the number.
// APM 2 (04.11.2026, owner: "60 s smooths the fights away"): an exponential average of the actions with about Smooth seconds of
// memory (5: 63% of a jump in 5 s, 86% in 10 s; game time, pauses don't count) - a fight shows within a couple of seconds, the number does not jump like a hard
// short window. Smooth=0: the old hard window of Window seconds (Window=0: the whole match). The log gets the match average and
// its peak (the most actions in any 10 s).
//
// Shown like the match clock (matchtimer.c): text fields of our own in the game's own HUD movies (common/gfxui.h, no
// Direct3D - docs/UI_RULES.md). Place=right|left (default right): beside the victory point counter at the top centre, on
// its line, not under it (where the match clock goes). A match without the counter (annihilation): under the Menu button,
// right of the match clock's plate when the clock is there. APM.ini is read again while the game runs.
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include "../common/gfxui.h"

void mt_start_for(const wchar_t *dir, const wchar_t *ini, const char *what);
int mt_ticking(void); DWORD mt_game_ms(void); int mt_online(void);

static wchar_t g_log[MAX_PATH], g_ini[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---------- settings: APM.ini [APM] ----------
enum { AP_RIGHT, AP_LEFT, AP_MINIMAP };
static int g_enabled = 1, g_place = AP_RIGHT, g_size = 18, g_shadow = 1, g_offX = 0, g_offY = 0, g_gap = 0, g_window = 60, g_onlineOnly = 0, g_smooth = 5;
static DWORD g_color = 0xFFFFFF; static char g_label[16] = "apm"; static int g_labelSize = 13, g_fallbackOn = 1;
static FILETIME g_iniTime;
static int ini_int(const wchar_t *k, int def, int lo, int hi) { int v = GetPrivateProfileIntW(L"APM", k, def, g_ini); return v < lo ? lo : v > hi ? hi : v; }
static void load_ini(void) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a)) g_iniTime = a.ftLastWriteTime;
    wchar_t s[32];
    g_enabled = ini_int(L"Enabled", 1, 0, 1);
    GetPrivateProfileStringW(L"APM", L"Place", L"top", s, 32, g_ini); g_place = !_wcsicmp(s, L"left") ? AP_LEFT : !_wcsicmp(s, L"minimap") ? AP_MINIMAP : AP_RIGHT;   // top / right = right of the clock at the top
    g_size = ini_int(L"FontSize", 0, 0, 40);   // 0 = the place's own: 18 at the top, 24 at the minimap
    g_shadow = ini_int(L"Shadow", 1, 0, 1);
    g_offX = ini_int(L"OffsetX", 0, -800, 800);
    g_offY = ini_int(L"OffsetY", 0, -800, 800);   // 03.11: +-200 cut the minimap corner short (330 read as 200)
    g_gap = ini_int(L"Gap", 0, -200, 200);
    g_window = ini_int(L"Window", 60, 0, 600);
    g_smooth = ini_int(L"Smooth", 5, 0, 120);
    g_onlineOnly = ini_int(L"OnlineOnly", 0, 0, 1);
    GetPrivateProfileStringW(L"APM", L"Color", L"FFFFFF", s, 32, g_ini); g_color = wcstoul(s, NULL, 16) & 0xFFFFFF;
    g_labelSize = ini_int(L"LabelSize", 0, 0, 40);   // 0 = 13 at the top, 17 at the minimap
    g_fallbackOn = ini_int(L"Fallback", 1, 0, 1);   // no victory point counter (annihilation): above the minimap
    GetPrivateProfileStringW(L"APM", L"Label", L"apm", s, 16, g_ini);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, g_label, sizeof g_label, NULL, NULL);
}
static void restyle(void);
static int g_host = -1;
static int fsize(void) { return g_size ? g_size : g_host == 2 ? 24 : 18; }
static int lsize(void) { return g_labelSize ? g_labelSize : g_host == 2 ? 17 : 13; }
static void reload_ini(DWORD now) {
    static DWORD last;
    if (now - last < 1000) return;
    last = now;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a) || !CompareFileTime(&a.ftLastWriteTime, &g_iniTime)) return;
    load_ini(); restyle();
    logmsg("APM.ini re-read: Enabled=%d Place=%s FontSize=%d Window=%d OffsetX=%d OffsetY=%d", g_enabled, g_place == AP_LEFT ? "left" : g_place == AP_MINIMAP ? "minimap" : "top", g_size, g_window, g_offX, g_offY);
}

// ---------- counting (UI thread, once a frame) ----------
#define BUCKETS 600                                   // one per second of game time, up to Window=600
static unsigned short g_bucket[BUCKETS]; static DWORD g_bucketSec[BUCKETS];
static DWORD g_total, g_matchMs0, g_emaMs, g_peakSec = (DWORD)-1, g_peak; static double g_ema; static int g_counting; static unsigned char g_down[256];
static DWORD g_lastFrame;
static int not_action(int vk) {
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || (vk >= VK_LSHIFT && vk <= VK_RMENU) || vk == VK_CAPITAL
        || vk == VK_LWIN || vk == VK_RWIN || vk == VK_APPS || vk == VK_NUMLOCK || vk == VK_SCROLL || vk == 0x07 || vk == 0x0A || vk == 0x0B
        || (vk >= 0xE9 && vk <= 0xF5) || vk == VK_PACKET;
}
static int game_in_front(void) {
    HWND f = GetForegroundWindow(); DWORD pid = 0;
    if (f) GetWindowThreadProcessId(f, &pid);
    return pid == GetCurrentProcessId();
}
static void count_frame(void) {
    DWORD now = GetTickCount();
    if (now == g_lastFrame) return;                    // both hooks run in one frame: once
    g_lastFrame = now;
    int live = mt_ticking() && game_in_front();
    if (!live) { if (g_counting && !mt_ticking()) { logmsg("match over: %lu actions in %lu:%02lu of game time = %lu APM on average, peak %lu APM (%lu actions in 10 s)", g_total, mt_game_ms() / 60000, mt_game_ms() / 1000 % 60, mt_game_ms() > 10000 ? (unsigned long)((double)g_total * 60000.0 / mt_game_ms()) : 0UL, (unsigned long)g_peak * 6, (unsigned long)g_peak); g_counting = 0; }
                 for (int vk = 1; vk < 256; vk++) g_down[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
                 return; }
    if (!g_counting || mt_game_ms() < g_matchMs0) {   // a new match
        g_counting = 1; g_total = 0; g_matchMs0 = 0; memset(g_bucketSec, 0xFF, sizeof g_bucketSec); g_ema = 0; g_emaMs = 0; g_peak = 0; g_peakSec = (DWORD)-1;
        logmsg("match: counting");
    }
    DWORD sec = mt_game_ms() / 1000; int n = 0;
    for (int vk = 1; vk < 256; vk++) {
        if (vk == 3 || not_action(vk)) continue;         // 3 = VK_CANCEL (Ctrl+Break)
        SHORT st = GetAsyncKeyState(vk); int d = (st & 0x8000) != 0;
        if (!g_down[vk] && (d || (st & 1))) n++;
        g_down[vk] = d;
    }
    // the peak: once a second, the actions of the 10 whole seconds before this one
    if (sec != g_peakSec) {
        g_peakSec = sec; DWORD m = 0;
        for (DWORD s2 = sec >= 10 ? sec - 10 : 0; s2 < sec; s2++) { int j = s2 % BUCKETS; if (g_bucketSec[j] == s2) m += g_bucket[j]; }
        if (m > g_peak) g_peak = m;
    }
    if (!n) return;
    { DWORD ms = mt_game_ms(); double tau = g_smooth * 1000.0;   // the exponential average: decay to now, then the new actions
      if (tau > 0 && ms > g_emaMs) g_ema *= exp(-(double)(ms - g_emaMs) / tau);
      g_emaMs = ms; g_ema += n; }
    int i = sec % BUCKETS;
    if (g_bucketSec[i] != sec) { g_bucketSec[i] = sec; g_bucket[i] = 0; }
    g_bucket[i] += n; g_total += n;
}
static int apm_now(void) {
    DWORD ms = mt_game_ms(), sec = ms / 1000;
    if (g_smooth) {                                      // APM 2: the exponential average, per minute
        double tau = g_smooth * 1000.0, e = g_ema;
        if (ms > g_emaMs) e *= exp(-(double)(ms - g_emaMs) / tau);
        double norm = 1.0 - exp(-(double)ms / tau);       // the start of the match: no fake history of zeros
        return norm > 0.05 ? (int)(e * 60000.0 / (tau * norm) + 0.5) : 0;
    }
    if (!g_window) return ms >= 5000 ? (int)((double)g_total * 60000.0 / ms) : 0;
    DWORD w = (DWORD)g_window, from = sec >= w ? sec - w + 1 : 0, n = 0;
    for (DWORD s = from; s <= sec; s++) { int i = s % BUCKETS; if (g_bucketSec[i] == s) n += g_bucket[i]; }
    double span = sec >= w ? w : ms / 1000.0;           // the first minute: per minute of what has passed
    return span >= 5 ? (int)(n * 60.0 / span + 0.5) : (int)(n * 12);
}

// ---------- the number in the HUD ----------
#define MENU_VT_UPD   0x0110C55C      // Menu button form, slot 4 (OnUpdate) - see matchtimer.c "the clock"
#define VP_VT_UPD     0x0111175C      // victory point panel form, slot 4
#define NULLSUB_RET4  0x009AB7D0
typedef void (__thiscall *Upd_t)(char *form, float dt);
static Upd_t o_menuUpd, o_vpUpd, o_mmUpd;
// the minimap panel (03.11.2026, experiment: siritos - "a little above the minimap"): a form of its own too (vtable 0x011160D8,
// made in 0xB3EE30 with uimovieshudminimap_panel); its slot 4 is its own per-frame update 0xB3F6E0 (not the empty
// ret 4 of the Menu button and the counter) - chained: the game's first, then ours
#define MM_VT_UPD 0x011160E8
#define MM_UPD_FN 0x00B3F6E0
static volatile DWORD g_vpSeen; static volatile LONG g_menuSinceVp;   // as in matchtimer.c: alt-tab is not annihilation
static void *g_hostMv[3];
static void *g_mv; static int g_shown = -1, g_lastVal = -1; static DWORD g_check;
static void restyle(void) { g_mv = NULL; }
static void fields(void *mv, double x, double y, double w, double h) {
    for (int k = 0; k < 2; k++) {
        const char *nm = k ? "mt_apm" : "mt_apm_sh"; char p[64];
        GfxVal a[6] = { gv_str(nm), gv_num(16010 + k), gv_num(x + (k ? 0 : 1.5)), gv_num(y + (k ? 0 : 2)), gv_num(w), gv_num(h) };
        int ok = gfx_invoke(mv, "_root.createTextField", NULL, a, 6);
        snprintf(p, sizeof p, "_root.%s.html", nm); gfx_setvar(mv, p, gv_bool(1));
        snprintf(p, sizeof p, "_root.%s.selectable", nm); gfx_setvar(mv, p, gv_bool(0));
        if (!k) { snprintf(p, sizeof p, "_root.%s._alpha", nm); gfx_setvar(mv, p, gv_num(g_shadow ? 70 : 0)); }
        if (!ok) logmsg("movie %p: createTextField %s failed", mv, nm);
    }
}
// the victory point movie: stage 436 x 115, the counter's bar (the two scores at its ends) in its upper part, the stars
// and the match clock under its middle (matchtimer.c). The movie shows nothing outside its stage (tried 03.11: at x 446 the
// text was gone), so the number goes under an end of the bar, on the stars' line: right of them (under the enemy score) or
// left (under yours) - beside the block of the stars and the clock, not under it. Seen in the game at 1920 x 1080.
#define VP_W 436.0
static void make_top(void *mv) {
    // on the match clock's line (siritos 03.11: on the stars' line a captured star covered it): the clock is a 100 wide
    // field centred on 218 - 7 at 115 - its height - 2 + 6 (matchtimer.c, Place=top defaults); "00:00" is ~46 wide
    double W = 90, fh = 31.0 * fsize() / 20.0, y = 115 - fh - 2 + 6 + g_offY;
    double x = g_place == AP_LEFT ? 211 - 28 - W - g_gap + g_offX : 211 + 28 + g_gap + g_offX;
    fields(mv, x, y, W, fh);
    logmsg("APM in the victory point movie %p at %.1f,%.1f (%s of the counter): %s", mv, x, y, g_place == AP_LEFT ? "left" : "right", gfx_exists(mv, "mt_apm") ? "ok" : "NOT THERE");
}
// the Menu button movie (a match without the counter): under the button; right of the clock's plate when it is there
static int g_withClock;                            // made right of the clock (it can come a frame later: made again then)
static void make_menu(void *mv) {
    int clock = g_withClock = gfx_exists(mv, "mt_clock");
    double fh = 31.0 * fsize() / 20.0, W = clock ? 80 : 184, x = (clock ? 27.95 + 104 : 27.95) + g_offX, y = -4.0 + 48 + 2 + (34 - fh) / 2 + 3 - (clock ? 8 : 0) + g_offY;   // the clock lifts the Menu button by 8 (Raise)
    fields(mv, x, y, W, fh);
    logmsg("APM in the Menu button movie %p at %.1f,%.1f (under the button%s): %s", mv, x, y, clock ? ", right of the clock" : "", gfx_exists(mv, "mt_apm") ? "ok" : "NOT THERE");
}
static void make_mini(void *mv) {
    // above the minimap; the match clock there too (MatchTimer Place=minimap): right of it on its line
    int clock = g_withClock = gfx_exists(mv, "mt_clock");
    // the bottom left corner of the frame (set in the game with siritos 03.11, 2560 x 1440: size 24, x 6, the line ending at 326;
    // the movie shows nothing outside the frame)
    double W = 140, fh = 31.0 * fsize() / 20.0, x = 6 + (clock ? 60 : 0) + g_offX, y = 326 - fh + g_offY;
    fields(mv, x, y, W, fh);
    logmsg("APM in the minimap movie %p at %.1f,%.1f: %s", mv, x, y, gfx_exists(mv, "mt_apm") ? "ok" : "NOT THERE");
}
static void set_text(void *mv, int v) {
    char h[320], t[96]; const char *al = g_host == 0 ? "center" : g_host == 1 && g_place == AP_LEFT ? "right" : "left";
    snprintf(t, sizeof t, "<font size=\"%d\">%s</font> %d", lsize(), g_label, v);   // the label small (siritos 03.11: "apm" in small letters)
    snprintf(h, sizeof h, "<p align=\"%s\"><font face=\"$TitleFont\" size=\"%d\" color=\"#000000\">%s</font></p>", al, fsize(), t);
    gfx_setvar(mv, "_root.mt_apm_sh.htmlText", gv_str(h));
    snprintf(h, sizeof h, "<p align=\"%s\"><font face=\"$TitleFont\" size=\"%d\" color=\"#%06lX\">%s</font></p>", al, fsize(), (unsigned long)g_color, t);
    gfx_setvar(mv, "_root.mt_apm.htmlText", gv_str(h));
}
static void frame(char *form, int host) {
    void *mv = gfx_movie_of_form(form);
    if (!mv) return;
    DWORD now = GetTickCount();
    reload_ini(now);
    count_frame();
    // where it goes: the victory point movie while that panel updates, else (annihilation) the Menu button movie
    static int want = 1;
    if (g_place == AP_MINIMAP) want = 2;
    else if (now - g_vpSeen < 1000) want = 1; else if (g_fallbackOn && now - g_vpSeen > 3000 && g_menuSinceVp > 120) want = 2;
    if (host != want) {
        if (mv == g_hostMv[host]) { gfx_setvar(mv, "_root.mt_apm._visible", gv_bool(0)); gfx_setvar(mv, "_root.mt_apm_sh._visible", gv_bool(0)); g_hostMv[host] = NULL; if (mv == g_mv) g_mv = NULL; }
        return;
    }
    int vis = g_enabled && mt_ticking() && (!g_onlineOnly || mt_online());
    if (mv != g_mv || g_host != host || (now - g_check > 1000 && (g_check = now, !gfx_exists(mv, "mt_apm") || (host != 1 && gfx_exists(mv, "mt_clock") != g_withClock)))) {
        if (!vis) return;                                // made when there is something to show (the clock may come first)
        g_mv = mv; g_hostMv[host] = mv; g_host = host; g_check = now; g_shown = -1; g_lastVal = -1;
        if (host == 2) make_mini(mv); else if (host) make_top(mv); else make_menu(mv);
    }
    if (vis != g_shown) {
        g_shown = vis;
        gfx_setvar(mv, "_root.mt_apm._visible", gv_bool(vis));
        gfx_setvar(mv, "_root.mt_apm_sh._visible", gv_bool(vis && g_shadow));
        logmsg(vis ? "APM shown" : "APM hidden");
    }
    if (!vis) return;
    int v = apm_now();
    if (v != g_lastVal) { g_lastVal = v; set_text(mv, v); }
}
static void __thiscall hk_menu_upd(char *form, float dt) { o_menuUpd(form, dt); InterlockedIncrement(&g_menuSinceVp); frame(form, 0); }
static void __thiscall hk_mm_upd(char *form, float dt) { o_mmUpd(form, dt); frame(form, 2); }
static void __thiscall hk_vp_upd(char *form, float dt) { o_vpUpd(form, dt); g_vpSeen = GetTickCount(); g_menuSinceVp = 0; frame(form, 1); }

static int already_loaded(void) {
    char n[64]; wsprintfA(n, "Local\\dow2patch_APM_%lu", GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    static wchar_t dir[MAX_PATH];
    GetModuleFileNameW(h, dir, MAX_PATH); wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0; else dir[0] = 0;
    _snwprintf(g_ini, MAX_PATH, L"%lsAPM.ini", dir); _snwprintf(g_log, MAX_PATH, L"%lsAPM.log", dir);
    load_ini();
    logmsg("APM 1 (own add-on: actions per minute beside the victory point counter, in the game's own HUD movie)");
    if (already_loaded()) { logmsg("another APM copy is already active in this game, this one stays idle"); return TRUE; }
    if (!g_enabled) { logmsg("disabled in APM.ini"); return TRUE; }
    if (gfx_hook_slot(MENU_VT_UPD, NULLSUB_RET4, (void *)hk_menu_upd, (void **)&o_menuUpd)) logmsg("hooked the Menu button's per-frame update%s", (DWORD)o_menuUpd != NULLSUB_RET4 ? " (after another add-on)" : "");
    else { logmsg("Menu button form not as in DOW2.exe 3.19 - no APM"); return TRUE; }
    if (gfx_hook_slot(VP_VT_UPD, NULLSUB_RET4, (void *)hk_vp_upd, (void **)&o_vpUpd)) logmsg("hooked the victory point panel's per-frame update%s", (DWORD)o_vpUpd != NULLSUB_RET4 ? " (after another add-on)" : "");
    else logmsg("victory point panel form not as in DOW2.exe 3.19 - APM only under the Menu button");
    if (gfx_hook_slot(MM_VT_UPD, MM_UPD_FN, (void *)hk_mm_upd, (void **)&o_mmUpd)) logmsg("hooked the minimap panel's per-frame update%s (Place=minimap)", (DWORD)o_mmUpd != MM_UPD_FN ? " (after another add-on)" : "");
    else logmsg("minimap panel form not as in DOW2.exe 3.19 - no Place=minimap");
    mt_start_for(dir, g_ini, "APM");
    return TRUE;
}
