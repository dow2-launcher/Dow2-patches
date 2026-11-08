// SoundFix.dll 1 - hardening against the Dawn of War II "sound bug" (late big fights at high FPS: sound stutters, one sound
// loops, unit responses vanish, then the game dies) and no frame cap while the add-on is on. Three parts, one tick:
//
// 1. Shark's sound queues do not grow with the frame rate.
//  Every rendered frame DOW2 pushes, for every live sound emitter, Container::setPosition / setOrientation / setVelocity /
//  setVolume and ControlSource::setData into Shark's message queues (Shark+0x6abfc / +0x6a1b8, one heap message each); the
//  Shark thread empties them only 20 times a second and applies every message, so its work is linear in the FPS. Here a newer
//  value REPLACES the payload of the not-yet-processed message for the same emitter and field instead of queueing another
//  one (under Shark's own queue mutex). play / stop / free are never touched and act as barriers. Measured on the stand at
//  150-345 fps: 2-3 thousand queued messages a second instead of ~33 thousand. Sound is not part of the simulation.
//
// 2. FMOD Ex 4.32.20 instead of Relic's 4.32.3 (the community "SMOL" fix; Elite ships the same file as Elite_bin/Elite_fmodex.bin
//  plus a Shark.dll with one byte changed). For vanilla: DOW2.exe imports Shark.dll and Shark imports fmodex.dll, so 4.32.3 is
//  mapped before any add-on. Before Shark starts FMOD (Shark's Driver::m_instance still 0) SoundFix loads SoundFix_fmodex.bin
//  (that 4.32.20 file) and points all of Shark's fmodex imports at it, then changes Shark's FMOD version check in memory
//  (Shark+0x39079 jne -> jl, the SMOL byte). No game file is changed. Already 4.32.20 (Elite) -> nothing to do.
//
// 3. No frame cap: DXVK reads DXVK_CONFIG when the game creates Direct3D; SoundFix sets d3d9/dxgi.maxFrameRate there (it
//  overrides dxvk.conf, e.g. the "maxFrameRate = 60" Elite.exe writes with IsDXVK=true). The game itself has no limiter
//  (app_fixedframerate is a dev-only frame-time setting, default off). Vertical sync stays the player's choice in the options.
//  SoundFix.ini [SoundFix] FpsCap=0 (default, no cap) or N: then DXVK caps at N and, without DXVK, SoundFix waits in Present.
//
// 4. Diagnostics only (no change): the game's FMOD format (Shark passes the "soundfrequency" setting of configuration.lua, 44100 by
//  default, to System::setSoftwareFormat; DSP buffer 2048 x 4), the default output's shared-mode mix format (IAudioClient::GetMixFormat
//  = Control Panel -> device -> Advanced), the output type and rate FMOD really runs at, every Windows audio device change
//  (added/removed/state/default) and Direct3D device lost/back - so player logs show them.
//
// Loads as an Elite_DLLs add-on (Profiler) or from dow2-patches (vanilla). Log: SoundFix.log next to the DLL.
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define SOUNDFIX_VERSION "1"
static char g_dir[MAX_PATH], g_log[MAX_PATH];   // g_dir: folder of the DLL with the trailing backslash
static int opt_fps, opt_fmod = 1, opt_merge = 1;
static void logmsg(const char *fmt, ...) {
    FILE *f = fopen(g_log, "a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d.%02d %02d:%02d:%02d ", t.wDay, t.wMonth, t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---- Shark.dll 3.19 layout (checked at start-up against the code bytes; any mismatch = SoundFix stays off)
static BYTE *SB;
#define SR(rva) ((BYTE *)SB + (rva))
#define Q_CL     0x6abfc   // write-queue pointer of InterlockedDispatcher<ContainerLogic>
#define MX_CL    0x7511c   // its RecursiveMutex
#define Q_CS     0x6a1b8   // write-queue pointer of InterlockedDispatcher<ControlSourceLogic>
#define MX_CS    0x70db4
#define VT_V3    0x523c0   // InterlockedCall<ContainerLogic>::ConstReferenceMessage<Vector3f>
#define VT_V3X2  0x523cc   // ...ConstReference2Message<Vector3f,Vector3f>
#define VT_F     0x523b4   // ...SimpleMessage<float>
#define VT_CS    0x4dc80   // InterlockedCall<ControlSourceLogic>::Simple2Message<unsigned,float>
#define FN_POS   0x42010   // ContainerLogic::setPosition
#define FN_ORI   0x42050   // ContainerLogic::setOrientation
#define FN_VEL   0x420a0   // ContainerLogic::setVelocity
#define FN_VOL   0x41f50   // ContainerLogic::setVolume
#define FN_DATA  0x11280   // ControlSourceLogic::setData
#define DRV_INST 0x6bda8   // shark::Driver::m_instance (FMODDriver::initialize sets it first, before any FMOD call)
// DOW2.exe 3.19 import slots of Shark.dll (fixed base 0x400000)
#define IAT_SETPOS  0xF87FC0
#define IAT_SETVEL  0xF87FB8
#define IAT_SETORI  0xF87FBC
#define IAT_SETVOL  0xF88098
#define IAT_SETDATA 0xF87FB0
#define IAT_CPLAY   0xF88078
#define IAT_CSTOP   0xF87FD0
#define IAT_FREE    0xF87FCC
#define IAT_PFREE   0xF88084

typedef void (__fastcall *mx_t)(void *, void *);
static mx_t mx_enter, mx_leave;

enum { K_POS, K_ORI, K_VEL, K_VOL, K_DATA };
typedef struct { void *obj; DWORD kind; void **q; LONG idx; DWORD *msg; } slot_t;
#define NSLOT 16384
static slot_t g_slot[NSLOT];
static volatile LONG n_merged, n_queued, n_barrier;

static DWORD hkey(void *obj, DWORD kind) { DWORD h = (DWORD)(UINT_PTR)obj * 2654435761u ^ kind * 0x9E3779B9u; return (h >> 7) & (NSLOT - 1); }
static slot_t *slot_get(void *obj, DWORD kind) {
    DWORD h = hkey(obj, kind);
    for (int i = 0; i < 4; i++) { slot_t *s = &g_slot[(h + i) & (NSLOT - 1)]; if (s->obj == obj && s->kind == kind) return s; }
    return NULL;
}
static slot_t *slot_put(void *obj, DWORD kind) {
    DWORD h = hkey(obj, kind); slot_t *free_s = NULL;
    for (int i = 0; i < 4; i++) { slot_t *s = &g_slot[(h + i) & (NSLOT - 1)]; if (s->obj == obj && s->kind == kind) return s; if (!s->obj && !free_s) free_s = s; }
    return free_s ? free_s : &g_slot[h];   // evicting an entry only loses a merge chance, never correctness
}
// The message still waits, unprocessed, in the current write queue at the remembered place?
static DWORD *pending(slot_t *s, DWORD qrva, DWORD vt, int objoff, void *obj, DWORD fn) {
    if (!s || !s->msg) return NULL;
    void **q = *(void ***)SR(qrva);
    if (q != s->q || s->idx >= (LONG)(UINT_PTR)q[1]) return NULL;
    DWORD **data = (DWORD **)q[0];
    DWORD *m = data[s->idx];
    if (m != s->msg || m[0] != (DWORD)(UINT_PTR)SR(vt) || m[objoff / 4] != (DWORD)(UINT_PTR)obj || m[objoff / 4 + 1] != (DWORD)(UINT_PTR)SR(fn)) return NULL;
    return m;
}
static void remember(void *obj, DWORD kind, DWORD qrva, DWORD vt, int objoff, DWORD fn) {
    void **q = *(void ***)SR(qrva); LONG n = (LONG)(UINT_PTR)q[1]; if (n <= 0) return;
    DWORD *m = ((DWORD **)q[0])[n - 1];
    if (!m || m[0] != (DWORD)(UINT_PTR)SR(vt) || m[objoff / 4] != (DWORD)(UINT_PTR)obj || m[objoff / 4 + 1] != (DWORD)(UINT_PTR)SR(fn)) return;
    slot_t *s = slot_put(obj, kind); s->obj = obj; s->kind = kind; s->q = q; s->idx = n - 1; s->msg = m;
}
static void barrier(void *obj) {   // a play/stop/free for this container: later values must queue behind it
    for (DWORD k = K_POS; k <= K_VOL; k++) { slot_t *s = slot_get(obj, k); if (s) s->msg = NULL; }
    InterlockedIncrement(&n_barrier);
}

// ---- hooks (thiscall exports of Shark called by DOW2; __fastcall(ecx, edx, ...) has the same ABI)
typedef void (__fastcall *v3_t)(void *, void *, const float *);
typedef void (__fastcall *v3x2_t)(void *, void *, const float *, const float *);
typedef void (__fastcall *f_t)(void *, void *, float);
typedef void (__fastcall *uf_t)(void *, void *, unsigned, float);
typedef void (__fastcall *cplay_t)(void *, void *, const float *);
typedef void (__fastcall *cstop_t)(void *, void *, int);
typedef void (__stdcall *free_t)(void **, int);
typedef void (__fastcall *pfree_t)(void *, void *, void **, int);
static v3_t o_setpos, o_setvel; static v3x2_t o_setori; static f_t o_setvol; static uf_t o_setdata;
static cplay_t o_cplay; static cstop_t o_cstop; static free_t o_free; static pfree_t o_pfree;

#define CL_OBJ(c) (*(void **)((BYTE *)(c) + 0x20))   // Container -> ContainerLogic (the message target)
static void v3_common(void *c, const float *v, DWORD kind, DWORD fn, v3_t orig) {
    void *obj = CL_OBJ(c); void *mx = SR(MX_CL);
    mx_enter(mx, 0);
    DWORD *m = pending(slot_get(obj, kind), Q_CL, VT_V3, 0x18, obj, fn);
    if (m) { m[2] = ((const DWORD *)v)[0]; m[3] = ((const DWORD *)v)[1]; m[4] = ((const DWORD *)v)[2]; n_merged++; }
    else { orig(c, 0, v); remember(obj, kind, Q_CL, VT_V3, 0x18, fn); n_queued++; }
    mx_leave(mx, 0);
}
static void __fastcall hk_setpos(void *c, void *e, const float *v) { v3_common(c, v, K_POS, FN_POS, o_setpos); }
static void __fastcall hk_setvel(void *c, void *e, const float *v) { v3_common(c, v, K_VEL, FN_VEL, o_setvel); }
static void __fastcall hk_setori(void *c, void *e, const float *a, const float *b) {
    void *obj = CL_OBJ(c); void *mx = SR(MX_CL);
    mx_enter(mx, 0);
    DWORD *m = pending(slot_get(obj, K_ORI), Q_CL, VT_V3X2, 0x20, obj, FN_ORI);
    if (m) { memcpy(&m[2], a, 12); memcpy(&m[5], b, 12); n_merged++; }
    else { o_setori(c, 0, a, b); remember(obj, K_ORI, Q_CL, VT_V3X2, 0x20, FN_ORI); n_queued++; }
    mx_leave(mx, 0);
}
static void __fastcall hk_setvol(void *c, void *e, float v) {
    void *obj = CL_OBJ(c); void *mx = SR(MX_CL);
    mx_enter(mx, 0);
    DWORD *m = pending(slot_get(obj, K_VOL), Q_CL, VT_F, 0x0c, obj, FN_VOL);
    if (m) { memcpy(&m[2], &v, 4); n_merged++; }
    else { o_setvol(c, 0, v); remember(obj, K_VOL, Q_CL, VT_F, 0x0c, FN_VOL); n_queued++; }
    mx_leave(mx, 0);
}
// ControlSource::setData(idx, value): target = ControlSource+4, one slot per (target, idx)
static void __fastcall hk_setdata(void *cs, void *e, unsigned idx, float v) {
    void *obj = *(void **)((BYTE *)cs + 4); void *mx = SR(MX_CS); DWORD kind = K_DATA + (idx << 4);
    mx_enter(mx, 0);
    slot_t *s = slot_get(obj, kind);
    DWORD *m = pending(s, Q_CS, VT_CS, 0x10, obj, FN_DATA);
    if (m && m[2] == idx) { memcpy(&m[3], &v, 4); n_merged++; }
    else { o_setdata(cs, 0, idx, v); remember(obj, kind, Q_CS, VT_CS, 0x10, FN_DATA); n_queued++; }
    mx_leave(mx, 0);
}
// barriers: clear the slots under the queue mutex, then call Shark without holding it (free/play take other Shark locks)
static void barrier_locked(void *c) { void *mx = SR(MX_CL); mx_enter(mx, 0); if (c) barrier(CL_OBJ(c)); mx_leave(mx, 0); }
static void __fastcall hk_cplay(void *c, void *e, const float *v) { barrier_locked(c); o_cplay(c, 0, v); }
static void __fastcall hk_cstop(void *c, void *e, int m) { barrier_locked(c); o_cstop(c, 0, m); }
static void *sp_container(void **sp) { return (sp && *sp) ? *(void **)*sp : NULL; }   // shared_ptr<PooledContainer> -> Container*
static void __stdcall hk_free(void **sp, int m) { barrier_locked(sp_container(sp)); o_free(sp, m); }
static void __fastcall hk_pfree(void *pool, void *e, void **sp, int m) { barrier_locked(sp_container(sp)); o_pfree(pool, 0, sp, m); }

// ---- install
static int code_is(DWORD rva, const char *hex) {   // compare Shark code bytes
    BYTE *p = SR(rva);
    for (int i = 0; hex[i] && hex[i + 1]; i += 2) { unsigned b; sscanf(hex + i, "%2x", &b); if (*p++ != (BYTE)b) return 0; }
    return 1;
}
static int imm_is(DWORD rva, DWORD target_rva) { return *(DWORD *)SR(rva) == (DWORD)(UINT_PTR)SR(target_rva); }
static int patch(DWORD slot, void *fn, void **orig, DWORD expect_rva) {
    void **s = (void **)(UINT_PTR)slot; DWORD o;
    if (expect_rva && *s != (void *)SR(expect_rva)) { logmsg("import slot %08lx -> %p, expected Shark+%05lx: not hooked", slot, *s, expect_rva); return 0; }
    if (!VirtualProtect(s, 4, PAGE_READWRITE, &o)) return 0;
    *orig = *s; *s = fn; VirtualProtect(s, 4, o, &o); return 1;
}
static DWORD exp_rva(const char *name) { FARPROC p = GetProcAddress((HMODULE)SB, name); return p ? (DWORD)((BYTE *)p - SB) : 0; }

// the Shark.dll of Retribution 3.19 (Steam/GOG, and the same file with the SMOL byte as Elite ships it): exports and code we rely on
static int shark_known(void) {
    return exp_rva("?setPosition@Container@shark@@QBEXABVVector3f@Math@@@Z") == 0x3fae0
        && exp_rva("?setVelocity@Container@shark@@QBEXABVVector3f@Math@@@Z") == 0x3fb50
        && exp_rva("?setOrientation@Container@shark@@QBEXABVVector3f@Math@@0@Z") == 0x3fb10
        && exp_rva("?setVolume@Container@shark@@QBEXM@Z") == 0x3fab0
        && exp_rva("?setData@ControlSource@shark@@QAEXIM@Z") == 0x10b50
        && exp_rva("?initialize@FMODDriver@shark@@QAE_NABUSetupParams@12@@Z") == 0x38f90
        && code_is(0x3faf4, "c74424") && imm_is(0x3faf8, FN_POS)    // setPosition queues ContainerLogic::setPosition
        && code_is(0x3fb64, "c74424") && imm_is(0x3fb68, FN_VEL)    // setVelocity
        && code_is(0x10b6c, "c74424") && imm_is(0x10b70, FN_DATA)   // setData
        && code_is(0x45b30, "a1") && imm_is(0x45b31, 0x70360)       // Shark thread tick (owner of the queue swap) as analysed
        && code_is(0x38fb1, "833d") && imm_is(0x38fb3, DRV_INST)    // FMODDriver::initialize: if (Driver::m_instance) return
        && code_is(0x39071, "817c2428033204000f") && (*SR(0x3907a) == 0x85 || *SR(0x3907a) == 0x8c);   // FMOD version check
}

// ---- part 2: FMOD Ex 4.32.20 (SoundFix_fmodex.bin next to the DLL) for Shark, before Shark starts FMOD
static DWORD64 modver(HMODULE m) {   // FileVersion from the module's own version resource (4.32.3 = 0x0000000400200003)
    HRSRC r = m ? FindResourceA(m, MAKEINTRESOURCEA(1), (LPCSTR)RT_VERSION) : NULL; if (!r) return 0;
    DWORD n = SizeofResource(m, r); BYTE *p = (BYTE *)LockResource(LoadResource(m, r)); if (!p) return 0;
    for (DWORD i = 0; i + 16 <= n; i += 4) if (*(DWORD *)(p + i) == 0xFEEF04BD) return ((DWORD64)*(DWORD *)(p + i + 8) << 32) | *(DWORD *)(p + i + 12);
    return 0;
}
#define VER_43220 0x0000000400200014ull
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
static void fmod_upgrade(void) {
    char vs[32], vs2[32];
    HMODULE fm = GetModuleHandleA("fmodex.dll"); DWORD64 v = modver(fm); verstr(v, vs);
    if (!SB || !shark_known()) { logmsg("FMOD Ex: unknown Shark.dll build - left as it is (fmodex %s)", vs); return; }
    int smol = *SR(0x3907a) == 0x8c;
    if (v >= VER_43220) {   // Elite (Elite_fmodex.bin) or the player's own SMOL copy
        if (!smol && !*(DWORD *)SR(DRV_INST) && set_byte(SR(0x3907a), 0x8c)) smol = 2;   // its Shark would refuse 4.32.20 - silent game
        logmsg("FMOD Ex %s already in use - nothing to change%s", vs, smol == 2 ? " (Shark's version check jne -> jl in memory)" : "");
        return;
    }
    if (!opt_fmod) { logmsg("FMOD Ex %s (Fmodex=0 in the ini: not changed)", vs); return; }
    if (*(DWORD *)SR(DRV_INST)) { logmsg("FMOD Ex %s: the sound system has already started - too late to change it this time", vs); return; }
    char path[MAX_PATH]; snprintf(path, MAX_PATH, "%sSoundFix_fmodex.bin", g_dir);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { logmsg("FMOD Ex %s: no SoundFix_fmodex.bin next to the DLL - not changed", vs); return; }
    HMODULE nm = LoadLibraryA(path); DWORD64 nv = modver(nm); verstr(nv, vs2);
    if (!nm || nv != VER_43220) { logmsg("FMOD Ex: SoundFix_fmodex.bin %s (error %lu) - not changed", nm ? vs2 : "not loaded", GetLastError()); if (nm) FreeLibrary(nm); return; }
    IMAGE_IMPORT_DESCRIPTOR *d = import_of(SB, "fmodex.dll");
    if (!d || !d->OriginalFirstThunk) { logmsg("FMOD Ex: Shark has no fmodex.dll import - not changed"); FreeLibrary(nm); return; }
    IMAGE_THUNK_DATA32 *nt = (IMAGE_THUNK_DATA32 *)(SB + d->OriginalFirstThunk); void **iat = (void **)(SB + d->FirstThunk);
    static void *nf[256]; int n = 0;
    for (; nt[n].u1.AddressOfData; n++) {   // resolve all first: a missing one = change nothing
        if (n >= 256 || (nt[n].u1.Ordinal & IMAGE_ORDINAL_FLAG32)) { logmsg("FMOD Ex: unexpected Shark import table - not changed"); FreeLibrary(nm); return; }
        const char *name = (char *)((IMAGE_IMPORT_BY_NAME *)(SB + nt[n].u1.AddressOfData))->Name;
        if (!(nf[n] = (void *)GetProcAddress(nm, name))) { logmsg("FMOD Ex: %s missing in 4.32.20 - not changed", name); FreeLibrary(nm); return; }
    }
    DWORD o;
    if (!VirtualProtect(iat, n * 4, PAGE_READWRITE, &o)) { FreeLibrary(nm); return; }
    if (*(DWORD *)SR(DRV_INST)) { VirtualProtect(iat, n * 4, o, &o); logmsg("FMOD Ex: the sound system started meanwhile - not changed"); FreeLibrary(nm); return; }
    for (int i = 0; i < n; i++) iat[i] = nf[i];
    VirtualProtect(iat, n * 4, o, &o);
    if (!smol) set_byte(SR(0x3907a), 0x8c);
    logmsg("FMOD Ex %s instead of %s: Shark's %d fmodex imports -> SoundFix_fmodex.bin %p (the old file stays loaded, unused)%s",
           vs2, vs, n, nm, smol ? "" : "; Shark's FMOD version check jne -> jl in memory");
}

// ---- part 3: frame cap (DXVK_CONFIG before Direct3D is created; FpsCap > 0 also waits in Present without DXVK)
static void dxvk_env(void) {
    char old[2048], v[2400]; DWORD n = GetEnvironmentVariableA("DXVK_CONFIG", old, sizeof old); if (n >= sizeof old) n = 0; old[n] = 0;
    snprintf(v, sizeof v, "%s%sd3d9.maxFrameRate = %d; dxgi.maxFrameRate = %d", old, n ? "; " : "", opt_fps, opt_fps);
    SetEnvironmentVariableA("DXVK_CONFIG", v);
}
static LONGLONG g_qf, g_next; static void *g_present_user;   // which Present the game uses (the device's or the swap chain's)
static void frame_wait(void *who) {
    if (opt_fps <= 0) return;
    if (!g_present_user) g_present_user = who; else if (g_present_user != who) return;
    LARGE_INTEGER t; QueryPerformanceCounter(&t); LONGLONG per = g_qf / opt_fps;
    if (g_next && t.QuadPart < g_next) {
        LONGLONG ms = (g_next - t.QuadPart) * 1000 / g_qf; if (ms > 1) Sleep((DWORD)ms - 1);
        do { Sleep(0); QueryPerformanceCounter(&t); } while (t.QuadPart < g_next);
        g_next += per;
    } else g_next = t.QuadPart + per;   // first frame or late: start a new interval from now
}
typedef HRESULT (WINAPI *dpresent_t)(void *, const RECT *, const RECT *, HWND, const void *);
typedef HRESULT (WINAPI *spresent_t)(void *, const RECT *, const RECT *, HWND, const void *, DWORD);
typedef HRESULT (WINAPI *getsc_t)(void *, UINT, void **);
typedef ULONG (WINAPI *release_t)(void *);
typedef HRESULT (WINAPI *createdev_t)(void *, UINT, DWORD, HWND, DWORD, D3DPRESENT_PARAMETERS *, void **);
typedef void *(WINAPI *d3dcreate_t)(UINT);
static dpresent_t o_dpresent; static spresent_t o_spresent; static createdev_t o_createdev; static d3dcreate_t o_d3dcreate;
static volatile LONG g_lost;
static void present_result(HRESULT r) {   // device lost / back (Alt-Tab, mode change, driver reset) into the log, once per change
    if (r == D3DERR_DEVICELOST || r == D3DERR_DEVICEREMOVED) { if (!InterlockedExchange(&g_lost, 1)) logmsg("Direct3D: device %s (Present %08lx)", r == D3DERR_DEVICELOST ? "lost" : "REMOVED", (DWORD)r); }
    else if (r >= 0 && g_lost && InterlockedExchange(&g_lost, 0)) logmsg("Direct3D: presenting again");
}
static HRESULT WINAPI hk_dpresent(void *d, const RECT *a, const RECT *b, HWND w, const void *r) { frame_wait((void *)1); HRESULT h = o_dpresent(d, a, b, w, r); present_result(h); return h; }
static HRESULT WINAPI hk_spresent(void *s, const RECT *a, const RECT *b, HWND w, const void *r, DWORD f) { frame_wait((void *)2); HRESULT h = o_spresent(s, a, b, w, r, f); present_result(h); return h; }
static void vt_hook(void *obj, int idx, void *fn, void **orig) {   // once per implementation (vtable)
    void **vt = *(void ***)obj; DWORD o;
    if (vt[idx] == fn || !VirtualProtect(&vt[idx], 4, PAGE_READWRITE, &o)) return;
    if (!*orig) *orig = vt[idx];
    vt[idx] = fn; VirtualProtect(&vt[idx], 4, o, &o);
}
static HRESULT WINAPI hk_createdev(void *d3d, UINT ad, DWORD type, HWND w, DWORD fl, D3DPRESENT_PARAMETERS *pp, void **out) {
    HRESULT r = o_createdev(d3d, ad, type, w, fl, pp, out);
    if (r >= 0 && out && *out) {
        void *dev = *out, *sc = NULL;
        vt_hook(dev, 17, hk_dpresent, (void **)&o_dpresent);                                    // IDirect3DDevice9::Present
        if (((getsc_t)(*(void ***)dev)[14])(dev, 0, &sc) >= 0 && sc) {                         // GetSwapChain(0)
            vt_hook(sc, 3, hk_spresent, (void **)&o_spresent);                                  // IDirect3DSwapChain9::Present
            ((release_t)(*(void ***)sc)[2])(sc);
        }
        if (pp) logmsg("Direct3D device %ux%u %s%s", pp->BackBufferWidth, pp->BackBufferHeight, pp->Windowed ? "windowed" : "fullscreen", opt_fps ? "" : "");
        if (opt_fps) logmsg("frame cap %d fps (Present)", opt_fps);
    } else logmsg("CreateDevice failed %08lx", (DWORD)r);
    return r;
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
static volatile LONG g_d3d_seen;
static void *WINAPI hk_d3dcreate(UINT sdk) {
    void *d = o_d3dcreate(sdk);
    if (!InterlockedExchange(&g_d3d_seen, 1)) {
        HMODULE dx = GetModuleHandleA("d3d9.dll"); int dxvk = is_dxvk(dx);
        if (opt_fps) logmsg("Direct3D created after SoundFix: frame cap %d%s", opt_fps, dxvk ? " (DXVK, via DXVK_CONFIG)" : "");
        else logmsg("Direct3D created after SoundFix: no frame cap%s", dxvk ? " (DXVK: maxFrameRate = 0 via DXVK_CONFIG, overrides dxvk.conf)" : " (no DXVK: the game has no cap of its own; vsync as set in the options)");
    }
    if (d) vt_hook(d, 16, hk_createdev, (void **)&o_createdev);                                  // IDirect3D9::CreateDevice
    return d;
}

// ---- Windows audio device changes into the log (IMMNotificationClient): what FMOD 4.32 may not survive
static const CLSID my_CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID my_IID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID my_IID_IMMNotificationClient = {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
static const IID my_IID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const PROPERTYKEY my_PKEY_FriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
static IMMDeviceEnumerator *g_en;
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
    logmsg("audio device %s: %s", nm, st == DEVICE_STATE_ACTIVE ? "active" : st == DEVICE_STATE_DISABLED ? "disabled" : st == DEVICE_STATE_NOTPRESENT ? "not present" : st == DEVICE_STATE_UNPLUGGED ? "unplugged" : "state ?");
    return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_add(IMMNotificationClient *t, LPCWSTR id) { char nm[256]; dev_name(id, nm, sizeof nm); logmsg("audio device added: %s", nm); return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_rem(IMMNotificationClient *t, LPCWSTR id) { char nm[256]; dev_name(id, nm, sizeof nm); logmsg("audio device removed: %s", nm); return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_def(IMMNotificationClient *t, EDataFlow f, ERole r, LPCWSTR id) {
    if (f != eRender || r != eConsole) return S_OK;
    char nm[256]; dev_name(id, nm, sizeof nm); logmsg("default audio output -> %s", nm); return S_OK; }
static HRESULT STDMETHODCALLTYPE nc_prop(IMMNotificationClient *t, LPCWSTR id, const PROPERTYKEY k) { return S_OK; }
static IMMNotificationClientVtbl nc_vt = {nc_qi, nc_ref, nc_ref, nc_state, nc_add, nc_rem, nc_def, nc_prop};
static IMMNotificationClient nc = {&nc_vt};
// ---- part 4 (diagnostics): the game's FMOD format vs the output device's mix format, what FMOD really runs at
static const IID my_IID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static char g_mix_desc[160] = "unknown";
static int query_mix(void) {   // shared-mode mix format of the default output = the "Default Format" of Control Panel -> device -> Advanced
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
        }
        IMMDeviceEnumerator_Release(en);
    }
    if (ci == S_OK || ci == S_FALSE) CoUninitialize();
    return rate;
}
// FMOD Ex C++ API: public __stdcall members, "this" (FMOD::System *) first
typedef int (__stdcall *setsw_t)(void *, int, int, int, int, int);
typedef int (__stdcall *setdsp_t)(void *, unsigned, int);
typedef int (__stdcall *finit_t)(void *, int, unsigned, void *);
typedef int (__stdcall *getsw_t)(void *, int *, int *, int *, int *, int *, int *);
typedef int (__stdcall *getout_t)(void *, int *);
typedef int (__stdcall *getdsp_t)(void *, unsigned *, int *);
static setsw_t o_setsw; static setdsp_t o_setdsp; static finit_t o_finit; static getsw_t p_getsw; static getout_t p_getout; static getdsp_t p_getdsp;
static int __stdcall hk_setsw(void *sys, int rate, int fmt, int outch, int maxin, int rs) {   // logged, passed on unchanged
    int mix = query_mix();
    logmsg("FMOD: the game's rate %d Hz (format %d, max input channels %d, resampler %d); output device mix format %s%s", rate, fmt, maxin, rs, g_mix_desc,
           !mix ? "" : mix == rate ? " (same)" : " (differs: resampled)");
    return o_setsw(sys, rate, fmt, outch, maxin, rs);
}
static int __stdcall hk_setdsp(void *sys, unsigned len, int n) { int r = o_setdsp(sys, len, n); logmsg("FMOD: DSP buffer %u samples x %d (result %d)", len, n, r); return r; }
static int __stdcall hk_finit(void *sys, int maxch, unsigned flags, void *x) {
    int r = o_finit(sys, maxch, flags, x), rate = 0, fmt = 0, och = 0, mi = 0, rs = 0, bits = 0, out = -1, nb = 0; unsigned bl = 0;
    if (p_getsw) p_getsw(sys, &rate, &fmt, &och, &mi, &rs, &bits);
    if (p_getout) p_getout(sys, &out);
    if (p_getdsp) p_getdsp(sys, &bl, &nb);
    logmsg("FMOD init(%d channels, flags %x) = %d: output %s, mixing at %d Hz, %d output channels, buffer %u x %d samples (%.1f ms each)", maxch, flags, r,
           out == 6 ? "DirectSound" : out == 7 ? "WinMM" : out == 8 ? "WASAPI" : out == 2 ? "no sound" : "other", rate, och, bl, nb, rate ? bl * 1000.0 / rate : 0.0);
    return r;
}
static void fmod_hooks(void) {   // Shark's fmodex imports (after fmod_upgrade: they point into the FMOD Ex in use, 4.32.3 or 4.32.20)
    if (!SB || !shark_known()) return;
    if (*(DWORD *)SR(DRV_INST)) { logmsg("FMOD: the sound system has already started - its format is not logged this time"); return; }
    static const char *N[3] = {"?setSoftwareFormat@System@FMOD@@QAG?AW4FMOD_RESULT@@HW4FMOD_SOUND_FORMAT@@HHW4FMOD_DSP_RESAMPLER@@@Z",
        "?setDSPBufferSize@System@FMOD@@QAG?AW4FMOD_RESULT@@IH@Z", "?init@System@FMOD@@QAG?AW4FMOD_RESULT@@HIPAX@Z"};
    void *H[3] = {(void *)hk_setsw, (void *)hk_setdsp, (void *)hk_finit}, **O[3] = {(void **)&o_setsw, (void **)&o_setdsp, (void **)&o_finit};
    int n = 0;
    for (int i = 0; i < 3; i++) {
        void **sl = import_slot(SB, "fmodex.dll", N[i]); DWORD o;
        if (!sl || !VirtualProtect(sl, 4, PAGE_READWRITE, &o)) continue;
        *O[i] = *sl; *sl = H[i]; VirtualProtect(sl, 4, o, &o); n++;
    }
    HMODULE fm = NULL;
    if (o_setsw && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)o_setsw, &fm) && fm) {
        p_getsw = (getsw_t)GetProcAddress(fm, "?getSoftwareFormat@System@FMOD@@QAG?AW4FMOD_RESULT@@PAHPAW4FMOD_SOUND_FORMAT@@00PAW4FMOD_DSP_RESAMPLER@@0@Z");
        p_getout = (getout_t)GetProcAddress(fm, "?getOutput@System@FMOD@@QAG?AW4FMOD_RESULT@@PAW4FMOD_OUTPUTTYPE@@@Z");
        p_getdsp = (getdsp_t)GetProcAddress(fm, "?getDSPBufferSize@System@FMOD@@QAG?AW4FMOD_RESULT@@PAIPAH@Z");
    }
    if (n != 3) logmsg("FMOD: %d/3 format hooks", n);
}

static DWORD WINAPI audio_watch(LPVOID _) {
    if (CoInitializeEx(NULL, COINIT_MULTITHREADED) < 0) return 0;
    if (CoCreateInstance(&my_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &my_IID_IMMDeviceEnumerator, (void **)&g_en) < 0 || !g_en) { logmsg("audio device watch: not available"); return 0; }
    IMMDevice *d = NULL; char nm[256] = "(none)";
    if (IMMDeviceEnumerator_GetDefaultAudioEndpoint(g_en, eRender, eConsole, &d) >= 0 && d) {
        LPWSTR id = NULL; if (IMMDevice_GetId(d, &id) >= 0) { dev_name(id, nm, sizeof nm); CoTaskMemFree(id); } IMMDevice_Release(d);
    }
    HRESULT r = IMMDeviceEnumerator_RegisterEndpointNotificationCallback(g_en, &nc);
    query_mix();
    logmsg("audio output: %s (mix format %s); watching audio device changes%s", nm, g_mix_desc, r < 0 ? " - FAILED" : "");
    for (;;) Sleep(INFINITE);   // the callbacks come on the system's threads; this one keeps COM alive
    return 0;
}

static DWORD WINAPI install(LPVOID _) {
    for (int i = 0; i < 600 && !SB && !(SB = (BYTE *)GetModuleHandleA("Shark.dll")); i++) Sleep(100);
    if (!SB) { logmsg("Shark.dll not loaded - off"); return 0; }
    if (!opt_merge) { logmsg("Merge=0 in the ini: sound queues left as they are"); return 0; }
    HMODULE pl = GetModuleHandleA("Platform.dll");
    mx_enter = pl ? (mx_t)GetProcAddress(pl, "?Enter@RecursiveMutex@Plat@@QAEXXZ") : NULL;
    mx_leave = pl ? (mx_t)GetProcAddress(pl, "?Leave@RecursiveMutex@Plat@@QAEXXZ") : NULL;
    if (!mx_enter || !mx_leave || !shark_known()) { logmsg("SoundFix %s: unknown Shark.dll / Platform.dll build - sound queues left as they are", SOUNDFIX_VERSION); return 0; }
    // wait for the sound system (queues created)
    for (int i = 0; i < 3000; i++) { void **q = *(void ***)SR(Q_CL); void **q2 = *(void ***)SR(Q_CS); if (q && q2) break; Sleep(100); }
    int n = 0;
    n += patch(IAT_SETPOS, hk_setpos, (void **)&o_setpos, 0x3fae0);
    n += patch(IAT_SETVEL, hk_setvel, (void **)&o_setvel, 0x3fb50);
    n += patch(IAT_SETORI, hk_setori, (void **)&o_setori, 0x3fb10);
    n += patch(IAT_SETVOL, hk_setvol, (void **)&o_setvol, 0x3fab0);
    n += patch(IAT_SETDATA, hk_setdata, (void **)&o_setdata, 0x10b50);
    n += patch(IAT_CPLAY, hk_cplay, (void **)&o_cplay, exp_rva("?play@Container@shark@@QAEXABVVector3f@Math@@@Z"));
    n += patch(IAT_CSTOP, hk_cstop, (void **)&o_cstop, exp_rva("?stop@Container@shark@@QBEXW4Method@Stop@2@@Z"));
    n += patch(IAT_FREE, hk_free, (void **)&o_free, exp_rva("?free@shark@@YGXAAV?$shared_ptr@VPooledContainer@shark@@@boost@@W4Method@Stop@1@@Z"));
    n += patch(IAT_PFREE, hk_pfree, (void **)&o_pfree, exp_rva("?free@ContainerPool@shark@@QAEXAAV?$shared_ptr@VPooledContainer@shark@@@boost@@W4Method@Stop@2@@Z"));
    logmsg("SoundFix %s: %d/9 hooks, Shark %p (%s version check)", SOUNDFIX_VERSION, n, SB, *SR(0x3907a) == 0x8c ? "jl" : "jne");
    for (int t = 1;; t++) {
        Sleep(60000);
        if (t == 1 && !g_d3d_seen) logmsg("Direct3D was created before SoundFix loaded (Elite loads its add-ons late): no frame cap change this session%s",
                                          is_dxvk(GetModuleHandleA("d3d9.dll")) ? " - DXVK keeps dxvk.conf's maxFrameRate" : " (no DXVK: the game has no cap of its own)");
        LONG m = n_merged, q = n_queued, b = n_barrier;
        logmsg("last minute: %ld state messages merged into waiting ones, %ld queued, %ld play/stop/free", m, q, b);
        n_merged = n_queued = n_barrier = 0;
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID _) {
    if (r != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    char m[48]; snprintf(m, sizeof m, "SoundFix-%lu", GetCurrentProcessId());
    CreateMutexA(NULL, FALSE, m); if (GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;   // loaded twice (dow2-patches and Elite_DLLs): the first one works
    GetModuleFileNameA(h, g_log, MAX_PATH); strcpy(g_dir, g_log);
    char *e = strrchr(g_log, '.'); if (e) strcpy(e, ".ini");
    opt_fps = GetPrivateProfileIntA("SoundFix", "FpsCap", 0, g_log); if (opt_fps < 0 || opt_fps > 1000) opt_fps = 0;
    opt_fmod = GetPrivateProfileIntA("SoundFix", "Fmodex", 1, g_log);
    opt_merge = GetPrivateProfileIntA("SoundFix", "Merge", 1, g_log);
    if (e) strcpy(e, ".log");
    e = strrchr(g_dir, '\\'); if (e) e[1] = 0;
    LARGE_INTEGER qf; QueryPerformanceFrequency(&qf); g_qf = qf.QuadPart;
    // everything that must happen before the game starts sound and Direct3D is done right here, while the DLL loads
    char fv[32]; verstr(modver(GetModuleHandleA("fmodex.dll")), fv);
    logmsg("SoundFix %s loaded from %s (FpsCap=%d Fmodex=%d Merge=%d; fmodex.dll %s)", SOUNDFIX_VERSION, g_dir, opt_fps, opt_fmod, opt_merge, fv);
    dxvk_env();
    HMODULE sp = GetModuleHandleA("Spooge.dll"); void **s = sp ? import_slot((BYTE *)sp, "d3d9.dll", "Direct3DCreate9") : NULL; DWORD o;
    if (s && VirtualProtect(s, 4, PAGE_READWRITE, &o)) { o_d3dcreate = (d3dcreate_t)*s; *s = (void *)hk_d3dcreate; VirtualProtect(s, 4, o, &o); }
    else logmsg("Spooge's Direct3DCreate9 import not found - frame cap only through DXVK_CONFIG");
    SB = (BYTE *)GetModuleHandleA("Shark.dll");
    fmod_upgrade();
    fmod_hooks();
    CloseHandle(CreateThread(NULL, 0, install, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, audio_watch, NULL, 0, NULL));
    return TRUE;
}
