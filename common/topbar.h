// topbar.h - buttons of our add-ons at the TOP of the screen in a live match, styled as the replay panel's buttons (MatchSpeed
// pause / speed, Marks), in one of the game's own full-screen HUD movies. Source shared by several add-ons (each DLL is still its
// own add-on with its own tick, docs/ADDON_RULES.md); only the game's own movie, no Direct3D (docs/UI_RULES.md).
//
// Why here and not by the minimap (MatchSpeed 1): the minimap movie takes every click on it as a click on the map - a click on our
// buttons there moved the camera to the map's corner (AI testers 13.11.2026), and they wanted the buttons at the top, where the
// replay panel has its own.
//
// The host: ui\movies\hud\resource_notifications (made in 0xB41190, form vtable 0x01116478) - a movie with a 1600 x 1200 stage
// over the whole screen, present in every match (live and replay), with nothing of its own at the top left. Its form has
//   slot 3 (+0x0C) = input (the game's 0x9A3360 "xor al, al; ret 8": takes no input, events go on to the next form / the world),
//   slot 4 (+0x10) = per-frame update (the game's empty "ret 4" 0x9AB7D0) - our buttons are made and driven there.
// A click over one of our buttons is swallowed in slot 3 (returns 1 = handled), so it never reaches the world (seen 13.11: the
// game calls slot 3 for the press and the release over a clip of ours with the movie's default mouse callbacks); the click itself
// is read like everywhere else in our add-ons (GetAsyncKeyState with the game in front, the clip's _xmouse / _ymouse).
// Several add-ons chain on both slots (gfx_hook_slot); each answers for its own buttons only.
//
// Layout: one row at the top left, where a replay's panel has its pause / speed buttons: just under the observer's camera panel
// (the same movie as the replay panel, see tb_place), on the screen as that panel is - whatever the resolution and UI scale.
// Places in the row: MatchSpeed 0, 1 (pause, speed); Marks after MatchSpeed's when those are shown, else at 0.
#ifndef TOPBAR_H
#define TOPBAR_H
#include "gfxui.h"
#include <stdio.h>

#define TB_FORM_VT    0x01116478u
#define TB_SLOT_INPUT (TB_FORM_VT + 0x0C)
#define TB_INPUT_FN   0x009A3360u
#define TB_SLOT_UPD   (TB_FORM_VT + 0x10)
#define TB_UPD_FN     0x009AB7D0u
#define TB_TEX "img://data:UI/textures/space_marines/hud/playback_panel/"
#define TB_BTN 40.0                       // the replay panel's button texture, 40 x 40
#define TB_GAP 0.06                       // between two buttons, of a button

typedef char (__thiscall *TbGetVar_t)(void *movie, GfxVal *v, const char *path);
typedef void (__thiscall *TbUpd_t)(char *form, float dt);
typedef char (__thiscall *TbInput_t)(char *form, void *a, void *b);

static int tb_getnum(void *mv, const char *path, double *out) {
    GfxVal v; memset(&v, 0, sizeof v);
    if (!(*(TbGetVar_t *)(*(char **)mv + 0x30))(mv, &v, path) || v.type != GV_NUMBER) return 0;
    *out = v.v.num; return 1;
}
static int tb_getbool(void *mv, const char *path) {
    GfxVal v; memset(&v, 0, sizeof v);
    return (*(TbGetVar_t *)(*(char **)mv + 0x30))(mv, &v, path) && v.type == GV_BOOL && v.v.b;
}
static void tb_vis(void *mv, const char *btn, const char *child, int on) {
    char p[128]; snprintf(p, sizeof p, "%s.%s._visible", btn, child); gfx_setvar(mv, p, gv_bool(on));
}
// a button as the replay panel's: holders up / over / down / on (frames) and ic0.. (icons), each with an "img" clip the texture is
// loaded into (loading replaces the target clip, its _visible too, so the holder is what is shown / hidden).
// btn = full path ("_root.ms_btn.ms_pause"); icons = texture names (without .dds), a NULL ends the list; iconScale % (100 = 40 x 40)
static int tb_make_button(void *mv, const char *parent, const char *name, int depth, const char *const *icons, double iconScale) {
    static const char *FR[4] = { "large_btn", "large_btn_over", "large_btn_down", "large_btn_toggled_on" }, *FC[4] = { "up", "over", "down", "on" };
    char p[128], btn[128];
    { GfxVal a[2] = { gv_str(name), gv_num(depth) }; snprintf(p, sizeof p, "%s.createEmptyMovieClip", parent); gfx_invoke(mv, p, NULL, a, 2); }
    snprintf(btn, sizeof btn, "%s.%s", parent, name);
    int k = 1;
    for (int f = 0; f < 4 + 8; f++) {
        char nm[8]; const char *tex;
        if (f < 4) { snprintf(nm, sizeof nm, "%s", FC[f]); tex = FR[f]; }
        else { if (!icons[f - 4]) break; snprintf(nm, sizeof nm, "ic%d", f - 4); tex = icons[f - 4]; }
        { GfxVal a[2] = { gv_str(nm), gv_num(k++) }; snprintf(p, sizeof p, "%s.createEmptyMovieClip", btn); gfx_invoke(mv, p, NULL, a, 2); }
        { GfxVal a[2] = { gv_str("img"), gv_num(1) }; snprintf(p, sizeof p, "%s.%s.createEmptyMovieClip", btn, nm); gfx_invoke(mv, p, NULL, a, 2); }
        char c[128], t[200];
        snprintf(c, sizeof c, "%s.%s.img", btn, nm);
        snprintf(t, sizeof t, "%s%s.dds", strncmp(tex, "img://", 6) ? TB_TEX : "", tex);
        GfxVal a[2] = { gv_str(c), gv_str(t) }; gfx_invoke(mv, "Receive_LoadMovie", NULL, a, 2);
        if (f >= 4 && iconScale != 100) {   // a smaller icon in the middle of the frame
            double s = iconScale, o = TB_BTN * (100 - s) / 200;
            snprintf(p, sizeof p, "%s.%s._xscale", btn, nm); gfx_setvar(mv, p, gv_num(s)); snprintf(p, sizeof p, "%s.%s._yscale", btn, nm); gfx_setvar(mv, p, gv_num(s));
            snprintf(p, sizeof p, "%s.%s._x", btn, nm); gfx_setvar(mv, p, gv_num(o)); snprintf(p, sizeof p, "%s.%s._y", btn, nm); gfx_setvar(mv, p, gv_num(o));
        }
    }
    snprintf(p, sizeof p, "%s", btn + 6);   // without "_root."
    return gfx_exists(mv, p);
}
static int tb_mouse_over(void *mv, const char *btn) {
    char p[128]; double x, y;
    snprintf(p, sizeof p, "%s._xmouse", btn); if (!tb_getnum(mv, p, &x)) return 0;
    snprintf(p, sizeof p, "%s._ymouse", btn); if (!tb_getnum(mv, p, &y)) return 0;
    return x >= 0 && y >= 0 && x < TB_BTN && y < TB_BTN;
}
// the row's place (topbar 2, 16.11.2026). Where a HUD movie really is on the screen is the game's own layout, vtable slot 6
// (+0x18) of its form: layout(screen w, screen h, k, &x, &y, &w, &h) - w / h come in as the movie's own stage size and go out
// times scale = 0x00B7C130(screen h, k) = clamp(h / 1200 * uiscale * k, 0.5, 1.0) (UIScale.dll replaces that function:
// S * k, 0.25 ... 4.0). Then
//   resource_notifications (0x00B46BE0): 1600 x 1200 * scale, CENTRED in the window (x = (W - w) / 2, y = (H - h) / 2);
//   playback_panel_v2      (0x00B4D950): 600 x 320 * scale, at the window's top left (0, 0) - the replay panel AND the
//     observer's camera panel (the ^ - v < person buttons and the fog-of-war list, the row above the replay's pause / speed row;
//     a live observer has only that row).
// topbar 1 took the host as fitted into the window (showAll), which is true only at 800 lines without UIScale (1280 x 800:
// scale 0.667, x = px * 1.5 - 160); at 1440p with UIScale (auto 133 %, AI tester fb 1489-1500) the host is 2133 x 1600
// pixels centred, the row landed at the window's y -3 and the observer panel covered it (its buttons 0 ... 50 pixels).
// Now both layouts are read where the game makes them (slot 6 of both forms, chained: the game's result is only copied), and
// the row is put where the replay panel has its pause button - panel (211, 58), just under the observer's row - in pixels of
// the panel's own scale, then turned into the host's units. Not laid out yet (the DLL came after): the game's formula with
// k = 1, the panel at (0, 0) with the host's scale. The movie draws only inside its stage, so the row is kept inside it
// (a host smaller than the window, e.g. 1440p without UIScale: 1600 x 1200 in the middle - the row goes right / down, never up).
#define TB_LEFT 211.0                     // the replay panel's pause button in its movie (panel 202, 53 + button 9, 5)
#define TB_TOP  58.0
#define TB_HOST_W 1600.0                  // resource_notifications' stage
#define TB_HOST_H 1200.0
#define TB_SLOT_LAYOUT  (TB_FORM_VT + 0x18)
#define TB_LAYOUT_FN    0x00B46BE0u
#define TB_PANEL_VT     0x011119F4u
#define TB_PANEL_LAYOUT (TB_PANEL_VT + 0x18)
#define TB_PANEL_LAY_FN 0x00B4D950u
typedef void (__thiscall *TbLayout_t)(char *form, int sw, int sh, float k, int *x, int *y, int *w, int *h);
static TbLayout_t tb_oHostLay, tb_oPanelLay;
static int tb_host[5], tb_panel[5];       // x, y, w, h in window pixels as the game laid the movie out, + its stage width; w 0 = not seen
static volatile LONG tb_laySeq;           // +1 on every change: the row is placed again
static int tb_in(int *w) { return !IsBadReadPtr(w, 4) ? *w : 0; }
static void tb_keep(int *r, int w0, int *x, int *y, int *w, int *h) {
    if (IsBadReadPtr(x, 4) || IsBadReadPtr(y, 4) || IsBadReadPtr(w, 4) || IsBadReadPtr(h, 4) || *w <= 0 || *h <= 0 || w0 <= 0) return;
    if (r[0] != *x || r[1] != *y || r[2] != *w || r[3] != *h || r[4] != w0) { r[0] = *x; r[1] = *y; r[2] = *w; r[3] = *h; r[4] = w0; InterlockedIncrement(&tb_laySeq); }
}
static void __thiscall tb_hk_host_lay(char *f, int sw, int sh, float k, int *x, int *y, int *w, int *h) {
    int w0 = tb_in(w); tb_oHostLay(f, sw, sh, k, x, y, w, h); tb_keep(tb_host, w0, x, y, w, h);
}
static void __thiscall tb_hk_panel_lay(char *f, int sw, int sh, float k, int *x, int *y, int *w, int *h) {
    int w0 = tb_in(w); tb_oPanelLay(f, sw, sh, k, x, y, w, h); tb_keep(tb_panel, w0, x, y, w, h);
}
// hook both layouts (each add-on with the row: chained, gfx_hook_slot); 1 = host's, 2 = panel's
static int tb_install_layout(void) {
    return gfx_hook_slot(TB_SLOT_LAYOUT, TB_LAYOUT_FN, (void *)tb_hk_host_lay, (void **)&tb_oHostLay)
         | gfx_hook_slot(TB_PANEL_LAYOUT, TB_PANEL_LAY_FN, (void *)tb_hk_panel_lay, (void **)&tb_oPanelLay) << 1;
}
static LONG tb_layout_seq(void) { return tb_laySeq; }
static HWND g_tbWnd;
static BOOL CALLBACK tb_enum(HWND h, LPARAM lp) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid); RECT r;
    if (pid == GetCurrentProcessId() && IsWindowVisible(h) && GetClientRect(h, &r) && r.right * r.bottom > *(LONG *)lp) { *(LONG *)lp = r.right * r.bottom; g_tbWnd = h; }
    return TRUE;
}
static int tb_client(LONG *w, LONG *h) {
    RECT r;
    if (!g_tbWnd || !IsWindow(g_tbWnd)) { LONG best = 0; g_tbWnd = NULL; EnumWindows(tb_enum, (LPARAM)&best); }
    if (!g_tbWnd || !GetClientRect(g_tbWnd, &r) || r.right < 200 || r.bottom < 200) return 0;
    *w = r.right; *h = r.bottom; return 1;
}
// scale % of the 40 x 40 texture (100 = the replay panel's own size), slot = place in the row; px / py / sz in the host's units;
// how (may be NULL, 160 bytes) = what the place came from, for the log
static void tb_place(void *mv, double scale, int slot, double offX, double offY, double *px, double *py, double *sz, char *how) {
    (void)mv;
    double hx, hy, hs, pX = 0, pY = 0, ps; const char *src;
    if (tb_host[2] > 0) { hx = tb_host[0]; hy = tb_host[1]; hs = (double)tb_host[2] / tb_host[4]; src = "laid out"; }
    else {                                   // the game's formula with k = 1 (without UIScale)
        LONG w = 1280, h = 800; tb_client(&w, &h);
        hs = h / 1200.0; if (hs < 0.5) hs = 0.5; if (hs > 1.0) hs = 1.0;
        hx = (w - TB_HOST_W * hs) / 2; hy = (h - TB_HOST_H * hs) / 2; src = "game formula";
    }
    if (tb_panel[2] > 0) { pX = tb_panel[0]; pY = tb_panel[1]; ps = (double)tb_panel[2] / tb_panel[4]; } else ps = hs;
    *sz = TB_BTN * scale / 100 * ps / hs;    // the replay panel's button size on the screen
    double x = (pX + TB_LEFT * ps - hx) / hs, y = (pY + TB_TOP * ps - hy) / hs;
    if (x < 4) x = 4; if (y < 4) y = 4;
    *px = x + slot * *sz * (1 + TB_GAP) + offX;
    *py = y + offY;
    if (how) snprintf(how, 160, "host %s %.0f,%.0f x%.3f (stage %d), panel %s %.0f,%.0f x%.3f (stage %d)", src, hx, hy, hs, tb_host[4],
                      tb_panel[2] > 0 ? "laid out" : "as host", pX, pY, ps, tb_panel[4]);
}
#endif
