// ChatHide.dll - hide / show incoming player chat during a match (DoW2: Retribution 3.19, Elite and vanilla).
// A small speech-bubble button by the minimap (the game's own minimap movie, no Direct3D) and Ctrl+Shift+C toggle it.
// While hidden, chat lines from other players (all, team, observer, referee) are not shown; system lines and the player's
// own messages still are. Each toggle shows one system line ("Chat hidden" / "Chat shown"). The state is kept in ChatHide.ini.
//
// How the game shows a chat line (DOW2.exe 3.19):
//   - a received or locally echoed message goes to OnChatMessage 0xB20710 (the HUD chat listener 0xACC680), which picks
//     the type from the message's channel (msg+0x10) and team flag (msg+0x14): 0 all, 3 team, 5 observer, 6 referee,
//     4 system - and calls the ONE display function at 0xB207EB: call 0xB214F0 (thiscall ECX = msg; handler, &text, type;
//     ret 0xC). 0xB214F0 resets the chat panel fade, formats the line and invokes the chat movie's Receive_AddMessage.
//     ChatHide sits on that call site: while hidden, types 0/3/5/6 return at once (nothing shown, no sound).
//   - sending: 0xB20F30 -> 0x401B6D -> call at 0x401D97 -> 0x429672 sends the packet and echoes the message locally through
//     the same listener SYNCHRONOUSLY (the network receive drops packets from oneself). ChatHide marks that call: any
//     display during it is the player's own message and is kept (also an observer's own message, sender id 0).
//   - the toggle line: ChatHide calls 0xB214F0 itself with type 4 (system: the game's own look, fade and sound) once the
//     chat handler is known (captured from the first displayed line); before that, the chat movie's Receive_AddMessage.
//   - the button: the minimap panel (ui/movies/hud/minimap_panel.gfx, form vtable 0x011160D8, slot +0x10 = its per-frame
//     update 0xB3F6E0) gets an empty clip "ch_btn" with the game's own textures generic/bridge/bubble_up (shown; hover
//     bubble_over) and war_room/starmap/cross turned 45 degrees over a dimmed bubble (hidden). The clip gets the movie's
//     default mouse callbacks (Receive_RegisterDefaultCallbacks), so the game sees the cursor over its interface and a click
//     on the button does not reach the world (units stay selected). Clicks are read like ReplaySpeed / Relay: the player's
//     own left button (GetAsyncKeyState) over the clip (_xmouse/_ymouse), the game window in front.
// Rules: docs/UI_RULES.md (nothing touches Direct3D), docs/ADDON_RULES.md. ChatHide.ini: [ChatHide] Enabled, Hidden,
// Hotkey (1 = Ctrl+Shift+C, 0 = off), Button (1/0), OffsetX / OffsetY (button position, minimap movie units).
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../common/gfxui.h"

#define VERSION "2"

#define AT_SHOW     0x00B207EBu   // call 0xB214F0 in OnChatMessage: the one place a chat line is shown
#define FN_SHOW     0x00B214F0u
#define AT_SEND     0x00401D97u   // call 0x429672: send + local echo of the player's own message
#define FN_SEND     0x00429672u
#define MM_SLOT     (0x011160D8u + 0x10)   // minimap panel form, per-frame update
#define MM_FN       0x00B3F6E0u
#define G_CHATFORMS 0x01335088u   // 0x6801C0 (thiscall on *this, form id *(0x12BB73C), 1) = the chat panel form
#define G_CHATID    0x012BB73Cu
#define FN_FORM     0x006801C0u
#define G_HUD       0x01335720u
#define G_SETTINGS  0x01335A78u

static wchar_t g_log[MAX_PATH], g_ini[MAX_PATH];
static int g_logLines;
static void logmsg(const char *fmt, ...) {
    if (g_logLines > 3000) return; g_logLines++;
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static int okp(const void *p, UINT n) { return p && (DWORD)p >= 0x10000 && !IsBadReadPtr(p, n); }
#define P32(p, o) (*(char **)((char *)(p) + (o)))
#define I32(p, o) (*(int *)((char *)(p) + (o)))
static LONGLONG g_qf;
static LONGLONG qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// ---- state (all on the game's main thread)
static volatile int g_hidden;          // chat hidden
volatile int g_sending; volatile LONG g_sendCalls;                // inside the player's own send (0x429672): every line shown then is his own
static char *g_handler;                // chat handler (arg 1 of 0xB214F0), captured from the first displayed line
static void *g_mvSeen;                // minimap movie of the current match (a new one = a new match)
static int g_hotkey = 1, g_button = 1; static double g_offX, g_offY;
static LONG g_shown, g_dropped, g_own;

static void save_state(void) { WritePrivateProfileStringW(L"ChatHide", L"Hidden", g_hidden ? L"1" : L"0", g_ini); }

// ---- the display call site
static const char *TYPE_NAME[7] = { "all", "?1", "?2", "team", "system", "observer", "referee" };
static char *cand_handler(void) {      // the HUD path the listener uses (*(*(listener+8)+0xBC)+0x50), tried from the HUD global
    char *hud = okp((void *)G_HUD, 4) ? *(char **)G_HUD : NULL, *h = okp(hud, 12) ? P32(hud, 8) : NULL, *c = okp(h, 0xC0) ? P32(h, 0xBC) : NULL;
    return okp(c, 0x60) ? c + 0x50 : NULL;
}
int __cdecl ch_decide(char *handler, int type) {
    if (handler != g_handler && okp(handler, 0x5C)) {
        char *c = cand_handler();
        logmsg("chat handler %p (from the HUD global: %p - %s)", handler, c, c == handler ? "same" : "differs");
        g_handler = handler;
    }
    int drop = g_hidden && !g_sending && (type == 0 || type == 3 || type == 5 || type == 6);
    if (drop) g_dropped++; else if (g_sending) g_own++; else g_shown++;
    logmsg("line %s%s: %s (shown %ld, own %ld, hidden %ld)", (unsigned)type < 7 ? TYPE_NAME[type] : "?", g_sending ? " (own)" : "",
           drop ? "HIDDEN" : "shown", g_shown, g_own, g_dropped);
    return drop;
}
void *g_fnShow = (void *)FN_SHOW, *g_fnSend = (void *)FN_SEND;
DWORD g_sendRet;
// entry: [esp] ret, [esp+4] handler, [esp+8] &text, [esp+0xC] type; drop = the callee's own ret 0xC
__attribute__((naked)) void stub_show(void) {
    __asm__ volatile(
        "pushl %eax\n\tpushl %ecx\n\tpushl %edx\n\t"
        "pushl 0x18(%esp)\n\t"          // type
        "pushl 0x14(%esp)\n\t"          // handler
        "call _ch_decide\n\t"
        "addl $8, %esp\n\t"
        "testl %eax, %eax\n\t"
        "popl %edx\n\tpopl %ecx\n\tpopl %eax\n\t"
        "jnz 1f\n\t"
        "jmp *_g_fnShow\n"
        "1:\n\tret $0xC\n");
}
// the player's own send (6 args, ret 0x18): flag set around the call; main thread only, not reentrant
__attribute__((naked)) void stub_send(void) {
    __asm__ volatile(
        "popl _g_sendRet\n\t"
        "movl $1, _g_sending\n\t"
        "incl _g_sendCalls\n\t"
        "call *_g_fnSend\n\t"
        "movl $0, _g_sending\n\t"
        "pushl _g_sendRet\n\t"
        "ret\n");
}

// ---- one system line in the chat
static GfxVal gv_wstr(const wchar_t *s) { GfxVal v; memset(&v, 0, sizeof v); v.type = GV_WSTRING; v.v.ws = s; return v; }
typedef void *(__thiscall *Form_t)(void *self, int id, int create);
typedef void (__thiscall *Show_t)(void *msg, void *handler, void *text, int type);
static void *chat_movie(void) {
    void *self = okp((void *)G_CHATFORMS, 4) ? *(void **)G_CHATFORMS : NULL;
    if (!okp(self, 4) || !okp((void *)G_CHATID, 4)) return NULL;
    char *form = ((Form_t)FN_FORM)(self, *(int *)G_CHATID, 1);
    return gfx_movie_of_form(form);
}
static void sys_line(const wchar_t *text) {
    static wchar_t buf[160]; static char dummy[0x40]; static struct { wchar_t *b, *e, *c; } s;
    wcsncpy(buf, text, 159); buf[159] = 0; s.b = buf; s.e = buf + wcslen(buf); s.c = s.e;
    // only through a chat handler seen in THIS match (reset at every new match / new minimap movie): the handler of an
    // earlier match is freed with that match's HUD - v1 crashed calling it (Grantovich 09.11 19:18, ebx = the 19:01 handler)
    if (g_handler && okp(g_handler, 0x5C)) { memset(dummy, 0, sizeof dummy); ((Show_t)FN_SHOW)(dummy, g_handler, &s, 4); return; }
    logmsg("no chat line seen in this match yet - system line not shown (the button shows the state)");
}
static void toggle(const char *how) {
    g_hidden = !g_hidden; save_state();
    logmsg("%s: chat %s", how, g_hidden ? "HIDDEN" : "shown");
    sys_line(g_hidden ? L"Chat hidden (Ctrl+Shift+C or the bubble by the minimap shows it)" : L"Chat shown");
}

#ifdef CH_TEST
// ---- test only (never shipped): fake incoming lines from an AI player through the game's own local delivery 0x429A27
// (chat, name, sender id, team flag, text, recipients) -> listener -> OnChatMessage -> display, as a network receive would
typedef void (__stdcall *Inject_t)(void *chat, const wchar_t *name, int sender, int team, const wchar_t *text, void *recip);
static LONGLONG g_injAt; static int g_injN;
static void test_inject(LONGLONG now) {
    if (now - g_injAt < g_qf * 4) return; g_injAt = now;
    char *st = okp((void *)G_SETTINGS, 4) ? *(char **)G_SETTINGS : NULL;
    char *a = okp(st, 0x114) ? P32(st, 0x110) : NULL, *b = okp(a, 8) ? P32(a, 4) : NULL;
    char *chat = okp(b, 0xC0) ? P32(b, 0xBC) : NULL;   // as the send code 0x401D71 finds it
    if (!okp(chat, 0x14) || !okp(st, 0x930)) { logmsg("TEST: no chat object"); return; }
    int me = I32(chat, 0x10); char *gi = st + 0x118, *arr = P32(gi, 0x810); int n = I32(gi, 0x814), ally = -1, enemy = -1;
    if (!okp(arr, n * 0xA8) || n <= 0 || n > 8) { logmsg("TEST: no player list (%d)", n); return; }
    for (int i = 0; i < n; i++) { int id = I32(arr + i * 0xA8, 0x9C); if (id == me) continue; if (i % 2 == 0 && ally < 0) ally = id; if (i % 2 && enemy < 0) enemy = id; }
    int team = g_injN % 2, from = team ? ally : enemy; if (from < 0) from = team ? enemy : ally; if (from < 0) from = me + 1 + team;   // alone in a lobby: a made-up sender
    wchar_t t[64]; swprintf(t, 64, L"test %ls line %d", team ? L"team" : L"all", ++g_injN);
    logmsg("TEST: inject %s line %d from player %d (me %d, %d players)", team ? "team" : "all", g_injN, from, me, n);
    ((Inject_t)0x00429A27u)(chat, team ? L"Ally AI" : L"Enemy AI", from, team, t, NULL);
}
#endif

// ---- the button in the minimap panel
typedef void (__thiscall *Upd_t)(char *form, float dt);
typedef char (__thiscall *GfxGetVar_t)(void *movie, GfxVal *v, const char *path);
static Upd_t o_mm;
static void *g_mv; static int g_made, g_tries, g_shownHidden = -1, g_hover = -1, g_lbWas, g_press, g_keyWas, g_remind;
static LONGLONG g_madeAt, g_mmAt, g_matchAt;
static int getnum(void *mv, const char *path, double *out) {
    GfxVal v; memset(&v, 0, sizeof v);
    if (!(*(GfxGetVar_t *)(*(char **)mv + 0x30))(mv, &v, path) || v.type != GV_NUMBER) return 0;
    *out = v.v.num; return 1;
}
static int getbool(void *mv, const char *path) {
    GfxVal v; memset(&v, 0, sizeof v);
    return (*(GfxGetVar_t *)(*(char **)mv + 0x30))(mv, &v, path) && v.type == GV_BOOL && v.v.b;
}
#ifdef CH_TEST
static void probe(void *mv) {   // where things are in this movie (placement data, logged once per movie)
    static const char *C[] = { "inscribing_circle", "map_rotator", "camera", "chat_btn", "ping_btn", "defend_btn", "objectives_btn", "rally_point_btn" };
    for (int i = 0; i < 8; i++) {
        char p[96]; double x = -1, y = -1, w = -1, h = -1;
        snprintf(p, sizeof p, "_root.%s._x", C[i]); getnum(mv, p, &x); snprintf(p, sizeof p, "_root.%s._y", C[i]); getnum(mv, p, &y);
        snprintf(p, sizeof p, "_root.%s._width", C[i]); getnum(mv, p, &w); snprintf(p, sizeof p, "_root.%s._height", C[i]); getnum(mv, p, &h);
        snprintf(p, sizeof p, "_root.%s._visible", C[i]);
        logmsg("probe %-18s x %7.1f y %7.1f w %6.1f h %6.1f %s", C[i], x, y, w, h, getbool(mv, p) ? "visible" : "hidden");
    }
    double sw = -1, sh = -1, rs = -1; getnum(mv, "Stage.width", &sw); getnum(mv, "Stage.height", &sh); getnum(mv, "_root._xscale", &rs);
    logmsg("probe Stage %.0f x %.0f, root scale %.1f", sw, sh, rs);
}
#endif
static void loadimg(void *mv, const char *clip, const char *tex) {
    GfxVal a[2] = { gv_str(clip), gv_str(tex) }; gfx_invoke(mv, "Receive_LoadMovie", NULL, a, 2);
}
#define BW 68.0                        // bubble texture 68 x 64
#define BH 64.0
#define BTN_SCALE 38.0                 // ~26 px in the movie
static void make_button(void *mv) {
#ifdef CH_TEST
    probe(mv);
#endif
    if (!gfx_exists(mv, "ch_btn")) { GfxVal a[2] = { gv_str("ch_btn"), gv_num(15900) }; gfx_invoke(mv, "_root.createEmptyMovieClip", NULL, a, 2); }
    if (!gfx_exists(mv, "ch_btn")) { logmsg("minimap %p: could not make the button clip", mv); g_made = -1; return; }
    static const char *K[3] = { "bub", "ov", "xr" };
    for (int i = 0; i < 3; i++) { GfxVal a[2] = { gv_str(K[i]), gv_num(i + 1) }; gfx_invoke(mv, "_root.ch_btn.createEmptyMovieClip", NULL, a, 2); }
    { GfxVal a[2] = { gv_str("img"), gv_num(1) }; gfx_invoke(mv, "_root.ch_btn.xr.createEmptyMovieClip", NULL, a, 2); }
    loadimg(mv, "_root.ch_btn.bub", "img://data:UI/textures/generic/bridge/bubble_up.dds");
    loadimg(mv, "_root.ch_btn.ov", "img://data:UI/textures/generic/bridge/bubble_over.dds");
    loadimg(mv, "_root.ch_btn.xr.img", "img://data:UI/textures/generic/war_room/starmap/cross.dds");
    // the cross (148 x 148, a thin plus) turned 45 degrees about the bubble's middle = an X over it
    gfx_setvar(mv, "_root.ch_btn.xr.img._x", gv_num(-74)); gfx_setvar(mv, "_root.ch_btn.xr.img._y", gv_num(-74));
    gfx_setvar(mv, "_root.ch_btn.xr._x", gv_num(BW / 2)); gfx_setvar(mv, "_root.ch_btn.xr._y", gv_num(BH / 2));
    gfx_setvar(mv, "_root.ch_btn.xr._rotation", gv_num(45));
    gfx_setvar(mv, "_root.ch_btn.xr._xscale", gv_num(62)); gfx_setvar(mv, "_root.ch_btn.xr._yscale", gv_num(62));
    gfx_setvar(mv, "_root.ch_btn.ov._visible", gv_bool(0)); gfx_setvar(mv, "_root.ch_btn.xr._visible", gv_bool(0));
    gfx_setvar(mv, "_root.ch_btn._xscale", gv_num(BTN_SCALE)); gfx_setvar(mv, "_root.ch_btn._yscale", gv_num(BTN_SCALE));
    // the game's own mouse callbacks: the cursor over the clip counts as over the interface (Receive_HitTest)
    { GfxVal a = gv_str("_root.ch_btn"), r; memset(&r, 0, sizeof r); gfx_invoke(mv, "Receive_RegisterDefaultCallbacks", &r, &a, 1);
      logmsg("mouse callbacks on the button: %s", r.type == GV_BOOL && r.v.b ? "registered" : "NOT registered"); }
    // place: inside the minimap frame, on its right edge, halfway between the right corner buttons (top-right and
    // bottom-right); the frame = the corner buttons' bounds. Anything right of the movie's stage (328 wide) is clipped.
    double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9; int n = 0;
    static const char *B[4] = { "chat_btn", "ping_btn", "defend_btn", "objectives_btn" };
    for (int i = 0; i < 4; i++) {
        char p[64]; double x, y, w, h;
        snprintf(p, sizeof p, "_root.%s._x", B[i]); if (!getnum(mv, p, &x)) continue; snprintf(p, sizeof p, "_root.%s._y", B[i]); if (!getnum(mv, p, &y)) continue;
        snprintf(p, sizeof p, "_root.%s._width", B[i]); if (!getnum(mv, p, &w)) continue; snprintf(p, sizeof p, "_root.%s._height", B[i]); if (!getnum(mv, p, &h)) continue;
        if (x < x0) x0 = x; if (y < y0) y0 = y; if (x + w > x1) x1 = x + w; if (y + h > y1) y1 = y + h; n++;
    }
    double bw = BW * BTN_SCALE / 100, bh = BH * BTN_SCALE / 100, px, py;
    if (n >= 2) { px = x1 - bw; py = (y0 + y1) / 2 - bh / 2; } else { px = 4; py = 4; }
    px += g_offX; py += g_offY;
    gfx_setvar(mv, "_root.ch_btn._x", gv_num(px)); gfx_setvar(mv, "_root.ch_btn._y", gv_num(py));
    logmsg("minimap %p: button at %.1f,%.1f (%.1f x %.1f; frame from %d corner buttons %.1f,%.1f - %.1f,%.1f; offset %g,%g)",
           mv, px, py, bw, bh, n, x0, y0, x1, y1, g_offX, g_offY);
    g_made = 1; g_shownHidden = -1; g_hover = -1;
}
static void show_state(void *mv, int over) {
    if (g_hidden != g_shownHidden) {
        gfx_setvar(mv, "_root.ch_btn.bub._alpha", gv_num(g_hidden ? 55 : 100));
        gfx_setvar(mv, "_root.ch_btn.xr._visible", gv_bool(g_hidden));
        g_shownHidden = g_hidden;
    }
    if (over != g_hover) { gfx_setvar(mv, "_root.ch_btn.ov._visible", gv_bool(over)); g_hover = over; }
}
static void __thiscall hk_mm(char *form, float dt) {
    o_mm(form, dt);
    g_sending = 0;                                         // never inside a send here (an exception out of it would leave it set)
    void *mv = gfx_movie_of_form(form); if (!mv) return;
    LONGLONG now = qpc();
    if (now - g_mmAt > g_qf * 3 || mv != g_mvSeen) {      // a new match (no minimap update for 3 s, or a new minimap movie)
        if (g_handler) logmsg("new match: chat handler %p of the last match dropped", g_handler);
        g_handler = NULL; g_remind = 1; g_matchAt = now; g_mvSeen = mv;
    }
    g_mmAt = now;
    DWORD fpid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &fpid);
    int front = fpid == GetCurrentProcessId();
    if (g_button) {
        if (mv != g_mv) { g_mv = mv; g_made = 0; g_tries = 0; }
        if (g_made == -1 && g_tries < 10 && now - g_madeAt > g_qf * 2) g_made = 0;
        if (g_made == 0 || (g_made == 1 && !gfx_exists(mv, "ch_btn"))) { g_tries++; g_madeAt = now; make_button(mv); }
        if (g_made == 1) {
            double x = -1, y = -1;
            int vis = getbool(mv, "_root._visible");
            int over = front && vis && getnum(mv, "_root.ch_btn._xmouse", &x) && getnum(mv, "_root.ch_btn._ymouse", &y) && x >= 0 && y >= 0 && x < BW && y < BH;
            int lb = front && (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000);
            if (lb && !g_lbWas) g_press = over;
            if (!lb && g_lbWas && over && g_press) toggle("button");
            if (!lb) g_press = 0;
            g_lbWas = lb;
            show_state(mv, over);
        }
    }
    if (g_hotkey) {
        int k = front && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000)
                && (GetAsyncKeyState('C') & 0x8000);
        if (k && !g_keyWas) toggle("Ctrl+Shift+C");
        g_keyWas = k;
    }
    // hidden from an earlier match: say so once, a few seconds in (the chat panel is up by then)
    if (g_remind && g_hidden && now - g_matchAt > g_qf * 4) { g_remind = 0; logmsg("match start with chat hidden: reminder line"); sys_line(L"Chat hidden (Ctrl+Shift+C or the bubble by the minimap shows it)"); }
    else if (g_remind && !g_hidden) g_remind = 0;
#ifdef CH_TEST
    { static LONG seen; if (g_sendCalls != seen) { seen = g_sendCalls; logmsg("TEST: own send call %ld", seen); } }
    test_inject(now);
#endif
}

// ---- install
static int patch_call(DWORD at, DWORD expect, void *fn) {
    unsigned char *p = (unsigned char *)at; DWORD old;
    if (IsBadReadPtr(p, 5) || p[0] != 0xE8 || at + 5 + *(int *)(p + 1) != expect) return 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    InterlockedExchange((LONG *)(p + 1), (LONG)((DWORD)fn - (at + 5)));
    VirtualProtect(p, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), p, 5);
    return 1;
}
static void install(void) {
    // 3.19 code: 0xB214F0's prolog (push -1; push 0xED15FA) and the caller's argument pushes before 0xB207EB / 0x401D97
    if (IsBadReadPtr((void *)FN_SHOW, 8) || memcmp((void *)FN_SHOW, "\x6A\xFF\x68\xFA\x15\xED\x00", 7)
        || memcmp((void *)(AT_SHOW - 3), "\x56\x8B\xCB", 3) || memcmp((void *)(AT_SEND - 2), "\x51\x50", 2)) {
        logmsg("DOW2.exe is not 3.19 (no match at the chat code) - not patching"); return;
    }
    if (!patch_call(AT_SHOW, FN_SHOW, (void *)stub_show)) { logmsg("chat display call not as expected - not patching"); return; }
    if (!patch_call(AT_SEND, FN_SEND, (void *)stub_send)) {
        patch_call(AT_SHOW, (DWORD)stub_show, (void *)FN_SHOW);
        logmsg("chat send call not as expected - not patching"); return;
    }
    int b = gfx_hook_slot(MM_SLOT, MM_FN, (void *)hk_mm, (void **)&o_mm);
    logmsg("ChatHide %s%s: chat %s, button %s, hotkey %s", VERSION,
#ifdef CH_TEST
           " TEST BUILD (fake incoming lines every 4 s)",
#else
           "",
#endif
           g_hidden ? "HIDDEN" : "shown", !b ? "NOT hooked (minimap slot differs) - no button, no hotkey" : g_button ? "on" : "off", b && g_hotkey ? "Ctrl+Shift+C" : "off");
}
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
static double ini_num(const wchar_t *key) { wchar_t b[32]; GetPrivateProfileStringW(L"ChatHide", key, L"0", b, 32, g_ini); return _wtof(b); }
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wcscpy(g_ini, g_log); wcscat(g_ini, L"ChatHide.ini"); wcscat(g_log, L"ChatHide.log");
    if (already_loaded("ChatHide")) { logmsg("another ChatHide copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"ChatHide", L"Enabled", 1, g_ini)) { logmsg("disabled in ChatHide.ini"); return TRUE; }
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qf = f.QuadPart ? f.QuadPart : 1000;
    g_hidden = GetPrivateProfileIntW(L"ChatHide", L"Hidden", 0, g_ini) != 0;
    g_hotkey = GetPrivateProfileIntW(L"ChatHide", L"Hotkey", 1, g_ini) != 0;
    g_button = GetPrivateProfileIntW(L"ChatHide", L"Button", 1, g_ini) != 0;
    g_offX = ini_num(L"OffsetX"); g_offY = ini_num(L"OffsetY");
    install();
    return TRUE;
}
