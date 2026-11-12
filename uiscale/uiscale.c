// UIScale.dll - the size of the in-match interface at any resolution (Dawn of War II: Retribution 3.19, vanilla and Elite).
//
// How the game sizes its interface (DOW2.exe 3.19, found 12.11.2026):
//  - Every in-match interface movie (Scaleform) is a form with a layout method (vtable slot 6, +0x18):
//    layout(screen_w, screen_h, k, &x, &y, &w, &h). The 12 forms of the match (minimap 0x00B3F860, unit panel, abilities,
//    resources, hero portraits, objectives, menu button, ...) all take their size from one function, 0x00B7C130:
//        scale = clamp(screen_h / 1200 * uiscale / 100 * k, 0.5, 1.0)
//    (uiscale = the "uiscale" setting of configuration.lua, read once per game start through SystemConfig::GetSetting(
//    "UIScale"); k = the form's own factor) and multiply the movie's own width and height by it.
//  - So the interface can only SHRINK from its own pixel size: the cap of 1.0 means that from 1200 lines up (1440p, 4K) any
//    uiscale above 100 does nothing, at 1080p everything above ~111 % looks the same. Vanilla and Elite alike (same DOW2.exe).
//  - The front end (main menu, lobby, army painter, loading) is not laid out by this function and does not read uiscale.
//
// UIScale (nothing is drawn by us; the game lays out and draws its own movies):
//  - 0x00B7C130 is replaced (a jump at its start) by our function with the same contract (stdcall-like, 2 stack args, the
//    result in xmm0, every other register kept): scale = S * k, clamped to 0.25 ... 4.0.
//  - Scale=auto (default): S = screen_h / 1080 * uiscale / 100 - by the resolution (1080p 100 %, 1440p 133 %, 4K 200 %) times the
//    game's own UI scale (configuration.lua "uiscale", the launcher's Settings slider 75-200 %, the game's 100 = just auto);
//    read once per game start, as the game does. Scale=<percent> in UIScale.ini: S = percent / 100 of the movies' own size,
//    whatever configuration.lua says.
//  - It only changes where and how big the interface movies are; not simulation: no effect on sync, replays or other players.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define FN_SCALE 0x00B7C130u
static const unsigned char FN_PAT[] = {             // xorps xmm0,xmm0; sub esp,8; comiss xmm0,[012FED7C]; jb +5F
    0x0F, 0x57, 0xC0, 0x83, 0xEC, 0x08, 0x0F, 0x2F, 0x05, 0x7C, 0xED, 0x2F, 0x01, 0x72, 0x5F};
static const unsigned char CAP_PAT[] = {            // ... movss xmm2,[0105B930] (1.0 = the cap) at 0x00B7C1C5
    0xF3, 0x0F, 0x10, 0x15, 0x30, 0xB9, 0x05, 0x01};

static wchar_t g_log[MAX_PATH];
static int g_auto = 1;
static double g_fixed = 1.0;                         // Scale/100
static void logmsg(const char *fmt, ...);
static double g_game = -1;                           // the game's own uiscale / 100 (auto only), read at the first layout
#define KEY_UISCALE 0x013AAFB4u                      // Dictionary::Key "UIScale" (made by DOW2.exe's static init 0x00F3A0F0)
typedef void *(__stdcall *SCInstFn)(void);
typedef int (__attribute__((thiscall)) *SCGetFn)(void *self, const void *key);

static double game_uiscale(void) {                  // SystemConfig::Instance()->GetSetting("UIScale"), like 0x00B7C130 does
    HMODULE u = GetModuleHandleA("Util.dll"); if (!u) return 1.0;
    SCInstFn inst = (SCInstFn)GetProcAddress(u, "?Instance@SystemConfig@@SGPAV1@XZ");
    SCGetFn get = (SCGetFn)GetProcAddress(u, "?GetSetting@SystemConfig@@QBEHABVKey@Dictionary@@@Z");
    if (!inst || !get) { logmsg("Util.dll has no SystemConfig - the game's UI scale taken as 100 %%"); return 1.0; }
    void *sc = inst(); if (!sc) return 1.0;
    int v = get(sc, (const void *)KEY_UISCALE);
    if (v == -1) { logmsg("configuration.lua has no uiscale - taken as 100 %%"); return 1.0; }
    double g = v / 100.0; if (g < 0.25) g = 0.25; if (g > 4.0) g = 4.0;
    logmsg("the game's own UI scale (configuration.lua uiscale, the launcher's slider): %d %%", v);
    return g;
}
static volatile LONG g_logged;

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

static double base_scale(int h) {
    if (!g_auto) return g_fixed;
    if (g_game < 0) g_game = game_uiscale();                      // the layout runs on the game's main thread, as the game's own read
    return (h > 0 ? h / 1080.0 : 1.0) * g_game;
}

// called from the asm stub below: the game's (screen height, form factor) -> the form's scale
float __cdecl us_compute(int h, float k) {
    double s = base_scale(h) * k;
    if (s < 0.25) s = 0.25; if (s > 4.0) s = 4.0;
    static int last_h = -1; static float last_k[16]; static int nk;
    if (h != last_h) { last_h = h; nk = 0; }
    int seen = 0; for (int i = 0; i < nk; i++) if (last_k[i] == k) seen = 1;
    if (!seen && g_logged < 64) {                                 // one line per (screen height, form factor), at most 64
        if (nk < 16) last_k[nk++] = k;
        InterlockedIncrement(&g_logged);
        double gs = h / 1200.0 * k * (g_game > 0 ? g_game : 1.0); gs = gs < 0.5 ? 0.5 : gs > 1.0 ? 1.0 : gs;
        logmsg("interface layout: screen height %d, form factor %.3f -> scale %.3f (the game's own: %.3f)", h, k, s, gs);
    }
    return (float)s;
}

// the replacement of 0x00B7C130: args [esp+4] = screen height (int), [esp+8] = form factor (float); result in xmm0; ret 8.
// The game's callers expect every register but xmm0 (and the flags) as it was - eax/ecx/edx are saved, our C code is x87 only.
__asm__(
    ".globl _us_stub\n"
    "_us_stub:\n"
    "  push %ecx\n"
    "  push %edx\n"
    "  push %eax\n"
    "  push 20(%esp)\n"                 // k   (eax,edx,ecx,ret,h,k -> k at +20)
    "  push 20(%esp)\n"                 // h   (one more push -> h at +20)
    "  call _us_compute\n"
    "  add $8, %esp\n"
    "  sub $4, %esp\n"
    "  fstps (%esp)\n"
    "  movss (%esp), %xmm0\n"
    "  add $4, %esp\n"
    "  pop %eax\n"
    "  pop %edx\n"
    "  pop %ecx\n"
    "  ret $8\n");
void us_stub(void);

static int install(void) {
    unsigned char *fn = (unsigned char *)FN_SCALE;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(fn, &mi, sizeof mi) || mi.State != MEM_COMMIT) { logmsg("DOW2.exe code not found - not changing the interface"); return 0; }
    if (memcmp(fn, FN_PAT, sizeof FN_PAT) || memcmp(fn + 0x95, CAP_PAT, sizeof CAP_PAT)) {
        if (fn[0] == 0xE9) logmsg("0x%08X is already patched by someone else (jmp) - not changing the interface", FN_SCALE);
        else logmsg("DOW2.exe is not 3.19 (no interface scale function at 0x%08X) - not changing the interface", FN_SCALE);
        return 0;
    }
    DWORD old;
    if (!VirtualProtect(fn, 5, PAGE_EXECUTE_READWRITE, &old)) { logmsg("VirtualProtect failed (%lu)", GetLastError()); return 0; }
    unsigned char j[5]; j[0] = 0xE9; *(DWORD *)(j + 1) = (DWORD)us_stub - (DWORD)(fn + 5);
    memcpy(fn, j, 5);                                             // the 5 bytes are written while no match is laid out
    VirtualProtect(fn, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), fn, 5);
    return 1;
}

static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"UIScale.ini"); wcscat(g_log, L"UIScale.log");
    if (already_loaded("UIScale")) { logmsg("another UIScale copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"UIScale", L"Enabled", 1, ini)) { logmsg("disabled in UIScale.ini"); return TRUE; }
    wchar_t v[32]; GetPrivateProfileStringW(L"UIScale", L"Scale", L"auto", v, 32, ini);
    double x = _wtof(v);
    if (_wcsicmp(v, L"auto") && x > 0) {
        g_auto = 0; g_fixed = x >= 10 ? x / 100.0 : x;            // 150 = 150 %, 1.5 = 150 % too
        if (g_fixed < 0.5) g_fixed = 0.5; if (g_fixed > 4.0) g_fixed = 4.0;
    }
    int ok = install();
    if (!ok) return TRUE;
    if (g_auto) logmsg("UIScale 1: interface scale auto (screen height / 1080: 1080p 100 %%, 1440p 133 %%, 4K 200 %%) x the game's own UI scale");
    else logmsg("UIScale 1: interface scale %.0f %% of its own size", g_fixed * 100);
    return TRUE;
}
