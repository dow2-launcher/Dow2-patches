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
// Layout: one row at the top left, where a replay's panel has its pause / speed buttons (an observer's own panel sits above it).
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
// the row's place on the host's stage. The game shows its full-screen HUD movies with scaleMode showAll: 1600 x 1200 fitted into
// the window, so one stage unit = max(1600 / width, 1200 / height) pixels^-1 and the stage is centred (on 16:10 1280 x 800:
// 1.5 units a pixel, the window's left edge at x -160 - measured with the cursor, 13.11). The movie draws only inside its stage
// (seen: a text past the stage edge of a smaller movie is cut). The replay panel's movie has the same scale, anchored at the
// window's top left; its pause button is at 211, 58 there (panel 202, 53 + button 9, 5). The row starts at that point - in the
// host's units the window's top left + (211, 58) - and is kept inside the stage (an ultra-wide window: further right).
#define TB_LEFT 211.0
#define TB_TOP  58.0
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
// scale % of the 40 x 40 texture (100 = the replay panel's own size), slot = place in the row
static void tb_place(void *mv, double scale, int slot, double offX, double offY, double *px, double *py, double *sz) {
    (void)mv;
    LONG w = 1280, h = 800; tb_client(&w, &h);
    double u = 1600.0 / w > 1200.0 / h ? 1600.0 / w : 1200.0 / h, x0 = 800 - w * u / 2, y0 = 600 - h * u / 2;
    *sz = TB_BTN * scale / 100;
    double x = x0 + TB_LEFT, y = y0 + TB_TOP;
    if (x < 4) x = 4; if (y < 4) y = 4;
    *px = x + slot * *sz * (1 + TB_GAP) + offX;
    *py = y + offY;
}
#endif
