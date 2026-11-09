// DXVK.dll - the game's Direct3D 9 through DXVK 2.7.1 (Direct3D 9 -> Vulkan), vanilla and Elite (09.11.2026).
//
// What it does (nothing else):
//  - Spooge.dll (the game's renderer) imports Direct3DCreate9 from d3d9.dll. Only that import slot is pointed at our function,
//    which loads <add-on folder>\DXVK_d3d9.bin (the official DXVK 2.7.1 x32 d3d9.dll, renamed: the loaders load every *.dll)
//    and returns its Direct3DCreate9. No device/vtable hooks; D3DPERF_* stay the system's.
//  - DXVK's files live in the add-on folder: DXVK_CONFIG_FILE = DXVK.conf there, DXVK_LOG_PATH = there (DOW2_d3d9.log),
//    DXVK_STATE_CACHE_PATH = there (DXVK 2.7 has no state cache any more - the driver caches pipelines; set for older/newer builds).
//  - No frame cap of our own (FPS unlock / Sound bug fix set d3d9.maxFrameRate through DXVK_CONFIG).
//  - The game's d3d9.dll is DXVK already (Elite with IsDXVK=true copies its own DXVK as d3d9.dll): nothing, one line in the log.
//  - No Vulkan check and no fallback (owner's decision 09.11.2026): a PC without Vulkan 1.3 fails at start - untick the add-on.
//    (Only if DXVK_d3d9.bin itself cannot be loaded, e.g. the file is missing, the game's own Direct3DCreate9 is called.)
//  - It has to be loaded before the game creates Direct3D: vanilla - the launcher's winmm loader (dow2-patches); Elite - the
//    early winmm (EARLY list, Automatch tick on). Loaded later (Elite's injector, >5 s after the start): no patch, a log line.
// Rendering only - nothing of the simulation is touched, no sync impact.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define DXVK_DLL_VERSION "1"
#define LATE_MS 5000

typedef void *(WINAPI *Create9_t)(UINT);
static wchar_t g_dir[MAX_PATH], g_log[MAX_PATH];
static void **g_slot;
static Create9_t g_orig, g_dxvk;
static volatile LONG g_calls;

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
// address of the import slot <dll>!<fn> in module m (by name, so a rebased Spooge.dll works too)
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
static HMODULE module_of(void *p) {
    HMODULE m = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)p, &m);
    return m;
}
static DWORD uptime_ms(void) {   // since the game process started
    FILETIME c, x, k, u, now; GetSystemTimeAsFileTime(&now);
    if (!GetProcessTimes(GetCurrentProcess(), &c, &x, &k, &u)) return 0;
    ULONGLONG a = ((ULONGLONG)c.dwHighDateTime << 32) | c.dwLowDateTime, b = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return b > a ? (DWORD)((b - a) / 10000) : 0;
}

// Spooge's Direct3DCreate9 (the game's main thread, once at the start)
static void *WINAPI hk_create9(UINT sdk) {
    LONG n = InterlockedIncrement(&g_calls);
    if (!g_dxvk) {
        wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsDXVK_d3d9.bin", g_dir); p[MAX_PATH - 1] = 0;
        HMODULE m = LoadLibraryW(p);
        Create9_t f = m ? (Create9_t)GetProcAddress(m, "Direct3DCreate9") : NULL;
        if (!f) {
            logmsg("DXVK_d3d9.bin %s (error %lu) - the game's own Direct3D this time", m ? "has no Direct3DCreate9" : "cannot be loaded", GetLastError());
            return g_orig(sdk);
        }
        g_dxvk = f;
        logmsg("DXVK_d3d9.bin loaded at %p", m);
    }
    void *r = g_dxvk(sdk);
    if (n <= 5) logmsg("Direct3DCreate9(%u) #%ld -> DXVK %p%s", sdk, n, r,
                       r ? "" : " (NULL: no usable Vulkan 1.3 GPU/driver? see DOW2_d3d9.log; untick DXVK if the game does not start)");
    return r;
}

static DWORD WINAPI watch(LPVOID _) {
    Sleep(60000);
    if (!g_calls) logmsg("the game did not call Direct3DCreate9 through DXVK.dll in 60 s - it was created before DXVK.dll was loaded (the game's own Direct3D)");
    return 0;
}

static void install(void) {
    HMODULE sp = GetModuleHandleA("Spooge.dll");
    if (!sp) { logmsg("no Spooge.dll in this process - nothing done"); return; }
    void **slot = find_slot(sp, "d3d9.dll", "Direct3DCreate9");
    if (!slot) { logmsg("Spooge.dll has no Direct3DCreate9 import (another build?) - nothing done"); return; }
    HMODULE d9 = module_of(*slot);
    wchar_t dn[MAX_PATH] = L"?"; if (d9) GetModuleFileNameW(d9, dn, MAX_PATH);
    if (is_dxvk(d9) || is_dxvk(GetModuleHandleA("d3d9.dll"))) {
        logmsg("the game's d3d9.dll is DXVK already (%ls; Elite's DXVK mode) - nothing done, Elite's DXVK and dxvk.conf stay", dn);
        return;
    }
    DWORD up = uptime_ms();
    if (up > LATE_MS) {
        logmsg("loaded %.1f s after the game started - Direct3D is created already (in Elite tick Automatch: it loads DXVK.dll early); nothing done", up / 1000.0);
        return;
    }
    // DXVK's files in the add-on folder (read by DXVK when it loads / at Direct3DCreate9)
    wchar_t v[MAX_PATH];
    _snwprintf(v, MAX_PATH, L"%lsDXVK.conf", g_dir); v[MAX_PATH - 1] = 0; SetEnvironmentVariableW(L"DXVK_CONFIG_FILE", v);
    wcscpy(v, g_dir); size_t l = wcslen(v); if (l > 3 && v[l - 1] == L'\\') v[l - 1] = 0;
    SetEnvironmentVariableW(L"DXVK_LOG_PATH", v);
    SetEnvironmentVariableW(L"DXVK_STATE_CACHE_PATH", v);
    g_orig = (Create9_t)*slot; g_slot = slot;
    DWORD old;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) { logmsg("VirtualProtect failed (%lu) - nothing done", GetLastError()); return; }
    InterlockedExchangePointer(slot, (void *)hk_create9);
    VirtualProtect(slot, sizeof *slot, old, &old);
    logmsg("DXVK %s (DXVK 2.7.1): Spooge's Direct3DCreate9 import (slot %p, was %p in %ls) -> DXVK_d3d9.bin; %.2f s after start; config %lsDXVK.conf, DXVK log here",
           DXVK_DLL_VERSION, slot, (void *)g_orig, dn, up / 1000.0, g_dir);
    CloseHandle(CreateThread(NULL, 0, watch, NULL, 0, NULL));
}
// one copy per game process: a patch can come both from Elite_DLLs (launcher/early winmm) and from dow2-patches\ (winmm loader)
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
    wcscpy(g_log, g_dir); wcscat(g_log, L"DXVK.log");
    if (already_loaded("DXVK")) { logmsg("another DXVK.dll copy is already active in this game, this one stays idle"); return TRUE; }
    install();
    return TRUE;
}
