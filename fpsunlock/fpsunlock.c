// FpsUnlock.dll - no DXVK frame cap (09.11.2026). The game has no limiter of its own; the cap players see comes from DXVK
// (Elite's DXVK mode writes dxvk.conf with d3d9/dxgi.maxFrameRate = 60). DXVK reads the DXVK_CONFIG variable when the game
// creates Direct3D (Direct3DCreate9), after dxvk.conf, so its lines win. FpsUnlock appends
// "d3d9.maxFrameRate = 0; dxgi.maxFrameRate = 0" there - before Direct3D is created.
//  - Appends, never overwrites: Sound bug fix (SoundFix FpsCap) writes the same keys; if they are there already, they stay
//    (SoundFix's number wins in either load order: loaded after us it appends its own after ours).
//  - Vertical sync stays as set in the game's options (no Direct3D hooks at all).
//  - Without DXVK there is no cap to lift (log line). Loaded too late (Elite without Automatch: no early loader): log line.
// Rendering only - nothing of the simulation is touched, no sync impact.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define FPSUNLOCK_VERSION "1"
#define LATE_MS 5000

static wchar_t g_log[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static int is_dxvk(HMODULE m) {   // DXVK's d3d9.dll has no export of its own; it carries the name of its config variable
    if (!m) return 0;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)m + ((IMAGE_DOS_HEADER *)m)->e_lfanew);
    IMAGE_SECTION_HEADER *sh = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sh++) {
        if (sh->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        BYTE *p = (BYTE *)m + sh->VirtualAddress; DWORD n = sh->Misc.VirtualSize;
        for (DWORD k = 0; k + 11 <= n; k++) if (p[k] == 'D' && !memcmp(p + k, "DXVK_CONFIG", 11)) return 1;
    }
    return 0;
}
static DWORD uptime_ms(void) {   // since the game process started
    FILETIME c, x, k, u, now; GetSystemTimeAsFileTime(&now);
    if (!GetProcessTimes(GetCurrentProcess(), &c, &x, &k, &u)) return 0;
    ULONGLONG a = ((ULONGLONG)c.dwHighDateTime << 32) | c.dwLowDateTime, b = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return b > a ? (DWORD)((b - a) / 10000) : 0;
}
static DWORD WINAPI report(LPVOID _) {   // which Direct3D the game got (after it started)
    Sleep(60000);
    HMODULE ours = GetModuleHandleW(L"DXVK_d3d9.bin"), sys = GetModuleHandleA("d3d9.dll");
    if (ours) logmsg("Direct3D: DXVK of the DXVK add-on - maxFrameRate from DXVK_CONFIG applies if FpsUnlock was loaded before Direct3D");
    else if (is_dxvk(sys)) logmsg("Direct3D: the game's d3d9.dll is DXVK (Elite's DXVK mode) - its dxvk.conf cap is overridden by DXVK_CONFIG if FpsUnlock was loaded before Direct3D");
    else logmsg("Direct3D: no DXVK - there is no frame cap to lift (the game has none; vsync as set in the options)");
    return 0;
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
    wcscat(g_log, L"FpsUnlock.log");
    if (already_loaded("FpsUnlock")) { logmsg("another FpsUnlock copy is already active in this game, this one stays idle"); return TRUE; }
    DWORD up = uptime_ms();
    char old[2048], v[2400]; DWORD n = GetEnvironmentVariableA("DXVK_CONFIG", old, sizeof old); if (n >= sizeof old) n = 0; old[n] = 0;
    if (strstr(old, "maxFrameRate")) {
        logmsg("FpsUnlock %s: DXVK_CONFIG already sets the frame rate (\"%s\", Sound bug fix FpsCap) - kept as it is", FPSUNLOCK_VERSION, old);
    } else {
        snprintf(v, sizeof v, "%s%sd3d9.maxFrameRate = 0; dxgi.maxFrameRate = 0", old, n ? "; " : "");
        SetEnvironmentVariableA("DXVK_CONFIG", v);
        logmsg("FpsUnlock %s: DXVK_CONFIG = \"%s\" (%.2f s after start; vsync untouched)", FPSUNLOCK_VERSION, v, up / 1000.0);
    }
    if (up > LATE_MS)
        logmsg("loaded %.1f s after the game started - too late, Direct3D is created already: the cap is NOT lifted (in Elite tick Automatch: it loads FpsUnlock early)", up / 1000.0);
    CloseHandle(CreateThread(NULL, 0, report, NULL, 0, NULL));
    return TRUE;
}
