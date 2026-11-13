// MatchSpeed.dll - speed x2 / x4 (and back to x1) and a pause for a LIVE match in which the player is the only person: a
// skirmish against computers, or computers against computers watched from an observer slot of a Custom Games lobby
// (DoW2: Retribution 3.19, Elite and vanilla). AI testers 12.11.2026: "посмотреть битву ботов с возможностью увеличить
// скорость игры x2 и x4"; "нужна и пауза - остановить, рассмотреть карту, сделать пометку".
//
// How the speed works (the same engine path as the replay speeds, see replayspeed/replayspeed.c):
//   - the sim rate interface *(*(0x01335720)+8)+0x68 (vtable 0x0108DFAC: +0x28 set = Game_SetSimRate 0x43ECE9, +0x2C get)
//     sets SimulationController (GameState+0x154) +0x2C = rate and +0x28 = 1/rate. The rate is SIMULATION TICKS PER
//     SECOND, 10 = x1 (a tick is always 0.1 s of game time), 20 = x2, 40 = x4. SCAR's Game_SetSimRate is the same call
//     (the AI test stand runs its AI-vs-AI series at 50 this way).
//   - SimulationController::Update (0x438613) adds the frame time and runs at most ONE tick per frame, so the real speed is
//     min(rate, frames per second) / 10: x4 needs 40 fps, x2 20 fps. MatchSpeed.log says every 10 s what really ran.
//   - the simulation is untouched: the same ticks with the same orders, only more of them per second. A replay records
//     orders per tick and plays at its own speed, so a match played at x4 replays as usual.
//   - the pause: rate 0.0001 (one tick in ~3 hours) - the simulation stands (the AI too: it thinks on sim ticks), the
//     camera, the minimap, selection and the interface work; resume = the speed chosen before. The game's own pause is a
//     different mechanism and works as before: in a match without other people the Menu pauses the game, but its dialog
//     covers the map; an observer's Pause key does nothing (stand, 12.11).
//   - a custom game hosted with only computers and the host in an observer slot runs as game type 4 (single-player
//     skirmish) once it starts: the game cuts its session and plays it on this machine (so EliteAI's new AI plays there too).
//
// Only when nobody else is in the match (checked when a match starts and every 2 s while it runs; anything unknown = no):
//   - the game is live (game type settings+0x118 not 0, not 0x800 = replay: replays have ReplaySpeed);
//   - every slot of the match's GameInfo (settings+0x118+0x810, 0xA8 each, +0 type, +0x9C player id) is the local human
//     (0), a computer (1), the local observer (2) or an empty slot (6 open, 7 closed; seen: the closed observer slots and the
//     unused team slots of a custom game; 8 "nobody there", see below) - a remote human (3), remote computer (4), remote
//     observer (5) or any other type means another person's game is in it;
//   - type 8 (MatchSpeed 3, 13.11.2026: a tester's observer bot match, launcher 1.6.3 / Elite 3.2.1.0.0, slots 2, 3, 5, 6, 7 =
//     8 with player id 0, so MatchSpeed 2 stayed out): the match start (0x0044618C, the GameInfo slots from the session's
//     members) writes 8 at 0x00446703 for a member slot of a person kind (0 / 3 / 4 / 5, not the local observer) that has NO
//     peer - nobody connected behind it. The engine then treats it exactly as a closed slot 7: no Player is made for it
//     (0x004269D5: 7 or 8 -> NULL), its name is "missing player" (0x0044EAF8), its race is not loaded (0x00434F3E: skips
//     7, 8, 2, 5). So 8 is an empty slot too - counted with 6 / 7 only with player id 0 (as seen); with any other id it stays
//     "other". A person really in the match keeps the session connected, which is checked on its own below.
//   - online (session flags *(*0x01335678+4) & 0x558): the match session is no longer connected. The game itself cuts the
//     session at the start of a match with one person in it (0x46FE8E: members == 1 -> Disconnect), so a solo custom game
//     runs on this machine alone; a connected session = somebody else is there.
//   Otherwise nothing is shown, no key does anything, and if the rate was ours it goes back to 10.
//
// Controls: two copies of the replay panel's own buttons (its textures hud/playback_panel/large_btn*, the same up / over /
//   down / toggled_on frames and icons, the same size) at the top left of the screen, where a replay has its pause / speed
//   buttons (common/topbar.h: in the game's full-screen HUD movie resource_notifications): pause / play (|| while running, >
//   and the gold frame while paused) and the speed (the game's 1x / 2x / 4x icons; a click steps 1x -> 2x -> 4x -> 1x; gold
//   frame above x1). Hotkeys: Ctrl+= / Ctrl+NumPad+ faster, Ctrl+- / Ctrl+NumPad- slower, Ctrl+P pause / resume (none of them
//   is bound in the game's keydefaults.lua). Clicks and keys are read like ChatHide / ReplaySpeed: GetAsyncKeyState with the
//   game window in front, the clip's _xmouse/_ymouse; a click over a button is handled in the movie form's input slot, so it
//   does not reach the world.
//   MatchSpeed 2 (13.11.2026, AI testers): the buttons were on the left edge of the minimap frame (MatchSpeed 1) - awkward
//   there, and every click on them also went to the minimap and moved the camera to the map's corner.
// Hooks: the top movie's per-frame update and input (form vtable 0x01116478 slots +0x10 / +0x0C, chained with Marks) and, for
//   the keys when that movie stands, the Menu button's per-frame update (0x0110C55C, chained).
// Rules: docs/UI_RULES.md (only the game's own movie, nothing touches Direct3D), docs/ADDON_RULES.md.
// MatchSpeed.ini [MatchSpeed]: Enabled, Button, Hotkeys, Scale (button size, % of the 40 x 40 texture: 100 = the replay
//   panel's), OffsetX / OffsetY (movie units, 1.5 a pixel at 800 lines).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/gfxui.h"
#include "../common/topbar.h"

#define VERSION "3"

#define G_SETTINGS   0x01335A78u   // +0x118 game type (0x800 playback), +0x118+0x810 slots, +0x814 count
#define G_SESSION    0x01335678u   // *(*G_SESSION + 4) & 0x558: online
#define G_WORLD      0x013356A4u   // +0x178 tick
#define G_HUD        0x01335720u   // +8 -> +0x68: sim rate interface
#define G_MGR        0x01335014u   // network manager: vtbl+0x24 = match (RLink::MatchInternal)
#define G_RLINK      0x01335030u
#define FN_FIND_SESS 0x00588C50u
#define VT_SIMRATE   0x0108DFACu
#define FN_SETRATE   0x0043ECE9u
#define FN_GETRATE   0x0043ECCBu
#define MENU_VT_UPD  0x0110C55Cu   // Menu button form, per-frame update (ret 4)
#define NULLSUB_RET4 0x009AB7D0u
#define T_PLAYBACK   0x800
#define ONLINE_MASK  0x558

#define RATE_1X      10.0f
#define RATE_PAUSE   0.0001f

static wchar_t g_log[MAX_PATH], g_ini[MAX_PATH];
static int g_logLines;
static void logmsg(const char *fmt, ...) {
    if (g_logLines > 5000) return; g_logLines++;
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static int okp(const void *p, UINT n) { return p && (DWORD)p >= 0x10000 && !IsBadReadPtr(p, n); }
#define P32(p, o) (*(char **)((char *)(p) + (o)))
#define I32(p, o) (*(int *)((char *)(p) + (o)))
static LONGLONG g_qf;
static LONGLONG qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

static int g_button = 1, g_hotkeys = 1; static double g_scale = 100, g_offX, g_offY;
static char g_testSeq[256]; static int g_testPos;   // stands only (MatchSpeed.ini TestSeq): "<s>:<1|2|4|p|r> ..." after the match start

// ---- the sim rate
typedef void (__thiscall *SetRate_t)(void *self, float r);
typedef float (__thiscall *GetRate_t)(void *self);
static void *rate_iface(void) {
    char *hud = okp((void *)G_HUD, 4) ? *(char **)G_HUD : NULL, *h = okp(hud, 12) ? P32(hud, 8) : NULL, *sr = okp(h, 0x70) ? P32(h, 0x68) : NULL;
    return okp(sr, 4) && *(DWORD *)sr == VT_SIMRATE ? sr : NULL;
}
static int set_rate(float r) {
    void *sr = rate_iface(); if (!sr) { logmsg("sim rate interface not found - rate not changed"); return 0; }
    ((SetRate_t)(*(void ***)sr)[0x28 / 4])(sr, r); return 1;
}
static float get_rate(void) { void *sr = rate_iface(); return sr ? ((GetRate_t)(*(void ***)sr)[0x2C / 4])(sr) : -1.0f; }
static char *settings(void) { return okp((void *)G_SETTINGS, 4) && okp(*(char **)G_SETTINGS, 0x940) ? *(char **)G_SETTINGS : NULL; }
static char *world(void) { return okp((void *)G_WORLD, 4) && okp(*(char **)G_WORLD, 0x17C) ? *(char **)G_WORLD : NULL; }
static int world_tick(void) { char *w = world(); return w ? I32(w, 0x178) : -1; }
static DWORD session_flags(void) {
    char *s = okp((void *)G_SESSION, 4) ? *(char **)G_SESSION : NULL;
    return okp(s, 8) ? *(DWORD *)(s + 4) : 0;
}

// ---- the match session (as Continue.dll): still connected = other people in it
typedef char (__stdcall *MatchBool)(void *match);
static unsigned char *match_obj(void) {
    void **mgr = okp((void *)G_MGR, 4) ? *(void ***)G_MGR : NULL; if (!okp(mgr, 4) || !okp(*mgr, 0x28)) return NULL;
    void *(__fastcall *get)(void *, void *) = ((void *(__fastcall **)(void *, void *))*mgr)[0x24 / 4];
    unsigned char *m = get(mgr, 0);
    return okp(m, 0x10) && okp(*(void **)m, 0x98) ? m : NULL;
}
static int match_connected(void) {
    unsigned char *m = match_obj();
    if (!m || (*(DWORD *)(m + 8) & *(DWORD *)(m + 0xC)) == 0xFFFFFFFF) return 0;
    return ((MatchBool)(*(void ***)m)[0x70 / 4])(m) != 0;
}

// ---- "nobody else here": 1 = yes; why[] says what was seen
static const char *TYPE_NAME[] = { "local human", "computer", "local observer", "remote human", "remote computer", "remote observer", "open", "closed", "nobody there" };
static int solo_check(char *why, int wn, int logSlots) {
    char *st = settings(); if (!st) { snprintf(why, wn, "no game settings"); return 0; }
    DWORD type = (DWORD)I32(st, 0x118), sf = session_flags();
    if (!type) { snprintf(why, wn, "not in a game"); return 0; }
#ifndef MS_TEST
    if (type & T_PLAYBACK) { snprintf(why, wn, "a replay (game type %lX)", type); return 0; }
#endif
    char *arr = P32(st, 0x118 + 0x810); int n = I32(st, 0x118 + 0x814);
    if (n <= 0 || n > 16 || !okp(arr, n * 0xA8)) { snprintf(why, wn, "no player list (%d)", n); return 0; }
    int c[9] = {0}, other = 0;
    for (int i = 0; i < n; i++) {
        char *s = arr + i * 0xA8; int t = I32(s, 0);
#ifdef MS_TEST8   // stands only: a closed slot (7) judged as the "nobody there" 8 of the launcher's rooms
        if (t == 7) t = 8;
#endif
        if (t >= 0 && t < 8) c[t]++; else if (t == 8 && I32(s, 0x9C) == 0) c[8]++; else other++;
        if (logSlots) logmsg("  slot %d: type %d (%s), player id %d, AI difficulty %d", i, t, t >= 0 && t <= 8 ? TYPE_NAME[t] : "?", I32(s, 0x9C), I32(s, 0x2C));
    }
    int people = c[3] + c[5] + other;        // 6 / 7 / 8: open / closed / nobody there (seen: closed observer slots and unused team slots = 7 or 8)
    int online = (sf & ONLINE_MASK) != 0, conn = online ? match_connected() : 0;
    snprintf(why, wn, "game type %lX, %s%s, %d slots: %d local human, %d computer, %d local observer, %d remote human, %d remote computer, %d remote observer, %d empty, %d other",
             type, online ? "network session" : "no network session", online ? (conn ? " (match session CONNECTED)" : ", match session not connected") : "",
             n, c[0], c[1], c[2], c[3], c[4], c[5], c[6] + c[7] + c[8], other);
#ifdef MS_TEST   // stands only: the verdict of a replay's slot table logged as if it were live, never acted on
    if (type & T_PLAYBACK) { int v = !(people || c[4]) && c[0] + c[2] == 1 && !conn; snprintf(why + strlen(why), wn - strlen(why), " [TEST: a replay - as live it would be %s]", v ? "ALONE" : "NOT alone"); return 0; }
#endif
    if (people || c[4]) return 0;            // a remote computer is run by another machine: someone else is in it
    if (c[0] + c[2] != 1) return 0;          // exactly one local person
    if (conn) return 0;
    return 1;
}

// ---- state (all on the game's main thread)
static int g_speed = 1, g_paused, g_solo = -1;   // g_solo: -1 not decided yet in this match, 0 no, 1 yes
static float g_setRate;                         // what we last set (0 = nothing in this match)
static char *g_world; static LONGLONG g_frameAt, g_matchAt, g_checkAt;
static int g_winTick = -1; static LONGLONG g_winAt; static LONG g_frames;
static int g_lastLoggedSolo = -2, g_lastTick = -1;
static int g_ctlTick = -1; static LONGLONG g_ctlAt;   // the x2 / x4 rate regulator

static float want_rate(void) { return g_paused ? RATE_PAUSE : RATE_1X * g_speed; }
static void apply(const char *how) {
    float r = want_rate(), was = get_rate();
    if (set_rate(r)) { g_setRate = r; logmsg("%s: %s, rate %g -> %g ticks/s (tick %d)", how, g_paused ? "PAUSED" : g_speed == 1 ? "x1" : g_speed == 2 ? "x2" : "x4", was, r, world_tick()); }
    g_winAt = qpc(); g_winTick = world_tick(); g_frames = 0; g_ctlTick = -1;
}
static void act(char a, const char *how) {
    if (g_solo != 1) { logmsg("%s: not a match without other people - nothing done", how); return; }
    if (a == '+') { if (g_speed < 4) g_speed *= 2; else return; }
    else if (a == '-') { if (g_speed > 1) g_speed /= 2; else return; }
    else if (a == 'c') g_speed = g_speed >= 4 ? 1 : g_speed * 2;
    else if (a == 'p') g_paused = !g_paused;
    else if (a == '1' || a == '2' || a == '4') { g_speed = a - '0'; g_paused = 0; }
    else if (a == 'r') g_paused = 0;
    else return;
    apply(how);
}
static void new_match(char *w, LONGLONG now) {
    logmsg("new match (World %p)", w);
    g_world = w; g_speed = 1; g_paused = 0; g_setRate = 0; g_solo = -1; g_matchAt = now; g_checkAt = 0; g_testPos = 0;
    g_lastLoggedSolo = -2; g_winTick = -1;
}
static void recheck(LONGLONG now) {
    char why[400]; int first = g_solo == -1;
    int s = solo_check(why, sizeof why, first);
    if (s != g_lastLoggedSolo) { logmsg("%s: %s - %s", first ? "match check" : "check", why, s ? "ONLY THIS PLAYER: speed and pause on" : "other people or unknown: MatchSpeed stays out"); g_lastLoggedSolo = s; }
    if (!s && g_setRate && (g_setRate < RATE_1X - 0.01f || g_setRate > RATE_1X + 0.01f)) {
        g_speed = 1; g_paused = 0; if (set_rate(RATE_1X)) logmsg("rate back to x1 (no longer a match without other people)");
        g_setRate = 0;
    }
    g_solo = s; g_checkAt = now;
}
static void measure(LONGLONG now) {
    g_frames++;
    if (now - g_winAt < g_qf * 10) return;
    int t = world_tick(); double s = (double)(now - g_winAt) / g_qf;
    if (g_winTick >= 0 && t >= g_winTick && (g_speed > 1 || g_paused))
        logmsg("%s: %.1f ticks/s = x%.2f game speed, %.0f frames/s (rate %g)", g_paused ? "paused" : g_speed == 2 ? "x2" : "x4",
               (t - g_winTick) / s, (t - g_winTick) / s / 10.0, g_frames / s, get_rate());
    g_winAt = now; g_winTick = t; g_frames = 0;
}
// x2 / x4: each drawn frame longer than a tick interval drops the rest of that interval (SimulationController keeps at most one
// interval of backlog), so 40 set gives ~39 real ticks/s in Elite at 60 fps. Every 2 s the rate set is nudged (at most +15 %)
// until the real ticks/s match the speed (only for a shortfall under 15 %; never above +15 %).
static void regulate(LONGLONG now) {
    if (g_paused || g_speed == 1 || !g_setRate) { g_ctlTick = -1; return; }
    if (g_ctlTick < 0) { g_ctlTick = world_tick(); g_ctlAt = now; return; }
    if (now - g_ctlAt < g_qf * 2) return;
    int t = world_tick(); double dt = (double)(now - g_ctlAt) / g_qf, got = (t - g_ctlTick) / dt;
    float target = RATE_1X * g_speed, nr = g_setRate + (float)(target - got) * 0.6f;
    if (nr < target) nr = target; if (nr > target * 1.15f) nr = target * 1.15f;
    // only a small shortfall is corrected: a big one is a pause of the game's own (its menu), a stall or the frame rate's limit,
    // where a higher rate gives nothing now and too much afterwards
    if (got >= target * 0.85 && dt < 4 && (nr < g_setRate - 0.2f || nr > g_setRate + 0.2f) && set_rate(nr)) g_setRate = nr;
    g_ctlTick = t; g_ctlAt = now;
}
static int key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static int g_kPlus, g_kMinus, g_kPause;
static void frame(int front) {
    LONGLONG now = qpc();
    if (now - g_frameAt < g_qf / 1000) return;    // both hooks in one frame: once
    char *w = world(); int tk = world_tick();
    // a new match: another World, or the same address again with the tick back near 0 after a gap (a World freed and made again)
    if (w != g_world || (now - g_frameAt > g_qf * 3 && tk < g_lastTick)) { if (w) new_match(w, now); else g_world = NULL; }
    g_frameAt = now; g_lastTick = tk;
    if (!w) return;
    if ((g_solo == -1 && now - g_matchAt > g_qf / 2) || (g_solo != -1 && now - g_checkAt > g_qf * 2)) recheck(now);
    if (g_solo != 1) return;
    // the rate changed by somebody else (a script, a test SCAR): take it as the state shown
    float r = get_rate();
    if (g_setRate && r > 0 && (r < g_setRate - 0.01f || r > g_setRate + 0.01f)) {
        g_paused = r < 1.0f; if (!g_paused) g_speed = r < 15 ? 1 : r < 30 ? 2 : 4;
        logmsg("rate changed outside MatchSpeed: %g (was %g) - shown as %s", r, g_setRate, g_paused ? "paused" : g_speed == 1 ? "x1" : g_speed == 2 ? "x2" : "x4");
        g_setRate = r;
    } else if (!g_setRate && r > 0 && (r > 10.5f || r < 9.5f)) {
        g_paused = r < 1.0f; if (!g_paused) g_speed = r < 15 ? 1 : r < 30 ? 2 : 4; g_setRate = r;
        logmsg("match runs at rate %g already (a script) - shown as %s", r, g_paused ? "paused" : g_speed == 1 ? "x1" : g_speed == 2 ? "x2" : "x4");
    }
    measure(now); regulate(now);
    if (g_hotkeys) {
        int ctrl = front && key(VK_CONTROL) && !key(VK_MENU) && !key(VK_SHIFT);
        int kp = ctrl && (key(VK_OEM_PLUS) || key(VK_ADD)), km = ctrl && (key(VK_OEM_MINUS) || key(VK_SUBTRACT)), kq = ctrl && key('P');
        if (kp && !g_kPlus) act('+', "Ctrl+=");
        if (km && !g_kMinus) act('-', "Ctrl+-");
        if (kq && !g_kPause) act('p', "Ctrl+P");
        g_kPlus = kp; g_kMinus = km; g_kPause = kq;
    }
    while (g_testSeq[g_testPos]) {               // stands only
        const char *p = g_testSeq + g_testPos; while (*p == ' ') p++;
        double at = atof(p); const char *c = strchr(p, ':'); if (!c || !c[1]) { g_testSeq[g_testPos] = 0; break; }
        if ((double)(now - g_matchAt) / g_qf < at) break;
        char how[48]; snprintf(how, sizeof how, "TEST %.0f s: %c", at, c[1]); act(c[1], how);
        g_testPos = (int)(c + 2 - g_testSeq);
    }
}

// ---- the two buttons at the top of the screen (common/topbar.h: the full-screen HUD movie, a row at the top left)
typedef void (__thiscall *Upd_t)(char *form, float dt);
static Upd_t o_top, o_menu; static TbInput_t o_input;
static void *g_mv; static int g_made, g_tries, g_lbWas, g_press = -1, g_shown = -1, g_overAny;
static int g_hover[2] = {-1, -1}, g_down[2] = {-1, -1}, g_on[2] = {-1, -1}, g_icon[2] = {-1, -1};
static LONGLONG g_madeAt, g_refreshAt, g_topAt;
static const char *BTN[2] = { "_root.ms_btn.ms_pause", "_root.ms_btn.ms_speed" };
static const char *const ICONS[2][4] = { { "large_btn_pause", "large_btn_play", NULL }, { "large_btn_ff", "large_btn_ff_4x", "large_btn_ff_4", NULL } };   // ff_4x is the game's "2x", ff_4 its "4x"
static void make_buttons(void *mv) {
    if (!gfx_exists(mv, "ms_btn")) { GfxVal a[2] = { gv_str("ms_btn"), gv_num(15910) }; gfx_invoke(mv, "_root.createEmptyMovieClip", NULL, a, 2); }
    if (!gfx_exists(mv, "ms_btn")) { logmsg("top movie %p: could not make the button clip", mv); g_made = -1; return; }
    for (int b = 0; b < 2; b++) {
        if (!tb_make_button(mv, "_root.ms_btn", BTN[b] + 13, b + 1, ICONS[b], 100)) { logmsg("top movie %p: button %s not made", mv, BTN[b]); g_made = -1; return; }
        g_hover[b] = g_down[b] = g_on[b] = g_icon[b] = -1;   // the first update sets every holder's _visible
    }
    gfx_setvar(mv, "_root.ms_btn.ms_speed._x", gv_num(TB_BTN * (1 + TB_GAP)));
    { GfxVal a = gv_str("_root.ms_btn"), r; memset(&r, 0, sizeof r); gfx_invoke(mv, "Receive_RegisterDefaultCallbacks", &r, &a, 1);
      if (!(r.type == GV_BOOL && r.v.b)) logmsg("mouse callbacks on the buttons NOT registered"); }
    double px, py, sz; tb_place(mv, g_scale, 0, g_offX, g_offY, &px, &py, &sz);
    gfx_setvar(mv, "_root.ms_btn._xscale", gv_num(sz / TB_BTN * 100)); gfx_setvar(mv, "_root.ms_btn._yscale", gv_num(sz / TB_BTN * 100));
    gfx_setvar(mv, "_root.ms_btn._x", gv_num(px)); gfx_setvar(mv, "_root.ms_btn._y", gv_num(py));
    logmsg("top movie %p: buttons at %.1f,%.1f (%.1f each; offset %g,%g)", mv, px, py, sz, g_offX, g_offY);
    g_made = 1; g_shown = -1;
}
static void set_icon(void *mv, int b, int i) {
    if (g_icon[b] == i) return;
    for (int k = 0; ICONS[b][k]; k++) { char nm[8]; snprintf(nm, sizeof nm, "ic%d", k); tb_vis(mv, BTN[b], nm, k == i); }
    g_icon[b] = i;
}
static void buttons(void *mv, int front) {
    LONGLONG now = qpc();
    if (mv != g_mv) { g_mv = mv; g_made = 0; g_tries = 0; }
    g_overAny = 0;
    if (g_solo != 1) { if (g_made == 1 && g_shown != 0) { gfx_setvar(mv, "_root.ms_btn._visible", gv_bool(0)); g_shown = 0; } return; }
    if (g_made == -1 && g_tries < 10 && now - g_madeAt > g_qf * 2) g_made = 0;
    if (g_made == 0 || (g_made == 1 && !gfx_exists(mv, "ms_btn"))) { g_tries++; g_madeAt = now; make_buttons(mv); }
    if (g_made != 1) return;
    if (g_shown != 1) { gfx_setvar(mv, "_root.ms_btn._visible", gv_bool(1)); g_shown = 1; }
    if (now - g_refreshAt > g_qf) { g_refreshAt = now; for (int b = 0; b < 2; b++) g_hover[b] = g_down[b] = g_on[b] = g_icon[b] = -1; }
    int shown = front && tb_getbool(mv, "_root._visible");
    int lb = shown && key(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON);
    for (int b = 0; b < 2; b++) {
        int over = shown && tb_mouse_over(mv, BTN[b]);
        g_overAny |= over;
        if (lb && !g_lbWas && over) g_press = b;
        int down = lb && over && g_press == b, on = b ? g_speed > 1 : g_paused;
        if (over != g_hover[b]) { tb_vis(mv, BTN[b], "over", over); g_hover[b] = over; }
        if (down != g_down[b]) { tb_vis(mv, BTN[b], "down", down); g_down[b] = down; }
        if (on != g_on[b]) { tb_vis(mv, BTN[b], "on", on); g_on[b] = on; }
        set_icon(mv, b, b ? (g_speed == 4 ? 2 : g_speed == 2 ? 1 : 0) : g_paused);
        if (!lb && g_lbWas && over && g_press == b) act(b ? 'c' : 'p', b ? "speed button" : "pause button");
    }
    if (!lb) g_press = -1;
    g_lbWas = lb;
}
static int front_window(void) { DWORD fpid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &fpid); return fpid == GetCurrentProcessId(); }
static void __thiscall hk_top(char *form, float dt) {
    o_top(form, dt);
    void *mv = gfx_movie_of_form(form);
    int front = front_window();
    frame(front); g_topAt = qpc();
    if (mv && g_button) buttons(mv, front);
}
// a click over our buttons is the movie's: handled here, so it never reaches the world (units stay selected, no camera move)
static char __thiscall hk_input(char *form, void *a, void *b) {
    if (g_overAny && g_shown == 1) return 1;
    return o_input(form, a, b);
}
static void __thiscall hk_menu(char *form, float dt) {
    o_menu(form, dt);
    if (qpc() - g_topAt > g_qf / 2) frame(front_window());   // no top movie (or its update stands): the keys still work
}

// ---- install
static void install(void) {
    static const unsigned char set_code[] = { 0xA1, 0x78, 0x5A, 0x33, 0x01, 0xD9, 0x44, 0x24, 0x04, 0x8B, 0x80, 0x10, 0x01, 0x00, 0x00 };
    DWORD *vt = (DWORD *)VT_SIMRATE;
    if (IsBadReadPtr(vt, 0x30) || vt[0x28 / 4] != FN_SETRATE || vt[0x2C / 4] != FN_GETRATE || memcmp((void *)FN_SETRATE, set_code, sizeof set_code)) {
        logmsg("DOW2.exe is not 3.19 (no match at the sim rate code) - not patching"); return;
    }
    int m = gfx_hook_slot(TB_SLOT_UPD, TB_UPD_FN, (void *)hk_top, (void **)&o_top);
    int in = m && gfx_hook_slot(TB_SLOT_INPUT, TB_INPUT_FN, (void *)hk_input, (void **)&o_input);
    int k = gfx_hook_slot(MENU_VT_UPD, NULLSUB_RET4, (void *)hk_menu, (void **)&o_menu);
    logmsg("MatchSpeed %s: x1 / x2 / x4 and pause in matches without other people; buttons %s, hotkeys %s%s%s", VERSION,
           !m ? "NOT hooked (top movie slot differs)" : !g_button ? "off" : in ? "at the top" : "at the top (input slot differs: a click may reach the world)",
           g_hotkeys ? "Ctrl+= / Ctrl+- / Ctrl+P" : "off", !k ? " (Menu button slot differs: keys only with the top movie)" : "",
           g_testSeq[0] ? " - TEST sequence set" : "");
}
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
static double ini_num(const wchar_t *k, const wchar_t *def) { wchar_t b[32]; GetPrivateProfileStringW(L"MatchSpeed", k, def, b, 32, g_ini); return _wtof(b); }
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wcscpy(g_ini, g_log); wcscat(g_ini, L"MatchSpeed.ini"); wcscat(g_log, L"MatchSpeed.log");
    if (already_loaded("MatchSpeed")) { logmsg("another MatchSpeed copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"MatchSpeed", L"Enabled", 1, g_ini)) { logmsg("disabled in MatchSpeed.ini"); return TRUE; }
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qf = f.QuadPart ? f.QuadPart : 1000;
    g_button = GetPrivateProfileIntW(L"MatchSpeed", L"Button", 1, g_ini) != 0;
    g_hotkeys = GetPrivateProfileIntW(L"MatchSpeed", L"Hotkeys", 1, g_ini) != 0;
    g_scale = ini_num(L"Scale", L"100"); if (g_scale < 30) g_scale = 30; if (g_scale > 200) g_scale = 200;
    g_offX = ini_num(L"OffsetX", L"0"); g_offY = ini_num(L"OffsetY", L"0");
    { char a[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, g_ini, -1, a, MAX_PATH, NULL, NULL); GetPrivateProfileStringA("MatchSpeed", "TestSeq", "", g_testSeq, sizeof g_testSeq, a); }
    install();
    return TRUE;
}
