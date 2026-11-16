// common/menu.h - the front end's menu movie and which of its full-screen panels is up (DOW2.exe 3.19), shared SOURCE of the
// add-ons that take the game somewhere from its menu. Each add-on keeps its own DLL and copy of this state (docs/ADDON_RULES.md).
// Taken from RoomJoin 16 (roomjoin/roomjoin.c: fs_hook, probe_screen, is_menu_movie, the movie destructor, nudge_menu) for
// ReplayOpen 4 / LiveReplay 5 (16.11.2026, siritos "a replay hangs 15 s in the menu", RESEARCH_TECH_RU.md 1.3-1.5): Elite's loader
// brings the add-ons in after the menu's first panel is up, so the panel's OnLoadComplete is never seen after a cold start and the
// screen stayed "unknown" (ReplayOpen 3 / LiveReplay 4 then waited 10 s of frames for the box). RoomJoin itself still has its own
// copy (unchanged in this round).
//
// How the screen is learnt:
//   - the menu movie: from ANY fscommand of the menu (_level0.display_item..., OnLoadComplete name:=_level0.display_item) and,
//     for other commands, a movie not asked yet that has _level0.display_item._visible (as bool) and a vtable inside DOW2.exe;
//   - the screen: OnLoadComplete of a ".panel_mc" (its url), or asked from the movie (menu_probe: panel1..3_mc.panel_mc ._visible
//     and ._url - GetVariable only, never Invoke from the frame: MMFind's lesson);
//   - no command at all (a menu nobody touches sends nothing): menu_nudge() posts a few WM_MOUSEMOVE over the right side of the
//     game's window (only when it is in front; the real cursor does not move) - the menu answers with its hover commands;
//   - the movie is forgotten when it is deleted (its class's deleting destructor, vtable 0x0104BBFC slot 0 = 0x00DCE310, wrapped
//     and chained with RoomJoin's wrap under Local\dow2patch_iat_<pid>), when a world starts, and when a match's movie loads.
// The caller (all on the game's thread: its fscommand hook and its frame hook):
//   #define MENU_LOG logmsg; patch_lock() / patch_unlock() / is_code() defined before the include (as every add-on has them);
//   menu_on_fscmd(self, movie, cmd, args, busy)  in its fscommand hook before the original (busy = a world runs or loads);
//     returns the screen of a full-screen panel that just loaded (OnLoadComplete .panel_mc), else -1;
//   menu_frame(now, busy)                         once a frame (forgets a deleted movie, or the movie when a world runs);
//   menu_tick(now, nudge)                         while the add-on wants to know the screen: probe every 250 ms, nudge if allowed;
//   menu_scr, menu_mv                             the screen (SC_*) and the movie (NULL = not known).
#ifndef COMMON_MENU_H
#define COMMON_MENU_H
#include <windows.h>
#include <string.h>
#include <stdio.h>
#ifndef MENU_LOG
#error "#define MENU_LOG <the add-on's printf-like log function> before including common/menu.h"
#endif

enum { SC_UNKNOWN, SC_OTHER, SC_MAIN, SC_MM, SC_LIST, SC_SETUP };
static const char *SC_NAME[] = {"unknown", "other", "main", "find_game", "games_list", "lobby"};

typedef struct { int type; int pad; union { double num; unsigned char b; const wchar_t *ws; const char *s; } v; } MenuGFxValue;
enum { MENU_GFX_BOOL = 2, MENU_GFX_STRING = 4, MENU_GFX_WSTRING = 5 };
typedef char (__fastcall *MenuGetVarFn)(void *movie, void *edx, MenuGFxValue *out, const char *path);
typedef void *(__fastcall *MenuDtorFn)(void *self, void *edx, unsigned flags);

static volatile int menu_scr;                       // SC_*: the screen
static void *menu_mv, *menu_hd;                     // the menu movie and the fscommand handler its buttons use (NULL = not known)
static char menu_panel[96];                         // the panel clip of the screen (as probed)
static void *menu_tried[16]; static int menu_ntried; // movies already asked "are you the menu?"
static DWORD menu_dtVt[4]; static MenuDtorFn menu_dtOrig[4]; static int menu_dtN; static volatile int menu_gone;
static DWORD menu_probeAt;

static int menu_url_screen(const char *u) {
    return strstr(u, "main_menu.gfx") ? SC_MAIN : strstr(u, "matchmaking.gfx") ? SC_MM : strstr(u, "game_list_movie.gfx") ? SC_LIST
         : strstr(u, "game_setup_movie.gfx") ? SC_SETUP : SC_OTHER;
}
static int menu_as_get(void *movie, const char *path, MenuGFxValue *v) {      // GFxMovie vtbl+0x30 = GetVariable
    memset(v, 0, sizeof *v); return ((MenuGetVarFn)(*(void ***)movie)[0x30 / 4])(movie, NULL, v, path) != 0;
}
static int menu_is_movie(void *mv) {
    if (!mv || IsBadReadPtr(mv, 0x600)) return 0;
    DWORD vt = *(DWORD *)mv;                                          // GFx is linked into DOW2.exe: its vtable is there
    if (vt < 0x00F87000 || vt >= 0x0124F000 || IsBadReadPtr((void *)vt, 0x34)) return 0;
    MenuGFxValue v; return menu_as_get(mv, "_level0.display_item._visible", &v) && v.type == MENU_GFX_BOOL;
}
static void menu_forget(const char *why) {
    if (menu_mv || menu_hd) MENU_LOG("the menu movie forgotten (%s)", why);
    if (menu_scr != SC_UNKNOWN) MENU_LOG("screen forgotten (%s)", why);
    menu_mv = NULL; menu_hd = NULL; menu_ntried = 0; menu_scr = SC_UNKNOWN; menu_panel[0] = 0;
}
// the menu movie's own end, whatever deletes it (RoomJoin 9 crashed in a movie the rebuilt front end had deleted - 05.11.2026)
static void *__fastcall menu_dtor(void *self, void *edx, unsigned flags) {
    MenuDtorFn orig = NULL; DWORD vt = self && !IsBadReadPtr(self, 4) ? *(DWORD *)self : 0;
    for (int i = 0; i < menu_dtN; i++) if (menu_dtVt[i] == vt) orig = menu_dtOrig[i];
    if (!orig) orig = menu_dtOrig[0];                                  // (a derived class's destructor sets its own vtable first)
    if (self && self == menu_mv) { menu_mv = NULL; menu_hd = NULL; menu_gone = 1; MENU_LOG("the menu movie %p is being deleted", self); }
    for (int i = 0; i < menu_ntried; i++) if (menu_tried[i] == self) menu_tried[i] = NULL;   // its address may come back as another movie
    return orig(self, edx, flags);
}
static void menu_hook_dtor(void *mv) {
    DWORD vt = *(DWORD *)mv;
    for (int i = 0; i < menu_dtN; i++) if (menu_dtVt[i] == vt) return;
    if (menu_dtN >= 4 || IsBadReadPtr((void *)vt, 4)) return;
    HANDLE mx = patch_lock(); DWORD cur = *(volatile DWORD *)vt, o;
    if (cur != (DWORD)menu_dtor && is_code(cur)) {
        menu_dtVt[menu_dtN] = vt; menu_dtOrig[menu_dtN] = (MenuDtorFn)cur;   // before the slot points to us
        if (VirtualProtect((void *)vt, 4, PAGE_READWRITE, &o)) {
            InterlockedExchange((volatile LONG *)vt, (LONG)(DWORD)menu_dtor); VirtualProtect((void *)vt, 4, o, &o); menu_dtN++;
            MENU_LOG("the menu movie's destructor wrapped (vtable %08lX, slot 0 was %08lX%s)", vt, cur,
                     cur == 0x00DCE310 ? "" : " - another add-on's wrap or not as in DOW2.exe 3.19: chained");
        }
    }
    patch_unlock(mx);
}
static void menu_seen(void *mv, void *handler, const char *how) {
    if (menu_mv == mv && (menu_hd || !handler)) return;
    menu_mv = mv; if (handler) menu_hd = handler;
    MENU_LOG("the menu movie %p (vtable %08lX, handler %p) from %s", mv, *(DWORD *)mv, menu_hd, how);
    menu_hook_dtor(mv);
}
// which full-screen panel is up, asked from the menu movie (every visible panel counts: Options sits in panel2 over a transition
// movie in panel1; one that is not ours -> SC_OTHER, else the one of ours) -> the screen, SC_UNKNOWN when no panel is visible
static int menu_probe(void) {
    void *m = menu_mv; if (!m) return SC_UNKNOWN;
    int found = SC_UNKNOWN; char pth[96] = "", fu[200] = "";
    for (int k = 1; k <= 3; k++) {
        char p[96], q[128]; MenuGFxValue v; snprintf(p, sizeof p, "_level0.display_item.panel%d_mc.panel_mc", k);
        snprintf(q, sizeof q, "%s._visible", p); if (!menu_as_get(m, q, &v) || v.type != MENU_GFX_BOOL || !v.v.b) continue;
        snprintf(q, sizeof q, "%s._url", p); if (!menu_as_get(m, q, &v)) continue;
        char u[200] = "";
        if (v.type == MENU_GFX_STRING && v.v.s && !IsBadReadPtr(v.v.s, 1)) snprintf(u, sizeof u, "%s", v.v.s);
        else if (v.type == MENU_GFX_WSTRING && v.v.ws && !IsBadReadPtr(v.v.ws, 2)) WideCharToMultiByte(CP_UTF8, 0, v.v.ws, -1, u, sizeof u, NULL, NULL);
        u[sizeof u - 1] = 0;
        if (!u[0]) continue;
        for (char *c = u; (c = strstr(c, "%5F")) || (c = strstr(u, "%5f")); ) { *c = '_'; memmove(c + 1, c + 3, strlen(c + 3) + 1); }   // main%5Fmenu.gfx
        if (strstr(u, "transition_movie.gfx")) continue;               // the fade between panels, not a screen
        int sc = menu_url_screen(u);
        if (found == SC_UNKNOWN || sc == SC_OTHER) { found = sc; strcpy(pth, p); strcpy(fu, u); }
        if (sc == SC_OTHER) break;
    }
    if (found == SC_UNKNOWN) return SC_UNKNOWN;
    if (found != menu_scr || strcmp(pth, menu_panel)) MENU_LOG("screen asked: %s = %s (%s)", pth, fu, SC_NAME[found]);
    strcpy(menu_panel, pth); menu_scr = found;
    return found;
}
// the game's fscommand (its own thread), before the original; busy = a world runs or loads -> the screen of a panel that loaded, else -1
static int menu_on_fscmd(void *self, void *movie, const char *cmd, const char *args, int busy) {
    if (!cmd || IsBadReadPtr(cmd, 1)) return -1;
    int haveArgs = args && !IsBadReadPtr(args, 1);
    if (!strncmp(cmd, "_level0.display_item", 20) || (!strcmp(cmd, "OnLoadComplete") && haveArgs && strstr(args, "name:=_level0.display_item"))) {
        if (!busy && (movie != menu_mv || menu_hd != self)) menu_seen(movie, self, "a menu command");
    } else if (movie != menu_mv && !busy) {
        int seen = 0;
        for (int i = 0; i < menu_ntried; i++) if (menu_tried[i] == movie) seen = 1;
        if (!seen && menu_ntried < 16) { menu_tried[menu_ntried++] = movie; if (menu_is_movie(movie)) menu_seen(movie, self, cmd); }
    }
    if (!strcmp(cmd, "OnLoadMovieComplete") && haveArgs && !IsBadReadPtr(args, 6) && !strncmp(args, "/mp_mc", 6)) { menu_forget("a match is loading"); return -1; }
    if (!strcmp(cmd, "OnLoadComplete") && haveArgs && (movie == menu_mv || !menu_mv)) {
        const char *nm = strstr(args, "name:="), *url = strstr(args, "url:=");
        if (nm && url) {
            const char *e = strchr(nm, ';'); int len = e ? (int)(e - nm - 6) : 0;
            if (len >= 9 && len < 95 && !strncmp(nm + 6 + len - 9, ".panel_mc", 9)) {
                int sc = menu_url_screen(url);
                if (sc != SC_OTHER && !menu_mv) menu_seen(movie, self, "a menu panel");
                if (sc != menu_scr) MENU_LOG("screen %s", SC_NAME[sc]);
                memcpy(menu_panel, nm + 6, len); menu_panel[len] = 0; menu_scr = sc;
                return sc;
            }
        }
    }
    return -1;
}
// once a frame (the game's thread): a deleted movie / a world -> forgotten
static void menu_frame(DWORD now, int busy) {
    (void)now;
    if (menu_gone) { menu_gone = 0; menu_forget("the menu movie deleted (the front end rebuilt or unloaded)"); }
    if (busy && (menu_mv || menu_scr != SC_UNKNOWN)) menu_forget("a world starts");
}
static HWND menu_game_window(void) {
    HWND w = GetForegroundWindow(); DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId() ? w : NULL;
}
// a menu nobody touches sends nothing: mouse moves posted over its buttons (the right side of the window), only with the game in front
static void menu_nudge(DWORD now) {
    static DWORD last; static int n;
    if (menu_mv || now - last < 400) return;
    HWND w = menu_game_window(); if (!w) return;
    last = now; RECT r; if (!GetClientRect(w, &r) || r.right < 200 || r.bottom < 200) return;
    int x = r.right - r.right / 8 - (n % 3) * (r.right / 20), y = r.bottom / 4 + (n % 7) * (r.bottom / 14);
    PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
    if (++n % 10 == 1) MENU_LOG("the menu movie not known yet: mouse moves over the menu's buttons (%d)", n);
    if (n % 21 == 20) {                                                // the game's idea of the cursor back where the cursor is
        POINT c; if (GetCursorPos(&c) && ScreenToClient(w, &c)) PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y)); }
}
// while the add-on wants the screen (never while a world runs or loads): asked every 250 ms, nudged without a movie when allowed
static void menu_tick(DWORD now, int nudge) {
    if (menu_mv) {
        if ((menu_scr == SC_UNKNOWN || menu_scr == SC_OTHER) && now - menu_probeAt > 250) { menu_probeAt = now; menu_probe(); }
        return;
    }
    if (nudge) menu_nudge(now);
}
// the game's cursor back once the movie is known (after a nudge)
static void menu_cursor_back(void) {
    HWND gw = menu_game_window(); POINT c;
    if (gw && GetCursorPos(&c) && ScreenToClient(gw, &c)) PostMessageW(gw, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y));
}
#endif
