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
//  - EliteMaps 2 (11.11.2026): NO Elite map in a PUBLIC room. A vanilla player without the add-on who joins a lobby whose map he
//    does not have crashes (DOW2.exe 0x00470581 reads the missing map's info), and anyone can walk into a public room from the
//    games list. The host's map choice (also the room's first map when it is created) goes through the game setup's map
//    handler 0x00487A7B(form, index) (map list: form+0x64..+0x68, 0x7AC bytes each, the scenario name at +0x280); in a public room
//    (GameInfo +0xC4 = 1; private 0) a pick of an Elite map is turned into the room's previous vanilla map (or the first vanilla
//    map with as many players) before the game's own handler runs. Private rooms (friends by invitation) keep Elite's maps.
//    The guard is installed before the archives are given to the game; if it cannot be (other DOW2.exe), no Elite map at all.
//  - EliteMaps 3 (11.11.2026, owner): Elite's maps in ANY room again, public ones too (players kick strangers or pick the Elite
//    map once everyone is in). The public-room guard of EliteMaps 2 is only an option now: EliteMaps.ini [EliteMaps]
//    BlockPublic=1 (default 0 - no hook at all).
// EliteMaps.ini [EliteMaps] Enabled=0: off. Log: EliteMaps.log next to the DLL.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define EM_VERSION "3"

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

// ---------------- EliteMaps 2: no Elite map in a public room ----------------
#define G_SESSION     0x01335014      // network manager; vtbl+0x24 -> match/lobby object
#define FN_INFO_CTOR  0x0040172D      // __stdcall(GameInfo*)
#define FN_INFO_DTOR  0x00401794
#define INFO_PUBLIC   0xC4            // GameInfo: 1 = public room (games list), 0 = private (stand 11.11.2026)
#define FN_MAPPICK    0x00487A7B      // game setup: map chosen, __stdcall(form, index)
#define FORM_MAPS     0x64            // form: vector of map entries (begin, end)
#define MAP_SIZE      0x7AC
#define MAP_NAME      0x280           // entry: scenario name (char[])
typedef void  (__stdcall *Fn1)(void *);
typedef void *(__fastcall *GetMatchFn)(void *self, void *edx);
typedef char  (__stdcall *GetInfoFn)(void *self, void *info);
typedef void  (__stdcall *MapPick_t)(void *form, int index);
static MapPick_t g_pick_orig;
static char (*g_elite)[64]; static int g_nelite;            // scenario names of the Elite map archives (lower case)
static void *g_lastForm; static int g_lastIdx = -1;         // the last vanilla map given to the handler

static int is_elite(const char *name) {
    for (int i = 0; i < g_nelite; i++) if (!_stricmp(g_elite[i], name)) return 1;
    return 0;
}
static int room_public(void) {
    void **mgr = *(void ***)G_SESSION; if (!mgr) return -1;
    void *m = ((GetMatchFn)(*(void ***)mgr)[0x24 / 4])(mgr, NULL); if (!m) return -1;
    static __attribute__((aligned(16))) unsigned char info[0x2000];
    memset(info, 0, sizeof info); ((Fn1)FN_INFO_CTOR)(info);
    int r = -1;
    if (((GetInfoFn)(*(void ***)m)[1])(m, info)) r = *(int *)(info + INFO_PUBLIC) != 0;
    ((Fn1)FN_INFO_DTOR)(info);
    return r;
}
static void __stdcall hk_pick(void *form, int idx) {
    unsigned char *b = *(unsigned char **)((char *)form + FORM_MAPS), *e = *(unsigned char **)((char *)form + FORM_MAPS + 4);
    int n = (b && e > b) ? (int)((e - b) / MAP_SIZE) : 0;
    if (idx >= 0 && idx < n) {
        const char *nm = (const char *)(b + idx * MAP_SIZE + MAP_NAME);
        if (is_elite(nm)) {
            int pub = room_public();
            if (pub != 0) {   // public, or not known: never an Elite map
                int to = (form == g_lastForm && g_lastIdx >= 0 && g_lastIdx < n && !is_elite((const char *)(b + g_lastIdx * MAP_SIZE + MAP_NAME))) ? g_lastIdx : -1;
                unsigned want = *(unsigned *)(b + idx * MAP_SIZE);
                for (int k = 0; to < 0 && k < n; k++) if (!is_elite((const char *)(b + k * MAP_SIZE + MAP_NAME)) && *(unsigned *)(b + k * MAP_SIZE) == want) to = k;
                for (int k = 0; to < 0 && k < n; k++) if (!is_elite((const char *)(b + k * MAP_SIZE + MAP_NAME))) to = k;
                logmsg("%s room: Elite map %s not allowed -> %s", pub > 0 ? "public" : "unknown", nm, to >= 0 ? (const char *)(b + to * MAP_SIZE + MAP_NAME) : "(none)");
                if (to < 0) return;
                idx = to;
            } else logmsg("private room: Elite map %s", nm);
        }
        if (!is_elite((const char *)(b + idx * MAP_SIZE + MAP_NAME))) { g_lastForm = form; g_lastIdx = idx; }
    }
    g_pick_orig(form, idx);
}
// the Elite archives' scenario names, from the archive's table (SGA v5: the table is the file's last <0xAC> bytes)
static void read_names(const wchar_t *path) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); if (h == INVALID_HANDLE_VALUE) return;
    unsigned char hd[0xB0]; DWORD rd; LARGE_INTEGER sz; unsigned char *t = NULL;
    if (!ReadFile(h, hd, sizeof hd, &rd, NULL) || rd != sizeof hd || memcmp(hd, "_ARCHIVE", 8) || *(DWORD *)(hd + 8) != 5 || !GetFileSizeEx(h, &sz)) goto out;
    DWORD hs = *(DWORD *)(hd + 0xAC); if (!hs || hs > 64u << 20 || hs > sz.QuadPart) goto out;
    LARGE_INTEGER at; at.QuadPart = sz.QuadPart - hs; t = malloc(hs);
    if (!t || !SetFilePointerEx(h, at, NULL, FILE_BEGIN) || !ReadFile(h, t, hs, &rd, NULL) || rd != hs) goto out;
    DWORD dirOff = *(DWORD *)(t + 6), fileOff = *(DWORD *)(t + 12), strOff = *(DWORD *)(t + 18);
    WORD dirCnt = *(WORD *)(t + 10);
    for (int i = 0; i < dirCnt; i++) {
        unsigned char *d = t + dirOff + i * 12; DWORD dn = *(DWORD *)d; WORD fs = *(WORD *)(d + 8), fe = *(WORD *)(d + 10);
        if (strOff + dn >= hs) continue;
        const char *dname = (const char *)t + strOff + dn;
        if (_strnicmp(dname, "maps\\pvp", 8) && _strnicmp(dname, "maps/pvp", 8) && _strnicmp(dname, "maps\\freeforall", 14) && _strnicmp(dname, "maps/freeforall", 14)) continue;
        for (int j = fs; j < fe; j++) {
            unsigned char *f = t + fileOff + j * 22; DWORD fn = *(DWORD *)f; if (strOff + fn >= hs) continue;
            const char *nm = (const char *)t + strOff + fn; const char *dot = strrchr(nm, '.');
            if (!dot || _stricmp(dot, ".scenario") || dot - nm >= 63) continue;
            char (*nn)[64] = realloc(g_elite, (g_nelite + 1) * 64); if (!nn) break; g_elite = nn;
            memcpy(g_elite[g_nelite], nm, dot - nm); g_elite[g_nelite][dot - nm] = 0; g_nelite++;
        }
    }
out:
    free(t); CloseHandle(h);
}
static int install_guard(void) {
    static const unsigned char want[10] = {0xB8, 0xEB, 0x59, 0xF1, 0x00, 0xE8, 0x4F, 0x34, 0x92, 0x00};   // mov eax,0xF159EB; call 0xDAAED4
    unsigned char *f = (unsigned char *)FN_MAPPICK;
    if (IsBadReadPtr(f, 10) || memcmp(f, want, 10) || IsBadReadPtr((void *)G_SESSION, 4)) return 0;
    unsigned char *tr = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); if (!tr) return 0;
    memcpy(tr, f, 5);                                                       // mov eax, 0xF159EB
    tr[5] = 0xE8; *(DWORD *)(tr + 6) = 0x00DAAED4 - (DWORD)(tr + 10);       // call 0xDAAED4
    tr[10] = 0xE9; *(DWORD *)(tr + 11) = (DWORD)(f + 10) - (DWORD)(tr + 15); // jmp back
    g_pick_orig = (MapPick_t)tr;
    DWORD old; if (!VirtualProtect(f, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    f[0] = 0xE9; *(DWORD *)(f + 1) = (DWORD)hk_pick - (DWORD)(f + 5);
    VirtualProtect(f, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 5);
    return 1;
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
        read_names(p);
    }
    if (!g_narch) { logmsg("no Elite map archive in this game folder (is Elite installed by the launcher?): nothing"); return; }
    if (GetPrivateProfileIntW(L"EliteMaps", L"BlockPublic", 0, ini)) {     // EliteMaps 3: the guard only on request
        if (!g_nelite) { logmsg("BlockPublic=1 but no map names read from the archives: no Elite maps"); return; }
        if (!install_guard()) { logmsg("BlockPublic=1 but the public-room guard is NOT installed (not DOW2.exe 3.19?): no Elite maps"); return; }
        logmsg("BlockPublic=1: %d Elite map names; no Elite map in a public room (guard 0x%08X)", g_nelite, FN_MAPPICK);
    } else logmsg("Elite maps in every room, public ones too (EliteMaps.ini BlockPublic=1 keeps them out of public rooms)");
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
