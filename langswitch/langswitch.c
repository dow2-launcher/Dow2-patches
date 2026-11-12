// LangSwitch.dll - switching the keyboard language inside the game window (Alt+Shift / Ctrl+Shift / Win+Space), for the chat
// (Dawn of War II: Retribution 3.19, Elite and vanilla). Orca 12.11.2026: in the game window the system combinations do not
// switch the language, players alt-tab out to switch and come back.
//
// Why (Platform.dll 3.19 + DOW2.exe 3.19.10320):
//  - The game's message loop is Plat::Window::ProcessMessages (Platform.dll 0x1000B7B0): PeekMessageA(&msg, <game window>, 0, 0,
//    PM_REMOVE) in a loop - with the game window as the filter. Messages posted to any OTHER window of the main thread (the
//    "Default IME" window, the hidden windows of the Text Services Framework: MSCTFIME UI, CicMarshalWnd...) and messages posted
//    to the thread itself are never taken out of the queue nor dispatched. On Windows 8/10/11 the language switch (the
//    hotkeys and Win+Space, the language bar) is done by the text services of the window's thread through exactly such posted
//    messages - so the request never reaches the game's thread and the language stays (the likely cause: the stand shows the
//    queue part - a hidden window of the game thread never gets its posted messages - but Wine has no such language switch). Alt-tab works because the switch then
//    happens in another program and the game only gets the new layout SENT to it when it is activated again (sent messages
//    are delivered by any PeekMessage, whatever the filter).
//  - The game's own window procedure chain does not swallow the language messages: Platform's subclass 0x1000B190 ->
//    DOW2.exe 0x0067F7F0 (keys / chars / IME composition to Scaleform, returns 0 for 0x50/0x51) -> Win32InputDevice
//    0x10003210 (handles only keys and mouse) -> the class procedure 0x1000B380 -> DefWindowProcA for WM_INPUTLANGCHANGEREQUEST
//    (0x50) and WM_INPUTLANGCHANGE (0x51). Spooge.dll's subclass passes everything on. So the posted request, if it arrives,
//    is carried out by DefWindowProc.
//  - Typing after a switch: TranslateMessage (Win32InputDevice +0x10, 0x100038D0) makes WM_CHAR with the thread's CURRENT
//    layout, DOW2.exe 0x0067F7F0 turns it into the Scaleform char event (CP_ACP -> UTF-16) - the chat follows the layout
//    by itself. (Scaleform's IME manager, 0x00E9EE40, only reports the language name to ActionScript: getInputLanguage.)
//
// LangSwitch:
//  1. Platform.dll's import of PeekMessageA is pointed at our function. When the game's loop asks for the next message of its
//     window, the messages queued before it for OTHER windows of the main thread and for the thread itself are taken out
//     and dispatched first (as any ordinary Windows message loop does): the text services get their messages, the language
//     switch is done in the game's thread. The game's own messages, WM_QUIT and the order of the game's messages are not
//     touched (at most 64 others per call; WM_PAINT of a window that does not validate itself is validated).
//  2. Fallback: the game's keys are watched (only watched, the game gets them as before). After a clean Alt+Shift or
//     Ctrl+Shift (no other key or mouse button in between) that Windows has as the language / layout hotkey
//     (HKCU\Keyboard Layout\Toggle), if the layout of the game's thread has not changed in FallbackDelay ms (500), LangSwitch
//     asks for the next one itself: WM_INPUTLANGCHANGEREQUEST to the game window (DefWindowProc activates it), and if that is
//     not done in another FallbackDelay, ActivateKeyboardLayout. Win+Space is the shell's, only (1) helps there.
// Nothing in the simulation, no drawing, no files read while running. LangSwitch.log tells which way the switch was done.
//
// LangSwitch.ini [LangSwitch]: Enabled=1, Pump=1, Fallback=1, FallbackDelay=500, Verbose=0.
// Built with -DSTAND_PROBE (stand only, not shipped): a hidden window of the game's thread gets posted messages from another
// thread (as the text services' windows do) and the layout requests are posted to the game window - counted in the log.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LS_VERSION  1
#define MAX_OTHERS  64                 // messages of other windows dispatched per call of the game's PeekMessageA
#define MAX_SEEN    24                 // window classes / messages logged once each

typedef BOOL (WINAPI *PeekA_t)(LPMSG, HWND, UINT, UINT, UINT);
static PeekA_t o_peek;
static DWORD *g_slot;
static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH];
static int g_pump = 1, g_fallback = 1, g_delay = 500, g_verbose = 0;
static int g_in;                                   // inside our pump (no recursion)
static HWND g_game;                                // the game's window (the filter of its loop)
static DWORD g_thread;                             // the game's thread
static unsigned long g_pumped, g_threadmsgs;
static struct { char cls[48]; UINT msg; } g_seen[MAX_SEEN]; static int g_nseen;
static int g_loglines;

// hotkeys from the Windows settings: '1' = Alt+Shift, '2' = Ctrl+Shift, '3' = none, '4' = grave accent
static char g_hk_lang = '1', g_hk_layout = '2';
#define M_SHIFT 1
#define M_CTRL  2
#define M_ALT   4
static int g_sess, g_taint, g_fired;              // modifiers held in this press, another key/button seen, done
static struct { DWORD t; HKL hkl0; int kind; int step; HKL target; } g_pend;   // kind 1 = language, 2 = layout

static void logmsg(const char *fmt, ...) {
    if (g_loglines > 400) return;                  // a session never fills the disk
    g_loglines++;
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsLangSwitch.log", g_dir);
    FILE *f = _wfopen(p, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static const char *hkname(char c) { return c == '1' ? "Alt+Shift" : c == '2' ? "Ctrl+Shift" : c == '4' ? "grave accent" : "none"; }
static void read_hotkeys(void) {
    HKEY k; char v[8]; DWORD n, ty;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Keyboard Layout\\Toggle", 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    n = sizeof v; if (RegQueryValueExA(k, "Language Hotkey", NULL, &ty, (BYTE *)v, &n) == ERROR_SUCCESS && ty == REG_SZ && n > 1) g_hk_lang = v[0];
    else { n = sizeof v; if (RegQueryValueExA(k, "Hotkey", NULL, &ty, (BYTE *)v, &n) == ERROR_SUCCESS && ty == REG_SZ && n > 1) g_hk_lang = v[0]; }
    n = sizeof v; if (RegQueryValueExA(k, "Layout Hotkey", NULL, &ty, (BYTE *)v, &n) == ERROR_SUCCESS && ty == REG_SZ && n > 1) g_hk_layout = v[0];
    RegCloseKey(k);
}
static void log_layouts(const char *why) {
    HKL l[16]; int n = GetKeyboardLayoutList(16, l); char s[256] = ""; int o = 0;
    for (int i = 0; i < n && o < 200; i++) o += _snprintf(s + o, sizeof s - o, " %08lX", (unsigned long)(ULONG_PTR)l[i]);
    logmsg("%s: layout %08lX, installed:%s", why, (unsigned long)(ULONG_PTR)GetKeyboardLayout(0), n > 0 ? s : " (none)");
}

// ---- 1. the messages of the main thread's other windows and of the thread itself ----
static void seen(HWND h, UINT m) {
    char c[48] = "(thread)";
    if (h && !GetClassNameA(h, c, sizeof c)) strcpy(c, "?");
    for (int i = 0; i < g_nseen; i++) if (g_seen[i].msg == m && !strcmp(g_seen[i].cls, c)) return;
    if (g_nseen >= MAX_SEEN) return;
    strcpy(g_seen[g_nseen].cls, c); g_seen[g_nseen].msg = m; g_nseen++;
    logmsg("dispatched a message the game's loop leaves in the queue: window class \"%s\", message 0x%04X (%lu so far)", c, m, g_pumped);
}
static void pump_others(HWND game) {
    MSG q, r;
    for (int i = 0; i < MAX_OTHERS; i++) {
        if (!PeekMessageW(&q, NULL, 0, 0, PM_NOREMOVE)) return;
        if (q.message == WM_QUIT) return;                                   // the game's own business
        if (q.hwnd && (q.hwnd == game || IsChild(game, q.hwnd))) return;    // the game takes it now, in its order
        HWND from = q.hwnd ? q.hwnd : (HWND)-1;                            // -1: only messages posted to the thread
        if (!PeekMessageW(&r, from, q.message, q.message, PM_REMOVE)) return;
        g_pumped++; if (!r.hwnd) g_threadmsgs++;
        seen(r.hwnd, r.message);
        if (r.hwnd || r.message == WM_TIMER) DispatchMessageW(&r);         // posted to the thread: taken out (and the
        if (r.message == WM_PAINT && r.hwnd) ValidateRect(r.hwnd, NULL);   // thread's message hooks saw it); no window
    }
}

// ---- 2. fallback: the hotkey seen in the game's keys, the layout did not change ----
static HKL next_layout(HKL cur, int kind) {
    HKL l[32]; int n = GetKeyboardLayoutList(32, l), at = -1;
    if (n < 2) return NULL;
    for (int i = 0; i < n; i++) if (l[i] == cur) at = i;
    for (int k = 1; k < n; k++) {
        HKL c = l[((at < 0 ? 0 : at) + k) % n];
        if (kind == 1 && LOWORD((ULONG_PTR)c) != LOWORD((ULONG_PTR)cur)) return c;    // the next language
        if (kind == 2 && LOWORD((ULONG_PTR)c) == LOWORD((ULONG_PTR)cur) && c != cur) return c;   // the next layout of this language
    }
    return kind == 1 && at < 0 ? l[0] : NULL;
}
static int mods_down(void) {
    return ((GetKeyState(VK_SHIFT) & 0x8000) ? M_SHIFT : 0) | ((GetKeyState(VK_CONTROL) & 0x8000) ? M_CTRL : 0) |
           ((GetKeyState(VK_MENU) & 0x8000) ? M_ALT : 0);
}
static int modbit(WPARAM vk) {
    switch (vk) { case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return M_SHIFT;
                  case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return M_CTRL;
                  case VK_MENU: case VK_LMENU: case VK_RMENU: return M_ALT; }
    return 0;
}
static void hotkey(int sess) {
    char c = sess == (M_ALT | M_SHIFT) ? '1' : '2';
    int kind = g_hk_lang == c ? 1 : g_hk_layout == c ? 2 : 0;
    if (g_verbose || kind) logmsg("%s pressed in the game (Windows: language %s, layout %s)%s", hkname(c), hkname(g_hk_lang),
                                  hkname(g_hk_layout), kind ? "" : " - not a switch hotkey in the Windows settings, nothing to do");
    if (!kind || !g_fallback) return;
    g_pend.t = GetTickCount(); g_pend.hkl0 = GetKeyboardLayout(0); g_pend.kind = kind; g_pend.step = 0; g_pend.target = NULL;
}
static void watch(const MSG *m) {
    switch (m->message) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        int b = modbit(m->wParam);
        if (b) g_sess = (g_sess & (mods_down() | b)) | b;      // stale bits (a key let go outside the game) dropped
        else if (g_sess || mods_down()) g_taint = 1;
        break; }
    case WM_KEYUP: case WM_SYSKEYUP:
        if (modbit(m->wParam)) {
            if (!g_fired && !g_taint && (g_sess == (M_ALT | M_SHIFT) || g_sess == (M_CTRL | M_SHIFT))) { g_fired = 1; hotkey(g_sess); }
            if (!mods_down()) g_sess = g_taint = g_fired = 0;
        }
        break;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN: case WM_MOUSEWHEEL:
        if (g_sess) g_taint = 1;
        break;
    }
}
static void fallback_tick(HWND game) {
    if (!g_pend.kind) return;
    HKL now = GetKeyboardLayout(0); DWORD dt = GetTickCount() - g_pend.t;
    if (now != g_pend.hkl0) {
        logmsg("layout %08lX -> %08lX in %lu ms (%s)", (unsigned long)(ULONG_PTR)g_pend.hkl0, (unsigned long)(ULONG_PTR)now, dt,
               g_pend.step == 0 ? "Windows did it" : g_pend.step == 1 ? "LangSwitch: WM_INPUTLANGCHANGEREQUEST" : "LangSwitch: ActivateKeyboardLayout");
        g_pend.kind = 0; return;
    }
    if (dt < (DWORD)g_delay * (g_pend.step + 1)) return;
    if (g_pend.step == 0) {
        g_pend.target = next_layout(now, g_pend.kind);
        if (!g_pend.target) { logmsg("no change in %lu ms and no other %s installed - nothing to do", dt, g_pend.kind == 1 ? "language" : "layout of this language");
                              g_pend.kind = 0; return; }
        logmsg("no change in %lu ms: asking for %08lX (WM_INPUTLANGCHANGEREQUEST)", dt, (unsigned long)(ULONG_PTR)g_pend.target);
        PostMessageW(game, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)g_pend.target); g_pend.step = 1;
    } else if (g_pend.step == 1) {
        logmsg("still no change in %lu ms: ActivateKeyboardLayout(%08lX)", dt, (unsigned long)(ULONG_PTR)g_pend.target);
        if (!ActivateKeyboardLayout(g_pend.target, 0)) logmsg("ActivateKeyboardLayout failed, error %lu", GetLastError());
        g_pend.step = 2;
    } else { logmsg("the layout did not change (%lu ms) - giving up", dt); g_pend.kind = 0; }
}

#ifdef STAND_PROBE
#include "probe.h"
#endif

static BOOL WINAPI ls_peek(LPMSG m, HWND h, UINT mn, UINT mx, UINT rm) {
    int loop = h && (rm & PM_REMOVE) && !mn && !mx && !g_in;      // the game's own loop: PeekMessageA(&m, window, 0, 0, PM_REMOVE)
    if (loop) {
        if (!g_game) { g_game = h; g_thread = GetCurrentThreadId(); logmsg("the game's message loop: window %p, thread %lu", h, g_thread); }
#ifdef STAND_PROBE
        probe_tick(h);
#endif
        g_in = 1;
        if (g_pump) pump_others(h);
        if (g_fallback) fallback_tick(h);
        g_in = 0;
    }
    BOOL r = o_peek(m, h, mn, mx, rm);
    if (r && loop && g_fallback) watch(m);
    return r;
}

static int hook_iat(void) {
    HMODULE plat = GetModuleHandleA("Platform.dll");
    if (!plat) return 0;
    unsigned char *b = (unsigned char *)plat;
    // Plat::Window::ProcessMessages of 3.19: PeekMessageA(&msg, hWnd, WM_QUIT, WM_QUIT, PM_NOREMOVE) first
    unsigned char *pm = (unsigned char *)GetProcAddress(plat, "?ProcessMessages@Window@Plat@@YGXXZ");
    static const unsigned char pat[] = {0x6A, 0x00, 0x6A, 0x12, 0x6A, 0x12};
    if (!pm || IsBadReadPtr(pm, 0x40)) { logmsg("Platform.dll has no ProcessMessages - not changing anything"); return -1; }
    int ok = 0; for (int i = 0; i < 0x30 && !ok; i++) ok = !memcmp(pm + i, pat, sizeof pat);
    if (!ok) { logmsg("Platform.dll ProcessMessages is not the one of 3.19 (%p) - not changing anything", pm); return -1; }
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)b; IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(b + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    void *real = (void *)GetProcAddress(GetModuleHandleA("user32.dll"), "PeekMessageA");
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(b + dd->VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)(b + d->Name), "user32.dll")) continue;
        IMAGE_THUNK_DATA32 *nm = (IMAGE_THUNK_DATA32 *)(b + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA32 *at = (IMAGE_THUNK_DATA32 *)(b + d->FirstThunk);
        for (; nm->u1.AddressOfData; nm++, at++) {
            if (nm->u1.Ordinal & IMAGE_ORDINAL_FLAG32) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(b + nm->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, "PeekMessageA")) continue;
            void *cur = (void *)at->u1.Function;
            if ((DWORD)(ULONG_PTR)cur == nm->u1.AddressOfData || (ULONG_PTR)cur < 0x10000) return 0;   // imports not bound yet
            if (cur != real) logmsg("Platform.dll PeekMessageA already points to %p (user32 %p) - chaining to it", cur, real);
            o_peek = (PeekA_t)cur; g_slot = (DWORD *)&at->u1.Function;
            DWORD old; VirtualProtect(g_slot, 4, PAGE_READWRITE, &old);
            InterlockedExchange((LONG *)g_slot, (LONG)(ULONG_PTR)ls_peek); VirtualProtect(g_slot, 4, old, &old);
            return 1;
        }
    }
    logmsg("Platform.dll does not import PeekMessageA - not changing anything"); return -1;
}

static DWORD WINAPI worker(LPVOID _) {          // Platform.dll not loaded / not bound yet (an early loader)
    int r = 0;
    for (int i = 0; i < 1200 && (r = hook_iat()) == 0; i++) Sleep(50);
    if (r == 1) logmsg("hooked Platform.dll PeekMessageA (the game's message loop)");
    else if (r == 0) logmsg("Platform.dll not loaded in 60 s - not changing anything");
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
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
    _snwprintf(g_ini, MAX_PATH, L"%lsLangSwitch.ini", g_dir);
    if (already_loaded("LangSwitch")) { logmsg("another LangSwitch copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"LangSwitch", L"Enabled", 1, g_ini)) { logmsg("LangSwitch %d: disabled in LangSwitch.ini", LS_VERSION); return TRUE; }
    g_pump = GetPrivateProfileIntW(L"LangSwitch", L"Pump", 1, g_ini) != 0;
    g_fallback = GetPrivateProfileIntW(L"LangSwitch", L"Fallback", 1, g_ini) != 0;
    g_delay = GetPrivateProfileIntW(L"LangSwitch", L"FallbackDelay", 500, g_ini);
    if (g_delay < 100) g_delay = 100; if (g_delay > 5000) g_delay = 5000;
    g_verbose = GetPrivateProfileIntW(L"LangSwitch", L"Verbose", 0, g_ini);
    read_hotkeys();
    OSVERSIONINFOA ov; ov.dwOSVersionInfoSize = sizeof ov; GetVersionExA(&ov);
    logmsg("LangSwitch %d%s: pump %s, fallback %s (%d ms); Windows %lu.%lu.%lu; hotkeys: language %s, layout %s", LS_VERSION,
#ifdef STAND_PROBE
           " (STAND PROBE)",
#else
           "",
#endif
           g_pump ? "on" : "off", g_fallback ? "on" : "off", g_delay, ov.dwMajorVersion, ov.dwMinorVersion, ov.dwBuildNumber,
           hkname(g_hk_lang), hkname(g_hk_layout));
    log_layouts("start");
    if (hook_iat() == 0) CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    else if (o_peek) logmsg("hooked Platform.dll PeekMessageA (the game's message loop)");
    return TRUE;
}
