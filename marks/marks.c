// Marks.dll - "Match marks": a button and a hotkey that note the current moment of a match or a replay for the AI testers
// (DoW2: Retribution 3.19, Elite and vanilla). AI tester 13.11.2026: he watches bot matches and replays and writes notes such as
// "15:26 bot 1 retreats for no reason"; one press now puts that moment, with what is needed to find it again, into a log:
// Marks.log next to the DLL (the launcher's "Send logs" takes it with the other add-on logs), and, in a live match with
// EliteAI.dll, the same mark into EliteAI.log between the AI's own lines.
//
// A mark (one line of Marks.log):
//   2026-11-13 08:15:02.123 MARK 3 | game 15:26 = 926.0 s = tick 9260 | live (recording Playback\temp.rec) |
//   map 2p_calderis_refinery | camera 123.4, 56.7 (height 40.2) | players: 0 "igor" space_marines team 1 local human; ...
//   - real time (local, the same clock as EliteAI.log and every add-on log);
//   - game time: the world's sim tick (World + 0x178; one tick = 0.1 s of game time, as the replay panel and MatchTimer count);
//     a replay bench takes the .rec and this tick / these seconds;
//   - live or replay: game type settings+0x118 (0x800 = a replay); in a replay the .rec the game plays (settings+0xFE4, set by
//     0x40F526 from "playback:<name>.rec"); in a live match the game records into Playback\temp.rec (it is overwritten by the
//     next match, so save it under a name from the score screen or copy it);
//   - map, players (slot, name, race, team, kind, AI difficulty) from the match's GameInfo (settings+0x118);
//   - camera: the ground point the camera looks at and its height (the camera stack of GameObj, as SCAR's Misc_GetCameraPos).
// EliteAI.log: only in a live match and only when EliteAI.dll is in the game (the new AI plays only there); the line is
//   "HH:MM:SS.mmm MARK 3 game 15:26 (tick 9260) ..." appended to <game>\Elite_DLLs\EliteAI.log (where EliteAI keeps it) the same way EliteAI writes its own lines (open,
//   append, close). No export or call into EliteAI is needed: both files carry the same real clock, so the AI's lines around a
//   mark are those with the times next to it. In a replay the AI does not run; there the game time is what matches.
//
// Controls:
//   - live match (player or observer, with other people too - a mark changes nothing in the game): a button in the replay
//     panel's style at the top left of the screen (common/topbar.h; after Match speed's pause / speed buttons when those are
//     shown);
//   - replay: the same button just left of the replay panel's pause button (playback_panel_v2; the panel's own buttons and
//     ReplaySpeed's 8x / Max stay where they are; right of the panel the movie's stage ends and nothing is drawn);
//   - hotkey Ctrl+M by default (not bound in the game's keydefaults.lua, Elite's or vanilla's); Marks.ini Hotkey= another one.
//   Feedback: the button flashes gold and a short text "Mark 3  15:26" shows next to it for 3 s - in the game's own movie.
// Rules: docs/UI_RULES.md (only the game's own movies, nothing touches Direct3D), docs/ADDON_RULES.md (its own DLL and tick).
// Marks.ini [Marks]: Enabled, Button, ReplayButton, Hotkey (Ctrl+M; "Ctrl+Shift+K", "F11", "Insert", "0" = off), Scale (85),
//   OffsetX / OffsetY, EliteAILog (1).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../common/gfxui.h"
#include "../common/topbar.h"

#define VERSION "1"

#define G_SETTINGS   0x01335A78u   // +0x110 GameObj (camera), +0x118 GameInfo: +0 game type (0x800 replay), +0x810 slots, +0x814 count
#define G_WORLD      0x013356A4u   // +0x178 tick
#define T_PLAYBACK   0x800
#define FN_CAM_BACK  0x00401B12u   // the camera stack's top (a deque's back), as Misc_GetCameraPos 0x43F6C7
#define MENU_VT_UPD  0x0110C55Cu   // Menu button form, per-frame update (the game's "ret 4"): the hotkey with no button movie
#define PANEL_SLOT   (0x011119F4u + 0x10)   // replay panel form, per-frame update (the game's "ret 4", ReplaySpeed chains there too)

static wchar_t g_log[MAX_PATH], g_ini[MAX_PATH], g_dir[MAX_PATH];
static int g_logLines;
static void logmsg(const char *fmt, ...) {
    if (g_logLines > 20000) return; g_logLines++;
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static int okp(const void *p, UINT n) { return p && (DWORD)p >= 0x10000 && !IsBadReadPtr(p, n); }
#define P32(p, o) (*(char **)((char *)(p) + (o)))
#define I32(p, o) (*(int *)((char *)(p) + (o)))
static LONGLONG g_qf;
static LONGLONG qpc(void) { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

static int g_debug, g_button = 1, g_replayButton = 1, g_eliteLog = 1; static double g_scale = 100, g_offX, g_offY;
static int g_hkVk = 'M', g_hkCtrl = 1, g_hkAlt, g_hkShift; static char g_hkName[32] = "Ctrl+M";

// ---------------- game state ----------------
static char *settings(void) { return okp((void *)G_SETTINGS, 4) && okp(*(char **)G_SETTINGS, 0x1000) ? *(char **)G_SETTINGS : NULL; }
static char *world(void) { return okp((void *)G_WORLD, 4) && okp(*(char **)G_WORLD, 0x17C) ? *(char **)G_WORLD : NULL; }
static int world_tick(void) { char *w = world(); return w ? I32(w, 0x178) : -1; }
static int is_replay(void) { char *st = settings(); return st && (I32(st, 0x118) & T_PLAYBACK); }

// the camera: the top of GameObj's camera stack, ecx = ***(*(settings+0x110)+4) for 0x401B12 (as Misc_GetCameraPos 0x43F6C7);
// it holds a 4 x 4 matrix at +4: rows right (+4), up (+0x14), forward (+0x24), the eye position (+0x34). Seen 13.11: eye
// -44.6, 144.2, -220.9, forward 0.5, -0.707, 0.5 (45 degrees down). What it looks at = where the forward ray meets the ground
// plane y = 0 (the terrain's own height there is not added: a few units off on hills).
static int camera(float *eye, float *at) {
    char *st = settings(); if (!st) return 0;
    char *a = P32(st, 0x110), *b = okp(a, 8) ? P32(a, 4) : NULL, *c = okp(b, 4) ? *(char **)b : NULL, *d = okp(c, 4) ? *(char **)c : NULL;
    if (!okp(d, 0x40)) return 0;
    char *cam = ((char *(__thiscall *)(void *))FN_CAM_BACK)(d);
    if (!okp(cam, 0x40)) return 0;
    float *f = (float *)(cam + 0x24); memcpy(eye, cam + 0x34, 12);
    if (g_debug) { char h[700]; int m = 0; for (int j = 0; j < 0x30; j++) m += snprintf(h + m, sizeof h - m, "%.3f ", ((float *)cam)[j]); logmsg("DEBUG camera %p: %s", cam, h); }
    if (f[1] < -0.05f && f[1] >= -1.01f) { float t = -eye[1] / f[1]; at[0] = eye[0] + f[0] * t; at[1] = 0; at[2] = eye[2] + f[2] * t; }
    else { at[0] = eye[0]; at[1] = 0; at[2] = eye[2]; }
    return 1;
}

// ---------------- what a mark says about the match ----------------
// GameInfo of the match (seen on stand 1, 13.11.2026, a replay of a 1v1 and a live skirmish):
//   settings + 0x2F8 char[0x104] the map's folder ("DATA:maps\pvp\"), + 0x3FC char[0x104] the map ("2p_calderisrefinery"),
//   * (settings + 0x1AC) its name on screen (L"(2p) Calderis Refinery"), * (settings + 0xEC4) the replay played ("PLAYBACK:x.rec");
//   slots settings + 0x118 + 0x810 (0xA8 each): +0 kind, +4 / +8 name (wchar begin / end), +0x10 / +0x14 race (char begin / end),
//   +0x2C AI difficulty, +0x58 team, +0x9C player id (1000, 1001 ... as in the AI's own log lines "p1000 ...").
static const char *KIND[] = { "local human", "computer", "local observer", "human", "remote computer", "observer", "open", "closed" };
static void cstr(const char *b, const char *e, int max, char *o, int cap) {   // a string of the game -> printable, quotes out
    int n = 0;
    if (okp(b, 1) && e > b && e - b <= max && okp(b, e - b))
        for (const char *c = b; c < e && *c && n < cap - 1; c++) o[n++] = (*c == '"' || *c == '|' || (unsigned char)*c < 32) ? '_' : *c;
    o[n] = 0;
}
static void wstr(const wchar_t *b, const wchar_t *e, int max, char *o, int cap) {
    o[0] = 0;
    if (!okp(b, 2)) return;
    if (!e) { e = b; while (e - b < max && okp(e, 2) && *e) e++; }
    if (e <= b || e - b > max || !okp(b, (e - b) * 2)) return;
    int n = WideCharToMultiByte(CP_UTF8, 0, b, (int)(e - b), o, cap - 1, NULL, NULL); o[n > 0 ? n : 0] = 0;
    for (char *c = o; *c; c++) if (*c == '"' || *c == '|' || (unsigned char)*c < 32) *c = '_';
}
static void info_where(char *st, int rep, char *o, int cap) {
    if (!rep) { snprintf(o, cap, "live (game type %X; the game records it into Playback\\temp.rec)", I32(st, 0x118)); return; }
    char f[300] = ""; const char *p = P32(st, 0xEC4);
    if (okp(p, 1)) { int n = 0; while (n < 290 && okp(p + n, 1) && p[n]) n++; cstr(p, p + n, 290, f, sizeof f); }
    const char *nm = !_strnicmp(f, "playback:", 9) ? f + 9 : f;
    snprintf(o, cap, "replay %s%s", nm[0] ? "Playback\\" : "(file not found)", nm);
}
static void info_map(char *st, char *o, int cap) {
    char m[0x105], d[200] = ""; memcpy(m, st + 0x3FC, 0x104); m[0x104] = 0;
    for (char *c = m; *c; c++) if ((unsigned char)*c < 32 || *c == '|') *c = '_';
    wstr(*(const wchar_t **)(st + 0x1AC), NULL, 80, d, sizeof d);
    snprintf(o, cap, "%s%s%s%s", m[0] ? m : "?", d[0] ? " \"" : "", d, d[0] ? "\"" : "");
}
// the computer's difficulty as the game setup shows it; with EliteAI.dll in a live game 0 / 1 / 2 are its new AI A / B / C
// (eliteai.c: "variant A rules A real_diff 0"), 3 stays Elite's Expert
static const char *GAME_DIFF[4] = { "Easy", "Normal", "Hard", "Expert" }, *NEWAI_DIFF[4] = { "NEW AI A", "NEW AI B", "NEW AI C", "Expert" };
static void info_players(char *st, char *o, int cap) {
    int newai = GetModuleHandleW(L"EliteAI.dll") && !(I32(st, 0x118) & T_PLAYBACK);
    char *arr = P32(st, 0x118 + 0x810); int n = I32(st, 0x118 + 0x814), k = 0, empty = 0;
    o[0] = 0;
    if (n <= 0 || n > 16 || !okp(arr, n * 0xA8)) { snprintf(o, cap, "? (no player list)"); return; }
    for (int i = 0; i < n; i++) {
        char *s = arr + i * 0xA8, name[100], race[40]; int t = I32(s, 0);
        if (t == 6 || t == 7) { empty++; continue; }
        wstr(*(const wchar_t **)(s + 4), *(const wchar_t **)(s + 8), 64, name, sizeof name);
        cstr(*(const char **)(s + 0x10), *(const char **)(s + 0x14), 40, race, sizeof race);
        char ai[48] = ""; int df = I32(s, 0x2C);
        if (t == 1 || t == 4) snprintf(ai, sizeof ai, ", AI difficulty %d (%s)", df, df < 0 || df > 3 ? "?" : (newai ? NEWAI_DIFF : GAME_DIFF)[df]);
        k += snprintf(o + k, cap - k, "%sslot %d p%d \"%s\" %s team %d %s%s", k ? "; " : "", i, I32(s, 0x9C), name, race[0] ? race : "-", I32(s, 0x58),
                      t >= 0 && t < 8 ? KIND[t] : "?", ai);
        if (k >= cap - 1) break;
    }
    if (empty && k < cap - 1) snprintf(o + k, cap - k, "%s%d empty", k ? "; " : "", empty);
}
// ---------------- a mark ----------------
static char *g_world; static int g_n; static SYSTEMTIME g_matchStart; static LONGLONG g_frameAt; static int g_lastTick = -1;
static LONGLONG g_flashUntil; static char g_flashText[64]; static int g_flashSeq;
static void elite_ai_log(const char *line) {
    if (!GetModuleHandleW(L"EliteAI.dll")) return;
    // EliteAI writes its log into <game>\Elite_DLLs\ always, also when it is loaded from dow2-patches (eliteai.c, g_dir)
    wchar_t p[MAX_PATH]; if (!GetModuleFileNameW(NULL, p, MAX_PATH)) return;
    wchar_t *sl = wcsrchr(p, L'\\'); if (!sl) return; wcscpy(sl + 1, L"Elite_DLLs\\EliteAI.log");
    FILE *f = _wfopen(p, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d.%03d %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, line); fclose(f);
}
static void mark(const char *how) {
    char *st = settings(), *w = world(); int tick = world_tick();
    if (!st || !w || tick < 0 || !I32(st, 0x118)) { logmsg("%s: not in a match - no mark", how); return; }
    int n = ++g_n, rep = is_replay(); double sec = tick / 10.0; int mm = tick / 600, ss = (tick / 10) % 60;
    char where[400], map[160], pl[1600], cam[128]; float c[3], at[3];
    info_where(st, rep, where, sizeof where); info_map(st, map, sizeof map); info_players(st, pl, sizeof pl);
    if (camera(c, at)) snprintf(cam, sizeof cam, "looks at x %.1f z %.1f (eye %.1f, %.1f, %.1f)", at[0], at[2], c[0], c[1], c[2]); else snprintf(cam, sizeof cam, "?");
    char head[96]; snprintf(head, sizeof head, "MARK %d | game %d:%02d = %.1f s = tick %d", n, mm, ss, sec, tick);
    logmsg("%s | %s | match since %02d:%02d:%02d | map %s | camera %s | players: %s | by %s", head, where,
           g_matchStart.wHour, g_matchStart.wMinute, g_matchStart.wSecond, map, cam, pl, how);
    if (g_eliteLog && !rep) {
        char l[600]; snprintf(l, sizeof l, "MARK %d game %d:%02d (%.1f s, tick %d) map %s camera %s - Marks.log has the players", n, mm, ss, sec, tick, map, cam);
        elite_ai_log(l);
    }
    snprintf(g_flashText, sizeof g_flashText, "Mark %d  %d:%02d", n, mm, ss);
    g_flashUntil = qpc() + g_qf * 3; g_flashSeq++;
}

// ---------------- frames: the hotkey and new matches (from any of the hooked per-frame calls, once a frame) ----------------
static int key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
static int front_window(void) { DWORD fpid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &fpid); return fpid == GetCurrentProcessId(); }
static int g_kWas;
static void frame(void) {
    LONGLONG now = qpc();
    if (now - g_frameAt < g_qf / 1000) return;
    char *w = world(); int tk = world_tick();
    if (w != g_world || (now - g_frameAt > g_qf * 3 && tk < g_lastTick)) {
        g_world = w; g_n = 0; GetLocalTime(&g_matchStart);
        if (w) { char *st = settings(); logmsg("match: %s (World %p)", st && (I32(st, 0x118) & T_PLAYBACK) ? "a replay" : "live", w); }
    }
    g_frameAt = now; g_lastTick = tk;
    if (!w || !g_hkVk) return;
    int k = front_window() && key(g_hkVk) && key(VK_CONTROL) == g_hkCtrl && key(VK_MENU) == g_hkAlt && key(VK_SHIFT) == g_hkShift;
    if (k && !g_kWas) mark(g_hkName);
    g_kWas = k;
}

// ---------------- the button ----------------
// live: "_root.mk_btn" in the top movie (common/topbar.h); replay: "_root.playback_panel_mc.mk_btn" in the replay panel movie,
// left of its pause button (panel units: pause at 9, 5; the panel at 202, 53 of the movie's 600 x 320 stage, which ends at the
// panel's right edge), so it hides with the panel.
typedef void (__thiscall *Upd_t)(char *form, float dt);
static Upd_t o_top, o_menu, o_panel; static TbInput_t o_input;
static const char *const ICONS[3] = { "img://data:UI/textures/space_marines/hud/minimap/ping_secondary_objective",
                                      "img://data:UI/textures/space_marines/hud/minimap/ping_primary_objective", NULL };   // blue ring, gold while flashing
#define PANEL_BTN_X -44.0         // left of the panel: its movie draws nothing right of x 600, the panel's right end
#define PANEL_BTN_Y 5.0
typedef struct {
    const char *root, *btn, *txt;          // "_root.mk_btn" / "_root.mk_btn.mk_mark" / "_root.mk_btn.mk_txt"
    void *mv; int made, tries, shown, hover, down, on, lbWas, press, over, slot, flashSeq; LONGLONG madeAt, slotAt;
} Btn;
static Btn g_top = { "_root.mk_btn", "_root.mk_btn.mk_mark", "_root.mk_btn.mk_txt", 0 },
           g_pb = { "_root.playback_panel_mc.mk_btn", "_root.playback_panel_mc.mk_btn.mk_mark", "_root.playback_panel_mc.mk_btn.mk_txt", 0 };
static LONGLONG g_topAt, g_pbAt;
static int make_text(void *mv, Btn *b, int replay) {   // live: right of the button; replay: under it (the panel is right of it)
    char p[128]; double x = replay ? 0 : TB_BTN * (1 + TB_GAP * 2), y = replay ? TB_BTN * 1.15 : TB_BTN * 0.18;
    GfxVal a[6] = { gv_str("mk_txt"), gv_num(20), gv_num(x), gv_num(y), gv_num(260), gv_num(TB_BTN * 0.8) };
    snprintf(p, sizeof p, "%s.createTextField", b->root); gfx_invoke(mv, p, NULL, a, 6);
    snprintf(p, sizeof p, "%s.html", b->txt); gfx_setvar(mv, p, gv_bool(1));
    snprintf(p, sizeof p, "%s.selectable", b->txt); gfx_setvar(mv, p, gv_bool(0));
    snprintf(p, sizeof p, "%s._visible", b->txt); gfx_setvar(mv, p, gv_bool(0));
    return 1;
}
static void make(void *mv, Btn *b, int replay) {
    const char *parent = replay ? "_root.playback_panel_mc" : "_root";
    if (replay && !gfx_exists(mv, "playback_panel_mc")) { logmsg("replay panel %p: no playback_panel_mc - no button", mv); b->made = -1; return; }
    { GfxVal a[2] = { gv_str("mk_btn"), gv_num(replay ? 16010 : 15930) }; char p[96]; snprintf(p, sizeof p, "%s.createEmptyMovieClip", parent); gfx_invoke(mv, p, NULL, a, 2); }
    if (!gfx_exists(mv, b->root + 6) || !tb_make_button(mv, b->root, "mk_mark", 1, ICONS, 72)) { logmsg("%s %p: button not made", replay ? "replay panel" : "top movie", mv); b->made = -1; return; }
    make_text(mv, b, replay);
    { GfxVal a = gv_str(b->root), r; memset(&r, 0, sizeof r); gfx_invoke(mv, "Receive_RegisterDefaultCallbacks", &r, &a, 1); }
    tb_vis(mv, b->btn, "ic1", 0); tb_vis(mv, b->btn, "over", 0); tb_vis(mv, b->btn, "down", 0); tb_vis(mv, b->btn, "on", 0);
    b->hover = b->down = b->on = 0; b->slot = -1; b->flashSeq = g_flashSeq;
    char p[96];
    if (replay) {
        snprintf(p, sizeof p, "%s._x", b->root); gfx_setvar(mv, p, gv_num(PANEL_BTN_X + g_offX));
        snprintf(p, sizeof p, "%s._y", b->root); gfx_setvar(mv, p, gv_num(PANEL_BTN_Y + g_offY));
        logmsg("replay panel %p: mark button at %.1f,%.1f of the panel", mv, PANEL_BTN_X + g_offX, PANEL_BTN_Y + g_offY);
    }
    b->made = 1; b->shown = -1;
}
// live: after Match speed's two buttons when they are shown (checked twice a second), else first in the row
static void place_top(void *mv, Btn *b, LONGLONG now) {
    if (now - b->slotAt < g_qf / 2 && b->slot >= 0) return;
    b->slotAt = now;
    int slot = gfx_exists(mv, "ms_btn") && tb_getbool(mv, "_root.ms_btn._visible") ? 2 : 0;
    if (slot == b->slot) return;
    double px, py, sz; tb_place(mv, g_scale, slot, g_offX, g_offY, &px, &py, &sz);
    gfx_setvar(mv, "_root.mk_btn._xscale", gv_num(sz / TB_BTN * 100)); gfx_setvar(mv, "_root.mk_btn._yscale", gv_num(sz / TB_BTN * 100));
    gfx_setvar(mv, "_root.mk_btn._x", gv_num(px)); gfx_setvar(mv, "_root.mk_btn._y", gv_num(py));
    logmsg("top movie %p: mark button at %.1f,%.1f (%.1f; place %d in the row%s)", mv, px, py, sz, slot, slot ? ", after Match speed" : "");
    b->slot = slot;
}
static void drive(void *mv, Btn *b, int replay, int want) {
    LONGLONG now = qpc(); char p[128];
    if (mv != b->mv) { b->mv = mv; b->made = 0; b->tries = 0; }
    b->over = 0;
    if (!want) { if (b->made == 1 && b->shown != 0) { snprintf(p, sizeof p, "%s._visible", b->root); gfx_setvar(mv, p, gv_bool(0)); b->shown = 0; } return; }
    if (b->made == -1 && b->tries < 10 && now - b->madeAt > g_qf * 2) b->made = 0;
    if (b->made == 0 || (b->made == 1 && !gfx_exists(mv, b->root + 6))) { b->tries++; b->madeAt = now; make(mv, b, replay); }
    if (b->made != 1) return;
    if (b->shown != 1) { snprintf(p, sizeof p, "%s._visible", b->root); gfx_setvar(mv, p, gv_bool(1)); b->shown = 1; }
    if (!replay) place_top(mv, b, now);
    // the flash: gold frame and ring 1.5 s, the text 3 s
    int fl = g_flashUntil && now < g_flashUntil, on = fl && g_flashUntil - now > g_qf * 3 / 2;
    if (b->flashSeq != g_flashSeq) {
        b->flashSeq = g_flashSeq; char h[200];
        snprintf(h, sizeof h, "<font face=\"$NormalFont\" size=\"22\" color=\"#FFD45A\">%s</font>", g_flashText);
        snprintf(p, sizeof p, "%s.htmlText", b->txt); gfx_setvar(mv, p, gv_str(h));
    }
    snprintf(p, sizeof p, "%s._visible", b->txt); if (tb_getbool(mv, p) != fl) gfx_setvar(mv, p, gv_bool(fl));
    if (on != b->on) { tb_vis(mv, b->btn, "on", on); tb_vis(mv, b->btn, "ic1", on); tb_vis(mv, b->btn, "ic0", !on); b->on = on; }
    int front = front_window() && tb_getbool(mv, replay ? "_root.playback_panel_mc._visible" : "_root._visible");
    int lb = front && key(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON);
    int over = front && tb_mouse_over(mv, b->btn);
    b->over = over;
    if (lb && !b->lbWas && over) b->press = 1;
    int down = lb && over && b->press;
    if (over != b->hover) { tb_vis(mv, b->btn, "over", over); b->hover = over; }
    if (down != b->down) { tb_vis(mv, b->btn, "down", down); b->down = down; }
    if (!lb && b->lbWas && over && b->press) mark(replay ? "the button (replay panel)" : "the button");
    if (!lb) b->press = 0;
    b->lbWas = lb;
}
static void __thiscall hk_top(char *form, float dt) {
    o_top(form, dt);
    frame(); g_topAt = qpc();
    void *mv = gfx_movie_of_form(form);
    if (mv && g_button) drive(mv, &g_top, 0, g_world && !is_replay());
}
static void __thiscall hk_panel(char *form, float dt) {
    o_panel(form, dt);
    frame(); g_pbAt = qpc();
    void *mv = gfx_movie_of_form(form);
    if (mv && g_replayButton) drive(mv, &g_pb, 1, g_world && is_replay());
}
static char __thiscall hk_input(char *form, void *a, void *b) {
    if (g_top.over && g_top.shown == 1) {                  // a click on the mark button never reaches the world
        if (g_debug) logmsg("DEBUG input over the button: %08lX %08lX - handled", okp(b, 4) ? *(DWORD *)b : 0, okp(a, 4) ? *(DWORD *)a : 0);
        return 1;
    }
    return o_input(form, a, b);
}
static void __thiscall hk_menu(char *form, float dt) {
    o_menu(form, dt);
    LONGLONG now = qpc();
    if (now - g_topAt > g_qf / 2 && now - g_pbAt > g_qf / 2) frame();   // no button movie: the hotkey still works
}
static void install(void) {
    int t = gfx_hook_slot(TB_SLOT_UPD, TB_UPD_FN, (void *)hk_top, (void **)&o_top);
    int in = t && gfx_hook_slot(TB_SLOT_INPUT, TB_INPUT_FN, (void *)hk_input, (void **)&o_input);
    int pb = gfx_hook_slot(PANEL_SLOT, TB_UPD_FN, (void *)hk_panel, (void **)&o_panel);
    int m = gfx_hook_slot(MENU_VT_UPD, TB_UPD_FN, (void *)hk_menu, (void **)&o_menu);
    logmsg("Marks %s: hotkey %s; button: live %s, replay %s; EliteAI.log %s%s", VERSION, g_hkName,
           !g_button ? "off" : !t ? "NOT hooked (top movie slot differs)" : in ? "at the top" : "at the top (input slot differs)",
           !g_replayButton ? "off" : pb ? "in the replay panel" : "NOT hooked (replay panel slot differs)", g_eliteLog ? "on" : "off",
           !m ? " (Menu button slot differs: the hotkey only with a button movie)" : "");
}

// ---------------- install ----------------
static int parse_hotkey(const wchar_t *s) {
    char a[64]; WideCharToMultiByte(CP_ACP, 0, s, -1, a, sizeof a, NULL, NULL);
    g_hkCtrl = g_hkAlt = g_hkShift = 0; g_hkVk = 0;
    if (!strcmp(a, "0") || !_stricmp(a, "off") || !a[0]) { strcpy(g_hkName, "off"); return 1; }
    char b[64]; snprintf(b, sizeof b, "%s", a);
    for (char *t = strtok(b, "+"); t; t = strtok(NULL, "+")) {
        while (*t == ' ') t++;
        if (!_stricmp(t, "ctrl") || !_stricmp(t, "control")) g_hkCtrl = 1;
        else if (!_stricmp(t, "alt")) g_hkAlt = 1;
        else if (!_stricmp(t, "shift")) g_hkShift = 1;
        else if (strlen(t) == 1 && isalnum((unsigned char)t[0])) g_hkVk = toupper((unsigned char)t[0]);
        else if ((t[0] == 'F' || t[0] == 'f') && atoi(t + 1) >= 1 && atoi(t + 1) <= 12) g_hkVk = VK_F1 + atoi(t + 1) - 1;
        else if (!_stricmp(t, "insert")) g_hkVk = VK_INSERT; else if (!_stricmp(t, "home")) g_hkVk = VK_HOME;
        else if (!_stricmp(t, "end")) g_hkVk = VK_END; else if (!_stricmp(t, "pageup")) g_hkVk = VK_PRIOR;
        else if (!_stricmp(t, "pagedown")) g_hkVk = VK_NEXT;
        else return 0;
    }
    if (!g_hkVk) return 0;
    snprintf(g_hkName, sizeof g_hkName, "%s", a); return 1;
}
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
static double ini_num(const wchar_t *k, const wchar_t *def) { wchar_t b[32]; GetPrivateProfileStringW(L"Marks", k, def, b, 32, g_ini); return _wtof(b); }
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
    wcscpy(g_ini, g_dir); wcscat(g_ini, L"Marks.ini"); wcscpy(g_log, g_dir); wcscat(g_log, L"Marks.log");
    if (already_loaded("Marks")) { logmsg("another Marks copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"Marks", L"Enabled", 1, g_ini)) { logmsg("disabled in Marks.ini"); return TRUE; }
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qf = f.QuadPart ? f.QuadPart : 1000;
    g_button = GetPrivateProfileIntW(L"Marks", L"Button", 1, g_ini) != 0;
    g_replayButton = GetPrivateProfileIntW(L"Marks", L"ReplayButton", 1, g_ini) != 0;
    g_debug = GetPrivateProfileIntW(L"Marks", L"Debug", 0, g_ini);
    g_eliteLog = GetPrivateProfileIntW(L"Marks", L"EliteAILog", 1, g_ini) != 0;
    g_scale = ini_num(L"Scale", L"100"); if (g_scale < 30) g_scale = 30; if (g_scale > 200) g_scale = 200;
    g_offX = ini_num(L"OffsetX", L"0"); g_offY = ini_num(L"OffsetY", L"0");
    { wchar_t hk[64]; GetPrivateProfileStringW(L"Marks", L"Hotkey", L"Ctrl+M", hk, 64, g_ini);
      if (!parse_hotkey(hk)) { logmsg("Hotkey in Marks.ini not understood - Ctrl+M"); parse_hotkey(L"Ctrl+M"); } }
    install();
    return TRUE;
}
