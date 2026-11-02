// gfxui.h - our text inside the game's own HUD movies (Scaleform GFx 2.x in DOW2.exe 3.19), no Direct3D (owner 02.11.2026:
// "встроиться в существующие элементы" - no drawing over the game through the video driver).
//
// How the engine itself talks to a HUD movie (seen in the widget code, 0x686080 = a text widget's SetText):
//   widget + 4 = its form (one per HUD movie), widget + 8 = the widget's clip path ("/ability_btn01");
//   form + 4   = the movie (GFxMovieView), its vtable in DOW2.exe's .rdata;
//   movie vtable +0x48 = Invoke(method, GFxValue *result, const GFxValue *args, n) - the engine calls the movie's own
//   Receive_* functions this way ("Receive_SetText" with the widget path and a wide string); a method may be a path
//   ("_root.menu_btn.createTextField": called with that clip as this);
//   movie vtable +0x2C = SetVariable(path, const GFxValue *, setType) (seen in GFx's own IME code, 0xDADB4B).
// Every HUD movie carries the same Receive_* API (ui/movies/hud/*.gfx, frame 1): SetText, SetVisible, SetTextColor,
// DuplicateMovieClip, SetMovieClipPosition, SetMovieClipScale, DoesInstanceExist... We call those first, SetVariable only
// for what they don't cover. Call all of it on the game's UI thread only (inside the engine's own per-frame calls).
#ifndef GFXUI_H
#define GFXUI_H
#include <windows.h>
#include <string.h>

typedef struct { int type; int pad; union { double num; unsigned char b; const wchar_t *ws; const char *s; } v; } GfxVal;
enum { GV_UNDEF = 0, GV_NULL = 1, GV_BOOL = 2, GV_NUMBER = 3, GV_STRING = 4, GV_WSTRING = 5 };
typedef char (__thiscall *GfxInvoke_t)(void *movie, const char *method, GfxVal *res, const GfxVal *args, unsigned n);
typedef char (__thiscall *GfxSetVar_t)(void *movie, const char *path, const GfxVal *v, int setType);

static inline GfxVal gv_str(const char *s) { GfxVal v; memset(&v, 0, sizeof v); v.type = GV_STRING; v.v.s = s; return v; }
static inline GfxVal gv_num(double d) { GfxVal v; memset(&v, 0, sizeof v); v.type = GV_NUMBER; v.v.num = d; return v; }
static inline GfxVal gv_bool(int b) { GfxVal v; memset(&v, 0, sizeof v); v.type = GV_BOOL; v.v.b = (unsigned char)(b != 0); return v; }

// a live movie: readable, its vtable inside DOW2.exe's .rdata (GFx is linked into the exe)
static inline int gfx_ok(void *movie) {
    if (!movie || IsBadReadPtr(movie, 4)) return 0;
    DWORD vt = *(DWORD *)movie;
    return vt >= 0x00F87000 && vt < 0x0124F000 && !IsBadReadPtr((void *)vt, 0x4C);
}
static inline void *gfx_movie_of_form(const char *form) {
    void *m = form && !IsBadReadPtr(form + 4, 4) ? *(void **)(form + 4) : NULL;
    return gfx_ok(m) ? m : NULL;
}
static inline void *gfx_movie_of_widget(const char *w) {
    return w && !IsBadReadPtr(w + 4, 4) ? gfx_movie_of_form(*(char **)(w + 4)) : NULL;
}
static inline int gfx_invoke(void *movie, const char *method, GfxVal *res, const GfxVal *args, unsigned n) {
    return (*(GfxInvoke_t *)(*(char **)movie + 0x48))(movie, method, res, args, n) != 0;
}
static inline int gfx_setvar(void *movie, const char *path, GfxVal v) {
    return (*(GfxSetVar_t *)(*(char **)movie + 0x2C))(movie, path, &v, 0) != 0;   // 0 = SV_Normal: nothing kept for later
}
// the movie's own Receive_* functions
static inline int gfx_set_text(void *movie, const char *path, const char *text) {
    GfxVal a[2] = { gv_str(path), gv_str(text) }; return gfx_invoke(movie, "Receive_SetText", NULL, a, 2);
}
static inline int gfx_set_visible(void *movie, const char *path, int on) {
    GfxVal a[2] = { gv_str(path), gv_bool(on) }; return gfx_invoke(movie, "Receive_SetVisible", NULL, a, 2);
}
static inline int gfx_exists(void *movie, const char *path) {
    GfxVal a = gv_str(path), r; memset(&r, 0, sizeof r);
    return gfx_invoke(movie, "Receive_DoesInstanceExist", &r, &a, 1) && r.type == GV_BOOL && r.v.b;
}
// a vtable slot of the engine (.rdata): put fn in if the slot still holds expect; 1 = done
static inline int gfx_patch_slot(DWORD slot, DWORD expect, void *fn) {
    DWORD *s = (DWORD *)slot, old;
    if (IsBadReadPtr(s, 4) || *s != expect || !VirtualProtect(s, 4, PAGE_READWRITE, &old)) return 0;
    *s = (DWORD)fn; VirtualProtect(s, 4, old, &old);
    return 1;
}
#endif
