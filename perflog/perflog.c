// PerfLog.dll - performance log for players (DoW2: Retribution 3.19, Elite and vanilla): what limits the FPS on this PC.
//
// Every 5 s one line into PerfLog.log, every minute one line with a verdict into PerfLog_summary.log (both next to the DLL,
// where the launcher's "Send logs" picks them up). Nothing in the game is changed: no simulation code, no drawing.
//
// How it measures (overhead: ~10 timed calls per frame, two QueryPerformanceCounter each, sums in memory):
//   - frame: shark::update through DOW2.exe's import slot 0x00F88068 = one pass of the main loop (call at 0x40E96A, top level
//     of the loop); a pass in which the draw 0x405935 ran = one shown frame. Frame times -> avg / max / 1% low (histogram).
//   - the main loop's parts, by an entry jump (prologue copied to a trampoline, bytes checked) that times only the outermost
//     call on the game's main thread ("XThread 0", the thread of shark::update): draw + Present 0x405935, 3D world
//     (SimVisPanel draw 0xB7DAA0 = GameSimVis: the world render passes and the world panel's movie with the unit bars),
//     HUD panels (0x681110: every other Scaleform panel), game frame 0x4240F5 (sim ticks + HUD model), visual update 0x439CE8 (anims of all visuals), UI update 0x680C00.
//     The jumps are written on the main thread between two passes (Elite's Profiler loads add-ons at the main menu).
//   - Present / EndScene as the game calls them: slots 0xC0 / 0xB8 of Spooge's sp::Device vtable (its Dx9Device wraps the
//     IDirect3DDevice9 calls): time waiting for the GPU / driver / vsync / FPS cap. The IDirect3DDevice9 vtable is NOT hooked
//     (overlays hook it; two such hooks calling each other forever crashed SoundFix 1 on a player's PC).
//   - main thread CPU time (GetThreadTimes) per frame; process memory; game mode, tick, replay speed, players, visuals.
// Call sites of the main loop (ReplaySpeed, ReplayTakeover) are not touched: those add-ons call the same functions through
// these entry jumps.
#include <windows.h>
#include <psapi.h>
#define COBJMACROS
#include <d3d9.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define VERSION "1"

#define IAT_SHARK_UPDATE 0x00F88068u
#define G_SETTINGS   0x01335A78u   // +0x118 game type: 0x800 = playback
#define G_WORLD      0x013356A4u   // +0x178 tick, +0x194 PlayerManager (vector of Player*)
#define G_HUD        0x01335720u   // +8 -> +0x68: sim rate interface (vtable 0x0108DFAC, +0x2C get)
#define G_VIZMGR     0x01335440u   // ** Viz manager: +0x2C/+0x30 proxies of 0x40 bytes (one per visual)
#define VT_SIMRATE   0x0108DFACu
#define FN_GETRATE   0x0043ECCBu
#define T_PLAYBACK   0x800

enum { P_RENDER, P_WORLD, P_HUD, P_GAME, P_VIS, P_UI, P_PRES, P_ENDS, NP };
#define NSITE P_PRES   // the code entries; P_PRES / P_ENDS are slots of Spooge's sp::Device vtable
typedef struct { DWORD addr; int len; BYTE b[8]; const char *name; } Site;
static const Site SITES[NSITE] = {
    {0x00405935, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}, "draw"},
    {0x00B7DAA0, 6, {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x34}, "world"},
    {0x00681110, 7, {0x6A, 0xFF, 0x68, 0x10, 0xE0, 0xEC, 0x00}, "hud"},
    {0x004240F5, 7, {0x55, 0x8B, 0xEC, 0x53, 0x8B, 0x5D, 0x08}, "game"},
    {0x00439CE8, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}, "visual"},
    {0x00680C00, 6, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8}, "ui"},
};
// 32 bytes each, the asm stubs below use these offsets
typedef struct { volatile DWORD depth; DWORD ret; void *tramp; volatile DWORD calls; LONGLONG t0; volatile LONGLONG acc; } Probe;
Probe g_pr[NP];
DWORD g_main;
void *g_qpc;

// entry stub of probe n: outermost call on the main thread -> time it (return address swapped for ours), else straight on
#define STUB(n) \
    void plhook##n(void); \
    __asm__(".text\n.globl _plhook" #n "\n_plhook" #n ":\n" \
            " push %eax\n mov %fs:0x24, %eax\n cmp _g_main, %eax\n jne 1f\n" \
            " cmpl $0, _g_pr+32*" #n "\n jne 1f\n" \
            " movl $1, _g_pr+32*" #n "\n pop %eax\n popl _g_pr+32*" #n "+4\n" \
            " push %eax\n push %ecx\n push %edx\n push $_g_pr+32*" #n "+16\n call *_g_qpc\n pop %edx\n pop %ecx\n pop %eax\n" \
            " call *_g_pr+32*" #n "+8\n" \
            " push %eax\n push %ecx\n push %edx\n sub $8, %esp\n push %esp\n call *_g_qpc\n pop %eax\n pop %edx\n" \
            " sub _g_pr+32*" #n "+16, %eax\n sbb _g_pr+32*" #n "+20, %edx\n" \
            " add %eax, _g_pr+32*" #n "+24\n adc %edx, _g_pr+32*" #n "+28\n incl _g_pr+32*" #n "+12\n" \
            " movl $0, _g_pr+32*" #n "\n pop %edx\n pop %ecx\n pop %eax\n jmp *_g_pr+32*" #n "+4\n" \
            "1:\n pop %eax\n jmp *_g_pr+32*" #n "+8\n");
STUB(0) STUB(1) STUB(2) STUB(3) STUB(4) STUB(5) STUB(6) STUB(7)
static void (*const HOOK[NP])(void) = {plhook0, plhook1, plhook2, plhook3, plhook4, plhook5, plhook6, plhook7};
static const char *const PNAME[NP] = {"draw", "world", "hud", "game", "visual", "ui", "present", "endscene"};

static wchar_t g_dir[MAX_PATH], g_log[MAX_PATH], g_sum[MAX_PATH];
static LONGLONG g_qf;
static int g_interval = 5;
static char g_game[32] = "vanilla";
static LONGLONG qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static int okp(const void *p, UINT n) { return p && (DWORD)p >= 0x10000 && !IsBadReadPtr(p, n); }

static void wlog(const wchar_t *file, const char *fmt, ...) {
    FILE *f = _wfopen(file, L"a"); if (!f) return;
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static void stamp(char *o, int secs) {
    SYSTEMTIME t; GetLocalTime(&t);
    if (secs) sprintf(o, "%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond); else sprintf(o, "%02d:%02d", t.wHour, t.wMinute);
}
#define LOG(...) do { char _s[16]; stamp(_s, 1); char _b[1024]; snprintf(_b, sizeof _b, __VA_ARGS__); wlog(g_log, "%s %s", _s, _b); } while (0)

// one patch at a time across all our DLLs (the import / code pages: two threads opening and closing a page at once crashed the
// game at start, see MMFind / LobbyStats): the shared mutex
static HANDLE patch_lock(void) {
    char n[64]; wsprintfA(n, "Local\\dow2patch_iat_%lu", GetCurrentProcessId());
    HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx) WaitForSingleObject(mx, 5000); return mx;
}
static void patch_unlock(HANDLE mx) { if (mx) { ReleaseMutex(mx); CloseHandle(mx); } }
static int in_module(const char *mod, DWORD a) {
    HMODULE m = GetModuleHandleA(mod); MODULEINFO mi;
    typedef BOOL (WINAPI *GMI)(HANDLE, HMODULE, MODULEINFO *, DWORD);
    GMI gmi = (GMI)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetModuleInformation");
    return m && gmi && gmi(GetCurrentProcess(), m, &mi, sizeof mi) && a >= (DWORD)mi.lpBaseOfDll && a < (DWORD)mi.lpBaseOfDll + mi.SizeOfImage;
}

// ---- static info (written by the logger thread)
static char g_gpu[700], g_disp[160];
static volatile LONG g_gpuReady, g_dispChanged;
static int g_refresh, g_vsync, g_cap;
static void read_present_params(IDirect3DDevice9 *dev) {   // main thread
    IDirect3DSwapChain9 *sc = NULL; D3DPRESENT_PARAMETERS pp;
    if (FAILED(IDirect3DDevice9_GetSwapChain(dev, 0, &sc)) || !sc) return;
    if (SUCCEEDED(IDirect3DSwapChain9_GetPresentParameters(sc, &pp))) {
        char d[160]; int vs = pp.PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE;
        D3DDISPLAYMODE dm; int hz = pp.FullScreen_RefreshRateInHz;
        if (!hz && SUCCEEDED(IDirect3DSwapChain9_GetDisplayMode(sc, &dm))) hz = dm.RefreshRate;
        char hzs[16] = ""; if (hz > 0) sprintf(hzs, " %d Hz", hz);
        snprintf(d, sizeof d, "%ux%u %s%s, vsync %s", pp.BackBufferWidth, pp.BackBufferHeight, pp.Windowed ? "windowed" : "fullscreen",
                 hzs, vs ? "on" : "off");
        if (strcmp(d, g_disp)) { strcpy(g_disp, d); g_refresh = hz; g_vsync = vs; InterlockedExchange(&g_dispChanged, 1); }
    }
    IDirect3DSwapChain9_Release(sc);
}
typedef void *(__stdcall *Inst_t)(void); typedef IDirect3DDevice9 *(__thiscall *GetDev_t)(void *);
static Inst_t g_inst; static GetDev_t g_getdev;
static IDirect3DDevice9 *game_device(void) {   // Spooge's sp::Device::Instance()->GetD3DDevice(), NULL while there is none
    if (!g_inst) {
        HMODULE sp = GetModuleHandleA("Spooge.dll"); if (!sp) return NULL;
        g_inst = (Inst_t)GetProcAddress(sp, "?Instance@Device@sp@@SGPAV12@XZ");
        g_getdev = (GetDev_t)GetProcAddress(sp, "?GetD3DDevice@Dx9Device@sp@@QAEPAUIDirect3DDevice9@@XZ");
        if (!g_inst || !g_getdev) { g_inst = NULL; return NULL; }
    }
    void *o = g_inst(); if (!okp(o, 0x44)) return NULL;
    IDirect3DDevice9 *dev = g_getdev(o); if (!okp(dev, 4) || !okp(*(void **)dev, 43 * 4)) return NULL;
    return dev;
}
// Present / EndScene: slots 0xC0 / 0xB8 of Spooge's own device class (sp::Dx9Device, called by the draw 0x405935 inside the
// game's own PerfBlock). Not the IDirect3DDevice9 vtable: overlays (Steam, RTSS, recorders) hook that one, and two hooks there
// can call each other forever (SoundFix 1 crashed so on a player's PC). No other program touches Spooge's vtable.
static char g_presHook[64] = "-";
static void hook_spooge_present(void *o) {
    DWORD *vt = *(DWORD **)o; static const int SLOT[2] = {0xC0 / 4, 0xB8 / 4}; char *w = g_presHook; *w = 0;
    if (!okp(vt, 0xC4)) return;
    HANDLE mx = patch_lock();
    for (int k = 0; k < 2; k++) {
        int i = P_PRES + k; DWORD *slot = &vt[SLOT[k]], cur = *slot, old;
        if (cur == (DWORD)HOOK[i]) continue;
        if (!in_module("Spooge.dll", cur) || !VirtualProtect(slot, 4, PAGE_READWRITE, &old)) { w += sprintf(w, "%s%s:no", *g_presHook ? " " : "", PNAME[i]); continue; }
        g_pr[i].tramp = (void *)cur; InterlockedExchange((LONG *)slot, (LONG)HOOK[i]); VirtualProtect(slot, 4, old, &old);
        w += sprintf(w, "%s%s", *g_presHook ? " " : "", PNAME[i]);
    }
    patch_unlock(mx);
}
static int dev_setup(void) {   // main thread, between two passes
    IDirect3DDevice9 *dev = game_device(); if (!dev) return g_inst || !GetModuleHandleA("Spooge.dll") ? 0 : -1;
    hook_spooge_present(g_inst());
    read_present_params(dev);
    // the adapter: plain COM queries on the game's device, nothing hooked
    D3DDEVICE_CREATION_PARAMETERS cp; IDirect3D9 *d3d = NULL; D3DADAPTER_IDENTIFIER9 id;
    char g[700] = "?";
    if (SUCCEEDED(IDirect3DDevice9_GetCreationParameters(dev, &cp)) && SUCCEEDED(IDirect3DDevice9_GetDirect3D(dev, &d3d)) && d3d) {
        if (SUCCEEDED(IDirect3D9_GetAdapterIdentifier(d3d, cp.AdapterOrdinal, 0, &id))) {
            LARGE_INTEGER v = id.DriverVersion;
            snprintf(g, sizeof g, "GPU \"%.200s\" (vendor %04lx device %04lx), driver %.100s %u.%u.%u.%u", id.Description, id.VendorId, id.DeviceId,
                     id.Driver, HIWORD(v.HighPart), LOWORD(v.HighPart), HIWORD(v.LowPart), LOWORD(v.LowPart));
        }
        IDirect3D9_Release(d3d);
    }
    snprintf(g_gpu, sizeof g_gpu, "%s; timed: %s", g, g_presHook);
    InterlockedExchange(&g_gpuReady, 1);
    return 1;
}

// ---- per-window snapshot (main thread fills, logger thread writes)
#define NBIN 1024              // frame time histogram, 0.25 ms bins
typedef struct {
    double secs; DWORD frames, passes; LONGLONG sumFrame, maxFrame; DWORD hist[NBIN + 1];
    LONGLONG part[NP], pres; DWORD calls[NP], presCalls;
    ULONGLONG cpu;                 // main thread CPU, 100 ns
    int mode;                      // 0 menu, 1 match, 2 replay
    float rate; int tick, ticks, players, visuals;
} Snap;
static Snap g_w;                    // current window (main thread only)
static Snap g_ring[8]; static volatile LONG g_ringW; static LONG g_ringR;
static HANDLE g_ev;
static LONGLONG g_winQ, g_lastFrameQ, g_devTry, g_lastPart[NP];
static DWORD g_lastCalls[NP], g_lastRenderCalls;
static ULONGLONG g_lastCpu; static int g_lastTick = -1, g_devState;

static ULONGLONG thread_cpu(void) {
    FILETIME a, b, k, u; if (!GetThreadTimes(GetCurrentThread(), &a, &b, &k, &u)) return 0;
    return ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
}
static void game_state(Snap *s) {
    char *st = okp((void *)G_SETTINGS, 4) ? *(char **)G_SETTINGS : NULL;
    char *w = okp((void *)G_WORLD, 4) ? *(char **)G_WORLD : NULL;
    int replay = okp(st, 0x120) && (*(int *)(st + 0x118) & T_PLAYBACK);
    s->tick = -1; s->players = -1; s->visuals = -1; s->rate = 0; s->mode = 0;
    if (okp(w, 0x19C)) {
        s->mode = replay ? 2 : 1; s->tick = *(int *)(w + 0x178);
        DWORD b = *(DWORD *)(w + 0x194), e = *(DWORD *)(w + 0x198);
        if (e >= b && e - b <= 4 * 32) s->players = (e - b) / 4;
        char *hud = okp((void *)G_HUD, 4) ? *(char **)G_HUD : NULL, *h = okp(hud, 12) ? *(char **)(hud + 8) : NULL;
        char *sr = okp(h, 0x6C) ? *(char **)(h + 0x68) : NULL;
        if (okp(sr, 4) && *(DWORD *)sr == VT_SIMRATE && okp((void *)VT_SIMRATE, 0x30) && ((DWORD *)VT_SIMRATE)[0x2C / 4] == FN_GETRATE) {
            typedef float (__thiscall *GetRate_t)(void *); s->rate = ((GetRate_t)FN_GETRATE)(sr);
        }
    }
    char **pm = okp((void *)G_VIZMGR, 4) ? *(char ***)G_VIZMGR : NULL; char *mgr = okp(pm, 4) ? *pm : NULL;
    if (okp(mgr + 0x2C, 8)) { DWORD b = *(DWORD *)(mgr + 0x2C), e = *(DWORD *)(mgr + 0x30); if (e >= b && e - b < 0x40 * 1000000u) s->visuals = (e - b) / 0x40; }
}
static void window_end(LONGLONG now) {
    Snap *s = &g_w;
    s->secs = (double)(now - g_winQ) / g_qf;
    for (int i = 0; i < NP; i++) {
        LONGLONG a = g_pr[i].acc; DWORD c = g_pr[i].calls;
        s->part[i] = a - g_lastPart[i]; s->calls[i] = c - g_lastCalls[i]; g_lastPart[i] = a; g_lastCalls[i] = c;
    }
    s->pres = s->part[P_PRES] + s->part[P_ENDS]; s->presCalls = s->calls[P_PRES];
    ULONGLONG c = thread_cpu(); s->cpu = g_lastCpu ? c - g_lastCpu : 0; g_lastCpu = c;
    game_state(s);
    s->ticks = s->tick >= 0 && g_lastTick >= 0 && s->tick >= g_lastTick ? s->tick - g_lastTick : 0; g_lastTick = s->tick;
    if (g_devState > 0 && (g_ringW & 0x7) == 0) { IDirect3DDevice9 *d = game_device(); if (d) read_present_params(d); }   // every ~40 s: resolution / mode changes
    LONG k = g_ringW; g_ring[k & 7] = *s; InterlockedExchange(&g_ringW, k + 1); SetEvent(g_ev);
    memset(s, 0, sizeof *s); g_winQ = now;
}

// ---- entry jumps (main thread, between two passes)
static BYTE *g_exec;
static char g_sites[200];
static void install_sites(void) {
    g_exec = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    char *o = g_sites; *o = 0;
    HANDLE mx = patch_lock();
    for (int i = 0; i < NSITE && g_exec; i++) {
        BYTE *t = (BYTE *)SITES[i].addr, *tr = g_exec + 32 * i; int n = SITES[i].len; DWORD old;
        if (!okp(t, 8) || memcmp(t, SITES[i].b, n)) { o += sprintf(o, " %s:no", SITES[i].name); continue; }
        memcpy(tr, t, n); tr[n] = 0xE9; *(LONG *)(tr + n + 1) = (LONG)((t + n) - (tr + n + 5));
        g_pr[i].tramp = tr;
        LONGLONG nv; BYTE *b = (BYTE *)&nv; memcpy(b, t, 8); b[0] = 0xE9; *(LONG *)(b + 1) = (LONG)((BYTE *)HOOK[i] - (t + 5));
        for (int k = 5; k < n; k++) b[k] = 0x90;
        if (!VirtualProtect(t, 8, PAGE_EXECUTE_READWRITE, &old)) { g_pr[i].tramp = NULL; o += sprintf(o, " %s:no", SITES[i].name); continue; }
        InterlockedExchange64((LONGLONG *)t, nv);
        VirtualProtect(t, 8, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 8);
        o += sprintf(o, " %s", SITES[i].name);
    }
    patch_unlock(mx);
}

// ---- the pass hook (shark::update, main loop)
typedef void (__stdcall *UpdFn)(float);
static UpdFn o_upd;
static void __stdcall hk_update(float dt) {
    DWORD tid = GetCurrentThreadId();
    if (!g_main) { install_sites(); g_winQ = qpc(); g_lastCpu = thread_cpu(); g_main = tid; }
    else if (tid == g_main) {
        LONGLONG now = qpc();
        g_w.passes++;
        DWORD rc = g_pr[P_RENDER].calls; int drew = g_pr[P_RENDER].tramp ? rc != g_lastRenderCalls : 1; g_lastRenderCalls = rc;
        if (drew) {
            if (g_lastFrameQ) {
                LONGLONG d = now - g_lastFrameQ; LONGLONG bin = d * 4000 / g_qf;
                g_w.frames++; g_w.sumFrame += d; if (d > g_w.maxFrame) g_w.maxFrame = d;
                g_w.hist[bin < NBIN ? bin : NBIN]++;
            }
            g_lastFrameQ = now;
        }
        if (g_devState == 0 && now - g_devTry > g_qf) { g_devTry = now; g_devState = dev_setup(); }
        if (now - g_winQ >= g_qf * g_interval) window_end(now);
    }
    o_upd(dt);
}

// ---- logger thread
typedef struct { double secs; DWORD frames; LONGLONG sumFrame, maxFrame; DWORD hist[NBIN + 1]; LONGLONG part[NP], pres; ULONGLONG cpu;
                 int n, mode, modes, tick0, tick1, visuals, players; float rate; } Acc;
static double ms_of(LONGLONG q, DWORD frames) { return frames ? (double)q * 1000.0 / g_qf / frames : 0; }
static double low1(const DWORD *h, DWORD frames, LONGLONG maxq) {   // 99th percentile frame time, ms
    DWORD need = frames / 100 + 1, c = h[NBIN];
    if (c >= need) return (double)maxq * 1000.0 / g_qf;
    for (int i = NBIN - 1; i >= 0; i--) { c += h[i]; if (c >= need) return (i + 1) * 0.25; }
    return 0;
}
typedef struct { double frame, cpu, pres, world, hud, draw, game, vis, ui, rest, fps, fmin, low; } Parts;
static void parts(Parts *p, double secs, DWORD frames, LONGLONG sumF, LONGLONG maxF, const DWORD *hist, const LONGLONG *part, LONGLONG pres, ULONGLONG cpu) {
    memset(p, 0, sizeof *p); if (!frames) return;
    p->frame = ms_of(sumF, frames); p->fps = frames / secs; p->fmin = maxF ? g_qf / (double)maxF : 0; p->low = low1(hist, frames, maxF);
    p->cpu = cpu / 1e4 / frames; p->pres = ms_of(pres, frames);
    double r = ms_of(part[P_RENDER], frames);
    p->world = ms_of(part[P_WORLD], frames); p->hud = ms_of(part[P_HUD], frames);
    p->game = ms_of(part[P_GAME], frames); p->vis = ms_of(part[P_VIS], frames); p->ui = ms_of(part[P_UI], frames);
    p->draw = r - p->world - p->hud - p->pres; if (p->draw < 0) p->draw = 0;
    p->rest = p->frame - r - p->game - p->vis - p->ui; if (p->rest < 0) p->rest = 0;
}
static const char *mode_s(int m, float rate, char *o) {
    if (m == 2) { if (rate > 0) sprintf(o, "replay x%g", (int)(rate + 0.5) / 10.0); else strcpy(o, "replay"); }
    else strcpy(o, m == 1 ? "match" : "menu");
    return o;
}
static void gtime(char *o, int tick) { if (tick < 0) *o = 0; else sprintf(o, " t=%d:%02d", tick / 600, tick / 10 % 60); }
static void verdict(char *o, const Parts *p) {
    struct { const char *n; double v; } P[] = { {"3D world draw (with unit bars)", p->world}, {"HUD (Scaleform panels)", p->hud}, {"other drawing (D3D calls)", p->draw},
        {"game frame (sim + HUD model)", p->game}, {"unit visuals update", p->vis}, {"UI update", p->ui} };
    int b = 0; for (int i = 1; i < 6; i++) if (P[i].v > P[b].v) b = i;
    if (p->frame <= 0) { strcpy(o, "no frames drawn"); return; }
    if (g_vsync && g_refresh > 0 && p->fps >= g_refresh * 0.95) { sprintf(o, "capped by vsync (%d Hz)", g_refresh); return; }
    if (g_cap > 0 && p->fps >= g_cap * 0.95) { sprintf(o, "capped by the FPS limit (%d)", g_cap); return; }
    if (p->pres >= p->frame * 0.30) { sprintf(o, "GPU/driver-bound (waits in Present %.1f of %.1f ms%s)", p->pres, p->frame, g_vsync ? ", vsync on" : ""); return; }
    if (p->cpu >= p->frame * 0.80) { sprintf(o, "CPU-bound (main thread %.0f%%), biggest: %s %.1f ms", 100 * p->cpu / p->frame, P[b].n, P[b].v); return; }
    sprintf(o, "mixed (main thread busy %.0f%%, Present %.1f ms), biggest: %s %.1f ms", 100 * p->cpu / p->frame, p->pres, P[b].n, P[b].v);
}
static void trim(const wchar_t *file) {   // keep the newest 256 KB of a log bigger than 1 MB
    FILE *f = _wfopen(file, L"rb"); if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f);
    if (n <= (1 << 20)) { fclose(f); return; }
    static char buf[256 << 10]; fseek(f, n - (long)sizeof buf, SEEK_SET); size_t k = fread(buf, 1, sizeof buf, f); fclose(f);
    char *s = memchr(buf, '\n', k); s = s ? s + 1 : buf;
    f = _wfopen(file, L"wb"); if (f) { fwrite(s, 1, buf + k - s, f); fclose(f); }
}
static void sysinfo(void) {
    char cpu[128] = "?"; DWORD n = sizeof cpu; HKEY k; SYSTEM_INFO si; MEMORYSTATUSEX ms = {sizeof ms};
    if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k)) {
        RegQueryValueExA(k, "ProcessorNameString", NULL, NULL, (BYTE *)cpu, &n); RegCloseKey(k); cpu[sizeof cpu - 1] = 0; }
    char *c = cpu; while (*c == ' ') c++;
    GetNativeSystemInfo(&si); GlobalMemoryStatusEx(&ms);
    char os[96] = "Windows"; HMODULE nt = GetModuleHandleA("ntdll.dll");
    typedef LONG (WINAPI *RGV)(OSVERSIONINFOW *); RGV rgv = (RGV)GetProcAddress(nt, "RtlGetVersion"); OSVERSIONINFOW ov = {sizeof ov};
    if (rgv && !rgv(&ov)) sprintf(os, "Windows %lu.%lu.%lu", ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber);
    typedef const char *(CDECL *WV)(void); WV wv = (WV)GetProcAddress(nt, "wine_get_version");
    if (wv) { size_t l = strlen(os); snprintf(os + l, sizeof os - l, " (Wine %s)", wv()); }
    char d3d[MAX_PATH] = "?"; HMODULE dm = GetModuleHandleA("d3d9.dll"); if (dm) GetModuleFileNameA(dm, d3d, sizeof d3d);
    LOG("PC: CPU %s, %lu threads, RAM %.1f GB, %s, d3d9.dll %s", c, si.dwNumberOfProcessors, ms.ullTotalPhys / 1073741824.0, os, d3d);
    // FPS cap of DXVK (d3d9.dll of DXVK, SoundFix FpsCap): dxvk.conf next to the game, DXVK_CONFIG on top of it
    FILE *f = fopen("dxvk.conf", "r"); char l[256];
    if (f) { while (fgets(l, sizeof l, f)) { char *m = strstr(l, "d3d9.maxFrameRate"); if (m && l[0] != '#' && (m = strchr(m, '='))) g_cap = atoi(m + 1); } fclose(f);
             if (g_cap) LOG("dxvk.conf: d3d9.maxFrameRate = %d", g_cap); }
    char env[512]; DWORD e = GetEnvironmentVariableA("DXVK_CONFIG", env, sizeof env);
    if (e && e < sizeof env) { LOG("DXVK_CONFIG: %s", env); char *m = strstr(env, "d3d9.maxFrameRate"); if (m && (m = strchr(m, '='))) g_cap = atoi(m + 1); }
}
static double qpc_cost_us(void) {   // what one timed call costs here (two QueryPerformanceCounter)
    LONGLONG a = qpc(), b = a; for (int i = 0; i < 2000; i++) b = qpc();
    return (double)(b - a) * 1e6 / g_qf / 1000;
}
static void addons_loaded(void) {   // which DLLs of our folder are loaded (the other add-ons in this game)
    HMODULE mods[512]; DWORD need = 0; char line[900] = ""; size_t l = 0;
    typedef BOOL (WINAPI *EPM)(HANDLE, HMODULE *, DWORD, DWORD *);
    EPM epm = (EPM)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32EnumProcessModules");
    if (!epm || !epm(GetCurrentProcess(), mods, sizeof mods, &need)) return;
    for (DWORD i = 0; i < need / sizeof(HMODULE) && i < 512; i++) {
        wchar_t p[MAX_PATH]; if (!GetModuleFileNameW(mods[i], p, MAX_PATH)) continue;
        wchar_t *sl = wcsrchr(p, L'\\'); if (!sl) continue;
        if (_wcsnicmp(p, g_dir, sl + 1 - p) || wcslen(g_dir) != (size_t)(sl + 1 - p)) continue;
        l += snprintf(line + l, sizeof line - l, " %ls", sl + 1); if (l >= sizeof line - 1) break;
    }
    LOG("add-ons loaded from this folder:%s", line);
}
static DWORD WINAPI logger(LPVOID u) {
    trim(g_log); trim(g_sum);
    SYSTEMTIME t; GetLocalTime(&t);
    wlog(g_log, "=== %04d-%02d-%02d %02d:%02d:%02d PerfLog %s (%s), a line every %d s: mode, game time, fps avg / min / 1%% low frame,"
         " then ms per frame: frame, main thread CPU, Present wait (GPU/driver), 3D world (with unit bars), HUD panels, other drawing, game frame (sim+HUD model),"
         " visuals update, UI update, rest of the loop", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, VERSION, g_game, g_interval);
    wlog(g_sum, "=== %04d-%02d-%02d %02d:%02d:%02d PerfLog %s (%s): one line per minute => verdict", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, VERSION, g_game);
    sysinfo();
    Acc A; memset(&A, 0, sizeof A); A.tick0 = -1; int firstLines = 0, addons = 0, idle = 0;
    for (;;) {
        if (WaitForSingleObject(g_ev, 30000) == WAIT_TIMEOUT) {
            if (!g_main) { if (++idle == 4) LOG("no game frames yet (main loop not seen)"); }
            else LOG("no window for 30 s (game minimized, loading or stuck)");
            continue;
        }
        if (!addons) { addons = 1; LOG("main loop hooked, timed:%s; one timed call costs %.2f us", g_sites, qpc_cost_us()); addons_loaded(); }
        if (g_gpuReady == 1) { g_gpuReady = 2; LOG("Direct3D: %s", g_gpu); }
        else if (g_devState < 0 && g_gpuReady == 0) { g_gpuReady = 3; LOG("Direct3D: Spooge's device not found - no Present time"); }
        if (InterlockedExchange(&g_dispChanged, 0)) LOG("display: %s", g_disp);
        while (g_ringR < g_ringW) {
            Snap *s = &g_ring[g_ringR & 7]; g_ringR++;
            if (g_ringW - g_ringR >= 7) continue;   // fell behind: skip
            Parts p; parts(&p, s->secs, s->frames, s->sumFrame, s->maxFrame, s->hist, s->part, s->pres, s->cpu);
            int loading = s->frames && !s->ticks && p.game > 0.5 * p.frame;   // the map loads inside the game frame
            char m[48], gt[24], mem[96] = ""; mode_s(s->mode, s->rate, m); gtime(gt, s->tick);
            if (loading) strcat(m, s->mode ? " (loading)" : " loading");
            PROCESS_MEMORY_COUNTERS_EX pmc = {sizeof pmc}; MEMORYSTATUSEX ms = {sizeof ms};
            typedef BOOL (WINAPI *GPMI)(HANDLE, PROCESS_MEMORY_COUNTERS *, DWORD);
            GPMI gp = (GPMI)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo");
            if (gp && gp(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof pmc) && GlobalMemoryStatusEx(&ms))
            {
                char pv[32] = ""; if (pmc.PrivateUsage) sprintf(pv, ", private %lu MB", (DWORD)(pmc.PrivateUsage >> 20));
                snprintf(mem, sizeof mem, " | mem %lu MB%s, address space %lu/%lu MB", (DWORD)(pmc.WorkingSetSize >> 20), pv,
                         (DWORD)((ms.ullTotalVirtual - ms.ullAvailVirtual) >> 20), (DWORD)(ms.ullTotalVirtual >> 20));
            }
            char cnt[48] = ""; if (s->players >= 0) sprintf(cnt, " players %d", s->players);
            if (!s->frames) LOG("%s%s | no frames drawn in %.1f s (%lu loop passes)%s", m, gt, s->secs, s->passes, mem);
            else LOG("%s%s | fps %.1f min %.1f 1%%low %.1f ms | frame %.2f cpu %.2f present %.2f | world %.2f hud %.2f draw %.2f game %.2f vis %.2f ui %.2f rest %.2f"
                     " | ticks %d passes %lu | visuals %d%s%s",
                     m, gt, p.fps, p.fmin, p.low, p.frame, p.cpu, p.pres, p.world, p.hud, p.draw, p.game, p.vis, p.ui, p.rest, s->ticks, s->passes, s->visuals, cnt, mem);
            if (s->frames && s->mode && s->ticks > 0 && firstLines < 1) {
                firstLines++;
                char c[300]; int l = 0; for (int i = 0; i < NP; i++) l += sprintf(c + l, " %s %.2f", PNAME[i], (double)s->calls[i] / s->frames);
                LOG("timed calls per shown frame:%s", c);
            }
            // minute summary
            A.secs += s->secs; A.frames += s->frames; A.sumFrame += s->sumFrame; if (s->maxFrame > A.maxFrame) A.maxFrame = s->maxFrame;
            for (int i = 0; i <= NBIN; i++) A.hist[i] += s->hist[i];
            for (int i = 0; i < NP; i++) A.part[i] += s->part[i];
            A.pres += s->pres; A.cpu += s->cpu; A.modes |= 1 << s->mode; A.mode = s->mode; A.rate = s->rate;
            if (A.tick0 < 0) { A.tick0 = s->tick; }
            A.tick1 = s->tick; A.visuals = s->visuals; A.players = s->players; A.n++;
            if (A.secs >= 59.5) {
                Parts q; parts(&q, A.secs, A.frames, A.sumFrame, A.maxFrame, A.hist, A.part, A.pres, A.cpu);
                char st[16], v[200], g0[24], g1[24]; stamp(st, 0); verdict(v, &q); mode_s(A.mode, A.rate, m);
                if ((A.modes & (A.modes - 1))) strcat(m, " (mode changed)");
                gtime(g0, A.tick0); gtime(g1, A.tick1);
                if (!A.frames) wlog(g_sum, "%s %s | no frames drawn", st, m);
                else wlog(g_sum, "%s %s%s%s%s | fps %.1f min %.1f 1%%low %.1f ms | frame %.1f cpu %.1f present %.1f | world %.1f hud %.1f draw %.1f game %.1f vis %.1f ui %.1f | visuals %d%s%s => %s",
                          st, m, g0, *g1 && strcmp(g0, g1) ? "-" : "", *g1 && strcmp(g0, g1) ? g1 + 3 : "", q.fps, q.fmin, q.low, q.frame, q.cpu, q.pres, q.world, q.hud, q.draw, q.game, q.vis, q.ui,
                          A.visuals, cnt, mem, v);
                memset(&A, 0, sizeof A); A.tick0 = -1;
            }
        }
    }
    return 0;
}

static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_dir); wcscat(ini, L"PerfLog.ini");
    wcscpy(g_log, g_dir); wcscat(g_log, L"PerfLog.log"); wcscpy(g_sum, g_dir); wcscat(g_sum, L"PerfLog_summary.log");
    if (already_loaded("PerfLog")) { LOG("another PerfLog copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"PerfLog", L"Enabled", 1, ini)) { LOG("disabled in PerfLog.ini"); return TRUE; }
    g_interval = GetPrivateProfileIntW(L"PerfLog", L"Interval", 5, ini); if (g_interval < 1) g_interval = 1; if (g_interval > 60) g_interval = 60;
    const char *cl = GetCommandLineA(), *mn = strstr(cl, "-modname ");
    if (mn) { mn += 9; int i = 0; while (mn[i] && mn[i] != ' ' && mn[i] != '"' && i < 31) { g_game[i] = mn[i]; i++; } g_game[i] = 0; }
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qf = f.QuadPart ? f.QuadPart : 1000;
    g_qpc = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryPerformanceCounter");
    void **slot = (void **)IAT_SHARK_UPDATE; HMODULE sh = GetModuleHandleA("Shark.dll"); DWORD old;
    void *exp = sh ? (void *)GetProcAddress(sh, "?update@shark@@YGXM@Z") : NULL;
    // the slot holds Shark's export, or another add-on's hook (MMFind, LobbyStats, RoomJoin...) that calls it on: chain either way
    if (!g_qpc || !exp || !okp(slot, 4) || !*slot) { LOG("not DoW2 3.19 (no Shark.dll update import) - nothing done"); return TRUE; }
    g_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE mx = patch_lock();
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) { patch_unlock(mx); LOG("import slot not writable - nothing done"); return TRUE; }
    for (;;) { void *cur = *slot; o_upd = (UpdFn)cur; if (InterlockedCompareExchange((LONG *)slot, (LONG)hk_update, (LONG)cur) == (LONG)cur) break; }
    VirtualProtect(slot, 4, old, &old); patch_unlock(mx);
    CloseHandle(CreateThread(NULL, 0, logger, NULL, 0, NULL));
    return TRUE;
}
