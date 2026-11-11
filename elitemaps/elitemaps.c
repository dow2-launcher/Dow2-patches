// EliteMaps.dll - Elite's maps in VANILLA Dawn of War II: Retribution (experiment, 11.11.2026).
//
// What it does (nothing else):
//  - Vanilla only (no -modname on the command line; under Elite it does nothing). It is loaded by the launcher's vanilla
//    loader (winmm.dll, dow2-patches).
//  - The game reads its .module (DOW2.module / DOW2Multiplayer.module, vanilla) with Util.dll's ConfigFile and asks for
//    archive.01, archive.02, ... of [data:common] until one is missing (DOW2.exe 0x008B9EB0). Only DOW2.exe's import slot of
//    ConfigFile::GetString is pointed at our function: when a key archive.NN of [data:common] is missing, the next of Elite's
//    map archives is answered, after the game's own ones (= the lowest priority: no vanilla file is replaced).
//  - The archives are the player's own Elite installation in the same game folder (the launcher keeps it up to date):
//    Elite\Archives\Elite_Data_21_Maps_01.sga, Elite_Data_22_Maps_02.sga (maps/pvp, maps/freeforall: only new map names)
//    and Elite_Data_20_UI_Maps.sga (their loading screens). A missing one is skipped; none - nothing changes. Nothing is
//    written to the game's files: untick the add-on and the next start is plain vanilla.
//  - Maps only: no attribute (game data) archive, so the game's data checksum stays vanilla's.
//  - It has to be in before the game mounts its archives (after Steam's start): the vanilla loader loads dow2-patches ~50-90 ms
//    after the start. Loaded too late: nothing, a log line.
// EliteMaps.ini [EliteMaps] Enabled=0: off. Log: EliteMaps.log next to the DLL.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define EM_VERSION "1"

typedef BOOL(__fastcall *GetString_t)(void *self, void *edx, const char *key, char *buf, unsigned size);
typedef const char *(__fastcall *GetSection_t)(void *self, void *edx);
static wchar_t g_dir[MAX_PATH], g_log[MAX_PATH];
static GetString_t g_orig;
static GetSection_t g_section;
static char g_arch[4][128];
static int g_narch;
static void *g_last_self; static int g_base;   // the first missing archive.NN of the [data:common] being read
static volatile LONG g_done;

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
// address of the import slot <dll>!<fn> in module m
static void **find_slot(HMODULE m, const char *dll, const char *fn) {
    BYTE *b = (BYTE *)m; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd->VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(b + dd->VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)(b + d->Name), dll) || !d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *o = (IMAGE_THUNK_DATA *)(b + d->OriginalFirstThunk), *f = (IMAGE_THUNK_DATA *)(b + d->FirstThunk);
        for (; o->u1.AddressOfData; o++, f++)
            if (!(o->u1.Ordinal & IMAGE_ORDINAL_FLAG) && !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(b + o->u1.AddressOfData))->Name, fn))
                return (void **)&f->u1.Function;
    }
    return NULL;
}
static BOOL __fastcall hk_GetString(void *self, void *edx, const char *key, char *buf, unsigned size) {
    BOOL r = g_orig(self, edx, key, buf, size);
    if (r || !key || _strnicmp(key, "archive.", 8) || !buf || size < 2) return r;
    const char *sec = g_section ? g_section(self, NULL) : NULL;
    if (!sec || _stricmp(sec, "data:common")) return r;
    int n = atoi(key + 8);
    if (self != g_last_self || n < g_base) { g_last_self = self; g_base = n; }
    int i = n - g_base;
    if (i < 0 || i >= g_narch) return r;
    lstrcpynA(buf, g_arch[i], size);
    logmsg("%s = %s (after the game's %d archives)", key, buf, g_base - 1);
    InterlockedExchange(&g_done, 1);
    return TRUE;
}
static int started_as_mod(void) {
    const wchar_t *c = GetCommandLineW();
    return c && (wcsstr(c, L"-modname") || wcsstr(c, L"/modname") || wcsstr(c, L"-MODNAME"));
}
static DWORD ms_since_start(void) {
    FILETIME c, e, k, u, now; if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    GetSystemTimeAsFileTime(&now);
    ULONGLONG a = ((ULONGLONG)c.dwHighDateTime << 32) | c.dwLowDateTime, b = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return b > a ? (DWORD)((b - a) / 10000) : 0;
}
static void start(HMODULE self) {
    GetModuleFileNameW(self, g_dir, MAX_PATH);
    wchar_t *sl = wcsrchr(g_dir, L'\\'); if (!sl) return; sl[1] = 0;
    _snwprintf(g_log, MAX_PATH, L"%lsEliteMaps.log", g_dir); g_log[MAX_PATH - 1] = 0;
    DeleteFileW(g_log);
    logmsg("EliteMaps %s, %lu ms after the game's start", EM_VERSION, ms_since_start());
    wchar_t ini[MAX_PATH]; _snwprintf(ini, MAX_PATH, L"%lsEliteMaps.ini", g_dir); ini[MAX_PATH - 1] = 0;
    if (!GetPrivateProfileIntW(L"EliteMaps", L"Enabled", 1, ini)) { logmsg("off (EliteMaps.ini Enabled=0)"); return; }
    if (started_as_mod()) { logmsg("a mod start (-modname): nothing - vanilla only"); return; }
    wchar_t game[MAX_PATH]; GetModuleFileNameW(NULL, game, MAX_PATH);
    sl = wcsrchr(game, L'\\'); if (!sl) return;
    if (_wcsicmp(sl + 1, L"DOW2.exe")) { logmsg("not DOW2.exe: nothing"); return; }
    sl[1] = 0;
    static const char *CAND[] = {"Elite\\Archives\\Elite_Data_21_Maps_01.sga", "Elite\\Archives\\Elite_Data_22_Maps_02.sga",
                                 "Elite\\Archives\\Elite_Data_20_UI_Maps.sga"};
    for (int i = 0; i < 3; i++) {
        wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%ls%hs", game, CAND[i]); p[MAX_PATH - 1] = 0;
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(p, GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            logmsg("not found: %s - skipped", CAND[i]); continue;
        }
        strcpy(g_arch[g_narch++], CAND[i]);
        logmsg("found %s (%lu bytes)", CAND[i], fa.nFileSizeLow);
    }
    if (!g_narch) { logmsg("no Elite map archive in this game folder (is Elite installed by the launcher?): nothing"); return; }
    HMODULE util = GetModuleHandleW(L"Util.dll");
    void **slot = find_slot(GetModuleHandleW(NULL), "Util.dll", "?GetString@ConfigFile@@QBE_NPBDPADI@Z");
    g_section = util ? (GetSection_t)GetProcAddress(util, "?GetSection@ConfigFile@@QBEPBDXZ") : NULL;
    if (!slot || !g_section) { logmsg("ConfigFile::GetString / GetSection not found: nothing"); return; }
    DWORD old;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) { logmsg("VirtualProtect failed: nothing"); return; }
    g_orig = (GetString_t)*slot; *slot = (void *)hk_GetString;
    VirtualProtect(slot, sizeof *slot, old, &old);
    logmsg("hooked ConfigFile::GetString (%lu ms after the start); %d archive(s) go after the game's own", ms_since_start(), g_narch);
}
static DWORD WINAPI late_check(LPVOID _) {
    Sleep(60000);
    if (g_orig && !g_done) logmsg("60 s: the game has not asked for its archives through us - loaded too late? (no Elite maps this start)");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    if (GetModuleHandleW(L"EliteMaps.dll") != h && GetModuleHandleW(L"EliteMaps.dll")) return TRUE;
    start(h);
    if (g_orig) CloseHandle(CreateThread(NULL, 0, late_check, NULL, 0, NULL));
    return TRUE;
}
