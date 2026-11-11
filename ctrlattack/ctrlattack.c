// CtrlAttack.dll - hold Ctrl and LEFT-click the ground: the selected squads that can attack the ground fire at that point
// (Dawn of War II: Retribution 3.19, Elite and vanilla). Only while the key is held - it is not a toggle.
// CtrlAttack 2 (owner 11.11.2026): left click, because Ctrl + right-click is Reverse's (backing off); the right click is not
// touched at all any more.
//
// The game has it (DOW2.exe 3.19.10320): the core action "Attack ground" (BindingAttackGround, vftable 0x0111977C, hotkey
// "attackground" = G). G puts the HUD into its targeting mode; the next world click goes to the binding's vftable +0x34,
// 0x00B70610 (stdcall: ctx, &click, click+0x0C, button, queued; ret 0x14): for the left button it takes the selected squads
// passing the game's own check 0x00B70240 (a weapon with weapon_bag behaviour attack_ground = true, read at 0x00820C77) and the
// min-range check 0x00B70290 and sends one ordinary order, WorldDoCommandSquads(SCMD_Attack = 48, queued, player, squads) with
// the point (0x007ED520) through CommandProxy +0x5C - every machine of the match and the replay get it. It returns 1 when it
// sent the order, 0 when no selected squad can.
// G is the hotkey handler 0x00B73570: binding = core actions +0x20, EDI = binding+4 (the HUD's targeting object), call
// 0x00AD5B50 (enter targeting mode: targeting object +0x08 = binding, the world input's click callback 0x00AD5CD0). The
// click is then dispatched in the HUD update by 0x00AD59C0 (targeting object +0x0C: position, +0x0C entity id under the
// cursor, +0x10 button, +0x11 Shift, +0x12 consumed); on success the mode is left (binding +0x38).
// CtrlAttack 2: the binding's per-frame update (vftable +0x0C, 0x00B703B0: returns 1 when a selected squad can attack the
// ground) is wrapped to know the binding, the HUD context and that answer. The game window is subclassed: a WM_LBUTTONDOWN
// with Ctrl, a selected squad that can, no targeting mode active and no unit within 3 m of the cursor's ground point
// (context +0xB8) -> the game's own 0x00AD5B50 for Attack ground, i.e. exactly G, before the game sees the click; the click
// itself then goes to Attack ground as after G. If the mode is still on 0.4 s after the button is up (no order: e.g. inside
// the minimum range), it is left the way the game leaves it. Ctrl + left click on a unit, with no squad that can, Ctrl +
// right click: the game's own. Shift = queued, as for G. Replays (game type 0x800): nothing. Nothing in the simulation.
//
// CtrlAttack.ini [CtrlAttack]: Enabled=1, Key=Ctrl (Ctrl|Alt|<virtual key code>), Verbose=0;
// re-read every 2 s. CtrlAttack.log: version, every checked hook byte, every order given.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#define CA_VERSION    2
#define VT_AGROUND    0x0111977Cu                    // BindingAttackGround vftable
#define SLOT_UPDATE   0x0C                           // per-frame update(ctx) -> 1: a selected squad can attack the ground
#define ORIG_UPDATE   0x00B703B0u
#define SLOT_CLICK    0x34                           // the targeting click (0x00B70610)
#define FN_ENTERTGT   0x00AD5B50u                    // ECX = binding, EDI = targeting object: enter targeting mode (= G)
#define FN_GHOTKEY    0x00B73570u                    // the G hotkey: binding = core actions +0x20, EDI = binding+4, call FN_ENTERTGT
#define FN_CLICKDISP  0x00AD59C0u                    // HUD update: dispatch the targeting click (ESI = targeting object)
#define AT_BTNSEND    0x00B7081Du                    // push 0x30; call [WorldDoCommandSquads] in the attack-ground click
#define G_HUD         0x01335720u                    // +8 -> +0x90 the world input (as 0x00AD59C0 when it leaves the mode)
#define G_WORLD       0x013356A4u                    // +4 the EntityManager
#define G_SETTINGS    0x01335A78u                    // +0x118 game type: 0x800 = playback
#define T_PLAYBACK    0x800

typedef struct { float x, y, z; } Vec3;
typedef unsigned char (__fastcall *Update_t)(void *self, void *edx, void *ctx);
typedef void **(__fastcall *GetEnts_t)(void *em, void *edx);
typedef void *(__fastcall *P0_t)(void *obj, void *edx);
typedef const Vec3 *(__fastcall *EntPos_t)(void *ent, void *edx);
static Update_t o_update; static GetEnts_t p_ents; static P0_t p_esquad; static EntPos_t p_epos;
static WNDPROC o_wndproc; static HWND g_wnd;
static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH];
static int g_enabled = 1, g_key = VK_CONTROL, g_verbose = 0, g_installed;
static DWORD g_iniAt;
static void *g_ag, *g_ctx; static int g_can; static DWORD g_canAt;     // from the binding's update (main thread)
static DWORD g_armed, g_upAt; static int g_queued; static Vec3 g_at;     // we entered the mode for a Ctrl + left click

static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsCtrlAttack.log", g_dir);
    FILE *f = _wfopen(p, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static void load_ini(void) {
    wchar_t k[16];
    g_enabled = GetPrivateProfileIntW(L"CtrlAttack", L"Enabled", 1, g_ini);
    GetPrivateProfileStringW(L"CtrlAttack", L"Key", L"Ctrl", k, 16, g_ini);
    int v = !lstrcmpiW(k, L"Alt") ? VK_MENU : !lstrcmpiW(k, L"Ctrl") ? VK_CONTROL : (int)wcstol(k, NULL, 0);
    g_key = (v <= 0 || v > 254) ? VK_CONTROL : v;
    g_verbose = GetPrivateProfileIntW(L"CtrlAttack", L"Verbose", 0, g_ini);
}
static int rd(const void *p, unsigned n) { return p && !IsBadReadPtr(p, n); }
static int is_replay(void) {
    char *st = rd((void *)G_SETTINGS, 4) ? *(char **)G_SETTINGS : NULL;
    return rd(st + 0x118, 4) && (*(DWORD *)(st + 0x118) & T_PLAYBACK);
}
static char *targeting(void) { return rd(g_ag, 8) ? *(char **)((char *)g_ag + 4) : NULL; }

// the nearest unit (an entity of a squad) to the point, metres; under 3 m the click is on it - the game's own Ctrl + click
static float unit_dist(const Vec3 *p) {
    char *w = rd((void *)G_WORLD, 4) ? *(char **)G_WORLD : NULL; if (!w) return 0;
    void **v = p_ents(w + 4, NULL); if (!rd(v, 8)) return 0;
    float best = 1e9f;
    for (void **it = (void **)v[0]; it && it < (void **)v[1]; it++) {
        if (!*it || !p_esquad(*it, NULL)) continue;
        const Vec3 *q = p_epos(*it, NULL); if (!q) continue;
        float dx = q->x - p->x, dz = q->z - p->z, d = dx * dx + dz * dz;
        if (d < best) best = d;
    }
    return best < 1e9f ? sqrtf(best) : 1e9f;
}
static void enter_mode(void *binding, char *tg) {
    __asm__ volatile ("pushl %%edi\n\tmovl %1, %%edi\n\tcall *%2\n\tpopl %%edi" : : "c"(binding), "r"(tg), "r"((DWORD)FN_ENTERTGT)
                      : "eax", "edx", "memory", "cc");
}
static void leave_mode(char *tg) {                    // as 0x00AD59C0 after a targeting click that gave an order
    void *b = *(void **)(tg + 8); if (!b) return;
    char *hud = rd((void *)G_HUD, 4) ? *(char **)G_HUD : NULL;
    char *h8 = rd(hud + 8, 4) ? *(char **)(hud + 8) : NULL;
    char *wi = rd(h8 + 0x90, 4) ? *(char **)(h8 + 0x90) : NULL;
    if (rd(wi + 0x40, 4) && rd(*(char **)(wi + 0x40) + 8, 4)) { *(DWORD *)(*(char **)(wi + 0x40) + 8) = 0; *(DWORD *)(wi + 8) = 0; }
    ((void (__fastcall *)(void *, void *))(*(void ***)b)[0x38 / 4])(b, NULL);
    *(void **)(tg + 8) = NULL;
}

static unsigned char __fastcall hk_update(void *self, void *edx, void *ctx) {
    unsigned char r = o_update(self, edx, ctx);
    g_ag = self; g_ctx = ctx; g_can = r; g_canAt = GetTickCount();
    return r;
}
static LRESULT CALLBACK hk_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_LBUTTONDOWN && g_installed) {
        DWORD now = GetTickCount();
        if (now - g_iniAt > 2000) { load_ini(); g_iniAt = now; }
        int key = g_key == VK_CONTROL ? (w & MK_CONTROL) != 0 : (GetAsyncKeyState(g_key) & 0x8000) != 0;
        char *tg = targeting();
        if (key && g_enabled && g_ag && tg && !*(void **)(tg + 8) && !is_replay()) {
            const Vec3 *cur = rd((char *)g_ctx + 0xB8, 12) ? (const Vec3 *)((char *)g_ctx + 0xB8) : NULL;
            if (!g_can || now - g_canAt > 1000) { if (g_verbose) logmsg("Ctrl + left click: no selected squad can attack the ground - the game's own click"); }
            else if (!cur || unit_dist(cur) < 3.0f) { if (g_verbose) logmsg("Ctrl + left click on a unit (%.1f m) - the game's own click", cur ? unit_dist(cur) : 0.0f); }
            else {
                if (g_verbose) logmsg("Ctrl + left click on the ground, nearest unit %.1f m - Attack ground", unit_dist(cur));
                enter_mode(g_ag, tg);
                g_armed = now; g_upAt = 0; g_at = *cur; g_queued = (w & MK_SHIFT) != 0;
            }
        }
    }
    if (m == WM_LBUTTONUP && g_armed) g_upAt = GetTickCount();
    return CallWindowProcW(o_wndproc, h, m, w, l);
}
// every HUD update, before the game's own dispatch of the targeting click (main thread)
void __cdecl ca_frame(void *ctx, char *tg) {
    (void)ctx;
    if (!g_armed || !rd(tg, 0x10) || tg != targeting()) return;
    DWORD now = GetTickCount();
    if (*(void **)(tg + 8) != g_ag) {                 // the click went through: the game left the mode after the order
        logmsg("Ctrl + left click at (%.1f, %.1f)%s: attack ground ordered (the game's own Attack ground)", g_at.x, g_at.z, g_queued ? " queued" : "");
        g_armed = 0; return;
    }
    if ((g_upAt && now - g_upAt > 400) || now - g_armed > 5000) {   // no order (e.g. inside the minimum range): leave the mode
        leave_mode(tg); g_armed = 0;
        logmsg("Ctrl + left click at (%.1f, %.1f): no attack-ground order (too close?) - mode left", g_at.x, g_at.z);
    }
}
unsigned char *o_tramp;                               // global: the asm stub jumps through it
__asm__(".text\n.globl _ca_stub\n_ca_stub:\n"
        "\tpushl %esi\n\tpushl 8(%esp)\n\tcall _ca_frame\n\taddl $8, %esp\n"
        "\tjmp *_o_tramp\n");
void ca_stub(void);

static int bytes_ok(DWORD at, const char *hex, const char *what) {
    unsigned char want[32]; int n = 0;
    for (const char *s = hex; s[0] && s[1] && n < 32; s += 2) { unsigned v; sscanf(s, "%2x", &v); want[n++] = (unsigned char)v; }
    if (IsBadReadPtr((void *)at, n)) { logmsg("%s: %08lX unreadable - not hooking", what, (unsigned long)at); return 0; }
    if (memcmp((void *)at, want, n)) {
        char got[80]; for (int i = 0; i < n; i++) sprintf(got + 2 * i, "%02x", ((unsigned char *)at)[i]);
        logmsg("%s: %08lX holds %s, not %s (not 3.19, or hooked by another add-on) - not hooking", what, (unsigned long)at, got, hex); return 0;
    }
    logmsg("%s: %08lX = %s ok", what, (unsigned long)at, hex);
    return 1;
}
static int slot_ok(DWORD at, DWORD expect, const char *what) {
    if (IsBadReadPtr((void *)at, 4) || *(DWORD *)at != expect) { logmsg("%s: %08lX is not %08lX - not hooking", what, (unsigned long)at, (unsigned long)expect); return 0; }
    logmsg("%s: %08lX -> %08lX ok", what, (unsigned long)at, (unsigned long)expect); return 1;
}
static void put32(DWORD at, DWORD v) { DWORD o; VirtualProtect((void *)at, 4, PAGE_EXECUTE_READWRITE, &o); *(DWORD *)at = v; VirtualProtect((void *)at, 4, o, &o); }
static BOOL CALLBACK find_wnd(HWND h, LPARAM l) {
    DWORD pid; wchar_t t[128]; GetWindowThreadProcessId(h, &pid); GetWindowTextW(h, t, 128);
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER) && wcsstr(t, L"Dawn of War")) { *(HWND *)l = h; return FALSE; }
    return TRUE;
}
static int install(void) {
    HMODULE m = GetModuleHandleA("SimEngine.dll"); if (!m) return 0;
    p_ents  = (GetEnts_t)GetProcAddress(m, "?GetEntities@EntityManager@@QAEAAV?$vector@PAVEntity@@V?$allocator@PAVEntity@@@_STL@@@_STL@@XZ");
    p_esquad = (P0_t)GetProcAddress(m, "?GetSquad@Entity@@QAEPAVSquad@@XZ");
    p_epos  = (EntPos_t)GetProcAddress(m, "?GetPosition@Entity@@QBEABVVector3f@Math@@XZ");
    if (!p_ents || !p_esquad || !p_epos) { logmsg("SimEngine.dll functions not found - not hooking"); return 0; }
    if (!slot_ok(VT_AGROUND + SLOT_UPDATE, ORIG_UPDATE, "BindingAttackGround update") ||
        !slot_ok(VT_AGROUND + SLOT_CLICK, 0x00B70610u, "BindingAttackGround targeting click") ||
        !bytes_ok(FN_ENTERTGT, "558bec83e4f864a100000000", "enter targeting mode") ||
        !bytes_ok(FN_GHOTKEY, "837c24080057750e", "G hotkey: enter targeting") ||
        !bytes_ok(FN_CLICKDISP, "558bec83e4f88b4e08", "HUD targeting click dispatch") ||
        !bytes_ok(AT_BTNSEND, "6a30ff15ec8bf800", "attack ground: SCMD_Attack order")) return 0;
    for (int i = 0; i < 600 && !g_wnd; i++) { EnumWindows(find_wnd, (LPARAM)&g_wnd); if (!g_wnd) Sleep(500); }
    if (!g_wnd) { logmsg("no game window - not hooking"); return 0; }
    o_tramp = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    unsigned char *t = (unsigned char *)FN_CLICKDISP;
    memcpy(o_tramp, t, 6); o_tramp[6] = 0xE9; *(DWORD *)(o_tramp + 7) = (DWORD)(t + 6) - (DWORD)(o_tramp + 11);
    DWORD o; VirtualProtect(t, 6, PAGE_EXECUTE_READWRITE, &o);
    t[0] = 0xE9; *(DWORD *)(t + 1) = (DWORD)ca_stub - (DWORD)(t + 5); t[5] = 0x90;
    VirtualProtect(t, 6, o, &o); FlushInstructionCache(GetCurrentProcess(), t, 6);
    o_update = (Update_t)ORIG_UPDATE; put32(VT_AGROUND + SLOT_UPDATE, (DWORD)hk_update);
    o_wndproc = (WNDPROC)SetWindowLongPtrW(g_wnd, GWLP_WNDPROC, (LONG_PTR)hk_wndproc);
    return 1;
}
static DWORD WINAPI worker(LPVOID _) {
    (void)_;
    for (int i = 0; i < 120 && !GetModuleHandleA("SimEngine.dll"); i++) Sleep(500);
    g_installed = install();
    logmsg("CtrlAttack %d start: hold %s and LEFT-click the ground - the selected squads that can attack the ground fire there - %s%s", CA_VERSION,
           g_key == VK_CONTROL ? "Ctrl" : g_key == VK_MENU ? "Alt" : "the key", g_installed ? "installed" : "NOT installed",
           g_enabled ? "" : " (Enabled=0 in the ini for now)");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0; else g_dir[0] = 0;
    _snwprintf(g_ini, MAX_PATH, L"%lsCtrlAttack.ini", g_dir);
    char mx[64]; wsprintfA(mx, "Local\\dow2patch_CtrlAttack_%lu", GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, mx);
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("another CtrlAttack copy is already active in this game, this one stays idle"); return TRUE; }
    load_ini(); g_iniAt = GetTickCount();
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
