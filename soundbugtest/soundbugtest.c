// SoundBugTest.dll 1 (TEST, 08.11.2026) - the Dawn of War II "sound bug" WORST CASE plus a detailed log, so that players can
// provoke the bug on purpose and send the log (launcher -> Send logs). It removes every mitigation we have:
//
// 1. Stock FMOD Ex 4.32.3 and stock Shark logic. Elite copies Elite_bin/Elite_fmodex.bin (4.32.20) and Elite_Shark.bin (one byte
//  changed: Shark+0x3907A 85 -> 8C, jne -> jl of Shark's FMOD version check, the community "SMOL" fix) over fmodex.dll /
//  Shark.dll (Elite.launchconfig AdditionalFiles) - so after a game of Elite even vanilla runs them. Before Shark starts FMOD
//  (Shark's Driver::m_instance, Shark+0x6BDA8, still 0) SoundBugTest loads SoundBugTest_fmodex.bin (Relic's own 4.32.3 from the
//  Steam depot), points all of Shark's fmodex imports at it and puts the version check back to jne in memory. No game file is
//  changed. If FMOD has already started when the DLL loads, the log says so: that session is NOT the worst case.
// 2. No merging of Shark's sound queues (SoundFix refuses to work while this DLL holds its mutex; this DLL refuses to work when
//  SoundFix came first) and no frame cap (DXVK_CONFIG maxFrameRate = 0 before Direct3D is created - Elite creates Direct3D before
//  it loads Elite_DLLs, so there DXVK's dxvk.conf cap stays; logged).
// 3. The log (SoundBugTest.log next to the DLL, one line a second + events at once; SoundBugTest_events.log = events and one line a
//  minute, so a whole evening fits in what the launcher sends). Written straight through to the disk (a crash loses nothing),
//  rotated by size into *.1.log. Read-only: nothing in the game or the simulation changes apart from the FMOD library and the byte.
#define COBJMACROS
#include <windows.h>
#include <psapi.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define SBT_VERSION "1"
#define LOG_MAX (3u << 20)          // SoundBugTest.log -> SoundBugTest.1.log
#define EV_MAX  (1u << 20)          // SoundBugTest_events.log -> SoundBugTest_events.1.log

// ---------------------------------------------------------------- log
static char g_dir[MAX_PATH], g_lp[MAX_PATH], g_lp1[MAX_PATH], g_ep[MAX_PATH], g_ep1[MAX_PATH], g_sess[200] = "";
static CRITICAL_SECTION g_lcs;
static HANDLE g_lh = INVALID_HANDLE_VALUE, g_eh = INVALID_HANDLE_VALUE;
static HANDLE lopen(const char *p) {
    return CreateFileA(p, FILE_APPEND_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
}
static void lput(HANDLE *h, const char *p, const char *p1, DWORD max, const char *s, int n) {
    DWORD w;
    if (*h == INVALID_HANDLE_VALUE && (*h = lopen(p)) == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(*h, NULL);
    if (sz != INVALID_FILE_SIZE && sz > max) {
        CloseHandle(*h); MoveFileExA(p, p1, MOVEFILE_REPLACE_EXISTING);
        if ((*h = lopen(p)) == INVALID_HANDLE_VALUE) return;
        char c[300]; int k = snprintf(c, sizeof c, "(continued, the earlier part is in %s) %s\r\n", strrchr(p1, '\\') ? strrchr(p1, '\\') + 1 : p1, g_sess);
        if (k > 0) WriteFile(*h, c, k, &w, NULL);
    }
    WriteFile(*h, s, n, &w, NULL);
}
enum { TO_MAIN = 1, TO_EV = 2 };
static void vlog(int to, const char *fmt, va_list a) {
    char b[1600]; SYSTEMTIME t; GetLocalTime(&t);
    int n = snprintf(b, sizeof b, "%02d.%02d %02d:%02d:%02d ", t.wDay, t.wMonth, t.wHour, t.wMinute, t.wSecond);
    int k = vsnprintf(b + n, sizeof b - n - 2, fmt, a); if (k < 0) k = 0; n += k; if (n > (int)sizeof b - 3) n = sizeof b - 3;
    b[n++] = '\r'; b[n++] = '\n';
    EnterCriticalSection(&g_lcs);
    if (to & TO_MAIN) lput(&g_lh, g_lp, g_lp1, LOG_MAX, b, n);
    if (to & TO_EV) lput(&g_eh, g_ep, g_ep1, EV_MAX, b, n);
    LeaveCriticalSection(&g_lcs);
}
static void ev(const char *fmt, ...) { va_list a; va_start(a, fmt); vlog(TO_MAIN | TO_EV, fmt, a); va_end(a); }   // an event: both files
static void sl(const char *fmt, ...) { va_list a; va_start(a, fmt); vlog(TO_MAIN, fmt, a); va_end(a); }            // the line a second
static void dg(const char *fmt, ...) { va_list a; va_start(a, fmt); vlog(TO_EV, fmt, a); va_end(a); }              // the line a minute

static LARGE_INTEGER g_qf;
static LONGLONG qpc(void) { LARGE_INTEGER x; QueryPerformanceCounter(&x); return x.QuadPart; }
static LONG us(LONGLONG d) { return (LONG)(d * 1000000 / g_qf.QuadPart); }
static void lmax(volatile LONG *m, LONG v) { if (v > *m) *m = v; }

// ---------------------------------------------------------------- Shark.dll 3.19 (Steam/GOG and Elite's copy: only 0x3907A differs)
static BYTE *SB;
#define SR(rva) ((BYTE *)SB + (rva))
#define DRV_INST 0x6bda8   // shark::Driver::m_instance (FMODDriver::initialize sets it first, before any FMOD call)
#define VCHECK   0x3907a   // jne (85, Relic) / jl (8C, SMOL) of the FMOD version check in FMODDriver::initialize
#define SM_PTR   0x70360   // the SoundManager the Shark thread ticks
#define SM_UPD   0x1d580   // SoundManager update: swaps the message queues, runs them, FMOD update
#define TICK     0x45b30   // Shark thread tick (20 a second): mov eax,[SM_PTR]; ...; call SM_UPD; mov al,1; ret 4
#define BUF_VT   0x4f6ec   // vtable slot of the sample loader's request pass (Shark+0x33410): static/streaming buffer pools
#define BUF_FN   0x33410
#define SH_THR   0x6c250   // HANDLE of the Shark thread
static const DWORD q_rva[7] = {0x69ff0, 0x6a1b8, 0x6abf0, 0x6aa24, 0x6abfc, 0x6ac04, 0x6ac0c};   // write-queue pointers (1 = ControlSource, 4 = ContainerLogic)

static int code_is(DWORD rva, const char *hex) {
    BYTE *p = SR(rva);
    for (int i = 0; hex[i] && hex[i + 1]; i += 2) { unsigned b; sscanf(hex + i, "%2x", &b); if (*p++ != (BYTE)b) return 0; }
    return 1;
}
static int imm_is(DWORD rva, DWORD target_rva) { return *(DWORD *)SR(rva) == (DWORD)(UINT_PTR)SR(target_rva); }
static DWORD exp_rva(const char *name) { FARPROC p = GetProcAddress((HMODULE)SB, name); return p ? (DWORD)((BYTE *)p - SB) : 0; }
static int shark_known(void) {
    return exp_rva("?setPosition@Container@shark@@QBEXABVVector3f@Math@@@Z") == 0x3fae0
        && exp_rva("?initialize@FMODDriver@shark@@QAE_NABUSetupParams@12@@Z") == 0x38f90
        && code_is(TICK, "a1") && imm_is(TICK + 1, SM_PTR) && code_is(0x45b42, "e839") && code_is(0x45b47, "b001c20400")
        && code_is(0x38fb1, "833d") && imm_is(0x38fb3, DRV_INST)
        && code_is(0x39071, "817c2428033204000f") && (*SR(VCHECK) == 0x85 || *SR(VCHECK) == 0x8c)
        && *(DWORD *)SR(BUF_VT) == (DWORD)(UINT_PTR)SR(BUF_FN);
}

// ---------------------------------------------------------------- PE helpers
static DWORD64 modver(HMODULE m) {   // FileVersion from the module's own version resource (4.32.3 = 0x0000000400200003)
    HRSRC r = m ? FindResourceA(m, MAKEINTRESOURCEA(1), (LPCSTR)RT_VERSION) : NULL; if (!r) return 0;
    DWORD n = SizeofResource(m, r); BYTE *p = (BYTE *)LockResource(LoadResource(m, r)); if (!p) return 0;
    for (DWORD i = 0; i + 16 <= n; i += 4) if (*(DWORD *)(p + i) == 0xFEEF04BD) return ((DWORD64)*(DWORD *)(p + i + 8) << 32) | *(DWORD *)(p + i + 12);
    return 0;
}
#define VER_43203 0x0000000400200003ull
static void verstr(DWORD64 v, char *s) { sprintf(s, "%u.%u.%u", (unsigned)(v >> 32) & 0xffff, (unsigned)(v >> 16) & 0xffff, (unsigned)v & 0xffff); }
static int set_byte(BYTE *p, BYTE b) { DWORD o; if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &o)) return 0; *p = b; VirtualProtect(p, 1, o, &o); FlushInstructionCache(GetCurrentProcess(), p, 1); return 1; }
static IMAGE_IMPORT_DESCRIPTOR *import_of(BYTE *mod, const char *dll) {
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(mod + ((IMAGE_DOS_HEADER *)mod)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; if (!dd.VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(mod + dd.VirtualAddress); d->Name; d++)
        if (!_stricmp((char *)mod + d->Name, dll)) return d;
    return NULL;
}
static void **import_slot(BYTE *mod, const char *dll, const char *fn) {
    IMAGE_IMPORT_DESCRIPTOR *d = import_of(mod, dll); if (!d || !d->OriginalFirstThunk) return NULL;
    IMAGE_THUNK_DATA32 *nt = (IMAGE_THUNK_DATA32 *)(mod + d->OriginalFirstThunk); void **iat = (void **)(mod + d->FirstThunk);
    for (int i = 0; nt[i].u1.AddressOfData; i++)
        if (!(nt[i].u1.Ordinal & IMAGE_ORDINAL_FLAG32) && !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(mod + nt[i].u1.AddressOfData))->Name, fn)) return &iat[i];
    return NULL;
}
static int slot_write(void **s, void *fn, void **orig) {
    DWORD o; if (!s || !VirtualProtect(s, 4, PAGE_READWRITE, &o)) return 0;
    if (orig) *orig = *s; *s = fn; VirtualProtect(s, 4, o, &o); return 1;
}
static int hook_import(BYTE *mod, const char *dll, const char *fn, void *hk, void **orig) { return slot_write(import_slot(mod, dll, fn), hk, orig); }
static int in_module(void *p, const char *dll) {
    HMODULE m = GetModuleHandleA(dll); MODULEINFO mi;
    return m && GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof mi) && (BYTE *)p >= (BYTE *)mi.lpBaseOfDll && (BYTE *)p < (BYTE *)mi.lpBaseOfDll + mi.SizeOfImage;
}
static int executable(void *p) {
    MEMORY_BASIC_INFORMATION mb;
    return VirtualQuery(p, &mb, sizeof mb) && (mb.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE));
}
// DOW2.exe 3.19 import slot (fixed base 0x400000): it must point into dll (or into another add-on's hook = executable memory)
static int exe_hook(DWORD slot, const char *dll, void *hk, void **orig) {
    void **s = (void **)(UINT_PTR)slot;
    if (IsBadReadPtr(s, 4) || !(in_module(*s, dll) || executable(*s))) return 0;
    return slot_write(s, hk, orig);
}

// ---------------------------------------------------------------- 1. stock FMOD Ex 4.32.3 + stock Shark
static int g_worst_fmod; static char g_fmver[32] = "?";
static void fmod_stock(void) {
    char vs[32], vs2[32];
    HMODULE fm = GetModuleHandleA("fmodex.dll"); DWORD64 v = modver(fm); verstr(v, vs); strcpy(g_fmver, vs);
    if (!SB || !shark_known()) { ev("FMOD: unknown Shark.dll build - nothing changed (fmodex.dll %s): NOT the worst case", vs); return; }
    BYTE b = *SR(VCHECK); int started = *(DWORD *)SR(DRV_INST) != 0;
    if (v == VER_43203) {
        if (b == 0x8c && !started && set_byte(SR(VCHECK), 0x85)) b = 0x85;
        g_worst_fmod = b == 0x85;
        ev("FMOD: the stock FMOD Ex 4.32.3 (fmodex.dll) is in use; Shark's version check %s%s", b == 0x85 ? "jne (stock)" : "jl (SMOL)",
           started ? " (the sound system had already started)" : "");
        return;
    }
    if (started) { ev("FMOD: FMOD Ex %s had ALREADY STARTED when SoundBugTest loaded - too late to switch to the stock 4.32.3: this session is NOT the worst case", vs); return; }
    char path[MAX_PATH]; snprintf(path, MAX_PATH, "%sSoundBugTest_fmodex.bin", g_dir);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { ev("FMOD: no SoundBugTest_fmodex.bin next to the DLL - FMOD Ex %s stays: NOT the worst case", vs); return; }
    HMODULE nm = LoadLibraryA(path); DWORD64 nv = modver(nm); verstr(nv, vs2);
    if (!nm || nv != VER_43203) { ev("FMOD: SoundBugTest_fmodex.bin %s (error %lu) - FMOD Ex %s stays: NOT the worst case", nm ? vs2 : "not loaded", GetLastError(), vs); if (nm) FreeLibrary(nm); return; }
    IMAGE_IMPORT_DESCRIPTOR *d = import_of(SB, "fmodex.dll");
    if (!d || !d->OriginalFirstThunk) { ev("FMOD: Shark has no fmodex.dll import - not changed"); FreeLibrary(nm); return; }
    IMAGE_THUNK_DATA32 *nt = (IMAGE_THUNK_DATA32 *)(SB + d->OriginalFirstThunk); void **iat = (void **)(SB + d->FirstThunk);
    static void *nf[256]; int n = 0;
    for (; nt[n].u1.AddressOfData; n++) {   // resolve all first: a missing one = change nothing
        if (n >= 256 || (nt[n].u1.Ordinal & IMAGE_ORDINAL_FLAG32)) { ev("FMOD: unexpected Shark import table - not changed"); FreeLibrary(nm); return; }
        const char *name = (char *)((IMAGE_IMPORT_BY_NAME *)(SB + nt[n].u1.AddressOfData))->Name;
        if (!(nf[n] = (void *)GetProcAddress(nm, name))) { ev("FMOD: %s missing in 4.32.3 - not changed", name); FreeLibrary(nm); return; }
    }
    DWORD o;
    if (!VirtualProtect(iat, n * 4, PAGE_READWRITE, &o)) { FreeLibrary(nm); return; }
    if (*(DWORD *)SR(DRV_INST)) { VirtualProtect(iat, n * 4, o, &o); ev("FMOD: the sound system started meanwhile - FMOD Ex %s stays: NOT the worst case", vs); FreeLibrary(nm); return; }
    for (int i = 0; i < n; i++) iat[i] = nf[i];
    VirtualProtect(iat, n * 4, o, &o);
    if (b == 0x8c) set_byte(SR(VCHECK), 0x85);
    strcpy(g_fmver, vs2); g_worst_fmod = *SR(VCHECK) == 0x85;
    ev("FMOD: the stock FMOD Ex 4.32.3 (SoundBugTest_fmodex.bin at %p) instead of %s: Shark's %d fmodex imports redirected; Shark's version check %s",
       nm, vs, n, b == 0x8c ? "jl -> jne (stock) in memory" : "jne (stock)");
}

// ---------------------------------------------------------------- 2. no frame cap (DXVK_CONFIG before Direct3D)
static void dxvk_env(void) {
    char old[2048], v[2400]; DWORD n = GetEnvironmentVariableA("DXVK_CONFIG", old, sizeof old); if (n >= sizeof old) n = 0; old[n] = 0;
    snprintf(v, sizeof v, "%s%sd3d9.maxFrameRate = 0; dxgi.maxFrameRate = 0", old, n ? "; " : "");
    SetEnvironmentVariableA("DXVK_CONFIG", v);
}
static int is_dxvk(HMODULE m) {   // DXVK's d3d9.dll carries the name of its config variable
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
static void dxvk_conf_line(char *out, int n) {   // maxFrameRate in the game folder's dxvk.conf (Elite.exe writes 60 with IsDXVK=true)
    char p[MAX_PATH], l[256]; GetModuleFileNameA(NULL, p, MAX_PATH); char *s = strrchr(p, '\\'); if (s) strcpy(s + 1, "dxvk.conf");
    snprintf(out, n, "no dxvk.conf"); FILE *f = fopen(p, "r"); if (!f) return;
    snprintf(out, n, "dxvk.conf without maxFrameRate");
    while (fgets(l, sizeof l, f)) if (strstr(l, "maxFrameRate") && l[strspn(l, " \t")] != '#') { l[strcspn(l, "\r\n")] = 0; snprintf(out, n, "dxvk.conf: %s", l + strspn(l, " \t")); break; }
    fclose(f);
}
typedef void *(WINAPI *d3dcreate_t)(UINT); static d3dcreate_t o_d3dcreate; static volatile LONG g_d3d_seen;
static void *WINAPI hk_d3dcreate(UINT sdk) {
    void *d = o_d3dcreate(sdk);
    if (!InterlockedExchange(&g_d3d_seen, 1)) {
        char c[160]; dxvk_conf_line(c, sizeof c); int dx = is_dxvk(GetModuleHandleA("d3d9.dll"));
        ev("Direct3D created after SoundBugTest: no frame cap%s (%s)", dx ? " - DXVK: maxFrameRate = 0 via DXVK_CONFIG, overrides dxvk.conf" : " - no DXVK, the game has no cap of its own; vsync as set in the options", c);
    }
    return d;
}

// ---------------------------------------------------------------- FMOD Ex (the copy Shark uses) - C++ API members are __stdcall, this first
typedef int (__stdcall *setsw_t)(void *, int, int, int, int, int);
typedef int (__stdcall *setdsp_t)(void *, unsigned, int);
typedef int (__stdcall *finit_t)(void *, int, unsigned, void *);
typedef int (__stdcall *getsw_t)(void *, int *, int *, int *, int *, int *, int *);
typedef int (__stdcall *geti_t)(void *, int *);
typedef int (__stdcall *getdsp_t)(void *, unsigned *, int *);
typedef int (__stdcall *fupd_t)(void *);
typedef int (__stdcall *getch_t)(void *, int, void **);
typedef int (__stdcall *chbool_t)(void *, char *);
typedef int (__stdcall *cpu_t)(void *, float *, float *, float *, float *, float *);
typedef int (__stdcall *drvinfo_t)(void *, int, char *, int, void *);
typedef int (__stdcall *memstats_t)(int *, int *, int);
typedef int (__stdcall *syscb_t)(void *, int, void *, void *);
typedef int (__stdcall *setcb_t)(void *, syscb_t);
static setsw_t o_setsw; static setdsp_t o_setdsp; static finit_t o_finit; static fupd_t o_fupd; static setcb_t o_setcb; static syscb_t g_shark_cb;
static getsw_t p_getsw; static geti_t p_getout, p_getchp, p_numdrv, p_getdrv; static getdsp_t p_getdsp; static getch_t p_getch;
static chbool_t p_isplay, p_isvirt; static cpu_t p_cpu; static drvinfo_t p_drvinfo; static memstats_t p_mem;
static int g_fmod_rate, g_maxch = 128; static void *volatile g_fsys;

// Windows: default output's shared-mode mix format (Control Panel -> device -> Advanced -> Default Format)
static const CLSID my_CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID my_IID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID my_IID_IMMNotificationClient = {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
static const IID my_IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID my_IID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const PROPERTYKEY my_PKEY_FriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
static const GUID my_PKEY_AudioEngine = {0xf19f064d, 0x082c, 0x4e27, {0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c}};   // pid 0 DeviceFormat, 3 OEMFormat
static char g_mix_desc[160] = "unknown"; static volatile LONG g_mix_rate;
static int query_mix(void) {
    HRESULT ci = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *en = NULL; IMMDevice *d = NULL; IAudioClient *ac = NULL; WAVEFORMATEX *wf = NULL; int rate = 0;
    if (CoCreateInstance(&my_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &my_IID_IMMDeviceEnumerator, (void **)&en) >= 0 && en) {
        if (IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &d) >= 0 && d) {
            if (IMMDevice_Activate(d, &my_IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&ac) >= 0 && ac) {
                if (IAudioClient_GetMixFormat(ac, &wf) >= 0 && wf) {
                    rate = (int)wf->nSamplesPerSec;
                    snprintf(g_mix_desc, sizeof g_mix_desc, "%lu Hz, %u channels, %u bit", wf->nSamplesPerSec, wf->nChannels, wf->wBitsPerSample);
                    CoTaskMemFree(wf);
                }
                IAudioClient_Release(ac);
            }
            IMMDevice_Release(d);
        } else snprintf(g_mix_desc, sizeof g_mix_desc, "no default output");
        IMMDeviceEnumerator_Release(en);
    }
    if (ci == S_OK || ci == S_FALSE) CoUninitialize();
    g_mix_rate = rate; return rate;
}
static const char *rate_cmp(int mix, int fm) { return !mix || !fm ? "" : mix == fm ? " (same)" : " (DIFFERS: FMOD output resampled by Windows)"; }

static int __stdcall hk_setsw(void *sys, int rate, int fmt, int outch, int maxin, int rs) {
    int mix = query_mix();
    ev("FMOD: the game asks for %d Hz (format %d, max input channels %d, resampler %d); Windows default output mix format %s%s", rate, fmt, maxin, rs, g_mix_desc, rate_cmp(mix, rate));
    return o_setsw(sys, rate, fmt, outch, maxin, rs);
}
static int __stdcall hk_setdsp(void *sys, unsigned len, int n) { int r = o_setdsp(sys, len, n); ev("FMOD: DSP buffer %u samples x %d (result %d)", len, n, r); return r; }
static int __stdcall hk_finit(void *sys, int maxch, unsigned flags, void *x) {
    int r = o_finit(sys, maxch, flags, x), rate = 0, fmt = 0, och = 0, mi = 0, rs = 0, bits = 0, out = -1, nb = 0; unsigned bl = 0;
    g_fsys = sys; if (maxch > 0 && maxch <= 4096) g_maxch = maxch;
    if (p_getsw) p_getsw(sys, &rate, &fmt, &och, &mi, &rs, &bits);
    if (p_getout) p_getout(sys, &out);
    if (p_getdsp) p_getdsp(sys, &bl, &nb);
    g_fmod_rate = rate;
    ev("FMOD %s init(%d channels, flags %x) = %d: output %s (%d), mixing at %d Hz, %d output channels, DSP buffer %u x %d (%.1f ms each); Windows mix %s%s",
       g_fmver, maxch, flags, r, out == 6 ? "DirectSound" : out == 7 ? "WinMM" : out == 8 ? "WASAPI" : out == 2 ? "NO SOUND" : out == 1 ? "autodetect" : "other", out,
       rate, och, bl, nb, rate ? bl * 1000.0 / rate : 0.0, g_mix_desc, rate_cmp(g_mix_rate, rate));
    return r;
}
static const char *cbname(int t) {
    static const char *N[] = {"device list changed", "DEVICE LOST", "memory allocation FAILED", "thread created", "bad DSP connection", "bad DSP level"};
    return t >= 0 && t < 6 ? N[t] : "?";
}
static int __stdcall my_syscb(void *sys, int type, void *a, void *b) {   // FMOD system callback (Shark's own runs after it)
    if (type == 0 && p_numdrv) {
        int nd = -1, cur = -1; char nm[256] = ""; p_numdrv(sys, &nd); if (p_getdrv) p_getdrv(sys, &cur);
        if (p_drvinfo && cur >= 0) p_drvinfo(sys, cur, nm, sizeof nm, NULL);
        ev("FMOD callback: %s - %d output devices, FMOD's device %d \"%s\"", cbname(type), nd, cur, nm);
    } else if (type == 3) ev("FMOD callback: thread created (%p)", a);
    else ev("FMOD callback: %s (type %d, %p %p)", cbname(type), type, a, b);
    return g_shark_cb ? g_shark_cb(sys, type, a, b) : 0;
}
static int __stdcall hk_setcb(void *sys, syscb_t cb) { g_shark_cb = cb; ev("FMOD: Shark's system callback %p wrapped (logged first)", cb); return o_setcb(sys, my_syscb); }

// per second, on the Shark thread inside FMOD's update (the FMOD Ex API is not thread-safe)
static volatile LONG c_fupd, c_fupd_us, c_fupd_max, want_stats, g_last_upd_err;
static volatile LONG s_chp = -1, s_real = -1, s_virt = -1; static float s_cpu;
static int __stdcall hk_fupd(void *sys) {
    LONGLONG t0 = qpc(); int r = o_fupd(sys); LONG d = us(qpc() - t0);
    g_fsys = sys; c_fupd++; c_fupd_us += d; lmax(&c_fupd_max, d);
    if (r != g_last_upd_err) { LONG was = g_last_upd_err; g_last_upd_err = r; ev("FMOD: System::update result %d (was %ld)", r, was); }
    if (want_stats) {
        int n = -1; if (p_getchp) p_getchp(sys, &n); s_chp = n;
        if (p_getch && p_isplay && p_isvirt) {
            int real = 0, virt = 0;
            for (int i = 0; i < g_maxch; i++) {
                void *ch = NULL; char pl = 0, vi = 0;
                if (p_getch(sys, i, &ch) || !ch || p_isplay(ch, &pl) || !pl) continue;
                if (!p_isvirt(ch, &vi) && vi) virt++; else real++;
            }
            s_real = real; s_virt = virt;
        }
        float a = 0, b = 0, c = 0, e = 0, t = 0; if (p_cpu && !p_cpu(sys, &a, &b, &c, &e, &t)) s_cpu = t;
        want_stats = 0;
    }
    return r;
}
static HMODULE g_fm_used;
static void fmod_hooks(void) {   // Shark's fmodex imports (after fmod_stock: they point into the FMOD Ex in use)
    static const char *N[5] = {"?setSoftwareFormat@System@FMOD@@QAG?AW4FMOD_RESULT@@HW4FMOD_SOUND_FORMAT@@HHW4FMOD_DSP_RESAMPLER@@@Z",
        "?setDSPBufferSize@System@FMOD@@QAG?AW4FMOD_RESULT@@IH@Z", "?init@System@FMOD@@QAG?AW4FMOD_RESULT@@HIPAX@Z",
        "?setCallback@System@FMOD@@QAG?AW4FMOD_RESULT@@P6G?AW43@PAUFMOD_SYSTEM@@W4FMOD_SYSTEM_CALLBACKTYPE@@PAX2@Z@Z", "?update@System@FMOD@@QAG?AW4FMOD_RESULT@@XZ"};
    void *H[5] = {(void *)hk_setsw, (void *)hk_setdsp, (void *)hk_finit, (void *)hk_setcb, (void *)hk_fupd};
    void **O[5] = {(void **)&o_setsw, (void **)&o_setdsp, (void **)&o_finit, (void **)&o_setcb, (void **)&o_fupd};
    int started = *(DWORD *)SR(DRV_INST) != 0, n = 0;
    for (int i = started ? 4 : 0; i < 5; i++) n += hook_import(SB, "fmodex.dll", N[i], H[i], O[i]);
    if (started) ev("FMOD: the sound system had already started - format/callback not logged this time");
    if (o_fupd) GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)o_fupd, &g_fm_used);
    HMODULE fm = g_fm_used;
    if (fm) {
        p_getsw = (getsw_t)GetProcAddress(fm, "?getSoftwareFormat@System@FMOD@@QAG?AW4FMOD_RESULT@@PAHPAW4FMOD_SOUND_FORMAT@@00PAW4FMOD_DSP_RESAMPLER@@0@Z");
        p_getout = (geti_t)GetProcAddress(fm, "?getOutput@System@FMOD@@QAG?AW4FMOD_RESULT@@PAW4FMOD_OUTPUTTYPE@@@Z");
        p_getdsp = (getdsp_t)GetProcAddress(fm, "?getDSPBufferSize@System@FMOD@@QAG?AW4FMOD_RESULT@@PAIPAH@Z");
        p_getchp = (geti_t)GetProcAddress(fm, "?getChannelsPlaying@System@FMOD@@QAG?AW4FMOD_RESULT@@PAH@Z");
        p_getch = (getch_t)GetProcAddress(fm, "?getChannel@System@FMOD@@QAG?AW4FMOD_RESULT@@HPAPAVChannel@2@@Z");
        p_isplay = (chbool_t)GetProcAddress(fm, "?isPlaying@Channel@FMOD@@QAG?AW4FMOD_RESULT@@PA_N@Z");
        p_isvirt = (chbool_t)GetProcAddress(fm, "?isVirtual@Channel@FMOD@@QAG?AW4FMOD_RESULT@@PA_N@Z");
        p_cpu = (cpu_t)GetProcAddress(fm, "?getCPUUsage@System@FMOD@@QAG?AW4FMOD_RESULT@@PAM0000@Z");
        p_numdrv = (geti_t)GetProcAddress(fm, "?getNumDrivers@System@FMOD@@QAG?AW4FMOD_RESULT@@PAH@Z");
        p_getdrv = (geti_t)GetProcAddress(fm, "?getDriver@System@FMOD@@QAG?AW4FMOD_RESULT@@PAH@Z");
        p_drvinfo = (drvinfo_t)GetProcAddress(fm, "?getDriverInfo@System@FMOD@@QAG?AW4FMOD_RESULT@@HPADHPAUFMOD_GUID@@@Z");
        p_mem = (memstats_t)GetProcAddress(fm, "FMOD_Memory_GetStats");   // (int *current, int *max, FMOD_BOOL blocking) - 3 arguments
    }
    char mp[MAX_PATH] = "?"; if (fm) GetModuleFileNameA(fm, mp, MAX_PATH);
    ev("FMOD: %d/%d hooks; Shark calls %s (FMOD Ex %s)", n, started ? 1 : 5, strrchr(mp, '\\') ? strrchr(mp, '\\') + 1 : mp, fm ? (verstr(modver(fm), g_fmver), g_fmver) : "?");
}

// ---------------------------------------------------------------- Shark: its own trace lines (buffer overflows, device list) and fatal errors
typedef void (__cdecl *trace_t)(const char *, ...); static trace_t o_trace, o_fatal;
static volatile LONG c_ovf_stream, c_ovf_static, c_trace_drop, tr_n; static volatile DWORD tr_sec;
static void __cdecl hk_trace(const char *fmt, ...) {
    char b[600]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt ? fmt : "", a); va_end(a);
    o_trace("%s", b);
    if (strstr(b, "streaming buffer overflow")) InterlockedIncrement(&c_ovf_stream);
    else if (strstr(b, "static buffer overflow")) InterlockedIncrement(&c_ovf_static);
    DWORD s = GetTickCount() / 1000; if (s != tr_sec) { tr_sec = s; tr_n = 0; }
    if (InterlockedIncrement(&tr_n) <= 6) ev("Shark: %s", b); else InterlockedIncrement(&c_trace_drop);
}
static void __cdecl hk_fatal(const char *fmt, ...) {
    char b[600]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt ? fmt : "", a); va_end(a);
    ev("Shark FATAL: %s", b);
    o_fatal("%s", b);
}

// ---------------------------------------------------------------- Shark thread tick: queue sizes, processing time
typedef void (__stdcall *smupd_t)(void *, float);
static volatile LONG c_ticks, c_tick_us, c_tick_max, c_msgs, c_msgs_cl, c_msgs_cs, c_batch_max;
static char __fastcall hk_tick(void *self, void *edx, float dt) {
    LONGLONG t0 = qpc(); LONG all = 0;
    for (int i = 0; i < 7; i++) { void **q = *(void ***)SR(q_rva[i]); if (q) { LONG n = (LONG)(UINT_PTR)q[1]; if (n > 0 && n < 10000000) { all += n; if (i == 4) c_msgs_cl += n; else if (i == 1) c_msgs_cs += n; } } }
    c_msgs += all; lmax(&c_batch_max, all);
    void *sm = *(void **)SR(SM_PTR);
    if (sm) ((smupd_t)SR(SM_UPD))(sm, dt);
    LONG d = us(qpc() - t0); c_ticks++; c_tick_us += d; lmax(&c_tick_max, d);
    return 1;
}
static int hook_tick(void) {   // jmp over the tick's first instruction (mov eax,[imm32], 5 bytes): one atomic 8-byte write
    BYTE *t = SR(TICK); DWORD o;
    if (!code_is(TICK, "a1") || !imm_is(TICK + 1, SM_PTR) || ((UINT_PTR)t & 7) || !VirtualProtect(t, 8, PAGE_EXECUTE_READWRITE, &o)) return 0;
    LONGLONG old = *(volatile LONGLONG *)t, nw = old; BYTE *p = (BYTE *)&nw;
    p[0] = 0xE9; *(DWORD *)(p + 1) = (DWORD)((BYTE *)hk_tick - (t + 5));
    int ok = InterlockedCompareExchange64((volatile LONGLONG *)t, nw, old) == old;
    VirtualProtect(t, 8, o, &o); FlushInstructionCache(GetCurrentProcess(), t, 8);
    return ok;
}
// the sample loader's buffer pools (FUN_10033410's this): +8/+0xC free static buffers, +0x14/+0x18 free streaming buffers (stacks of pointers)
typedef void (__fastcall *bufpass_t)(void *, void *, void *); static bufpass_t o_bufpass; static BYTE *volatile g_bufmgr;
static void __fastcall hk_bufpass(void *self, void *edx, void *a) { g_bufmgr = self; o_bufpass(self, edx, a); }
static int pool_free(BYTE *m, int off) {
    if (!m || IsBadReadPtr(m + off, 8)) return -1;
    DWORD b = *(DWORD *)(m + off), e = *(DWORD *)(m + off + 4);
    return e >= b && e - b < 4096 * 4 ? (int)((e - b) / 4) : -1;
}

// ---------------------------------------------------------------- frames (DOW2 calls shark::update once a frame) and matches (SimEngine)
#define IAT_SHARK_UPD 0x00F88068
#define IAT_SIM_PRE   0x00F88154   // World::SimulatePre  - once per match, at its start
#define IAT_SIM       0x00F885E4   // World::Simulate     - every sim tick (100 ms of game time)
#define IAT_SIM_POST  0x00F88100   // World::SimulatePost - once per match, at its end
#define IAT_GAME_OVER 0x00F88498   // SimManager::SetGameOver
#define G_SESSION     0x01335678   // *(*G_SESSION + 4) & 0x558: online (live multiplayer)
#define G_SETTINGS    0x01335A78   // +0x118 game type (4 skirmish, 0x800 playback)
typedef void (__stdcall *supd_t)(float); static supd_t o_supd; static volatile LONG c_frames;
static void __stdcall hk_supd(float dt) { c_frames++; o_supd(dt); }
typedef void (__fastcall *thisfn_t)(void *, void *); static thisfn_t o_pre, o_sim, o_post, o_over;
typedef void *(__fastcall *getpm_t)(void *, void *); typedef unsigned (__fastcall *plcount_t)(void *, void *);
static getpm_t p_getpm; static plcount_t p_plcount;
static void *volatile g_world; static volatile LONG g_match, g_inmatch, g_over, g_mticks, g_players = -1, g_pl_logged;
static DWORD session_flags(void) {
    DWORD *g = (DWORD *)G_SESSION;
    if (IsBadReadPtr(g, 4) || !*g || IsBadReadPtr((void *)(*g + 4), 4)) return 0;
    return *(DWORD *)(*g + 4);
}
static DWORD game_type(void);
static const char *mode_str(void) { return game_type() == 0x800 ? "replay" : session_flags() & 0x558 ? "online" : "offline"; }
static DWORD game_type(void) { DWORD *g = (DWORD *)G_SETTINGS; return !IsBadReadPtr(g, 4) && *g && !IsBadReadPtr((void *)(*g + 0x118), 4) ? *(DWORD *)(*g + 0x118) : 0; }
static void __fastcall hk_pre(void *w, void *e) {
    g_world = w; g_mticks = 0; g_players = -1; g_pl_logged = 0; g_over = 0; g_inmatch = 1; LONG m = InterlockedIncrement(&g_match);
    ev("match %ld of this game session starts (World %p, %s, game type %lX)", m, w, mode_str(), game_type());
    o_pre(w, e);
}
static void __fastcall hk_sim(void *w, void *e) { if (w == g_world) g_mticks++; o_sim(w, e); }
static void __fastcall hk_post(void *w, void *e) {
    if (g_inmatch) ev("match %ld ends: %ld:%02ld of game time, %ld players", g_match, g_mticks / 600, g_mticks / 10 % 60, g_players);
    g_inmatch = 0; o_post(w, e);
}
static void __fastcall hk_over(void *w, void *e) {
    if (!g_over && g_inmatch) ev("match %ld decided (game over) at %ld:%02ld of game time", g_match, g_mticks / 600, g_mticks / 10 % 60);
    g_over = 1; o_over(w, e);
}
static int players_now(void) {
    void *w = g_world; if (!w || !p_getpm || !p_plcount) return -1;
    void *pm = p_getpm(w, 0); if (!pm) return -1;
    unsigned n = p_plcount(pm, 0); return n <= 16 ? (int)n : -1;
}

// ---------------------------------------------------------------- Windows audio device changes (IMMNotificationClient)
static IMMDeviceEnumerator *g_en; static volatile LONG g_fmt_changed;
static void dev_name(LPCWSTR id, char *out, int n) {
    IMMDevice *d = NULL; IPropertyStore *ps = NULL; PROPVARIANT v; PropVariantInit(&v);
    snprintf(out, n, "%ls", id ? id : L"(none)");
    if (id && g_en && IMMDeviceEnumerator_GetDevice(g_en, id, &d) >= 0) {
        if (IMMDevice_OpenPropertyStore(d, STGM_READ, &ps) >= 0) {
            if (IPropertyStore_GetValue(ps, &my_PKEY_FriendlyName, &v) >= 0 && v.vt == VT_LPWSTR) WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, out, n, NULL, NULL);
            PropVariantClear(&v); IPropertyStore_Release(ps);
        }
        IMMDevice_Release(d);
    }
}
static HRESULT STDMETHODCALLTYPE nc_qi(IMMNotificationClient *t, REFIID r, void **o) {
    if (IsEqualIID(r, &my_IID_IUnknown) || IsEqualIID(r, &my_IID_IMMNotificationClient)) { *o = t; return S_OK; } *o = NULL; return E_NOINTERFACE; }
static ULONG STDMETHODCALLTYPE nc_ref(IMMNotificationClient *t) { return 1; }
static HRESULT STDMETHODCALLTYPE nc_state(IMMNotificationClient *t, LPCWSTR id, DWORD st) {
    char nm[256]; dev_name(id, nm, sizeof nm);
    ev("Windows audio device %s: %s", nm, st == DEVICE_STATE_ACTIVE ? "active" : st == DEVICE_STATE_DISABLED ? "disabled" : st == DEVICE_STATE_NOTPRESENT ? "not present" : st == DEVICE_STATE_UNPLUGGED ? "unplugged" : "state ?");
    return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_add(IMMNotificationClient *t, LPCWSTR id) { char nm[256]; dev_name(id, nm, sizeof nm); ev("Windows audio device added: %s", nm); return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_rem(IMMNotificationClient *t, LPCWSTR id) { char nm[256]; dev_name(id, nm, sizeof nm); ev("Windows audio device removed: %s", nm); return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_def(IMMNotificationClient *t, EDataFlow f, ERole r, LPCWSTR id) {
    if (f != eRender || r != eConsole) return S_OK;
    char nm[256]; dev_name(id, nm, sizeof nm); ev("Windows default audio output -> %s", nm); g_fmt_changed = 1; return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_prop(IMMNotificationClient *t, LPCWSTR id, const PROPERTYKEY k) {
    if (IsEqualGUID(&k.fmtid, &my_PKEY_AudioEngine) && (k.pid == 0 || k.pid == 3)) {   // the device's format (sample rate) was changed
        char nm[256]; dev_name(id, nm, sizeof nm); ev("Windows audio device %s: its %s format was changed", nm, k.pid ? "OEM" : "default"); g_fmt_changed = 1;
    }
    return S_OK; }
static IMMNotificationClientVtbl nc_vt = {nc_qi, nc_ref, nc_ref, nc_state, nc_add, nc_rem, nc_def, nc_prop};
static IMMNotificationClient nc = {&nc_vt};
static DWORD WINAPI audio_watch(LPVOID _) {
    if (CoInitializeEx(NULL, COINIT_MULTITHREADED) < 0) return 0;
    if (CoCreateInstance(&my_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &my_IID_IMMDeviceEnumerator, (void **)&g_en) < 0 || !g_en) { ev("Windows audio device watch: not available"); return 0; }
    IMMDevice *d = NULL; char nm[256] = "(none)";
    if (IMMDeviceEnumerator_GetDefaultAudioEndpoint(g_en, eRender, eConsole, &d) >= 0 && d) {
        LPWSTR id = NULL; if (IMMDevice_GetId(d, &id) >= 0) { dev_name(id, nm, sizeof nm); CoTaskMemFree(id); } IMMDevice_Release(d);
    }
    HRESULT r = IMMDeviceEnumerator_RegisterEndpointNotificationCallback(g_en, &nc);
    query_mix();
    ev("Windows default audio output: %s (mix format %s); watching audio device changes%s", nm, g_mix_desc, r < 0 ? " - FAILED" : "");
    for (;;) Sleep(INFINITE);   // the callbacks come on the system's threads; this one keeps COM alive
    return 0;
}

// ---------------------------------------------------------------- where is the Shark thread (when it stops ticking)
static void modoff(DWORD a, char *out) {
    HMODULE m = NULL; char n[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(UINT_PTR)a, &m) && m) {
        GetModuleFileNameA(m, n, MAX_PATH); char *b = strrchr(n, '\\'); sprintf(out, "%s+%lX", b ? b + 1 : n, a - (DWORD)(UINT_PTR)m);
    } else sprintf(out, "%08lX", a);
}
static void thread_where(HANDLE h, const char *what) {
    CONTEXT c; memset(&c, 0, sizeof c); c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (!h || SuspendThread(h) == (DWORD)-1) return;
    char line[1400]; int n = 0;
    if (GetThreadContext(h, &c)) {
        char mo[300]; modoff(c.Eip, mo); n += snprintf(line + n, sizeof line - n, "%s: EIP %s |", what, mo);
        DWORD *sp = (DWORD *)(UINT_PTR)c.Esp; int k = 0;
        for (int i = 0; i < 512 && k < 12 && n < (int)sizeof line - 80; i++) {
            if (IsBadReadPtr(sp + i, 4)) break; DWORD v = sp[i]; HMODULE m;
            if (v > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(UINT_PTR)v, &m) && m) {
                modoff(v, mo); n += snprintf(line + n, sizeof line - n, " %s", mo); k++; }
        }
    } else snprintf(line, sizeof line, "%s: no context", what);
    ResumeThread(h);
    ev("%s", line);
}
static DWORD largest_free_mb(void) {
    MEMORY_BASIC_INFORMATION mb; BYTE *a = (BYTE *)0x10000; SIZE_T big = 0;
    while ((UINT_PTR)a < 0xFFFF0000u && VirtualQuery(a, &mb, sizeof mb)) {
        if (mb.State == MEM_FREE && mb.RegionSize > big) big = mb.RegionSize;
        BYTE *n = (BYTE *)mb.BaseAddress + mb.RegionSize; if (n <= a) break; a = n;
    }
    return (DWORD)(big >> 20);
}

static volatile LONG g_setup_done;
static void shark_setup(void) {   // FMOD library + FMOD/trace hooks: as early as possible (DllMain), else from the worker
    if (InterlockedExchange(&g_setup_done, 1)) return;
    fmod_stock();
    if (!shark_known()) return;
    fmod_hooks();
    hook_import(SB, "Debug.dll", "?TimeStampedTracef@dbInternal@@YAXPBDZZ", (void *)hk_trace, (void **)&o_trace);
    hook_import(SB, "Debug.dll", "?Fatalf@dbInternal@@YAXPBDZZ", (void *)hk_fatal, (void **)&o_fatal);
}

// ---------------------------------------------------------------- the worker: hooks that need the game running, then one line a second
static DWORD WINAPI worker(LPVOID _) {
    for (int i = 0; i < 600 && !SB; i++) { SB = (BYTE *)GetModuleHandleA("Shark.dll"); if (!SB) Sleep(100); }
    if (!SB) { ev("Shark.dll not loaded - nothing to watch"); return 0; }
    shark_setup();
    int known = shark_known();
    int fr = exe_hook(IAT_SHARK_UPD, "Shark.dll", hk_supd, (void **)&o_supd);
    HMODULE se = NULL; int pre = 0, sim = 0, post = 0, over = 0;
    for (int i = 0; i < 120 && !(se = GetModuleHandleA("SimEngine.dll")); i++) Sleep(1000);
    if (se) {
        p_getpm = (getpm_t)GetProcAddress(se, "?GetPlayerManager@World@@QAEPAVPlayerManager@@XZ");
        p_plcount = (plcount_t)GetProcAddress(se, "?GetPlayerCount@PlayerManager@@QBEIXZ");
        sim = exe_hook(IAT_SIM, "SimEngine.dll", hk_sim, (void **)&o_sim);
        pre = exe_hook(IAT_SIM_PRE, "SimEngine.dll", hk_pre, (void **)&o_pre);
        post = exe_hook(IAT_SIM_POST, "SimEngine.dll", hk_post, (void **)&o_post);
        over = exe_hook(IAT_GAME_OVER, "SimEngine.dll", hk_over, (void **)&o_over);
    }
    int tick = 0, buf = 0;
    if (known) {
        for (int i = 0; i < 3000; i++) { void **q = *(void ***)SR(q_rva[4]); void **q2 = *(void ***)SR(q_rva[1]); if (q && q2) break; Sleep(100); }
        tick = hook_tick();
        DWORD o; void **vs = (void **)SR(BUF_VT);
        if (*vs == (void *)SR(BUF_FN) && VirtualProtect(vs, 4, PAGE_READWRITE, &o)) { o_bufpass = (bufpass_t)*vs; *vs = (void *)hk_bufpass; VirtualProtect(vs, 4, o, &o); buf = 1; }
    }
    ev("watching: frames %s, match start/tick/end/game over %s/%s/%s/%s, Shark tick %s, buffer pools %s, Shark trace %s, FMOD update %s",
       fr ? "yes" : "NO", pre ? "yes" : "NO", sim ? "yes" : "NO", post ? "yes" : "NO", over ? "yes" : "NO", tick ? "yes" : "NO", buf ? "yes" : "NO",
       o_trace ? "yes" : "NO", o_fupd ? "yes" : "NO");
    QueryPerformanceFrequency(&g_qf);
    int st_cap = 0, sm_cap = 0, stuck = 0; DWORD vfree = largest_free_mb();
    // the minute's aggregates (the events file gets one line a minute)
    LONG m_fmin = 1 << 30, m_fmax = 0, m_fsum = 0, m_busy = 0, m_msg = 0, m_ch = 0, m_virt = 0, m_ovf = 0, m_n = 0; LONG m_tickmin = 1 << 30;
    for (int sec = 1;; sec++) {
        want_stats = 1;
        Sleep(1000);
        LONG f = InterlockedExchange(&c_frames, 0), tk = InterlockedExchange(&c_ticks, 0), tus = InterlockedExchange(&c_tick_us, 0), tmx = InterlockedExchange(&c_tick_max, 0);
        LONG ms = InterlockedExchange(&c_msgs, 0), mcl = InterlockedExchange(&c_msgs_cl, 0), mcs = InterlockedExchange(&c_msgs_cs, 0), bmx = InterlockedExchange(&c_batch_max, 0);
        LONG fu = InterlockedExchange(&c_fupd, 0), fus = InterlockedExchange(&c_fupd_us, 0), fmx = InterlockedExchange(&c_fupd_max, 0);
        LONG os = InterlockedExchange(&c_ovf_stream, 0), ost = InterlockedExchange(&c_ovf_static, 0), drop = InterlockedExchange(&c_trace_drop, 0);
        LONG pend = 0; if (known) for (int i = 0; i < 7; i++) { void **q = *(void ***)SR(q_rva[i]); if (q) pend += (LONG)(UINT_PTR)q[1]; }
        int cur = -1, mx = -1; if (p_mem) p_mem(&cur, &mx, 0);
        int sfree = pool_free(g_bufmgr, 0x14), stfree = pool_free(g_bufmgr, 0x08);
        if (sfree > sm_cap) sm_cap = sfree;
        if (stfree > st_cap) st_cap = stfree;
        PROCESS_MEMORY_COUNTERS_EX pm = {sizeof pm}; GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pm, sizeof pm);
        if (sec % 10 == 0) vfree = largest_free_mb();
        if (g_inmatch && !g_pl_logged && g_mticks > 20) {
            g_players = players_now(); g_pl_logged = 1;
            ev("match %ld: %ld players, %s, game type %lX", g_match, g_players, mode_str(), game_type());
        }
        if (g_fmt_changed) { g_fmt_changed = 0; int mix = query_mix(); ev("Windows default output mix format now %s; FMOD mixes at %d Hz%s", g_mix_desc, g_fmod_rate, rate_cmp(mix, g_fmod_rate)); }
        char where[48];
        if (g_inmatch) snprintf(where, sizeof where, "m%ld %ldp %ld:%02ld%s", g_match, g_players, g_mticks / 600, g_mticks / 10 % 60, g_over ? " over" : "");
        else snprintf(where, sizeof where, "menu (%ld matches)", g_match);
        sl("%s | fps %ld | shark %ld/s busy %.2f/%.2f ms msgs %ld (CL %ld CS %ld) batch %ld pend %ld | fmod %.2f/%.2f ms ch %ld real %ld virt %ld cpu %.1f%% mem %d/%d KB | "
           "stream %d/%d static %d/%d ovf %ld/%ld | priv %lu MB va %lu MB%s",
           where, f, tk, tk ? tus / 1000.0 / tk : 0.0, tmx / 1000.0, ms, mcl, mcs, bmx, pend, fu ? fus / 1000.0 / fu : 0.0, fmx / 1000.0, s_chp, s_real, s_virt, s_cpu,
           cur / 1024, mx / 1024, sfree < 0 ? -1 : sm_cap - sfree, sm_cap, stfree < 0 ? -1 : st_cap - stfree, st_cap, os, ost,
           (unsigned long)(pm.PagefileUsage >> 20), (unsigned long)vfree, drop ? " (Shark lines not logged: more than 6 a second)" : "");
        if (os || ost) ev("Shark buffer overflow this second: streaming %ld, static %ld (streaming %d of %d in use)", os, ost, sfree < 0 ? -1 : sm_cap - sfree, sm_cap);
        // the Shark thread stopped ticking while the game draws frames: where is it
        if (tick && f > 0 && tk == 0) { if (++stuck <= 3) { ev("Shark thread did NOT tick this second (%ld frames)", f); thread_where(*(HANDLE *)SR(SH_THR), "Shark thread"); } }
        else { if (stuck > 3) ev("Shark thread ticks again after %d seconds", stuck); stuck = 0; }
        if (sec == 60) {
            if (!g_d3d_seen) { char c[160]; dxvk_conf_line(c, sizeof c);
                ev("Direct3D was created before SoundBugTest loaded (Elite loads its add-ons late): frame cap not changed (%s%s)", c, is_dxvk(GetModuleHandleA("d3d9.dll")) ? ", DXVK" : ", no DXVK: no cap"); }
            if (GetModuleHandleA("SoundFix.dll")) ev("SoundFix.dll was loaded after SoundBugTest: it stays idle (SoundBugTest holds its mutex)");
        }
        // the minute line
        m_n++; if (f < m_fmin) m_fmin = f; if (f > m_fmax) m_fmax = f; m_fsum += f; if (tmx > m_busy) m_busy = tmx; if (ms > m_msg) m_msg = ms;
        if (s_chp > m_ch) m_ch = s_chp; if (s_virt > m_virt) m_virt = s_virt; m_ovf += os + ost; if (tk < m_tickmin) m_tickmin = tk;
        if (sec % 60 == 0) {
            dg("minute: %s | fps %ld..%ld (avg %ld) | shark ticks/s min %ld, busy max %.1f ms, msgs/s max %ld | fmod ch max %ld virt max %ld mem %d KB | overflows %ld | priv %lu MB va %lu MB | FMOD %s, Shark %s",
               where, m_fmin, m_fmax, m_fsum / m_n, m_tickmin, m_busy / 1000.0, m_msg, m_ch, m_virt, cur / 1024, m_ovf, (unsigned long)(pm.PagefileUsage >> 20), (unsigned long)vfree,
               g_fmver, known ? (*SR(VCHECK) == 0x85 ? "jne" : "jl") : "?");
            m_fmin = 1 << 30; m_fmax = m_fsum = m_busy = m_msg = m_ch = m_virt = m_ovf = m_n = 0; m_tickmin = 1 << 30;
        }
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID _) {
    if (r != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    char m[48]; snprintf(m, sizeof m, "SoundBugTest-%lu", GetCurrentProcessId());
    CreateMutexA(NULL, FALSE, m); if (GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;   // loaded twice (dow2-patches and Elite_DLLs): the first one works
    InitializeCriticalSection(&g_lcs);
    QueryPerformanceFrequency(&g_qf);
    GetModuleFileNameA(h, g_dir, MAX_PATH); char *e = strrchr(g_dir, '\\'); if (e) e[1] = 0;
    snprintf(g_lp, MAX_PATH, "%sSoundBugTest.log", g_dir); snprintf(g_lp1, MAX_PATH, "%sSoundBugTest.1.log", g_dir);
    snprintf(g_ep, MAX_PATH, "%sSoundBugTest_events.log", g_dir); snprintf(g_ep1, MAX_PATH, "%sSoundBugTest_events.1.log", g_dir);
    const char *cl = GetCommandLineA(); int elite = cl && strstr(cl, "-modname");
    RTL_OSVERSIONINFOW ov = {sizeof ov}; typedef LONG (WINAPI *rgv_t)(RTL_OSVERSIONINFOW *);
    rgv_t rgv = (rgv_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion"); if (rgv) rgv(&ov);
    char fv[32]; verstr(modver(GetModuleHandleA("fmodex.dll")), fv);
    SB = (BYTE *)GetModuleHandleA("Shark.dll");
    snprintf(g_sess, sizeof g_sess, "[SoundBugTest %s, %s, pid %lu]", SBT_VERSION, elite ? "Elite" : "vanilla", GetCurrentProcessId());
    ev("===== SoundBugTest %s (TEST: the sound bug worst case + this log) loaded from %s; game %s; Windows %lu.%lu build %lu; fmodex.dll %s; Shark %s, version check %s, FMOD %s",
       SBT_VERSION, g_dir, elite ? "Elite (-modname)" : "vanilla", ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber, fv, SB ? "loaded" : "not loaded yet",
       SB ? (*SR(VCHECK) == 0x85 ? "jne (stock)" : *SR(VCHECK) == 0x8c ? "jl (SMOL)" : "?") : "-", SB ? (*(DWORD *)SR(DRV_INST) ? "already started" : "not started yet") : "-");
    // SoundFix (merges Shark's queues, FMOD 4.32.20) is the opposite of this test: never both. Its mutex held here keeps a later SoundFix idle.
    snprintf(m, sizeof m, "SoundFix-%lu", GetCurrentProcessId());
    HANDLE sf = CreateMutexA(NULL, FALSE, m); DWORD le = GetLastError();
    if (GetModuleHandleA("SoundFix.dll") || le == ERROR_ALREADY_EXISTS) {
        ev("SoundFix.dll is already working in this game - SoundBugTest stays OFF (it tests the game WITHOUT SoundFix: turn SoundFix off in the launcher)");
        if (sf) CloseHandle(sf);
        return TRUE;
    }
    dxvk_env();
    HMODULE sp = GetModuleHandleA("Spooge.dll");
    if (sp && !hook_import((BYTE *)sp, "d3d9.dll", "Direct3DCreate9", (void *)hk_d3dcreate, (void **)&o_d3dcreate)) ev("Spooge's Direct3DCreate9 import not found - frame cap only through DXVK_CONFIG");
    if (SB) shark_setup();
    snprintf(g_sess, sizeof g_sess, "[SoundBugTest %s, %s, pid %lu, FMOD %s, Shark %s]", SBT_VERSION, elite ? "Elite" : "vanilla", GetCurrentProcessId(), g_fmver,
             SB ? (*SR(VCHECK) == 0x85 ? "jne" : "jl") : "?");
    ev("worst case %s: FMOD Ex %s + Shark version check %s, Shark's queues not merged, frame cap removed where Direct3D comes later", g_worst_fmod ? "ACTIVE" : "NOT complete",
       g_fmver, SB ? (*SR(VCHECK) == 0x85 ? "jne (stock)" : "jl (SMOL)") : "?");
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, audio_watch, NULL, 0, NULL));
    return TRUE;
}
