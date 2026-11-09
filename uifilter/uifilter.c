// UIFilter.dll - UI state filter (09.11.2026). The interface (Scaleform GFx in Spooge.dll) sets the same Direct3D state again
// and again while it draws: the same render state, sampler state, texture, shader, vertex declaration before every batch
// (reports/drawcalls_1109). Each of those calls still goes into the Direct3D runtime / driver (or DXVK). UIFilter drops the
// repeats - only the calls of the interface renderer, only while it draws one movie.
//  - How: GFx's D3D9 renderer keeps its device at renderer+0xD4 (every device call of the renderer loads it from there) and a
//    copy at renderer+0x18C (its vertex/index buffer helper: SetStreamSource, SetIndices). At the start of each display
//    (BeginDisplay 0x10051380, the instruction 0x100513B8 that loads +0xD4) both are replaced by our proxy IDirect3DDevice9:
//    its own vtable, every method forwarded to the real device. The real device and its vtable are not touched; nobody else
//    (the 3D world, DXVK, other add-ons) sees the proxy. It works the same with the system Direct3D 9 and with DXVK, early or
//    late loading (the device is wrapped on the next display).
//    (Not at SetDependentVideoMode 0x100510E7 as first planned: that one also copies the real device into +0x18C at
//    0x1005118D, a 2-byte store; wrapping at BeginDisplay covers both, and a device set before UIFilter was loaded too.)
//  - Filtered (a repeat of the value the same display already set = dropped, D3D_OK): SetRenderState, SetSamplerState,
//    SetTexture, SetVertexShader, SetPixelShader, SetStreamSource, SetIndices, SetVertexDeclaration, SetFVF. Shader constants
//    and everything else always go through.
//  - The cache lives from BeginDisplay to EndDisplay (0x10051F10) of one display on one thread; it is emptied at the start
//    of the display, on BeginScene / EndScene / Present / Reset / SetRenderTarget / SetDepthStencilSurface, on another
//    thread's call. DrawPrimitiveUP / DrawIndexedPrimitiveUP forget stream 0 (/ indices), SetFVF and SetVertexDeclaration
//    forget each other. Between BeginStateBlock and EndStateBlock nothing is dropped (the block records the calls). A state
//    block created through the proxy (its Apply is not seen) turns the filter off for the session (GFx makes none).
//  - UIFilter.ini (optional, next to the DLL): [UIFilter] Enabled=0 - nothing patched; Verify=1 - every dropped value is
//    checked against the device (Get*), a mismatch is logged and the call goes through. UIFilter.log: once a minute the calls
//    per type (came in / dropped) and, in Verify, mismatches.
// Rendering only - nothing of the simulation is touched, no sync impact.
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>

#define UIFILTER_VERSION "1"
#define NV 119   // IDirect3DDevice9 methods

// Spooge.dll RVAs (md5 aa133d9f5cd4ace948f3e353c892478e, the same in Elite and vanilla 3.19)
#define RVA_BEGIN 0x513B8   // BeginDisplay: mov eax,[esi+0xd4]; fld dword [esp+0x1c]  (qword aligned)
#define RVA_END   0x51F10   // EndDisplay:   push esi; mov esi,ecx; cmp dword [esi+0xd4],0; je ...  (qword aligned)
#define RVA_END_BACK 0x51F1A
static const BYTE ORIG_BEGIN[8] = {0x8B, 0x86, 0xD4, 0x00, 0x00, 0x00, 0xD9, 0x44};
static const BYTE ORIG_END[8]   = {0x56, 0x8B, 0xF1, 0x83, 0xBE, 0xD4, 0x00, 0x00};

static wchar_t g_log[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---- forwarding thunks: [esp+4] = proxy -> real device, jump to the real method -------------------------------------------
__asm__(
    ".text\n"
    ".altmacro\n"
    ".macro UIF_FWD n\n"
    "_uif_fwd_\\n\\():\n"
    "  movl 4(%esp),%eax\n"
    "  movl 4(%eax),%eax\n"
    "  movl %eax,4(%esp)\n"
    "  movl (%eax),%eax\n"
    "  jmp *(\\n*4)(%eax)\n"
    ".endm\n"
    ".macro UIF_TAB n\n"
    "  .long _uif_fwd_\\n\n"
    ".endm\n"
    ".set uif_i,0\n"
    ".rept 119\n"
    "  UIF_FWD %uif_i\n"
    "  .set uif_i,uif_i+1\n"
    ".endr\n"
    ".section .rdata,\"dr\"\n"
    ".p2align 2\n"
    ".globl _uif_fwd_table\n"
    "_uif_fwd_table:\n"
    ".set uif_i,0\n"
    ".rept 119\n"
    "  UIF_TAB %uif_i\n"
    "  .set uif_i,uif_i+1\n"
    ".endr\n"
    ".noaltmacro\n"
    ".text\n");
extern void *uif_fwd_table[NV];

typedef struct { void **vtbl; IDirect3DDevice9 *real; } Proxy;
static void *g_vtbl[NV];
static Proxy g_px[4];
static CRITICAL_SECTION g_pxcs;

// ---- the cache --------------------------------------------------------------------------------------------------------
enum { T_RS, T_SS, T_TEX, T_VS, T_PS, T_STREAM, T_IDX, T_DECL, T_FVF, T_N };
static const char *TNAME[T_N] = {"RenderState", "SamplerState", "Texture", "VShader", "PShader", "StreamSource", "Indices", "VDecl", "FVF"};
static struct {
    DWORD gen;                                   // an entry is valid when its gen == gen; gen++ empties everything
    DWORD rs[256], rsg[256];
    DWORD ss[16][16], ssg[16][16];
    void *tex[16]; DWORD texg[16];
    void *vs, *ps, *ib, *decl; DWORD vsg, psg, ibg, declg;
    DWORD fvf, fvfg;
    struct { void *vb; UINT off, stride; } st[16]; DWORD stg[16];
} C;
static volatile LONG g_active;     // inside BeginDisplay..EndDisplay
static DWORD g_tid;                // the display's thread
static int g_recording;            // between BeginStateBlock and EndStateBlock
static int g_off;                  // filter off for the session (a state block made through the proxy)
static int g_verify, g_pureWarned;
static volatile LONG g_in[T_N], g_drop[T_N], g_mism[T_N], g_noget, g_passes, g_wraps;
static volatile LONG g_mismLines;

static void reset(void) { if (++C.gen == 0) { memset(&C, 0, sizeof C); C.gen = 1; } }
// may this call be dropped / remembered? (counts it)
static int filtering(int t) {
    if (!g_active || g_off || g_recording) return 0;
    if (GetCurrentThreadId() != g_tid) { reset(); return 0; }
    InterlockedIncrement(&g_in[t]);
    return 1;
}
static void mismatch(int t, const char *fmt, ...) {
    InterlockedIncrement(&g_mism[t]);
    if (InterlockedIncrement(&g_mismLines) > 20) return;
    char b[200]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    logmsg("VERIFY mismatch %s: %s (the call went through)", TNAME[t], b);
}
static void noget(HRESULT hr) {
    InterlockedIncrement(&g_noget);
    if (!g_pureWarned) { g_pureWarned = 1; logmsg("VERIFY: the device does not answer Get* (hr 0x%08lx, pure device?) - not checked", (unsigned long)hr); }
}
#define REAL(p) (((Proxy *)(p))->real)
// a repeat: drop it (Verify: unless the device has something else)
#define DROP(t) do { InterlockedIncrement(&g_drop[t]); return D3D_OK; } while (0)

static HRESULT WINAPI px_SetRenderState(Proxy *p, D3DRENDERSTATETYPE s, DWORD v) {
    if (filtering(T_RS) && (DWORD)s < 256) {
        if (C.rsg[s] == C.gen && C.rs[s] == v) {
            if (!g_verify) DROP(T_RS);
            DWORD d = 0; HRESULT hr = IDirect3DDevice9_GetRenderState(p->real, s, &d);
            if (FAILED(hr)) noget(hr); else if (d == v) DROP(T_RS); else mismatch(T_RS, "state %d: ours %lu, device %lu", (int)s, (unsigned long)v, (unsigned long)d);
        }
        HRESULT hr = IDirect3DDevice9_SetRenderState(p->real, s, v);
        if (SUCCEEDED(hr)) { C.rs[s] = v; C.rsg[s] = C.gen; } else C.rsg[s] = 0;
        return hr;
    }
    return IDirect3DDevice9_SetRenderState(p->real, s, v);
}
static HRESULT WINAPI px_SetSamplerState(Proxy *p, DWORD smp, D3DSAMPLERSTATETYPE s, DWORD v) {
    if (filtering(T_SS) && smp < 16 && (DWORD)s < 16) {
        if (C.ssg[smp][s] == C.gen && C.ss[smp][s] == v) {
            if (!g_verify) DROP(T_SS);
            DWORD d = 0; HRESULT hr = IDirect3DDevice9_GetSamplerState(p->real, smp, s, &d);
            if (FAILED(hr)) noget(hr); else if (d == v) DROP(T_SS); else mismatch(T_SS, "sampler %lu state %d: ours %lu, device %lu", (unsigned long)smp, (int)s, (unsigned long)v, (unsigned long)d);
        }
        HRESULT hr = IDirect3DDevice9_SetSamplerState(p->real, smp, s, v);
        if (SUCCEEDED(hr)) { C.ss[smp][s] = v; C.ssg[smp][s] = C.gen; } else C.ssg[smp][s] = 0;
        return hr;
    }
    return IDirect3DDevice9_SetSamplerState(p->real, smp, s, v);
}
static HRESULT WINAPI px_SetTexture(Proxy *p, DWORD st, IDirect3DBaseTexture9 *t) {
    if (filtering(T_TEX) && st < 16) {
        if (C.texg[st] == C.gen && C.tex[st] == (void *)t) {
            if (!g_verify) DROP(T_TEX);
            IDirect3DBaseTexture9 *d = NULL; HRESULT hr = IDirect3DDevice9_GetTexture(p->real, st, &d);
            if (d) IDirect3DBaseTexture9_Release(d);
            if (FAILED(hr)) noget(hr); else if (d == t) DROP(T_TEX); else mismatch(T_TEX, "stage %lu: ours %p, device %p", (unsigned long)st, t, d);
        }
        HRESULT hr = IDirect3DDevice9_SetTexture(p->real, st, t);
        if (SUCCEEDED(hr)) { C.tex[st] = t; C.texg[st] = C.gen; } else C.texg[st] = 0;
        return hr;
    }
    return IDirect3DDevice9_SetTexture(p->real, st, t);
}
static HRESULT WINAPI px_SetVertexShader(Proxy *p, IDirect3DVertexShader9 *s) {
    if (filtering(T_VS)) {
        if (C.vsg == C.gen && C.vs == (void *)s) {
            if (!g_verify) DROP(T_VS);
            IDirect3DVertexShader9 *d = NULL; HRESULT hr = IDirect3DDevice9_GetVertexShader(p->real, &d);
            if (d) IDirect3DVertexShader9_Release(d);
            if (FAILED(hr)) noget(hr); else if (d == s) DROP(T_VS); else mismatch(T_VS, "ours %p, device %p", s, d);
        }
        HRESULT hr = IDirect3DDevice9_SetVertexShader(p->real, s);
        if (SUCCEEDED(hr)) { C.vs = s; C.vsg = C.gen; } else C.vsg = 0;
        return hr;
    }
    return IDirect3DDevice9_SetVertexShader(p->real, s);
}
static HRESULT WINAPI px_SetPixelShader(Proxy *p, IDirect3DPixelShader9 *s) {
    if (filtering(T_PS)) {
        if (C.psg == C.gen && C.ps == (void *)s) {
            if (!g_verify) DROP(T_PS);
            IDirect3DPixelShader9 *d = NULL; HRESULT hr = IDirect3DDevice9_GetPixelShader(p->real, &d);
            if (d) IDirect3DPixelShader9_Release(d);
            if (FAILED(hr)) noget(hr); else if (d == s) DROP(T_PS); else mismatch(T_PS, "ours %p, device %p", s, d);
        }
        HRESULT hr = IDirect3DDevice9_SetPixelShader(p->real, s);
        if (SUCCEEDED(hr)) { C.ps = s; C.psg = C.gen; } else C.psg = 0;
        return hr;
    }
    return IDirect3DDevice9_SetPixelShader(p->real, s);
}
static HRESULT WINAPI px_SetStreamSource(Proxy *p, UINT n, IDirect3DVertexBuffer9 *vb, UINT off, UINT stride) {
    if (filtering(T_STREAM) && n < 16) {
        if (C.stg[n] == C.gen && C.st[n].vb == (void *)vb && C.st[n].off == off && C.st[n].stride == stride) {
            if (!g_verify) DROP(T_STREAM);
            IDirect3DVertexBuffer9 *d = NULL; UINT o = 0, s = 0; HRESULT hr = IDirect3DDevice9_GetStreamSource(p->real, n, &d, &o, &s);
            if (d) IDirect3DVertexBuffer9_Release(d);
            if (FAILED(hr)) noget(hr);
            else if (d == vb && (!vb || (o == off && s == stride))) DROP(T_STREAM);
            else mismatch(T_STREAM, "stream %u: ours %p+%u/%u, device %p+%u/%u", n, vb, off, stride, d, o, s);
        }
        HRESULT hr = IDirect3DDevice9_SetStreamSource(p->real, n, vb, off, stride);
        if (SUCCEEDED(hr)) { C.st[n].vb = vb; C.st[n].off = off; C.st[n].stride = stride; C.stg[n] = C.gen; } else C.stg[n] = 0;
        return hr;
    }
    return IDirect3DDevice9_SetStreamSource(p->real, n, vb, off, stride);
}
static HRESULT WINAPI px_SetIndices(Proxy *p, IDirect3DIndexBuffer9 *ib) {
    if (filtering(T_IDX)) {
        if (C.ibg == C.gen && C.ib == (void *)ib) {
            if (!g_verify) DROP(T_IDX);
            IDirect3DIndexBuffer9 *d = NULL; HRESULT hr = IDirect3DDevice9_GetIndices(p->real, &d);
            if (d) IDirect3DIndexBuffer9_Release(d);
            if (FAILED(hr)) noget(hr); else if (d == ib) DROP(T_IDX); else mismatch(T_IDX, "ours %p, device %p", ib, d);
        }
        HRESULT hr = IDirect3DDevice9_SetIndices(p->real, ib);
        if (SUCCEEDED(hr)) { C.ib = ib; C.ibg = C.gen; } else C.ibg = 0;
        return hr;
    }
    return IDirect3DDevice9_SetIndices(p->real, ib);
}
static HRESULT WINAPI px_SetVertexDeclaration(Proxy *p, IDirect3DVertexDeclaration9 *v) {
    if (filtering(T_DECL)) {
        if (C.declg == C.gen && C.decl == (void *)v) {
            if (!g_verify) DROP(T_DECL);
            IDirect3DVertexDeclaration9 *d = NULL; HRESULT hr = IDirect3DDevice9_GetVertexDeclaration(p->real, &d);
            if (d) IDirect3DVertexDeclaration9_Release(d);
            if (FAILED(hr)) noget(hr); else if (d == v) DROP(T_DECL); else mismatch(T_DECL, "ours %p, device %p", v, d);
        }
        HRESULT hr = IDirect3DDevice9_SetVertexDeclaration(p->real, v);
        C.fvfg = 0;   // a declaration replaces the FVF
        if (SUCCEEDED(hr)) { C.decl = v; C.declg = C.gen; } else C.declg = 0;
        return hr;
    }
    return IDirect3DDevice9_SetVertexDeclaration(p->real, v);
}
static HRESULT WINAPI px_SetFVF(Proxy *p, DWORD f) {
    if (filtering(T_FVF)) {
        if (C.fvfg == C.gen && C.fvf == f) {
            if (!g_verify) DROP(T_FVF);
            DWORD d = 0; HRESULT hr = IDirect3DDevice9_GetFVF(p->real, &d);
            if (FAILED(hr)) noget(hr); else if (d == f) DROP(T_FVF); else mismatch(T_FVF, "ours 0x%lx, device 0x%lx", (unsigned long)f, (unsigned long)d);
        }
        HRESULT hr = IDirect3DDevice9_SetFVF(p->real, f);
        C.declg = 0;  // an FVF replaces the declaration
        if (SUCCEEDED(hr)) { C.fvf = f; C.fvfg = C.gen; } else C.fvfg = 0;
        return hr;
    }
    return IDirect3DDevice9_SetFVF(p->real, f);
}
// calls after which the device state is not what the cache says
static HRESULT WINAPI px_Reset(Proxy *p, D3DPRESENT_PARAMETERS *pp) { reset(); HRESULT hr = IDirect3DDevice9_Reset(p->real, pp); reset(); return hr; }
static HRESULT WINAPI px_Present(Proxy *p, const RECT *a, const RECT *b, HWND w, const RGNDATA *r) { reset(); return IDirect3DDevice9_Present(p->real, a, b, w, r); }
static HRESULT WINAPI px_SetRenderTarget(Proxy *p, DWORD i, IDirect3DSurface9 *s) { reset(); HRESULT hr = IDirect3DDevice9_SetRenderTarget(p->real, i, s); reset(); return hr; }
static HRESULT WINAPI px_SetDepthStencilSurface(Proxy *p, IDirect3DSurface9 *s) { reset(); return IDirect3DDevice9_SetDepthStencilSurface(p->real, s); }
static HRESULT WINAPI px_BeginScene(Proxy *p) { reset(); return IDirect3DDevice9_BeginScene(p->real); }
static HRESULT WINAPI px_EndScene(Proxy *p) { reset(); return IDirect3DDevice9_EndScene(p->real); }
static HRESULT WINAPI px_BeginStateBlock(Proxy *p) { reset(); g_recording = 1; return IDirect3DDevice9_BeginStateBlock(p->real); }
static HRESULT WINAPI px_EndStateBlock(Proxy *p, IDirect3DStateBlock9 **sb) { reset(); g_recording = 0; return IDirect3DDevice9_EndStateBlock(p->real, sb); }
static HRESULT WINAPI px_CreateStateBlock(Proxy *p, D3DSTATEBLOCKTYPE t, IDirect3DStateBlock9 **sb) {
    reset();
    if (!g_off) { g_off = 1; logmsg("the interface made a state block (its Apply is not seen) - filter OFF for this session"); }
    return IDirect3DDevice9_CreateStateBlock(p->real, t, sb);
}
static HRESULT WINAPI px_DrawPrimitiveUP(Proxy *p, D3DPRIMITIVETYPE t, UINT n, const void *v, UINT s) {
    HRESULT hr = IDirect3DDevice9_DrawPrimitiveUP(p->real, t, n, v, s); C.stg[0] = 0; return hr;   // stream 0 is reset
}
static HRESULT WINAPI px_DrawIndexedPrimitiveUP(Proxy *p, D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT pc, const void *i, D3DFORMAT f, const void *v, UINT s) {
    HRESULT hr = IDirect3DDevice9_DrawIndexedPrimitiveUP(p->real, t, mi, nv, pc, i, f, v, s); C.stg[0] = 0; C.ibg = 0; return hr;   // and the indices
}

static void build_vtbl(void) {
    memcpy(g_vtbl, uif_fwd_table, sizeof g_vtbl);
#define OV(i, f) g_vtbl[i] = (void *)f
    OV(16, px_Reset); OV(17, px_Present); OV(37, px_SetRenderTarget); OV(39, px_SetDepthStencilSurface);
    OV(41, px_BeginScene); OV(42, px_EndScene); OV(57, px_SetRenderState); OV(59, px_CreateStateBlock);
    OV(60, px_BeginStateBlock); OV(61, px_EndStateBlock); OV(65, px_SetTexture); OV(69, px_SetSamplerState);
    OV(83, px_DrawPrimitiveUP); OV(84, px_DrawIndexedPrimitiveUP); OV(87, px_SetVertexDeclaration); OV(89, px_SetFVF);
    OV(92, px_SetVertexShader); OV(100, px_SetStreamSource); OV(104, px_SetIndices); OV(107, px_SetPixelShader);
#undef OV
}
static int is_proxy(void *d) { return d && *(void ***)d == g_vtbl; }
static Proxy *wrap(IDirect3DDevice9 *real) {
    Proxy *r = NULL;
    EnterCriticalSection(&g_pxcs);
    for (int i = 0; i < 4 && !r; i++) if (g_px[i].real == real) r = &g_px[i];
    for (int i = 0; i < 4 && !r; i++) if (!g_px[i].real) { g_px[i].vtbl = g_vtbl; g_px[i].real = real; r = &g_px[i]; }
    if (!r) { r = &g_px[3]; r->real = real; }   // more than 4 devices in one game: reuse the last slot
    LeaveCriticalSection(&g_pxcs);
    return r;
}

// ---- called from the patched Spooge code ------------------------------------------------------------------------------
// BeginDisplay: wrap the renderer's device (and its buffer helper's copy), start a display; returns [renderer+0xD4]
void *__cdecl uif_pass_begin(BYTE *renderer) {
    void **pd = (void **)(renderer + 0xD4), **pb = (void **)(renderer + 0x18C);
    void *d = *pd;
    if (d && !is_proxy(d)) {
        Proxy *p = wrap((IDirect3DDevice9 *)d); *pd = p;
        if (*pb == d) *pb = p;
        if (InterlockedIncrement(&g_wraps) <= 5) logmsg("interface renderer %p: device %p wrapped (buffer helper %s)", renderer, d, *pb == p ? "too" : "not - its own");
    } else if (d && *pb && !is_proxy(*pb) && *pb == REAL(d)) *pb = d;   // the helper got the real device again
    reset(); g_tid = GetCurrentThreadId(); g_active = 1; InterlockedIncrement(&g_passes);
    return *pd;
}
void __cdecl uif_pass_end(BYTE *renderer) { (void)renderer; g_active = 0; reset(); }
DWORD uif_end_back;

__asm__(
    ".text\n"
    ".globl _uif_stub_begin\n"
    "_uif_stub_begin:\n"           // replaces mov eax,[esi+0xd4] (call + nop)
    "  pushl %ecx\n  pushl %edx\n  pushl %esi\n"
    "  call _uif_pass_begin\n"
    "  addl $4,%esp\n  popl %edx\n  popl %ecx\n"
    "  ret\n"
    ".globl _uif_stub_end\n"
    "_uif_stub_end:\n"             // jumped to from EndDisplay's first bytes (ecx = renderer)
    "  pushl %eax\n  pushl %ecx\n  pushl %edx\n  pushl %ecx\n"
    "  call _uif_pass_end\n"
    "  addl $4,%esp\n  popl %edx\n  popl %ecx\n  popl %eax\n"
    "  pushl %esi\n"               // the replaced push esi; mov esi,ecx; cmp dword [esi+0xd4],0
    "  movl %ecx,%esi\n"
    "  cmpl $0,0xd4(%esi)\n"
    "  jmp *_uif_end_back\n");
extern void uif_stub_begin(void), uif_stub_end(void);

// ---- patching -----------------------------------------------------------------------------------------------------------
static HANDLE patch_lock(void) {
    char n[64]; wsprintfA(n, "Local\\dow2patch_iat_%lu", GetCurrentProcessId());
    HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx) WaitForSingleObject(mx, 5000); return mx;
}
static void patch_unlock(HANDLE mx) { if (mx) { ReleaseMutex(mx); CloseHandle(mx); } }
// one aligned 8-byte write (lock cmpxchg8b): a thread running there sees the old or the new code, never half
static int patch8(BYTE *at, const BYTE *orig, const BYTE *neu) {
    DWORD old;
    if (memcmp(at, orig, 8)) return 0;
    if (!VirtualProtect(at, 8, PAGE_EXECUTE_READWRITE, &old)) return 0;
    LONGLONG o, n; memcpy(&o, orig, 8); memcpy(&n, neu, 8);
    LONGLONG r = InterlockedCompareExchange64((volatile LONGLONG *)at, n, o);
    VirtualProtect(at, 8, old, &old); FlushInstructionCache(GetCurrentProcess(), at, 8);
    return r == o;
}

static DWORD WINAPI stats(LPVOID _) {
    LONG last[T_N] = {0}, lastd[T_N] = {0}, lastm[T_N] = {0}, lastp = 0;
    for (;;) {
        Sleep(60000);
        LONG in = 0, dr = 0, p = g_passes - lastp; lastp += p;
        char b[900], *w = b; *w = 0;
        for (int t = 0; t < T_N; t++) {
            LONG i = g_in[t] - last[t], d = g_drop[t] - lastd[t], m = g_mism[t] - lastm[t];
            last[t] += i; lastd[t] += d; lastm[t] += m; in += i; dr += d;
            if (i) w += sprintf(w, " %s %ld/%ld", TNAME[t], d, i);
            if (m) w += sprintf(w, " (mismatch %ld)", m);
        }
        g_mismLines = 0;
        if (!p && !in) continue;
        logmsg("minute: %ld displays, calls dropped/came in %ld/%ld (%.1f%%):%s%s", p, dr, in, in ? 100.0 * dr / in : 0.0, b,
               g_verify ? " [Verify]" : "");
        if (g_noget) logmsg("  Verify: %ld checks impossible (Get* failed)", g_noget);
    }
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
    wchar_t ini[MAX_PATH];
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wcscpy(ini, g_log); wcscat(ini, L"UIFilter.ini"); wcscat(g_log, L"UIFilter.log");
    if (already_loaded("UIFilter")) { logmsg("another UIFilter copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"UIFilter", L"Enabled", 1, ini)) { logmsg("UIFilter %s: disabled in UIFilter.ini - nothing patched", UIFILTER_VERSION); return TRUE; }
    g_verify = GetPrivateProfileIntW(L"UIFilter", L"Verify", 0, ini) != 0;
    BYTE *sp = (BYTE *)GetModuleHandleA("Spooge.dll");
    if (!sp) { logmsg("UIFilter %s: no Spooge.dll in this process - nothing done", UIFILTER_VERSION); return TRUE; }
    InitializeCriticalSection(&g_pxcs); build_vtbl(); C.gen = 1;
    uif_end_back = (DWORD)(sp + RVA_END_BACK);
    BYTE nb[8], ne[8];
    memcpy(nb, ORIG_BEGIN, 8); nb[0] = 0xE8; *(DWORD *)(nb + 1) = (DWORD)uif_stub_begin - (DWORD)(sp + RVA_BEGIN + 5); nb[5] = 0x90;
    memcpy(ne, ORIG_END, 8);   ne[0] = 0xE9; *(DWORD *)(ne + 1) = (DWORD)uif_stub_end - (DWORD)(sp + RVA_END + 5);
    HANDLE mx = patch_lock();
    // EndDisplay first: a display that starts with the old BeginDisplay just never becomes active
    int okE = patch8(sp + RVA_END, ORIG_END, ne), okB = okE && patch8(sp + RVA_BEGIN, ORIG_BEGIN, nb);
    patch_unlock(mx);
    if (!okE || !okB) {
        if (okE) logmsg("UIFilter %s: BeginDisplay bytes differ (another Spooge.dll?) - the filter stays idle", UIFILTER_VERSION);
        else logmsg("UIFilter %s: Spooge.dll %p EndDisplay bytes differ (another build?) - nothing done", UIFILTER_VERSION, sp);
        return TRUE;
    }
    HMODULE d9 = GetModuleHandleA("d3d9.dll"), dx = GetModuleHandleW(L"DXVK_d3d9.bin");
    logmsg("UIFilter %s: Spooge.dll %p, interface renderer patched (BeginDisplay/EndDisplay); filters repeats of render/sampler state, "
           "texture, shaders, streams, indices, declaration/FVF inside one display; Verify=%d; Direct3D now: %s",
           UIFILTER_VERSION, sp, g_verify, dx ? "DXVK add-on" : d9 ? "d3d9.dll loaded" : "not loaded yet");
    CloseHandle(CreateThread(NULL, 0, stats, NULL, 0, NULL));
    return TRUE;
}
