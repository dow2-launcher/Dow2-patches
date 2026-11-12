// common/docscheck.h - can the GAME PROCESS write its user folder? (12.11.2026, Malum: the volume reset after every start,
// no replay since 30.08 - configuration.lua and Playback\temp.rec of 30.08 in Documents\My Games\Dawn of War II - Retribution,
// while the game kept reading that folder and our logs in the game folder were written fine.)
//
// How the game gets that folder (Platform.dll, Plat::File::Setup, 0x10002335): SHGetFolderPathA(NULL, CSIDL_PERSONAL = 5,
// NULL, SHGFP_TYPE_CURRENT) + PathAppendA "My Games" + CreateDirectoryA, + PathAppendA "Dawn of War II - Retribution" (the
// game's name) + CreateDirectoryA, then dbCrash::SetWorkingFolder (Debug.dll: ErrorLog/MiniDump go there). The path stays in
// Platform.dll's static buffer (0x10015708), which ?GetPathUserStorage@File@Plat@@YGPBDXZ returns; DOW2.exe builds its
// Logfiles\ Screenshots\ Settings\ Playback\ Settings\Stats\ Cache\ "Saved Games\" Badges\ aliases from it (0x0041453A) and
// reads Settings/Local.ini through it (0x00413569). ANSI: a Documents path outside the system code page breaks it.
//
// One check = two lines in the add-on's log: a readable one and "DOCSCHECK k=v ..." for the log summary on rts:
//   the folder the game uses (Platform.dll's buffer, else Documents + the game's name), Documents as Windows gives it (wide)
//   and whether it is %USERPROFILE%\Documents / under OneDrive / survives the ANSI round trip; attributes of Documents,
//   Settings and Playback (read-only, reparse point = redirected / OneDrive, offline / cloud placeholders); configuration.lua
//   and temp.rec: when last written, attributes, whether configuration.lua can be OPENED for writing (nothing written);
//   a test file created, written, closed and deleted in Settings and in Playback FROM THE GAME PROCESS with GetLastError of
//   the step that failed (5 = access denied: Controlled folder access / permissions; 19 = write-protected; 112 = disk full);
//   Windows Security's Controlled folder access setting from the registry (when a normal user may read it) and whether
//   DOW2.exe is in its allowed apps. Nothing else is changed; the test file is deleted at once.
// Controlled folder access (Windows Security > Ransomware protection) logs every block as event 1123 (1124 audit) in
// Microsoft-Windows-Windows Defender/Operational: the launcher's "Send logs" carries the recent ones (launcher 1.5.9).
#ifndef DOCSCHECK_H
#define DOCSCHECK_H
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define DC_GAME_NAME L"Dawn of War II - Retribution"
typedef void (*dc_log_fn)(const char *fmt, ...);
typedef HRESULT (WINAPI *dc_shgfpw_fn)(HWND, int, HANDLE, DWORD, LPWSTR);

static const char *dc_u8(const wchar_t *w, char *o, int cap) {      // paths in the log as UTF-8 (a Cyrillic user name)
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, o, cap, NULL, NULL)) snprintf(o, cap, "?");
    return o;
}
static void dc_ft(const FILETIME *f, char *o, int cap) {             // local "2026-08-30 18:46:41"
    FILETIME l; SYSTEMTIME t;
    if (!f || (!f->dwHighDateTime && !f->dwLowDateTime) || !FileTimeToLocalFileTime(f, &l) || !FileTimeToSystemTime(&l, &t)) { snprintf(o, cap, "-"); return; }
    snprintf(o, cap, "%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}
static const char *dc_errname(DWORD e) {
    switch (e) {
    case 0: return "ok"; case 2: return "file not found"; case 3: return "path not found"; case 5: return "access denied";
    case 19: return "write-protected"; case 32: return "in use by another program"; case 33: return "locked";
    case 80: return "file exists"; case 112: return "disk full"; case 1920: return "file cannot be accessed by the system";
    case 225: return "blocked as a virus"; case 4390: return "not a reparse point";
    default: return "";
    }
}
static void dc_attrs(DWORD a, char *o, int cap) {                    // "dir READONLY REPARSE ..." / "missing"
    if (a == INVALID_FILE_ATTRIBUTES) { snprintf(o, cap, "missing"); return; }
    static const struct { DWORD f; const char *n; } F[] = {
        {FILE_ATTRIBUTE_DIRECTORY, "dir"}, {FILE_ATTRIBUTE_READONLY, "READONLY"}, {FILE_ATTRIBUTE_HIDDEN, "hidden"},
        {FILE_ATTRIBUTE_SYSTEM, "system"}, {FILE_ATTRIBUTE_REPARSE_POINT, "REPARSE"}, {FILE_ATTRIBUTE_OFFLINE, "OFFLINE"},
        {FILE_ATTRIBUTE_ENCRYPTED, "encrypted"}, {0x00040000, "CLOUD-RECALL-ON-OPEN"}, {0x00400000, "CLOUD-RECALL-ON-ACCESS"},
        {0x00080000, "pinned"}, {0x00100000, "unpinned"}};
    int n = 0; o[0] = 0;
    for (unsigned i = 0; i < sizeof F / sizeof *F; i++) if (a & F[i].f) n += snprintf(o + n, cap - n, "%s%s", n ? " " : "", F[i].n);
    snprintf(o + n, cap - n, n ? " 0x%lX" : "0x%lX", a);
}
// create + write + close + delete a small file in dir (from this process); 0 = all fine, else the error of the step *stage
static DWORD dc_probe_write(const wchar_t *dir, const char **stage) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%ls\\elite_write_test_%lu.tmp", dir, GetCurrentProcessId()); p[MAX_PATH - 1] = 0;
    *stage = "create";
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (h == INVALID_HANDLE_VALUE) { DWORD e = GetLastError(); return e ? e : 1; }
    DWORD w = 0, e = 0; static const char T[] = "Elite write test\r\n";
    if (!WriteFile(h, T, sizeof T - 1, &w, NULL) || w != sizeof T - 1) { *stage = "write"; e = GetLastError(); if (!e) e = 1; }
    if (!CloseHandle(h) && !e) { *stage = "close"; e = GetLastError(); if (!e) e = 1; }
    if (!DeleteFileW(p) && !e) { *stage = "delete"; e = GetLastError(); if (!e) e = 1; }
    return e;
}
// an existing file opened for writing and closed at once - nothing is written, the date stays; 0 = could, else the error
static DWORD dc_probe_open(const wchar_t *f) {
    HANDLE h = CreateFileW(f, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { DWORD e = GetLastError(); return e ? e : 1; }
    CloseHandle(h); return 0;
}
// Controlled folder access from the registry: -1 not readable (*err), 0 off, 1 on, 2 audit only, 3 block disk changes only,
// 4 audit disk changes only, -2 no value (never set = off). The group policy value wins over the user's own setting.
static int dc_cfa(DWORD *err, int *allowed, const wchar_t *exe) {
    static const wchar_t *K[2] = {
        L"SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Windows Defender Exploit Guard\\Controlled Folder Access",
        L"SOFTWARE\\Microsoft\\Windows Defender\\Windows Defender Exploit Guard\\Controlled Folder Access"};
    int v = -2; *err = 0; *allowed = -1;
    for (int i = 0; i < 2; i++) {
        HKEY k; LONG r = RegOpenKeyExW(HKEY_LOCAL_MACHINE, K[i], 0, KEY_READ | KEY_WOW64_64KEY, &k);
        if (r != ERROR_SUCCESS) { if (r != ERROR_FILE_NOT_FOUND && !*err) *err = r; continue; }
        DWORD d = 0, sz = sizeof d, ty = 0;
        if (v == -2 && RegQueryValueExW(k, L"EnableControlledFolderAccess", NULL, &ty, (BYTE *)&d, &sz) == ERROR_SUCCESS && ty == REG_DWORD) v = (int)d;
        HKEY a;                                                     // allowed apps: value names are the programs' paths
        if (*allowed != 1 && RegOpenKeyExW(k, L"AllowedApplications", 0, KEY_READ | KEY_WOW64_64KEY, &a) == ERROR_SUCCESS) {
            if (*allowed < 0) *allowed = 0;
            wchar_t n[1024]; for (DWORD j = 0;; j++) {
                DWORD nl = 1024; if (RegEnumValueW(a, j, n, &nl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
                if (exe && !_wcsicmp(n, exe)) { *allowed = 1; break; }
            }
            RegCloseKey(a);
        }
        RegCloseKey(k);
    }
    if (v == -2 && *err) return -1;
    return v;
}
static const char *dc_cfa_name(int v) {
    switch (v) { case -2: return "off (never set)"; case -1: return "unknown (registry not readable)"; case 0: return "off";
    case 1: return "ON (blocks)"; case 2: return "audit only"; case 3: return "blocks disk changes only"; case 4: return "audits disk changes only"; }
    return "?";
}
// the folder the running game uses: Platform.dll's own buffer (ANSI) when it is set; else Documents + the game's name
static int dc_game_folder(wchar_t *out, dc_shgfpw_fn shgfpw, int *fromPlatform) {
    *fromPlatform = 0; out[0] = 0;
    HMODULE pl = GetModuleHandleA("Platform.dll");
    const char *(__stdcall *gp)(void) = pl ? (const char *(__stdcall *)(void))GetProcAddress(pl, "?GetPathUserStorage@File@Plat@@YGPBDXZ") : NULL;
    const char *a = gp ? gp() : NULL;
    if (a && a[0] && MultiByteToWideChar(CP_ACP, 0, a, -1, out, MAX_PATH)) { *fromPlatform = 1; return 1; }
    wchar_t d[MAX_PATH];
    if ((shgfpw ? shgfpw : SHGetFolderPathW)(NULL, CSIDL_PERSONAL, NULL, 0, d) != S_OK) return 0;
    _snwprintf(out, MAX_PATH, L"%ls\\My Games\\" DC_GAME_NAME, d); out[MAX_PATH - 1] = 0;
    return 1;
}
typedef struct {                       // one check's result (also for the caller: the add-on decides on it)
    DWORD settings, playback;          // dc_probe_write errors (0 = could write)
    DWORD cfgOpen;                     // configuration.lua opened for writing (0 = could; 2 = no such file)
    FILETIME cfg, rec;                 // last written (0 = no file)
    int cfa, allowed;
} DcResult;
// One check of <folder> (the game's user folder): "when" = "start" / "match end" / ...; docs = Documents as Windows gives
// it (wide, NULL = ask); tag = what the folder is ("the game's" / "redirected"). Two log lines; the result to *r.
static void dc_check(dc_log_fn log, const char *when, const wchar_t *folder, const wchar_t *docs, const char *tag, int platform,
                     DcResult *r) {
    DcResult z; if (!r) r = &z; memset(r, 0, sizeof *r);
    wchar_t st[MAX_PATH], pb[MAX_PATH], cfg[MAX_PATH], rec[MAX_PATH];
    _snwprintf(st, MAX_PATH, L"%ls\\Settings", folder); _snwprintf(pb, MAX_PATH, L"%ls\\Playback", folder);
    _snwprintf(cfg, MAX_PATH, L"%ls\\configuration.lua", st); _snwprintf(rec, MAX_PATH, L"%ls\\temp.rec", pb);
    st[MAX_PATH - 1] = pb[MAX_PATH - 1] = cfg[MAX_PATH - 1] = rec[MAX_PATH - 1] = 0;
    char aDoc[160] = "-", aSt[160], aPb[160], aCfg[160], aRec[160], tCfg[32], tRec[32];
    DWORD da = docs ? GetFileAttributesW(docs) : INVALID_FILE_ATTRIBUTES;
    if (docs) dc_attrs(da, aDoc, sizeof aDoc);
    dc_attrs(GetFileAttributesW(st), aSt, sizeof aSt); dc_attrs(GetFileAttributesW(pb), aPb, sizeof aPb);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(cfg, GetFileExInfoStandard, &fa)) { r->cfg = fa.ftLastWriteTime; dc_attrs(fa.dwFileAttributes, aCfg, sizeof aCfg); }
    else snprintf(aCfg, sizeof aCfg, "missing");
    if (GetFileAttributesExW(rec, GetFileExInfoStandard, &fa)) { r->rec = fa.ftLastWriteTime; dc_attrs(fa.dwFileAttributes, aRec, sizeof aRec); }
    else snprintf(aRec, sizeof aRec, "missing");
    dc_ft(&r->cfg, tCfg, sizeof tCfg); dc_ft(&r->rec, tRec, sizeof tRec);
    const char *sgS = "", *sgP = "";
    r->settings = GetFileAttributesW(st) == INVALID_FILE_ATTRIBUTES ? GetLastError() : dc_probe_write(st, &sgS);
    r->playback = GetFileAttributesW(pb) == INVALID_FILE_ATTRIBUTES ? GetLastError() : dc_probe_write(pb, &sgP);
    if (!r->settings) sgS = ""; if (!r->playback) sgP = "";
    r->cfgOpen = (r->cfg.dwLowDateTime || r->cfg.dwHighDateTime) ? dc_probe_open(cfg) : 2;
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    DWORD cerr = 0; r->cfa = dc_cfa(&cerr, &r->allowed, exe);
    // Documents: %USERPROFILE%\Documents? OneDrive? the ANSI round trip (the game asks the ANSI way)
    char dd[MAX_PATH * 3] = "-", ff[MAX_PATH * 3]; const char *where = "", *ansi = "ok";
    if (docs) {
        dc_u8(docs, dd, sizeof dd);
        wchar_t up[MAX_PATH], def[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
        _snwprintf(def, MAX_PATH, L"%ls\\Documents", n && n < MAX_PATH ? up : L"?"); def[MAX_PATH - 1] = 0;
        where = !_wcsicmp(def, docs) ? "the usual %USERPROFILE%\\Documents" : "NOT %USERPROFILE%\\Documents (moved)";
        for (const wchar_t *c = docs; *c; c++) if (!_wcsnicmp(c, L"OneDrive", 8)) { where = "under OneDrive"; break; }
        char a[MAX_PATH * 2]; BOOL lossy = FALSE;
        WideCharToMultiByte(CP_ACP, 0, docs, -1, a, sizeof a, NULL, &lossy); if (lossy) ansi = "LOSSY (letters outside the ANSI code page: the game's path is broken)";
    }
    dc_u8(folder, ff, sizeof ff);
    char es[96], ep[96], eo[96];
    snprintf(es, sizeof es, r->settings ? "FAILED at %s, error %lu %s" : "ok", sgS, r->settings, dc_errname(r->settings));
    snprintf(ep, sizeof ep, r->playback ? "FAILED at %s, error %lu %s" : "ok", sgP, r->playback, dc_errname(r->playback));
    snprintf(eo, sizeof eo, r->cfgOpen == 2 ? "no file" : r->cfgOpen ? "FAILED, error %lu %s" : "ok", r->cfgOpen, dc_errname(r->cfgOpen));
    log("docs check (%s): %s folder %s%s | write test from the game process: Settings %s, Playback %s | configuration.lua %s (attributes %s), "
        "open for writing %s | temp.rec %s (attributes %s) | Settings: %s, Playback: %s | Documents %s (%s, %s; ANSI %s) | Controlled folder access: %s%s%s",
        when, tag, ff, platform ? " (the game's own, Platform.dll)" : "", es, ep, tCfg, aCfg, eo, tRec, aRec, aSt, aPb, dd, aDoc, where, ansi,
        dc_cfa_name(r->cfa), r->allowed == 1 ? ", DOW2.exe allowed" : r->allowed == 0 ? ", DOW2.exe NOT among the allowed apps" : "",
        r->cfa == -1 ? (cerr == 5 ? " (access denied)" : "") : "");
    // the line the log summary reads (no spaces inside values)
    char k1[24], k2[24], k3[24];
    snprintf(k1, sizeof k1, r->settings ? "fail:%lu" : "ok", r->settings); snprintf(k2, sizeof k2, r->playback ? "fail:%lu" : "ok", r->playback);
    snprintf(k3, sizeof k3, r->cfgOpen == 2 ? "none" : r->cfgOpen ? "fail:%lu" : "ok", r->cfgOpen);
    for (char *c = tCfg; *c; c++) if (*c == ' ') *c = 'T';
    for (char *c = tRec; *c; c++) if (*c == ' ') *c = 'T';
    log("DOCSCHECK when=%s tag=%s settings=%s playback=%s cfg_open=%s cfg=%s rec=%s cfa=%d allowed=%d onedrive=%d moved=%d ansi=%s ro=%d reparse=%d",
        when, tag[0] == 'r' ? "redirected" : "game", k1, k2, k3, tCfg, tRec, r->cfa, r->allowed, strstr(where, "OneDrive") != NULL,
        strstr(where, "NOT") != NULL, ansi[0] == 'o' ? "ok" : "lossy",
        (GetFileAttributesW(st) != INVALID_FILE_ATTRIBUTES && (GetFileAttributesW(st) & FILE_ATTRIBUTE_READONLY)) || strstr(aCfg, "READONLY") != NULL,
        (da != INVALID_FILE_ATTRIBUTES && (da & FILE_ATTRIBUTE_REPARSE_POINT)) || strstr(aSt, "REPARSE") || strstr(aPb, "REPARSE"));
}
// The process itself (once, after the start): which exe (Controlled folder access decides per program: Windows' own
// reputation or the allowed list), its Authenticode signature as Windows checks it (cached data only, no network), the
// integrity level / elevation of this process and who started it. Loads wintrust.dll only for this.
typedef struct { DWORD cbStruct; LPCWSTR pcwszFilePath; HANDLE hFile; GUID *pgKnownSubject; } DcWtFile;
typedef struct { DWORD cbStruct; LPVOID pPolicyCallbackData, pSIPClientData; DWORD dwUIChoice, fdwRevocationChecks, dwUnionChoice;
                 DcWtFile *pFile; DWORD dwStateAction; HANDLE hWVTStateData; WCHAR *pwszURLReference; DWORD dwProvFlags, dwUIContext;
                 void *pSignatureSettings; } DcWtData;
static void dc_exe_info(dc_log_fn log) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH);
    char e8[MAX_PATH * 3], sig[64] = "not checked";
    HMODULE wt = LoadLibraryA("wintrust.dll");
    LONG (WINAPI *wvt)(HWND, GUID *, LPVOID) = wt ? (LONG (WINAPI *)(HWND, GUID *, LPVOID))GetProcAddress(wt, "WinVerifyTrust") : NULL;
    if (wvt) {
        GUID act = {0xaac56b, 0xcd44, 0x11d0, {0x8c, 0xc2, 0x00, 0xc0, 0x4f, 0xc2, 0x95, 0xee}};   // WINTRUST_ACTION_GENERIC_VERIFY_V2
        DcWtFile f = {sizeof f, exe, NULL, NULL};
        DcWtData d; memset(&d, 0, sizeof d); d.cbStruct = sizeof d; d.dwUIChoice = 2 /*WTD_UI_NONE*/; d.fdwRevocationChecks = 0;
        d.dwUnionChoice = 1 /*WTD_CHOICE_FILE*/; d.pFile = &f; d.dwStateAction = 1 /*VERIFY*/; d.dwProvFlags = 0x1000 /*CACHE_ONLY_URL*/ | 0x40 /*REVOCATION_CHECK_NONE*/;
        LONG r = wvt((HWND)INVALID_HANDLE_VALUE, &act, &d);
        snprintf(sig, sizeof sig, r == 0 ? "valid" : (DWORD)r == 0x800B0100 ? "NOT signed" : (DWORD)r == 0x80096010 ? "BROKEN (file changed after signing)" :
                 "not valid (0x%08lX)", (DWORD)r);
        d.dwStateAction = 2 /*CLOSE*/; wvt((HWND)INVALID_HANDLE_VALUE, &act, &d);
    }
    const char *il = "?", *elev = "?"; HANDLE tk;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tk)) {
        BYTE b[64]; DWORD n = 0;
        if (GetTokenInformation(tk, TokenIntegrityLevel, b, sizeof b, &n)) {
            PSID s = ((TOKEN_MANDATORY_LABEL *)b)->Label.Sid; DWORD rid = *GetSidSubAuthority(s, *GetSidSubAuthorityCount(s) - 1);
            il = rid < 0x2000 ? "LOW" : rid < 0x3000 ? "medium" : rid < 0x4000 ? "high (as administrator)" : "system";
        }
        TOKEN_ELEVATION te; if (GetTokenInformation(tk, TokenElevation, &te, sizeof te, &n)) elev = te.TokenIsElevated ? "elevated" : "not elevated";
        CloseHandle(tk);
    }
    char par[MAX_PATH] = "?"; DWORD me = GetCurrentProcessId(), pp = 0;
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (sn != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe; pe.dwSize = sizeof pe;
        for (BOOL ok = Process32First(sn, &pe); ok; ok = Process32Next(sn, &pe)) if (pe.th32ProcessID == me) { pp = pe.th32ParentProcessID; break; }
        pe.dwSize = sizeof pe;
        for (BOOL ok = Process32First(sn, &pe); ok && pp; ok = Process32Next(sn, &pe)) if (pe.th32ProcessID == pp) { lstrcpynA(par, pe.szExeFile, sizeof par); break; }
        CloseHandle(sn);
    }
    log("docs check: the game process %s (signature %s; %s, %s; started by %s)", dc_u8(exe, e8, sizeof e8), sig, il, elev, pp ? par : "?");
    log("DOCSEXE sig=%s il=%s elevated=%d parent=%s", !strcmp(sig, "valid") ? "valid" : !strncmp(sig, "NOT", 3) ? "none" : "bad",
        il[0] == 'L' ? "low" : il[0] == 'm' ? "medium" : il[0] == 'h' ? "high" : "other", elev[0] == 'e', pp ? par : "?");
}
// one prober per game process (DocsFix and CrashLog both carry the check): the first DLL that takes this name does it
static int dc_owner(void) {
    char n[64]; wsprintfA(n, "Local\\dow2docscheck_%lu", GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() != ERROR_ALREADY_EXISTS;
}
// the game's state: [*(0x01335A78) + 0x114] != 0 in a game (loading screen on), 0 in the menus (SteamKeep, EliteAI)
static int dc_in_game(void) {
    DWORD *g = (DWORD *)0x01335A78;
    if (IsBadReadPtr(g, 4) || !*g || IsBadReadPtr((void *)(*g + 0x114), 4)) return 0;
    return *(DWORD *)(*g + 0x114) != 0;
}
#endif
