// DocsFix.dll - "Documents fix": the game keeps its settings and replays when it may not write to Documents (12.11.2026).
//
// Case: Malum (12.11.2026) - the volume reset after every start, no replay since 30.08: configuration.lua and
// Playback\temp.rec of 30.08 in Documents\My Games\Dawn of War II - Retribution, the game kept READING that folder, our add-on
// logs in the game folder were written fine. common/docscheck.h checks it from the game process at the start and after
// every match (write test, GetLastError, attributes, Controlled folder access); this add-on also acts on it.
//
// How the game gets its folder (Platform.dll Plat::File::Setup): SHGetFolderPathA(CSIDL_PERSONAL) + "\My Games\Dawn of War II
// - Retribution", kept in Platform.dll's buffer that Plat::File::GetPathUserStorage returns; DOW2.exe takes it through its
// import of that function and builds its Settings\ Playback\ Logfiles\ ... aliases from it early at the start.
//
// What this add-on does, at the game start (it has to be loaded early: vanilla - the launcher's loader; Elite - Automatch's
// early winmm.dll, which loads DocsFix.dll first; loaded later = only the check, a log line, nothing redirected):
//  1. DOW2.exe's import of GetPathUserStorage and Platform.dll's import of SHGetFolderPathA go through a gate that holds the
//     game until step 2 has decided (at most 20 s) - so the game never sees half a decision.
//  2. Decision: the redirect was made before (marker DocsFix.txt in the new place: it stays - the newer settings are there), or
//     the write test from the game process fails in Settings or in Playback -> redirect; otherwise nothing changes at all.
//  3. Redirect: the new "Documents" of the game is %LOCALAPPDATA%\EliteLauncher\Documents (not a protected folder of
//     Controlled folder access, the user's own, no admin rights); the first time the game's folder is copied there (never
//     overwriting; Settings and the rest before the game goes on, Playback and Screenshots right after). The game folder is
//     then <new>\My Games\Dawn of War II - Retribution: Platform.dll's buffer is set to it (when Setup ran already) and the
//     game's crash folder too (dbCrash::SetWorkingFolder), SHGetFolderPathA/W(CSIDL_PERSONAL) answer <new> to the modules of
//     the game folder (the game's DLLs, our add-ons: EliteAI / MMFind / LiveReplay find temp.rec there) - shell32's export
//     and the imports of the game folder's modules loaded already; any other module gets Windows' answer as before.
//     The launcher (1.5.9) reads DocsFix.redirect next to this DLL: hotkeys, UI scale and "Send logs" use the new folder.
//  No system setting is changed (no registry, no Windows Security, no folder permissions); untick = the game is back on
//  Documents (with the files as they were when the redirect began; the newer ones stay in the new place).
// DocsFix.ini [DocsFix] Mode=auto (default) | always (redirect even when Documents is writable: tests) | never (only the check).
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../common/docscheck.h"

#define DF_VERSION "1"
#define GATE_MS 20000
#define GPUS_NAME "?GetPathUserStorage@File@Plat@@YGPBDXZ"

typedef HRESULT (WINAPI *ShA)(HWND, int, HANDLE, DWORD, LPSTR);
typedef HRESULT (WINAPI *ShW)(HWND, int, HANDLE, DWORD, LPWSTR);
typedef const char *(__stdcall *GpusFn)(void);
static wchar_t g_dir[MAX_PATH], g_log[MAX_PATH], g_ini[MAX_PATH], g_game[MAX_PATH];   // add-on folder, log, ini, game folder (with \)
static CRITICAL_SECTION g_lk;
static ShA g_origA; static ShW g_origW; static GpusFn g_gpus;
static HANDLE g_ready;                                   // set when the decision is made (and the first copy is done)
static volatile LONG g_redirect;                          // 1: the game's Documents is g_newW
static wchar_t g_newW[MAX_PATH], g_userW[MAX_PATH];      // the new Documents, the new game folder
static char g_newA[MAX_PATH], g_userA[MAX_PATH];         // the same, ANSI (the game's own calls)
static volatile LONG g_callsGpus, g_callsA, g_callsW;
static DWORD g_t0;

static void logmsg(const char *fmt, ...) {
    EnterCriticalSection(&g_lk);
    FILE *f = _wfopen(g_log, L"a");
    if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
             va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_lk);
}
static DWORD uptime_ms(void) {                            // since the game process started
    FILETIME c, x, k, u, now; GetSystemTimeAsFileTime(&now);
    if (!GetProcessTimes(GetCurrentProcess(), &c, &x, &k, &u)) return 0;
    ULONGLONG a = ((ULONGLONG)c.dwHighDateTime << 32) | c.dwLowDateTime, b = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return b > a ? (DWORD)((b - a) / 10000) : 0;
}
// address of the import slot <dll>!<fn> in module m (by name)
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
static int set_slot(void **slot, void *v) {
    DWORD old; if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) return 0;
    InterlockedExchangePointer(slot, v); VirtualProtect(slot, sizeof *slot, old, &old); return 1;
}
// shell32's export <name> -> hook (modules loaded later and GetProcAddress get the hook); 32-bit RVAs wrap around
static int eat_patch(HMODULE m, const char *name, void *hook) {
    BYTE *b = (BYTE *)m; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dd->VirtualAddress) return 0;
    IMAGE_EXPORT_DIRECTORY *ed = (IMAGE_EXPORT_DIRECTORY *)(b + dd->VirtualAddress);
    DWORD *fn = (DWORD *)(b + ed->AddressOfFunctions), *nm = (DWORD *)(b + ed->AddressOfNames); WORD *od = (WORD *)(b + ed->AddressOfNameOrdinals);
    for (DWORD i = 0; i < ed->NumberOfNames; i++) {
        if (strcmp((char *)(b + nm[i]), name)) continue;
        DWORD *slot = &fn[od[i]], v = (DWORD)(UINT_PTR)hook - (DWORD)(UINT_PTR)b, old;
        if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) return 0;
        InterlockedExchange((LONG *)slot, (LONG)v); VirtualProtect(slot, 4, old, &old); return 1;
    }
    return 0;
}
// a caller inside a module of the game folder (DOW2.exe, the game's DLLs, Elite_DLLs, dow2-patches)
static int from_game(void *addr) {
    HMODULE m = NULL; wchar_t p[MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &m) || !m) return 0;
    if (!GetModuleFileNameW(m, p, MAX_PATH)) return 0;
    size_t n = wcslen(g_game); return n && !_wcsnicmp(p, g_game, n);
}
static void wait_ready(void) { if (g_ready) WaitForSingleObject(g_ready, GATE_MS); }

// ---- the hooks ----
static const char *__stdcall hk_gpus(void) {              // DOW2.exe -> Plat::File::GetPathUserStorage
    wait_ready();
    const char *r = g_gpus();
    if (InterlockedIncrement(&g_callsGpus) <= 3) logmsg("the game asks for its folder (%.1f s after the start): %s", uptime_ms() / 1000.0, r ? r : "(null)");
    return r;
}
static HRESULT WINAPI hk_shA(HWND h, int csidl, HANDLE tok, DWORD fl, LPSTR out) {
    void *ra = __builtin_return_address(0);
    if ((csidl & 0xFF) == CSIDL_PERSONAL && !tok && out && from_game(ra)) {
        wait_ready();
        if (g_redirect) {
            lstrcpynA(out, g_newA, MAX_PATH);
            if (InterlockedIncrement(&g_callsA) <= 5) logmsg("SHGetFolderPathA(Documents) from the game -> %s (%.1f s after the start)", out, uptime_ms() / 1000.0);
            return S_OK;
        }
    }
    return g_origA(h, csidl, tok, fl, out);
}
static HRESULT WINAPI hk_shW(HWND h, int csidl, HANDLE tok, DWORD fl, LPWSTR out) {
    void *ra = __builtin_return_address(0);
    if ((csidl & 0xFF) == CSIDL_PERSONAL && !tok && out && from_game(ra)) {
        wait_ready();
        if (g_redirect) {
            lstrcpynW(out, g_newW, MAX_PATH);
            if (InterlockedIncrement(&g_callsW) <= 5) { char u[MAX_PATH * 3]; HMODULE m = NULL; char mn[MAX_PATH] = "?";
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)ra, &m);
                if (m) GetModuleFileNameA(m, mn, MAX_PATH); const char *b = strrchr(mn, '\\');
                logmsg("SHGetFolderPathW(Documents) from %s -> %s", b ? b + 1 : mn, dc_u8(out, u, sizeof u)); }
            return S_OK;
        }
    }
    return g_origW(h, csidl, tok, fl, out);
}

// ---- the new place ----
static int ansi_ok(const wchar_t *w, char *a) {           // the game opens its files the ANSI way
    BOOL lossy = FALSE;
    return WideCharToMultiByte(CP_ACP, 0, w, -1, a, MAX_PATH, NULL, &lossy) && !lossy;
}
static int mkdirs(const wchar_t *p) {                     // every missing folder of p
    wchar_t t[MAX_PATH]; lstrcpynW(t, p, MAX_PATH);
    for (wchar_t *c = t + 3; *c; c++) if (*c == L'\\') { *c = 0; CreateDirectoryW(t, NULL); *c = L'\\'; }
    return CreateDirectoryW(t, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}
// %LOCALAPPDATA%\EliteLauncher\Documents, made and write-tested; its short (8.3) name when the long one is not ANSI-safe
static int pick_new(void) {
    wchar_t la[MAX_PATH]; if (g_origW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, la) != S_OK) { logmsg("no %%LOCALAPPDATA%% - nothing redirected"); return 0; }
    _snwprintf(g_newW, MAX_PATH, L"%ls\\EliteLauncher\\Documents", la); g_newW[MAX_PATH - 1] = 0;
    _snwprintf(g_userW, MAX_PATH, L"%ls\\My Games\\" DC_GAME_NAME, g_newW); g_userW[MAX_PATH - 1] = 0;
    if (!mkdirs(g_userW)) { char u[MAX_PATH * 3]; logmsg("cannot make %s (error %lu) - nothing redirected", dc_u8(g_userW, u, sizeof u), GetLastError()); return 0; }
    const char *stage; DWORD e = dc_probe_write(g_userW, &stage);
    if (e) { char u[MAX_PATH * 3]; logmsg("the new place %s is not writable either (%s: error %lu %s) - nothing redirected", dc_u8(g_userW, u, sizeof u), stage, e, dc_errname(e)); return 0; }
    if (!ansi_ok(g_newW, g_newA)) {
        wchar_t s[MAX_PATH];
        if (!GetShortPathNameW(g_newW, s, MAX_PATH) || !ansi_ok(s, g_newA)) { logmsg("the new place has letters outside the ANSI code page and no short name - nothing redirected"); return 0; }
        lstrcpynW(g_newW, s, MAX_PATH); _snwprintf(g_userW, MAX_PATH, L"%ls\\My Games\\" DC_GAME_NAME, g_newW); g_userW[MAX_PATH - 1] = 0;
    }
    snprintf(g_userA, MAX_PATH, "%s\\My Games\\Dawn of War II - Retribution", g_newA);
    return 1;
}
typedef struct { int files, skipped, failed; ULONGLONG bytes; DWORD lastErr; } CopyStat;
static void copy_tree(const wchar_t *from, const wchar_t *to, CopyStat *s, int depth) {
    wchar_t q[MAX_PATH]; WIN32_FIND_DATAW fd; _snwprintf(q, MAX_PATH, L"%ls\\*", from); q[MAX_PATH - 1] = 0;
    HANDLE h = FindFirstFileW(q, &fd); if (h == INVALID_HANDLE_VALUE) return;
    CreateDirectoryW(to, NULL);
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t a[MAX_PATH], b[MAX_PATH];
        _snwprintf(a, MAX_PATH, L"%ls\\%ls", from, fd.cFileName); _snwprintf(b, MAX_PATH, L"%ls\\%ls", to, fd.cFileName);
        a[MAX_PATH - 1] = b[MAX_PATH - 1] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth < 6 && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) copy_tree(a, b, s, depth + 1); continue; }
        if (GetFileAttributesW(b) != INVALID_FILE_ATTRIBUTES) { s->skipped++; continue; }    // never overwrite
        if (CopyFileW(a, b, TRUE)) { s->files++; s->bytes += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            SetFileAttributesW(b, GetFileAttributesW(b) & ~FILE_ATTRIBUTE_READONLY); }
        else { s->failed++; s->lastErr = GetLastError(); }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
// the first copy: phase 0 = everything but Playback / Screenshots / Logfiles (before the game goes on), 1 = Playback, Screenshots
static void copy_phase(const wchar_t *real, int phase) {
    wchar_t q[MAX_PATH]; WIN32_FIND_DATAW fd; _snwprintf(q, MAX_PATH, L"%ls\\*", real); q[MAX_PATH - 1] = 0;
    CopyStat s = {0}; DWORD t0 = GetTickCount();
    HANDLE h = FindFirstFileW(q, &fd); if (h == INVALID_HANDLE_VALUE) { logmsg("copy: nothing in the old folder (error %lu)", GetLastError()); return; }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        int dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int late = dir && (!_wcsicmp(fd.cFileName, L"Playback") || !_wcsicmp(fd.cFileName, L"Screenshots"));
        if (dir && !_wcsicmp(fd.cFileName, L"Logfiles")) continue;           // the game's logs of earlier runs: not needed
        if (late != phase) continue;
        wchar_t a[MAX_PATH], b[MAX_PATH];
        _snwprintf(a, MAX_PATH, L"%ls\\%ls", real, fd.cFileName); _snwprintf(b, MAX_PATH, L"%ls\\%ls", g_userW, fd.cFileName);
        a[MAX_PATH - 1] = b[MAX_PATH - 1] = 0;
        if (dir) copy_tree(a, b, &s, 0);
        else if (GetFileAttributesW(b) != INVALID_FILE_ATTRIBUTES) s.skipped++;
        else if (CopyFileW(a, b, TRUE)) { s.files++; s.bytes += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; }
        else { s.failed++; s.lastErr = GetLastError(); }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    logmsg("copy %s: %d files (%llu KB) copied, %d already there (kept), %d failed%s in %lu ms", phase ? "Playback + Screenshots" : "Settings and the rest",
           s.files, s.bytes / 1024, s.skipped, s.failed, s.failed ? (s.lastErr == 5 ? " (last: access denied)" : " (see the error codes)") : "", GetTickCount() - t0);
}
static void marker_paths(wchar_t *inNew, wchar_t *forLauncher) {
    _snwprintf(inNew, MAX_PATH, L"%ls\\DocsFix.txt", g_newW); inNew[MAX_PATH - 1] = 0;
    _snwprintf(forLauncher, MAX_PATH, L"%lsDocsFix.redirect", g_dir); forLauncher[MAX_PATH - 1] = 0;
}
static void write_utf8(const wchar_t *p, const char *fmt, ...) {
    char b[2048]; va_list a; va_start(a, fmt); int n = vsnprintf(b, sizeof b, fmt, a); va_end(a);
    if (n < 0) return; if (n >= (int)sizeof b) n = sizeof b - 1;
    HANDLE f = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, b, n, &w, NULL); CloseHandle(f); }
}
static void patch_loaded_imports(void) {                  // the game folder's modules loaded already: their shell32 imports
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId()); if (sn == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32W me; me.dwSize = sizeof me; int n = 0; HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)patch_loaded_imports, &self);
    for (BOOL ok = Module32FirstW(sn, &me); ok; ok = Module32NextW(sn, &me)) {
        if (me.hModule == self || _wcsnicmp(me.szExePath, g_game, wcslen(g_game))) continue;
        void **s;
        if ((s = find_slot(me.hModule, "shell32.dll", "SHGetFolderPathA")) && *s != (void *)hk_shA && set_slot(s, (void *)hk_shA)) n++;
        if ((s = find_slot(me.hModule, "shell32.dll", "SHGetFolderPathW")) && *s != (void *)hk_shW && set_slot(s, (void *)hk_shW)) n++;
    }
    CloseHandle(sn);
    if (n) logmsg("imports of SHGetFolderPath in the game folder's loaded modules redirected: %d", n);
}

// ---- the start: decide (the game waits at the gate), then the checks and the match watch ----
static DWORD WINAPI worker(LPVOID late_) {
    int late = (int)(INT_PTR)late_;
    wchar_t docs[MAX_PATH] = L"", real[MAX_PATH] = L"";
    if (g_origW(NULL, CSIDL_PERSONAL, NULL, 0, docs) == S_OK) { _snwprintf(real, MAX_PATH, L"%ls\\My Games\\" DC_GAME_NAME, docs); real[MAX_PATH - 1] = 0; }
    int plat = 0; wchar_t gameNow[MAX_PATH] = L""; dc_game_folder(gameNow, g_origW, &plat);
    wchar_t mode[16]; GetPrivateProfileStringW(L"DocsFix", L"Mode", L"auto", mode, 16, g_ini);
    int never = !_wcsicmp(mode, L"never"), always = !_wcsicmp(mode, L"always");
    int owner = dc_owner();
    DcResult r; memset(&r, 0, sizeof r);
    char u[MAX_PATH * 3];
    if (real[0]) {
        // the real folder may be missing (a new install): the game would make it - test its parent then
        if (GetFileAttributesW(real) == INVALID_FILE_ATTRIBUTES) mkdirs(real);
        wchar_t st[MAX_PATH], pb[MAX_PATH]; _snwprintf(st, MAX_PATH, L"%ls\\Settings", real); _snwprintf(pb, MAX_PATH, L"%ls\\Playback", real);
        CreateDirectoryW(st, NULL); CreateDirectoryW(pb, NULL);
        dc_check(logmsg, "start", real, docs, "the game's", plat && !_wcsicmp(gameNow, real), &r);
    } else logmsg("Windows gives no Documents folder (SHGetFolderPathW failed)");
    int denied = !real[0] || r.settings || r.playback;
    // made before? the newer settings are there: stay
    int before = 0;
    { wchar_t la[MAX_PATH], m[MAX_PATH];
      if (g_origW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, la) == S_OK) { _snwprintf(m, MAX_PATH, L"%ls\\EliteLauncher\\Documents\\DocsFix.txt", la); before = GetFileAttributesW(m) != INVALID_FILE_ATTRIBUTES; } }
    const char *why = never ? NULL : always ? "Mode=always" : before ? "redirected at an earlier start (the newer files are there)" : denied ? "the game may not write to its folder in Documents" : NULL;
    wchar_t mk[MAX_PATH], ml[MAX_PATH];
    if (why && late) {
        logmsg("NOT redirected although %s: DocsFix.dll was loaded too late (%.1f s after the start, the game had set its folder) - "
               "it has to be loaded at the start: Elite - Automatch's early winmm.dll (tick Automatch, its version with DocsFix); vanilla - the launcher's loader.",
               why, g_t0 / 1000.0);
        why = NULL;
    }
    if (why && pick_new()) {
        marker_paths(mk, ml);
        int first = GetFileAttributesW(mk) == INVALID_FILE_ATTRIBUTES;
        if (first && real[0]) copy_phase(real, 0);
        // the game's buffer, when Setup ran already (else Setup asks SHGetFolderPathA, which answers the new place)
        char *buf = g_gpus ? (char *)g_gpus() : NULL;
        if (buf && buf[0]) { lstrcpynA(buf, g_userA, MAX_PATH); logmsg("Platform.dll's folder set to %s (Setup had run already)", buf); }
        HMODULE dbg = GetModuleHandleA("Debug.dll");
        void (__stdcall *swf)(const char *) = dbg ? (void (__stdcall *)(const char *))GetProcAddress(dbg, "?SetWorkingFolder@dbCrash@@YGXPBD@Z") : NULL;
        if (swf && buf && buf[0]) swf(g_userA);
        HMODULE sh = GetModuleHandleA("shell32.dll");
        int ea = sh && eat_patch(sh, "SHGetFolderPathA", (void *)hk_shA), ew = sh && eat_patch(sh, "SHGetFolderPathW", (void *)hk_shW);
        InterlockedExchange(&g_redirect, 1);
        patch_loaded_imports();
        if (first) {
            SYSTEMTIME t; GetLocalTime(&t);
            write_utf8(mk, "DocsFix: the game's Documents since %04d-%02d-%02d %02d:%02d (%s).\r\nOld folder: %s\r\nUntick \"Documents fix\" in the launcher to go back to it.\r\n",
                       t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, why, dc_u8(real, u, sizeof u));
        }
        write_utf8(ml, "%s\r\n", dc_u8(g_newW, u, sizeof u));
        logmsg("REDIRECTED (%s): the game's folder is now %s%s; shell32 exports %s/%s; old folder %s", why, dc_u8(g_userW, u, sizeof u),
               first ? " (first time: copied from the old one)" : "", ea ? "A" : "-", ew ? "W" : "-", real[0] ? "kept as it is" : "-");
        SetEvent(g_ready);
        if (first && real[0]) copy_phase(real, 1);
    } else {
        marker_paths(mk, ml); DeleteFileW(ml);                  // the launcher: the game uses Documents
        if (!why && !late && !never && real[0] && !denied) logmsg("Documents is writable for the game - nothing redirected");
        if (never) logmsg("Mode=never: only the check");
        SetEvent(g_ready);
    }
    if (g_redirect) { DcResult r2; dc_check(logmsg, "start", g_userW, g_newW, "redirected", 1, &r2); }
    if (owner) dc_exe_info(logmsg);
    if (!owner) return 0;
    // after every match: the same check of the folder in use, and whether temp.rec of the match is there
    // (the state is also set for a moment while the game starts up: a "game" shorter than 20 s is not a match)
    int was = 0; FILETIME start = {0}; DWORD since = 0;
    for (;;) {
        Sleep(1000);
        int in = dc_in_game();
        if (in && !was) { GetSystemTimeAsFileTime(&start); since = GetTickCount(); }
        if (!in && was && GetTickCount() - since >= 20000) {
            Sleep(3000);                                       // the game finishes the replay file
            const wchar_t *f = g_redirect ? g_userW : real;
            DcResult m; dc_check(logmsg, "match-end", f, g_redirect ? g_newW : docs, g_redirect ? "redirected" : "the game's", 1, &m);
            char a[32], b[32]; dc_ft(&start, a, sizeof a); dc_ft(&m.rec, b, sizeof b);
            logmsg("match of %s: temp.rec %s (%s)", a, CompareFileTime(&m.rec, &start) >= 0 ? "WRITTEN" : "NOT written", b);
        }
        was = in;
    }
    return 0;
}
static int already_loaded(void) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_DocsFix_%lu", GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_lk);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
    _snwprintf(g_log, MAX_PATH, L"%lsDocsFix.log", g_dir); _snwprintf(g_ini, MAX_PATH, L"%lsDocsFix.ini", g_dir);
    GetModuleFileNameW(NULL, g_game, MAX_PATH); sl = wcsrchr(g_game, L'\\'); if (sl) sl[1] = 0; else g_game[0] = 0;
    if (already_loaded()) { logmsg("another DocsFix.dll copy is already active in this game, this one stays idle"); return TRUE; }
    g_t0 = uptime_ms();
    HMODULE sh = GetModuleHandleA("shell32.dll"); if (!sh) sh = LoadLibraryA("shell32.dll");
    g_origA = sh ? (ShA)GetProcAddress(sh, "SHGetFolderPathA") : NULL; g_origW = sh ? (ShW)GetProcAddress(sh, "SHGetFolderPathW") : NULL;
    if (!g_origA || !g_origW) { logmsg("DocsFix " DF_VERSION ": no SHGetFolderPath in shell32 - nothing done"); return TRUE; }
    HMODULE pl = GetModuleHandleA("Platform.dll");
    g_gpus = pl ? (GpusFn)GetProcAddress(pl, GPUS_NAME) : NULL;
    const char *now = g_gpus ? g_gpus() : NULL;
    // early enough = the game has not built its paths yet: the early loader holds its first question (winmm.dll's hold event,
    // not set yet), or Platform's Setup has not even run (its buffer is still empty). Otherwise - Elite's injector ~9 s after the
    // start, an older loader - it may have built them already: only the check, nothing redirected.
    char hn[64]; wsprintfA(hn, "Local\\dow2_docsfix_hold_%lu", GetCurrentProcessId());
    HANDLE hold = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, hn);
    int held = hold && WaitForSingleObject(hold, 0) == WAIT_TIMEOUT;
    int late = !held && !(g_gpus && now && !now[0]);
    logmsg("DocsFix " DF_VERSION " (%s): loaded %.2f s after the game started; the game's folder %s; %s", GetModuleHandleA("Platform.dll") ? "game" : "no Platform.dll?",
           g_t0 / 1000.0, !g_gpus ? "unknown (no Platform.dll export)" : now && now[0] ? now : "not set yet (Platform's Setup has not run)",
           held ? "the early loader holds the game's first question for it" : late ? "LATE: the game may have built its paths already" : "in time");
    g_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!late && pl && g_gpus) {
        void **s1 = find_slot(GetModuleHandleW(NULL), "Platform.dll", GPUS_NAME), **s2 = find_slot(pl, "shell32.dll", "SHGetFolderPathA");
        int a = s1 && set_slot(s1, (void *)hk_gpus), b = s2 && set_slot(s2, (void *)hk_shA);
        logmsg("gate: DOW2.exe's GetPathUserStorage import %s, Platform.dll's SHGetFolderPathA import %s", a ? "held" : "NOT found", b ? "held" : "NOT found");
    }
    if (hold) { SetEvent(hold); CloseHandle(hold); }               // the early loader's hold: our own gate is in place now
    HANDLE t = CreateThread(NULL, 0, worker, (LPVOID)(INT_PTR)late, 0, NULL);
    if (!t) { SetEvent(g_ready); logmsg("no worker thread (%lu)", GetLastError()); } else CloseHandle(t);
    return TRUE;
}
