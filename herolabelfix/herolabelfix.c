// HeroLabelFix.dll - the bar over a hero (squad_leader_decorator: icon + health/energy) sometimes never comes back after the
// hero is downed and revived (DoW2: Retribution 3.19; seen in replays, the same path exists in live games).
//
// Each frame the game's movie manager (0x00680C00, DAT_01335088) walks its elements and asks only the SHOWN ones
// (+0x1C != 0) whether they are on screen (vtable slot 0x04, 0x00B92520); that test calls the decorator's draw
// (vtable 0x011129E4 slot 0x10 = 0x00B25070, thiscall, one float argument it does not use, ret 4). When the hero is downed,
// the decorator's update (slot 0x30 = 0x00B24370, called from the UI sync 0x00B92380 for every unit record) clears "valid"
// (+0x9C = 0, e.g. a downed hero of a player hostile to the viewing one, always so in a replay), and the next draw hides the
// movie (+0x1C = 0). After the revive the update sets "valid" back to 1, but a hidden element is never asked again, so the
// draw that would show it is never called: no bar over the hero for the rest of the match. "Sometimes": only when the hero
// was on screen when the draw saw him downed.
//
// Fix: after the decorator's own update, a decorator that is valid but hidden (and already set up by the manager) gets
// the game's own draw once, under the manager's lock - the same show step the game does itself for its pop-up elements.
// From then on the normal per-frame path draws it. Interface only - nothing in the simulation is read differently or changed.
// (v1 hooked slot 0x04 and never ran for hidden decorators: the manager skips them before that call.)
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define VT_SQUAD_LEADER_DECO 0x011129E4u
#define SLOT_DRAW   0x10
#define SLOT_UPDATE 0x30
#define FN_DRAW     0x00B25070u
#define FN_UPDATE   0x00B24370u
#define P_MOVIE_MGR 0x01335088u
static wchar_t g_log[MAX_PATH];
static volatile LONG g_restored;
typedef void (__fastcall *Upd_t)(void *self, void *edx, void *a, void *b, void *c);
typedef void (__fastcall *Draw_t)(void *self, void *edx, float unused);
typedef void (__fastcall *Mx_t)(void *mx, void *edx);
typedef BOOL (__fastcall *MxTry_t)(void *mx, void *edx);
static Upd_t o_update;
static MxTry_t p_try; static Mx_t p_leave;

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static void __fastcall hk_update(void *self, void *edx, void *a, void *b, void *c) {
    o_update(self, edx, a, b, c);
    unsigned char *d = (unsigned char *)self;
    // valid again, still hidden from the time the hero was down; +0x28/+0x2C (screen divisors) are set when the manager
    // takes the element in - before that the draw must not run
    if (!d[0x9C] || d[0x1C] || !*(int *)(d + 0x28) || !*(int *)(d + 0x2C)) return;
    char *mgr = *(char **)P_MOVIE_MGR; if (!mgr) return;
    if (!p_try) {                                     // the manager's lock (Platform.dll exports), resolved on first need
        static int tried; if (tried) return; tried = 1;
        HMODULE plat = GetModuleHandleA("Platform.dll");
        p_leave = plat ? (Mx_t)GetProcAddress(plat, "?Leave@RecursiveMutex@Plat@@QAEXXZ") : NULL;
        p_try = p_leave ? (MxTry_t)GetProcAddress(plat, "?TryEnter@RecursiveMutex@Plat@@QAE_NXZ") : NULL;
        if (!p_try) { logmsg("Platform.dll has no RecursiveMutex exports - fix stays idle"); return; }
    }
    void *mx = mgr + 0x45;
    if (!(unsigned char)p_try(mx, NULL)) return;      // the manager is busy on another thread: try again on the next update
    if (!d[0x1C]) ((Draw_t)FN_DRAW)(self, NULL, 0.0f);
    p_leave(mx, NULL);
    LONG n = InterlockedIncrement(&g_restored);
    if (n <= 50 || n % 100 == 0) logmsg("bar shown again (decorator %p, total %ld)", self, n);
}
static void install(void) {
    DWORD *vt = (DWORD *)VT_SQUAD_LEADER_DECO;
    static const unsigned char upd[13] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x6A, 0xFF, 0x68, 0xDB, 0x61, 0xF1, 0x00 };
    if (IsBadReadPtr(vt, SLOT_UPDATE + 4) || vt[SLOT_UPDATE / 4] != FN_UPDATE || vt[SLOT_DRAW / 4] != FN_DRAW
        || memcmp((void *)FN_DRAW, "\x51\x53\x56\x8B\xF1\x33\xDB\x38\x9E\x9C\x00\x00\x00", 13) || memcmp((void *)FN_UPDATE, upd, sizeof upd)) {
        logmsg("DOW2.exe is not 3.19 (no match at the squad leader decorator) - not patching"); return;
    }
    o_update = (Upd_t)vt[SLOT_UPDATE / 4];
    DWORD old; VirtualProtect(&vt[SLOT_UPDATE / 4], 4, PAGE_READWRITE, &old); vt[SLOT_UPDATE / 4] = (DWORD)hk_update; VirtualProtect(&vt[SLOT_UPDATE / 4], 4, old, &old);
    logmsg("HeroLabelFix 1: hero bars come back after a revive (squad leader decorator update %08X hooked)", FN_UPDATE);
}
// one copy per game process: a patch can come both from Elite_DLLs (launcher) and from dow2-patches\ (winmm loader)
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"HeroLabelFix.ini"); wcscat(g_log, L"HeroLabelFix.log");
    if (already_loaded("HeroLabelFix")) { logmsg("another HeroLabelFix copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"HeroLabelFix", L"Enabled", 1, ini)) { logmsg("disabled in HeroLabelFix.ini"); return TRUE; }
    install();
    return TRUE;
}
