// ReplaySpeed.dll - two more replay speeds, 8x and Max, as two buttons next to the game's own speed button in the replay
// panel (DoW2: Retribution 3.19, Elite and vanilla; replays and live replays only).
//
// How the game's replay speed works (DOW2.exe 3.19):
//   - the replay panel (ui/movies/hud/playback_panel_v2, form vtable 0x011119F4 made in 0xB18910, its binding object vtable
//     0x01111D44) has play / pause and ONE speed button, /playback_panel_mc/fast_forward_btn, with four icon clips .5x / 1x /
//     2x / 4x. A click (0xB1FCC0) steps through the table 0x012C07F4 {.5x: 0, 1x: 2, 2x: 3, 4x: 4} into the rate table
//     0x012C07DC {5, 7.5, 10, 20, 40} and calls the sim rate interface *(*(0x01335720)+8)+0x68 (vtable 0x0108DFAC: +0x28
//     set = Game_SetSimRate 0x43ECE9, +0x2C get). The rate is SIMULATION TICKS PER SECOND: 10 = 1x (a tick is 0.1 s of game
//     time), 40 = 4x. The setter (0x425106) has no clamp: SimulationController (GameState+0x154) +0x2C = rate, +0x28 = 1/rate.
//     Only the panel's icon lookup (0xACC2E0) clamps to 5..40.
//   - SimulationController::Update (0x438613) adds the frame time to an accumulator and runs at most ONE tick when it passes
//     1/rate; the backlog is cut to one interval. So ticks per second = min(rate, frames per second): 4x needs 40 fps, 8x
//     needs 80, and drawing (~20 ms a frame on a busy replay) is the limit, not the simulation (~1-4 ms a tick).
//   - the main loop (0x40E2xx) calls per pass: the visual update 0x439CE8 (at 0x40E58D), the game frame 0x4240F5 (sim,
//     HUD model) and the draw + Present 0x405935 (at 0x40E6DC). The frame caps (vsync, DXVK maxFrameRate) are inside Present.
//
// What ReplaySpeed does:
//   - 8x: the same setter with 80 ticks/s, raised (at most 160) while the replay runs below 80 real ticks a second: every
//     drawn frame is longer than 1/80 s and the controller drops that remainder. Max: 1000 ticks/s, i.e. a tick whenever the
//     simulation can take the next one.
//   - while 8x / Max runs in a replay, passes between two shown frames skip the visual update and the draw/Present (the two
//     call sites go through this DLL; a skipped pass returns at once): the loop then runs as fast as the simulation and shows
//     a frame every 1/30 s (8x) or 1/15 s (Max) - ReplaySpeed.ini Fps8x / FpsMax. The interface (Scaleform) still advances on
//     every pass, so the buttons answer at once. Paused, at the end of a replay or while a live replay waits for data (no tick
//     for 0.25 s) every pass is drawn as usual.
//   - the buttons: two copies of the game's own speed button (Receive_DuplicateMovieClip of fast_forward_btn) in the same panel,
//     right of it: the button's own up / over / down / toggled_on frames; "8x" a text in the panel's font like the game's icons,
//     "Max" the game's unused icon hud/playback_panel/large_btn_ff_8x (four arrows). The active one shows toggled_on - read from
//     the real rate every frame, so the game's own button and these always agree. Clicks: the player's own left button over a
//     copy (GetAsyncKeyState + the copy's _xmouse/_ymouse, the window in front), the same way Relay's "don't wait" button works.
//     While 8x / Max is on, pressing the game's speed button first sets 0.5x, so its own click lands on 1x.
//   - only in a replay (game type settings+0x118 has 0x800, also LiveReplay); in any other game nothing is read or changed, no
//     pass is skipped. The simulation itself is untouched: same ticks, same orders, only how many per second.
// Rules: docs/UI_RULES.md (no Direct3D drawing - nothing here touches Direct3D; frames are just not drawn), docs/ADDON_RULES.md.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../common/gfxui.h"

#define VERSION "1"

#define G_SETTINGS   0x01335A78u   // +0x118 game type: 0x800 = playback
#define G_WORLD      0x013356A4u   // +0x174 replay length (ticks), +0x178 tick
#define G_HUD        0x01335720u   // +8 -> +0x68: sim rate interface (vtable 0x0108DFAC: +0x28 set, +0x2C get)
#define VT_SIMRATE   0x0108DFACu
#define FN_SETRATE   0x0043ECE9u
#define FN_GETRATE   0x0043ECCBu
#define T_PLAYBACK   0x800
#define PANEL_SLOT   (0x011119F4u + 0x10)   // replay panel form, slot 4 = per-frame update (an empty "ret 4")
#define PANEL_FN     0x009AB7D0u
#define AT_VISUAL    0x0040E58Du   // call 0x439CE8 (stdcall, 3 args)
#define FN_VISUAL    0x00439CE8u
#define AT_DRAW      0x0040E6DCu   // call 0x405935 (stdcall, 6 args, EDI = perf block)
#define FN_DRAW      0x00405935u

#define RATE_1X  10.0f
#define RATE_8X  80.0f
static float g_rateMax = 1000.0f;
static int g_fps8 = 30, g_fpsMax = 15;

static wchar_t g_log[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static int okp(const void *p, UINT n) { return p && (DWORD)p >= 0x10000 && !IsBadReadPtr(p, n); }
#define P32(p, o) (*(char **)((char *)(p) + (o)))
#define I32(p, o) (*(int *)((char *)(p) + (o)))

static LONGLONG g_qf;
static LONGLONG qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// ---- state shared by the panel update and the two call-site hooks (all on the game's main thread)
static volatile LONGLONG g_panelAt;     // last panel update (a replay panel is alive)
static volatile float g_rate;           // the sim rate seen by the panel update
static volatile int g_mode;             // 1 = our 8x, 2 = our Max, 0 = the game's own speeds
static float g_setRate;                 // what we last set; any other rate = the game changed it (its button, a takeover)
static LONGLONG g_ctlAt; static int g_ctlTick = -1;   // 8x: the realized speed, measured once a second
static volatile LONGLONG g_tickAt;      // when the tick last moved
static int g_lastTick = -1;
static LONGLONG g_lastShown;            // last pass that was drawn
static int g_passDecided, g_passSkip;   // the visual hook decides for its pass, the draw hook follows
static LONG g_passes, g_shown; static LONGLONG g_winAt; static int g_winTick = -1;

typedef void (__thiscall *SetRate_t)(void *self, float r);
typedef float (__thiscall *GetRate_t)(void *self);
static void *rate_iface(void) {
    char *hud = okp((void *)G_HUD, 4) ? *(char **)G_HUD : NULL, *h = okp(hud, 12) ? P32(hud, 8) : NULL, *sr = okp(h, 0x70) ? P32(h, 0x68) : NULL;
    return okp(sr, 4) && *(DWORD *)sr == VT_SIMRATE ? sr : NULL;
}
static int set_rate(float r) {
    void *sr = rate_iface(); if (!sr) { logmsg("sim rate interface not found - speed not changed"); return 0; }
    ((SetRate_t)(*(void ***)sr)[0x28 / 4])(sr, r); g_rate = r;
    return 1;
}
static float get_rate(void) { void *sr = rate_iface(); return sr ? ((GetRate_t)(*(void ***)sr)[0x2C / 4])(sr) : 0.0f; }
static int is_replay(void) {
    char *st = okp((void *)G_SETTINGS, 4) ? *(char **)G_SETTINGS : NULL;
    return okp(st, 0x120) && (I32(st, 0x118) & T_PLAYBACK);
}
static int world_tick(int *len) {
    char *w = okp((void *)G_WORLD, 4) ? *(char **)G_WORLD : NULL;
    if (!okp(w, 0x17C)) return -1;
    if (len) *len = I32(w, 0x174);
    return I32(w, 0x178);
}

// ---- the pass decision: skip this pass's visual update and draw?
static int decide(void) {
    LONGLONG now = qpc();
    if (now - g_panelAt > g_qf / 2) return 0;              // no live replay panel: never skip
    int m = g_mode; if (!m) return 0;
    if (now - g_tickAt > g_qf / 4) return 0;               // paused / end / no data: draw as usual
    LONGLONG per = g_qf / (m == 2 ? g_fpsMax : g_fps8);
    return now - g_lastShown < per;
}
static void pass_done(int skipped) {
    LONGLONG now = qpc();
    g_passes++; if (!skipped) { g_shown++; g_lastShown = now; }
    if (now - g_winAt >= g_qf * 5) {                       // every 5 s while 8x / Max runs: what it really does
        int t = world_tick(NULL); int m = now - g_panelAt <= g_qf / 2 ? g_mode : 0;
        double s = (double)(now - g_winAt) / g_qf;
        if (m && g_winTick >= 0 && t >= g_winTick)
            logmsg("%s: %.1f ticks/s = %.2f game s per real s, %.1f frames shown/s, %.0f loop passes/s",
                   m == 2 ? "Max" : "8x", (t - g_winTick) / s, (t - g_winTick) / s / 10.0, g_shown / s, g_passes / s);
        g_winAt = now; g_winTick = t; g_passes = g_shown = 0;
    }
}
int __cdecl rs_visual(void) { g_passSkip = decide(); g_passDecided = 1; return g_passSkip; }
int __cdecl rs_draw(void) {
    int skip = g_passDecided ? g_passSkip : decide();
    g_passDecided = 0; pass_done(skip);
    return skip;
}
void *g_fnVisual = (void *)FN_VISUAL, *g_fnDraw = (void *)FN_DRAW;
// call-site stubs: eax/ecx/edx kept, EDI (the draw's hidden argument) untouched; skip = return with the callee's own ret N
__attribute__((naked)) void stub_visual(void) {
    __asm__ volatile(
        "pushl %eax\n\tpushl %ecx\n\tpushl %edx\n\t"
        "call _rs_visual\n\t"
        "testl %eax, %eax\n\t"
        "popl %edx\n\tpopl %ecx\n\tpopl %eax\n\t"
        "jnz 1f\n\t"
        "jmp *_g_fnVisual\n"
        "1:\n\tret $0xC\n");
}
__attribute__((naked)) void stub_draw(void) {
    __asm__ volatile(
        "pushl %eax\n\tpushl %ecx\n\tpushl %edx\n\t"
        "call _rs_draw\n\t"
        "testl %eax, %eax\n\t"
        "popl %edx\n\tpopl %ecx\n\tpopl %eax\n\t"
        "jnz 1f\n\t"
        "jmp *_g_fnDraw\n"
        "1:\n\tret $0x18\n");
}

// ---- the two buttons in the replay panel
typedef void (__thiscall *Upd_t)(char *form, float dt);
typedef char (__thiscall *GfxGetVar_t)(void *movie, GfxVal *v, const char *path);
static Upd_t o_panel;
static void *g_mv, *g_layoutMv; static LONGLONG g_madeAt; static int g_tries, g_made, g_hover[2], g_down[2], g_on[2], g_lbWas, g_pressOn = -1, g_ffPress;
static const char *BTN[2] = { "rs_8x", "rs_max" };
static int getnum(void *mv, const char *path, double *out) {
    GfxVal v; memset(&v, 0, sizeof v);
    if (!(*(GfxGetVar_t *)(*(char **)mv + 0x30))(mv, &v, path) || v.type != GV_NUMBER) return 0;
    *out = v.v.num; return 1;
}
static int getbool(void *mv, const char *path) {
    GfxVal v; memset(&v, 0, sizeof v);
    return (*(GfxGetVar_t *)(*(char **)mv + 0x30))(mv, &v, path) && v.type == GV_BOOL && v.v.b;
}
static void setvis(void *mv, int b, const char *child, int on) {
    char p[96]; snprintf(p, sizeof p, "/playback_panel_mc/%s/%s", BTN[b], child); gfx_set_visible(mv, p, on);
}
static void make_buttons(void *mv) {
    double fx = 0, fy = 0, fw = 40;
    if (!getnum(mv, "_root.playback_panel_mc.fast_forward_btn._x", &fx) || !getnum(mv, "_root.playback_panel_mc.fast_forward_btn._y", &fy)) {
        logmsg("replay panel %p: no fast_forward_btn - no buttons", mv); g_made = -1; return;
    }
    getnum(mv, "_root.playback_panel_mc.fast_forward_btn.up._width", &fw); if (fw < 20 || fw > 80) fw = 40;
    double px = 0, pb = 0; getnum(mv, "_root.playback_panel_mc.play_btn._x", &px);
    double step = fx - px > fw && fx - px < 2 * fw ? fx - px : fw + 1;  // the game's own spacing play -> speed
    for (int b = 0; b < 2; b++) {
        char src[64], p[96];
        if (!gfx_exists(mv, b ? "playback_panel_mc.rs_max" : "playback_panel_mc.rs_8x")) {
            GfxVal a[2] = { gv_str("/playback_panel_mc/fast_forward_btn"), gv_str(BTN[b]) }, r; memset(&r, 0, sizeof r);
            gfx_invoke(mv, "Receive_DuplicateMovieClip", &r, a, 2);
        }
        snprintf(src, sizeof src, "playback_panel_mc.%s", BTN[b]);
        if (!gfx_exists(mv, src)) { logmsg("replay panel %p: copy %s NOT made - no buttons", mv, BTN[b]); g_made = -1; return; }
        snprintf(p, sizeof p, "_root.%s._x", src); gfx_setvar(mv, p, gv_num(fx + step * (b + 1)));
        snprintf(p, sizeof p, "_root.%s._y", src); gfx_setvar(mv, p, gv_num(fy));
        setvis(mv, b, "up", 1); setvis(mv, b, "over", 0); setvis(mv, b, "down", 0); setvis(mv, b, "toggled_on", 0);
        setvis(mv, b, ".5x", 0); setvis(mv, b, "2x", 0); setvis(mv, b, "4x", 0);
        if (b) {   // Max: the game's own four-arrow icon, loaded into the copy's "1x" icon clip (40 x 40 at 0,0 like the texture)
            char slash[64]; snprintf(slash, sizeof slash, "/playback_panel_mc/%s/1x", BTN[b]);
            GfxVal a[2] = { gv_str(slash), gv_str("img://data:UI/textures/space_marines/hud/playback_panel/large_btn_ff_8x.dds") };
            gfx_invoke(mv, "Receive_LoadMovie", NULL, a, 2);
        } else {   // 8x: a text like the game's "1x" / "2x" icons
            setvis(mv, b, "1x", 0);
            GfxVal a[6] = { gv_str("rs_lbl"), gv_num(100), gv_num(0), gv_num(fw * 0.12), gv_num(fw), gv_num(fw * 0.7) };
            snprintf(p, sizeof p, "_root.%s.createTextField", src); gfx_invoke(mv, p, NULL, a, 6);
            snprintf(p, sizeof p, "_root.%s.rs_lbl.html", src); gfx_setvar(mv, p, gv_bool(1));
            snprintf(p, sizeof p, "_root.%s.rs_lbl.selectable", src); gfx_setvar(mv, p, gv_bool(0));
            char h[160]; snprintf(h, sizeof h, "<p align=\"center\"><font face=\"$NormalFont\" size=\"%d\" color=\"#D5D9DC\">8x</font></p>", (int)(fw * 0.47 + 0.5));
            snprintf(p, sizeof p, "_root.%s.rs_lbl.htmlText", src); gfx_setvar(mv, p, gv_str(h));
        }
        g_hover[b] = g_down[b] = g_on[b] = 0;
    }
    // room: the elapsed time and the progress bar (right of the speed button) move right by the two buttons; the bar keeps its
    // right end (the remaining time stays where it is). Once per panel movie, from the positions the movie came with.
    double pw = 0, ex = 0, sh = 2 * step;
    getnum(mv, "_root.playback_panel_mc.progress_bar_mc._x", &pb); getnum(mv, "_root.playback_panel_mc.progress_bar_mc._width", &pw);
    getnum(mv, "_root.playback_panel_mc.time_elapsed_txt._x", &ex);
    if (g_layoutMv != mv && pb > fx && pw - sh >= 40) {
        g_layoutMv = mv;
        gfx_setvar(mv, "_root.playback_panel_mc.progress_bar_mc._x", gv_num(pb + sh));
        gfx_setvar(mv, "_root.playback_panel_mc.progress_bar_mc._width", gv_num(pw - sh));
        gfx_setvar(mv, "_root.playback_panel_mc.time_elapsed_txt._x", gv_num(ex + sh));
    }
    logmsg("replay panel %p: buttons 8x / Max made at x %.1f / %.1f (speed button %.1f,%.1f, %g wide); progress bar %.1f +%.1f -> %.1f +%.1f, elapsed time %.1f -> %.1f",
           mv, fx + step, fx + 2 * step, fx, fy, fw, pb, pw, pb + sh, pw - sh, ex, ex + sh);
    g_made = 1;
}
static int mouse_over(void *mv, const char *clip, double w) {
    char p[96]; double x, y;
    snprintf(p, sizeof p, "_root.playback_panel_mc.%s._xmouse", clip); if (!getnum(mv, p, &x)) return 0;
    snprintf(p, sizeof p, "_root.playback_panel_mc.%s._ymouse", clip); if (!getnum(mv, p, &y)) return 0;
    return x >= 0 && y >= 0 && x < w && y < w;
}
static void __thiscall hk_panel(char *form, float dt) {
    o_panel(form, dt);
    if (!is_replay()) return;
    void *mv = gfx_movie_of_form(form); if (!mv) return;
    LONGLONG now = qpc();
    float r = get_rate(); g_rate = r;
    int t = world_tick(NULL); if (t != g_lastTick) { g_lastTick = t; g_tickAt = now; }
    if (g_mode && (r < g_setRate - 0.01f || r > g_setRate + 0.01f)) { logmsg("rate changed by the game (%g): 8x / Max off", r); g_mode = 0; }
    // 8x: the controller drops up to one tick interval on each drawn frame (longer than 1/80 s), so the rate set is raised
    // until the replay really runs 80 ticks a second (at most 160); measured once a second while ticks come
    if (g_mode == 1 && now - g_ctlAt >= g_qf) {
        double dt = (double)(now - g_ctlAt) / g_qf; int dtk = t - g_ctlTick;
        if (g_ctlTick >= 0 && dtk > 0 && now - g_tickAt < g_qf / 4 && dt < 3) {
            float got = (float)(dtk / dt), nr = g_setRate + (RATE_8X - got) * 0.6f;
            if (nr < RATE_8X) nr = RATE_8X; if (nr > 2 * RATE_8X) nr = 2 * RATE_8X;
            if (nr < g_setRate - 0.5f || nr > g_setRate + 0.5f) { if (set_rate(nr)) { g_setRate = nr; r = nr; } }
        }
        g_ctlAt = now; g_ctlTick = t;
    }
    g_panelAt = now;
    if (mv != g_mv) { g_mv = mv; g_made = 0; g_tries = 0; g_mode = 0; }
    if (g_made == -1 && g_tries < 10 && now - g_madeAt > g_qf * 2) g_made = 0;   // the panel not ready yet: try again
    if (g_made == 0 || (g_made == 1 && !gfx_exists(mv, "playback_panel_mc.rs_8x"))) { g_tries++; g_madeAt = now; make_buttons(mv); }
    if (g_made != 1) return;
    // the player's own left button: press over a button and release over it = click
    DWORD fpid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &fpid);
    int front = fpid == GetCurrentProcessId() && getbool(mv, "_root.playback_panel_mc._visible");
    int lb = front && (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000);
    int m = g_mode;
    for (int b = 0; b < 2; b++) {
        int over = front && mouse_over(mv, BTN[b], 40);
        if (lb && !g_lbWas && over) g_pressOn = b;
        int down = lb && over && g_pressOn == b, on = m == b + 1;
        if (over != g_hover[b]) { setvis(mv, b, "over", over); g_hover[b] = over; }
        if (down != g_down[b]) { setvis(mv, b, "down", down); g_down[b] = down; }
        if (on != g_on[b]) { setvis(mv, b, "toggled_on", on); g_on[b] = on; }
        if (!lb && g_lbWas && over && g_pressOn == b) {
            int nm = m == b + 1 ? 0 : b + 1;                              // the active one again = back to 1x
            float nr = nm == 2 ? g_rateMax : nm == 1 ? RATE_8X : RATE_1X;
            if (set_rate(nr)) { logmsg("%s pressed at tick %d: rate %g -> %g ticks/s", b ? "Max" : "8x", t, r, nr); g_mode = nm; g_setRate = nr; g_ctlAt = now; g_ctlTick = t; }
            g_lastShown = 0;
        }
    }
    // the game's own speed button pressed while 8x / Max is on: 0.5x first, so its click (on release) steps to 1x
    if (m && lb && !g_lbWas && mouse_over(mv, "fast_forward_btn", 40)) { set_rate(5.0f); g_mode = 0; g_ffPress = 1; logmsg("speed button pressed during %s: back through 1x", m == 2 ? "Max" : "8x"); }
    if (!lb) { g_pressOn = -1; g_ffPress = 0; }
    g_lbWas = lb;
}

// ---- install
static int patch_call(DWORD at, DWORD expect, void *fn) {
    unsigned char *p = (unsigned char *)at; DWORD old;
    if (IsBadReadPtr(p, 5) || p[0] != 0xE8 || at + 5 + *(int *)(p + 1) != expect) return 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    InterlockedExchange((LONG *)(p + 1), (LONG)((DWORD)fn - (at + 5)));   // 4 bytes at p+1: one store
    VirtualProtect(p, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), p, 5);
    return 1;
}
static void install(void) {
    static const unsigned char set_code[] = { 0xA1, 0x78, 0x5A, 0x33, 0x01, 0xD9, 0x44, 0x24, 0x04, 0x8B, 0x80, 0x10, 0x01, 0x00, 0x00 };
    DWORD *vt = (DWORD *)VT_SIMRATE;
    if (IsBadReadPtr(vt, 0x30) || vt[0x28 / 4] != FN_SETRATE || vt[0x2C / 4] != FN_GETRATE || memcmp((void *)FN_SETRATE, set_code, sizeof set_code)
        || memcmp((void *)FN_DRAW, "\x55\x8B\xEC\x83\xE4\xF8", 6) || memcmp((void *)FN_VISUAL, "\x55\x8B\xEC\x83\xE4\xF8", 6)) {
        logmsg("DOW2.exe is not 3.19 (no match at the sim rate / draw code) - not patching"); return;
    }
    if (!gfx_hook_slot(PANEL_SLOT, PANEL_FN, (void *)hk_panel, (void **)&o_panel)) { logmsg("replay panel update slot not as expected - not patching"); return; }
    int v = patch_call(AT_VISUAL, FN_VISUAL, (void *)stub_visual), d = v && patch_call(AT_DRAW, FN_DRAW, (void *)stub_draw);
    if (v && !d) patch_call(AT_VISUAL, (DWORD)stub_visual, (void *)FN_VISUAL);
    logmsg("ReplaySpeed %s: buttons 8x / Max in the replay panel (8x = %g ticks/s shown at %d fps, Max = %g ticks/s at %d fps)%s",
           VERSION, RATE_8X, g_fps8, g_rateMax, g_fpsMax, v && d ? "" : " - frame skipping NOT hooked (call sites differ): speed limited by fps");
}
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"ReplaySpeed.ini"); wcscat(g_log, L"ReplaySpeed.log");
    if (already_loaded("ReplaySpeed")) { logmsg("another ReplaySpeed copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"ReplaySpeed", L"Enabled", 1, ini)) { logmsg("disabled in ReplaySpeed.ini"); return TRUE; }
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qf = f.QuadPart ? f.QuadPart : 1000;
    g_fps8 = GetPrivateProfileIntW(L"ReplaySpeed", L"Fps8x", 30, ini); if (g_fps8 < 5) g_fps8 = 5; if (g_fps8 > 240) g_fps8 = 240;
    g_fpsMax = GetPrivateProfileIntW(L"ReplaySpeed", L"FpsMax", 15, ini); if (g_fpsMax < 2) g_fpsMax = 2; if (g_fpsMax > 240) g_fpsMax = 240;
    install();
    return TRUE;
}
