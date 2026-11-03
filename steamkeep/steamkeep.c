// SteamKeep.dll - the match goes on when Steam goes away (Dawn of War II: Retribution 3.19, Elite).
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
//  2. a close request of the game window that comes together with Steam going away (within CloseWindowSec) is dropped.
// After the match the game is as usual: when Steam is back the game logs in again by itself (SetConnected(true)).
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
static DWORD g_close_ms = 30000;
static volatile DWORD g_lost_at; static volatile int g_lost, g_kept;
static DWORD g_req_at, g_poll_at;
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
    g_lost_at = GetTickCount(); g_lost = 1; g_kept++;
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

static void steam_poll(void) {                                       // log when Steam is back
    DWORD now = GetTickCount();
    if (!g_lost || !g_steamuser || now - g_poll_at < 1000) return;
    g_poll_at = now;
    void *u = g_steamuser(); if (!u) return;
    BLoggedOnFn on = (BLoggedOnFn)(*(void ***)u)[1];
    if (on(u)) { logmsg("Steam is back (%lu s after it went away)", (now - g_lost_at) / 1000); g_lost = 0; }
}
// Plat::Window::ShutdownRequested for GameApp::Loop: the window's close request that comes with Steam going away is dropped
static unsigned char __stdcall sk_shutdown_requested(void) {
    unsigned char r = g_sr();
    int live = live_game(NULL);
    if (live) steam_poll();
    if (!r || !live) { g_req_at = 0; return r; }
    DWORD now = GetTickCount();
    if (g_kept && now - g_lost_at < g_close_ms) {
        *g_srflag = 0; g_req_at = 0;
        logmsg("the game window was asked to close %lu ms after Steam went away - ignored, the match goes on "
               "(close it from the game menu, or Alt+F4 again after %lu s)", now - g_lost_at, g_close_ms / 1000);
        return 0;
    }
    if (!g_req_at) { g_req_at = now ? now : 1; return 0; }          // Steam's own event may come a moment after its close request
    if (now - g_req_at < 1500) return 0;
    g_req_at = 0; return r;
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
    const char *close = g_close_ms ? (hook_close() ? "yes" : "waiting for the game's imports") : "no";
    if (g_close_ms && strcmp(close, "yes")) CloseHandle(CreateThread(NULL, 0, close_later, NULL, 0, NULL));
    HMODULE sa = GetModuleHandleA("steam_api.dll");
    g_steamuser = sa ? (SteamUserFn)GetProcAddress(sa, "SteamUser") : NULL;
    logmsg("SteamKeep 1: the match goes on when Steam goes away (patched %08X; online %s, offline %s; "
           "close request with Steam: %s, %lu s)", FN_STEAM_EVT, g_mp ? "yes" : "no", g_sp ? "yes" : "no", close, g_close_ms / 1000);
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
    g_close_ms = GetPrivateProfileIntW(L"SteamKeep", L"CloseWindowSec", 30, ini) * 1000;
    install();
    return TRUE;
}
