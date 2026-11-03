// SteamKeep.dll v2 - the match goes on when Steam goes away, for the whole match (Dawn of War II: Retribution 3.19, Elite).
//
// What the game does when Steam drops (DOW2.exe 3.19):
//  - Steam callbacks of Net::SteamLoginService: SteamServersDisconnected_t 0x004F6940 and SteamServerConnectFailure_t
//    0x004F6990 (EResult -> reason 0x004F6790), IPCFailure_t 0x004F6A20 (reason 2 SteamClientFailure), SteamShutdown_t
//    0x004F6A00 (reason 3 SteamServerShutdown); every frame 0x004E5900 also asks SteamUser()->BLoggedOn().
//    All of them end in SetConnected(false) 0x004F6F20, which puts events on the game's event bus (0x004CAB30):
//    type 0x0C "Steam client failure / shutdown" (reasons 2, 3), type 1 "signed out" (4 LoggedOut, 5 LoggedInElsewhere),
//    and through OnConnect 0x004D5C60 type 2 "connect failure" (any reason).
//  - GameObj listens on that bus at 0x0041FB0E ("GameObj::GameQuitForSteam - endReason[%d] is saved"): during a game it
//    turns those events into an end reason (0x0C -> 17 or 19, 1 -> 18, 2 -> 4) in GameObj+0x198. The next frame
//    (0x004240F5) runs GameQuitForSteamInternal 0x00426D9C: stats are reported, a dialog is shown and the player leaves
//    the match (17: a Windows message box and the game closes). The game itself skips it only for LAN games.
//  - When the Steam client shuts down or logs the user off it also sends the game window a close request; the game
//    sees it in GameApp::Loop (Plat::Window::ShutdownRequested, called at 0x0040E28A) and quits to the desktop
//    (players' logs: "SteamServerShutdown" -> "GameApp::Loop - shutdown requested" -> GameApp::Quit in the same frame).
//
// The simulation does not need Steam: the match runs over the relay (BattleServer) and the game's own network session.
// SteamKeep, only while a game is running (not in the menus, not in replays):
//  1. at 0x0041FB0E the Steam events 0x0C / 1 / 2 are not turned into an end reason - the player stays in the match;
//  2. a close request of the game window from outside (a bare WM_CLOSE - what the Steam client sends) is dropped for the rest
//     of the match once Steam has gone away in it (or while Steam is not logged on). The player's own ways out still work:
//     the game's Alt+F4 and menu Quit (the game's own path, OnQuitFinal), the window's close button / taskbar Close
//     (WM_SYSCOMMAND SC_CLOSE, seen by our window procedure), Windows shutting down (WM_QUERYENDSESSION / WM_ENDSESSION).
// v2 (owner 03.11.2026: Steam's servers can be away for an hour): no time limit - any number of drops and returns, the
// every-frame "logged on?" check included, never end the match. After the match the game is NOT closed even if Steam asked
// during it (the result and the replay go out as usual); when Steam is back the game logs in again by itself.
// Stats: ReportStats / the Steam stats calls (0x004F7B70, 0x007AA570) are asynchronous Steam / RLink calls, no waits.
// Nothing in the simulation changes (no sync impact): only the local "leave the match" reaction is not taken.
// Not covered: a match over Steam P2P (no relay) - its connections close with Steam; a dead internet connection.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define GAMEAPP_PTR   0x01335A78   // game state: +0x114 in a game (0 = menus), +0x118 game type flags
#define FN_STEAM_EVT  0x0041FB0E   // GameObj: Steam event -> end reason
#define SR_IAT        0x00F87D0C   // DOW2.exe import slot of Platform.dll Plat::Window::ShutdownRequested
#define FL_PLAYBACK   0x800
#define FL_MP         0x558        // MP campaign / skirmish / last stand / FFA solo / FFA team
static const unsigned char EVT_ORIG[20] = {0xA1,0x78,0x5A,0x33,0x01, 0x83,0xB8,0x14,0x01,0x00,0x00,0x00, 0x74,0x75,
                                          0x83,0x79,0x30,0x01, 0x75,0x6F};
// mov eax,[01335A78]; cmp dword [eax+114h],0; je ret; cmp dword [ecx+30h],1; jne ret

static wchar_t g_log[MAX_PATH];
static int g_mp = 1, g_sp = 1;
static int g_keep_close = 1;
static volatile DWORD g_lost_at; static volatile int g_lost, g_kept;
static DWORD g_req_at, g_poll_at;
static int g_live, g_lost_match, g_asked, g_logged_on = 1;           // per match: Steam gone at least once / asked us to close
static volatile int g_close_kind;                                    // 1 = the player / Windows, 2 = a bare WM_CLOSE from outside
static volatile DWORD g_sc_at; static WNDPROC g_oldproc; static HWND g_hwnd;
static unsigned char *g_srflag;
typedef unsigned char (__stdcall *SRFn)(void);
static SRFn g_sr;
typedef void *(__cdecl *SteamUserFn)(void);
typedef unsigned char (__attribute__((thiscall)) *BLoggedOnFn)(void *);
static SteamUserFn g_steamuser;
DWORD g_sk_cont = FN_STEAM_EVT + 5;

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static int live_game(DWORD *flags) {
    unsigned char *g = *(unsigned char **)GAMEAPP_PTR;
    if (!g || *(int *)(g + 0x114) == 0) return 0;
    DWORD fl = *(DWORD *)(g + 0x118); if (flags) *flags = fl;
    if (fl & FL_PLAYBACK) return 0;
    return (fl & FL_MP) ? g_mp : g_sp;
}
static const char *evt_name(int type) {
    switch (type) { case 0x0C: return "Steam client failure / Steam shut down"; case 1: return "signed out of Steam";
                    case 2: return "Steam connection failed"; default: return "?"; }
}
// 1 = the event is not turned into an end reason (the match goes on), 0 = the game's own code runs
__attribute__((used)) int __cdecl sk_decide(unsigned char *self, unsigned char *ev) {
    DWORD fl = 0;
    if (!live_game(&fl)) return 0;
    if (*(int *)(self + 0x30) != 1) return 0;                       // an end reason is already there: the game does nothing
    int type = *(int *)(ev + 0x10), would = 0;
    if (type == 0x0C) would = *(unsigned char *)(ev + 0x14) ? 0x11 : 0x13;
    else if (type == 1) would = 0x12;
    else if (type == 2 && *(unsigned char *)(ev + 0x0C) == 0) would = 4;
    if (!would) return 0;
    g_lost_at = GetTickCount(); g_lost = 1; g_kept++; g_lost_match = 1;
    logmsg("Steam went away during the %s game (event %d: %s) - the game would leave the match (end reason %d); "
           "staying in the match", (fl & FL_MP) ? "online" : "offline", type, evt_name(type), would);
    return 1;
}
// jump here from 0x0041FB0E (thiscall: ecx = GameObj part, [esp+4] = event, ret 4)
__asm__(".globl _sk_evt_hook\n_sk_evt_hook:\n"
        "  push %ecx\n  pushl 8(%esp)\n  push %ecx\n  call _sk_decide\n  add $8, %esp\n  pop %ecx\n"
        "  test %eax, %eax\n  jnz 1f\n"
        "  movl 0x01335A78, %eax\n  jmp *_g_sk_cont\n"
        "1:\n  ret $4\n");
extern void sk_evt_hook(void);

static void steam_poll(void) {                                       // Steam logged on? (once a second); log when it is back
    DWORD now = GetTickCount();
    if (!g_steamuser || now - g_poll_at < 1000) return;
    g_poll_at = now;
    void *u = g_steamuser(); int on = 0;
    if (u) { BLoggedOnFn f = (BLoggedOnFn)(*(void ***)u)[1]; on = f(u) != 0; }
    g_logged_on = on;
    if (!on) { if (!g_lost) { g_lost = 1; g_lost_at = now; } g_lost_match = 1; return; }
    if (g_lost) { logmsg("Steam is back (%lu s after it went away)", (now - g_lost_at) / 1000); g_lost = 0; }
}
// the game window's procedure in front of the game's own: tells the player's close from a bare WM_CLOSE sent from outside
static LRESULT CALLBACK sk_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SYSCOMMAND && (w & 0xFFF0) == SC_CLOSE) { g_sc_at = GetTickCount(); g_close_kind = 1; }
    else if (m == WM_CLOSE) {
        int keys = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;      // Alt+F4 held (not Alt alone: the game uses it)
        g_close_kind = (keys || (g_sc_at && GetTickCount() - g_sc_at < 2000)) ? 1 : 2;
    } else if (m == WM_QUERYENDSESSION || m == WM_ENDSESSION) g_close_kind = 1;
    return CallWindowProcW(g_oldproc, h, m, w, l);
}
static BOOL CALLBACK find_win(HWND h, LPARAM _) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || GetWindow(h, GW_OWNER) || !IsWindowVisible(h)) return TRUE;
    wchar_t c[64]; GetClassNameW(h, c, 64); if (!wcscmp(c, L"ConsoleWindowClass")) return TRUE;
    g_hwnd = h; return FALSE;
}
static DWORD WINAPI subclass_later(LPVOID _) {                       // the game makes its window a few seconds after the start
    for (int i = 0; i < 1200 && !g_hwnd; i++) { Sleep(250); EnumWindows(find_win, 0); }
    if (!g_hwnd) { logmsg("window: the game window was not found - every outside close request counts as Steam's"); return 0; }
    g_oldproc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)sk_wndproc);
    logmsg("window: %p subclassed (the player's close button / Alt+F4 / menu work as usual)", (void *)g_hwnd);
    return 0;
}
// Plat::Window::ShutdownRequested for GameApp::Loop: Steam's close request is dropped for the rest of the match
static unsigned char __stdcall sk_shutdown_requested(void) {
    unsigned char r = g_sr();
    int live = live_game(NULL);
    if (live && !g_live) { g_lost_match = 0; g_asked = 0; g_close_kind = 0; g_req_at = 0; }        // a new match
    if (!live && g_live && g_asked)
        logmsg("match over: Steam asked the game to close during it - the game stays open (result and replay go out as usual)");
    g_live = live;
    if (live) steam_poll();
    if (!r || !live) { g_req_at = 0; return r; }
    if (g_close_kind == 1) { logmsg("the player closes the game window - closing"); g_close_kind = 0; g_req_at = 0; return r; }
    DWORD now = GetTickCount();
    if (g_keep_close && (g_lost_match || !g_logged_on)) {
        *g_srflag = 0; g_req_at = 0; g_close_kind = 0;
        if (!g_asked++ || g_asked % 10 == 0)
            logmsg("close request from outside (Steam) during the match, Steam %s - ignored, the match goes on (#%d)",
                   g_logged_on ? "went away in this match" : "is not logged on", g_asked);
        return 0;
    }
    if (!g_req_at) { g_req_at = now ? now : 1; return 0; }          // Steam's own event may come a moment after its close request
    if (now - g_req_at < 3000) return 0;
    logmsg("close request from outside with Steam fine - closing"); g_req_at = 0; g_close_kind = 0; return r;
}
// the window close check is the game's import of Plat::Window::ShutdownRequested (one call, GameApp::Loop 0x0040E28A)
static int hook_close(void) {
    HMODULE plat = GetModuleHandleA("Platform.dll");
    unsigned char *sr = plat ? (unsigned char *)GetProcAddress(plat, "?ShutdownRequested@Window@Plat@@YG_NXZ") : NULL;
    DWORD *slot = (DWORD *)SR_IAT, old;
    if (!sr || IsBadReadPtr(slot, 4) || *slot != (DWORD)sr || sr[0] != 0xA0 || sr[5] != 0xC3) return 0;
    g_srflag = *(unsigned char **)(sr + 1); g_sr = (SRFn)sr;           // mov al,[flag]; ret
    VirtualProtect(slot, 4, PAGE_READWRITE, &old); *slot = (DWORD)sk_shutdown_requested; VirtualProtect(slot, 4, old, &old);
    return 1;
}
static DWORD WINAPI close_later(LPVOID _) {                         // loaded before the game's imports are bound (early loader)
    for (int i = 0; i < 600; i++) { Sleep(100); if (hook_close()) { logmsg("close request hook: installed"); return 0; } }
    unsigned char *sr = NULL; HMODULE plat = GetModuleHandleA("Platform.dll");
    if (plat) sr = (unsigned char *)GetProcAddress(plat, "?ShutdownRequested@Window@Plat@@YG_NXZ");
    logmsg("close request hook: Plat::Window::ShutdownRequested not as expected (%p, slot %08lX) - skipped", sr, *(DWORD *)SR_IAT);
    return 0;
}
static void install(void) {
    unsigned char *t = (unsigned char *)FN_STEAM_EVT;
    if (IsBadReadPtr(t, sizeof EVT_ORIG) || memcmp(t, EVT_ORIG, sizeof EVT_ORIG)) {
        logmsg("DOW2.exe is not 3.19 (no match at %08X) - not patching", FN_STEAM_EVT); return; }
    DWORD old; VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old);
    t[0] = 0xE9; *(DWORD *)(t + 1) = (DWORD)sk_evt_hook - (DWORD)(t + 5);
    VirtualProtect(t, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 5);
    const char *close = hook_close() ? "yes" : "waiting for the game's imports";
    if (strcmp(close, "yes")) CloseHandle(CreateThread(NULL, 0, close_later, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, subclass_later, NULL, 0, NULL));
    HMODULE sa = GetModuleHandleA("steam_api.dll");
    g_steamuser = sa ? (SteamUserFn)GetProcAddress(sa, "SteamUser") : NULL;
    logmsg("SteamKeep 2: the match goes on when Steam goes away, the whole match (patched %08X; online %s, offline %s; "
           "Steam's close request ignored in the match: %s, hook %s)", FN_STEAM_EVT, g_mp ? "yes" : "no", g_sp ? "yes" : "no",
           g_keep_close ? "yes" : "no", close);
}
// one copy per game process: a patch can come both from Elite_DLLs (launcher) and from dow2-patches\ (winmm loader)
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"SteamKeep.ini"); wcscat(g_log, L"SteamKeep.log");
    if (already_loaded("SteamKeep")) { logmsg("another SteamKeep copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"SteamKeep", L"Enabled", 1, ini)) { logmsg("disabled in SteamKeep.ini"); return TRUE; }
    g_mp = GetPrivateProfileIntW(L"SteamKeep", L"Online", 1, ini) != 0;
    g_sp = GetPrivateProfileIntW(L"SteamKeep", L"Offline", 1, ini) != 0;
    g_keep_close = GetPrivateProfileIntW(L"SteamKeep", L"IgnoreSteamClose", 1, ini) != 0;
    install();
    return TRUE;
}
