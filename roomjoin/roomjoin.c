// RoomJoin.dll (experiment, 03.11.2026, siritos) - "Join" from the launcher's Lobbies tab, for DoW2 Retribution / Elite (DOW2.exe 3.19).
//
// The launcher writes Elite_DLLs/RoomJoin.txt (line 1: the room id from Relic's list of open custom games = the advertisement
// id; line 2: its name) and starts the game. Here, once the game is in its main menu: a cover ("Joining the Game", the room's
// name, Cancel), the game browser's own join by the room id (the call MMFind's fast automatch guests use since 0.37), the lobby
// screen - then the cover goes and the lobby is the player's (hero, ready: as always). No way in within 20 s, or Cancel: the
// cover goes and the game opens the public games list. The launcher reads Elite_DLLs/RoomJoin_result.txt ("ok <room>" /
// "fail <room> <why>"). A request older than 3 minutes is dropped (the game was started some other way). Rooms with a password:
// not offered by the launcher.
//
// Its own add-on next to MMFind (the automatch is not touched): only what joining needs. Hooks, all chained onto whatever is
// there already:
//   - the fscommand handler (Flash -> engine): which full-screen panel is up (OnLoadComplete), the menu movie and its handler
//     (to send Back / Public Games), the loading screen. Not a patch of its code (0x681AB0 - MMFind and LobbyStats patch that,
//     and MMFind refuses to start when it does not find the original bytes there) but its vtable slot 0x010BAC9C, the only
//     reference to it in DOW2.exe 3.19 (no direct call): we come first and call whatever the slot held, so the order the
//     add-ons load in does not matter (RoomJoin 8; review 03.11: a later MMFind would have found our jump and not started);
//   - Shark.dll shark::update (import 0x00F88068): the main-thread tick.
//   - the engine -> Flash Invoke 0x45DB6C (MMFind and LobbyStats chain onto each other there too): the menu movie and its
//     fscommand handler at once (Elite's injector loads us ~9 s after the start, when the main menu is up already - its
//     OnLoadComplete is long gone and the menu sends no command until the player clicks something: 03.11 the cover never came).
// The automatch first (RoomJoin 8, review 03.11): while MMFind's cover is up (its "Match Found" walk through the menus) or a
// search runs on Find a Game, a Join request is dropped and nothing on the screen is touched (no Back on Find a Game).
// RoomJoin.ini [RoomJoin] Enabled=0: off (no hooks).
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>

#define HOOK_ADDR   0x0045DB6C                    // Invoke (engine -> Flash), this = SFMovie: +4 the movie, +0x18 its fscommand handler
#define HOOK_RESUME 0x0045DB71
static const unsigned char INV_PRO[5] = {0xB8, 0x59, 0x70, 0xEF, 0x00};
#define FN_FSCMD    0x00681AB0                    // SFMovie's FSCommandHandler::Callback
#define VT_FSCMD    0x010BAC9C                    // its vtable slot (.rdata; the vtable starts at 0x010BAC98, slot 0 = 0x00681E80)
#define FN_FSCMD_VT0 0x00681E80
#define IAT_SHARK_UPDATE 0x00F88068

typedef struct { int type; int pad; union { double num; unsigned char b; const wchar_t *ws; const char *s; } v; } GFxValue;
enum { GFX_BOOL = 2, GFX_NUMBER = 3, GFX_STRING = 4, GFX_WSTRING = 5 };
typedef void (__fastcall *FsCmdFn)(void *self, void *edx, void *movie, const char *cmd, const char *args);
typedef void (__stdcall *UpdFn)(float dt);
typedef char (__fastcall *InvokeFn)(void *self, void *edx, const char *method, int flags, GFxValue *args, unsigned count);
static FsCmdFn g_fsOrig; static UpdFn g_updOrig; static InvokeFn g_invOrig;

static wchar_t g_dir[MAX_PATH]; static CRITICAL_SECTION g_lk;
static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsRoomJoin.log", g_dir);
    EnterCriticalSection(&g_lk);
    FILE *f = _wfopen(p, L"a"); if (f) {
        SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_lk);
}

// ---------------- the menu, as the fscommands show it ----------------
enum { SC_OTHER, SC_MAIN, SC_MM, SC_LIST, SC_SETUP };
static volatile int g_screen, g_loading;
static char g_panel[160];
static void *g_fsSelf, *g_fsMovie; static int g_sending;
// the automatch (MMFind / the game's own search) on Find a Game: the engine's search animation as it is sent (-1 not seen),
// the player's Start / Cancel Searching since the panel came up. Both forgotten when another panel comes or a match loads.
static volatile int g_engSearch = -1, g_mmClicked;
static void screen_reset(const char *why) {
    if (g_screen != SC_OTHER || g_panel[0]) logmsg("screen forgotten (%s)", why);
    g_screen = SC_OTHER; g_panel[0] = 0; g_engSearch = -1; g_mmClicked = 0;
}

// ---------------- ActionScript access (Scaleform 2.x inside DOW2.exe 3.19; as in MMFind) ----------------
#define FN_STR_NEW   0x00DB64D0
#define FN_AS_CALL   0x00DD27E0
#define FN_STK_GROW  0x00DB7950
#define FN_STK_POP   0x00DB7BE0
#define FN_VAL_FREE  0x00DBA000
typedef struct { unsigned char b[16]; } ASValue;
typedef void *(__fastcall *StrNewFn)(void *mgr, void *edx, const char *s);
typedef void  (__fastcall *ThisFn)(void *self, void *edx);
typedef void *(__fastcall *EnvFn)(void *sprite, void *edx);
typedef char  (__fastcall *AsCallFn)(void *sprite, void *edx, const char *path, ASValue *res, int n);
typedef void  (__fastcall *PopFn)(void *stk, void *edx, int n);
typedef char  (__fastcall *GetVarFn)(void *movie, void *edx, GFxValue *out, const char *path);
typedef char  (__fastcall *SetVarFn)(void *movie, void *edx, const char *path, const GFxValue *v, int type);
enum { AV_UNDEF, AV_NUM, AV_BOOL, AV_STR };
typedef struct { int kind; double num; const char *s; } Arg;
static void *as_str(void *movie, const char *s) {
    void *mgr = (char *)*(void **)((char *)movie + 0x5C4) + 0x238;
    int *n = ((StrNewFn)FN_STR_NEW)(mgr, NULL, s); n[3]++; return n;
}
static int as_call(void *movie, const char *method, const Arg *args, int n) {
    void *sprite = *(void **)((char *)movie + 0x24); if (!sprite) return 0;
    void *env = ((EnvFn)(*(void ***)sprite)[0x5C / 4])(sprite, NULL);
    char *stk = (char *)env + 8;
    for (int i = n - 1; i >= 0; i--) {
        *(char **)stk += 16;
        if (*(char **)stk >= *(char **)(stk + 8)) ((ThisFn)FN_STK_GROW)(stk, NULL);
        unsigned char *v = *(unsigned char **)stk; if (!v) continue;
        memset(v, 0, 16);
        if (args[i].kind == AV_NUM) { v[0] = 3; memcpy(v + 4, &args[i].num, 8); }
        else if (args[i].kind == AV_BOOL) { v[0] = 2; v[4] = args[i].num != 0; }
        else if (args[i].kind == AV_STR) { v[0] = 5; *(void **)(v + 4) = as_str(movie, args[i].s); }
    }
    ASValue res; memset(&res, 0, sizeof res);
    char ok = ((AsCallFn)FN_AS_CALL)(sprite, NULL, method, &res, n);
    ((PopFn)FN_STK_POP)(stk, NULL, n);
    if (res.b[0] >= 5) ((ThisFn)FN_VAL_FREE)(&res, NULL);
    return ok;
}
static int as_fnv(void *movie, const char *fn, const char *sig, ...) {      // n number, b bool, s string
    Arg a[12]; int n = 0; va_list ap; va_start(ap, sig);
    for (const char *c = sig; *c && n < 12; c++, n++) {
        a[n].kind = *c == 'n' ? AV_NUM : *c == 'b' ? AV_BOOL : AV_STR; a[n].num = 0; a[n].s = NULL;
        if (*c == 'n') a[n].num = va_arg(ap, double); else if (*c == 'b') a[n].num = va_arg(ap, int); else a[n].s = va_arg(ap, const char *);
    }
    va_end(ap); return as_call(movie, fn, a, n);
}
static int as_set(void *movie, const char *path, GFxValue *v) { return ((SetVarFn)(*(void ***)movie)[0x2C / 4])(movie, NULL, path, v, 1) != 0; }
static int as_get(void *movie, const char *path, GFxValue *v) { memset(v, 0, sizeof *v); return ((GetVarFn)(*(void ***)movie)[0x30 / 4])(movie, NULL, v, path) != 0; }
static void as_setw(void *movie, const char *path, const wchar_t *w) { GFxValue v; memset(&v, 0, sizeof v); v.type = GFX_WSTRING; v.v.ws = w; as_set(movie, path, &v); }
static void as_setn(void *movie, const char *path, double d) { GFxValue v; memset(&v, 0, sizeof v); v.type = GFX_NUMBER; v.v.num = d; as_set(movie, path, &v); }
static void as_setb(void *movie, const char *path, int b) { GFxValue v; memset(&v, 0, sizeof v); v.type = GFX_BOOL; v.v.b = b; as_set(movie, path, &v); }

static void fs(const char *rel, const char *args) {          // a menu command as its button sends it; rel ".OnClickX" = on the panel
    if (!g_fsSelf || !g_fsMovie || g_loading) return;
    char cmd[300]; if (rel[0] == '.') snprintf(cmd, sizeof cmd, "%s%s", g_panel, rel); else snprintf(cmd, sizeof cmd, "%s", rel);
    logmsg("-> fscommand %s", cmd);
    g_sending++; g_fsOrig(g_fsSelf, NULL, g_fsMovie, cmd, args ? args : ""); g_sending--;
}

// which full-screen panel is up, asked from the menu movie itself (the panel clip's _url) - for the screen we came in on
static int probe_screen(void) {
    void *m = g_fsMovie; if (!m) return -1;
    for (int k = 1; k <= 3; k++) {
        char pth[96], q[128]; GFxValue v; snprintf(pth, sizeof pth, "_level0.display_item.panel%d_mc.panel_mc", k);
        snprintf(q, sizeof q, "%s._visible", pth); if (!as_get(m, q, &v) || v.type != GFX_BOOL || !v.v.b) continue;
        snprintf(q, sizeof q, "%s._url", pth); if (!as_get(m, q, &v)) continue;
        char u[200] = "";
        if (v.type == GFX_STRING && v.v.s && !IsBadReadPtr(v.v.s, 1)) snprintf(u, sizeof u, "%s", v.v.s);
        else if (v.type == GFX_WSTRING && v.v.ws && !IsBadReadPtr(v.v.ws, 2)) WideCharToMultiByte(CP_UTF8, 0, v.v.ws, -1, u, sizeof u, NULL, NULL);
        for (char *c = u; (c = strstr(c, "%5F")) || (c = strstr(u, "%5f")); ) { *c = '_'; memmove(c + 1, c + 3, strlen(c + 3) + 1); }   // the URL comes encoded (main%5Fmenu.gfx)
        int sc = strstr(u, "main_menu.gfx") ? SC_MAIN : strstr(u, "matchmaking.gfx") ? SC_MM : strstr(u, "game_list_movie.gfx") ? SC_LIST
               : strstr(u, "game_setup_movie.gfx") ? SC_SETUP : -1;
        if (sc >= 0) {
            strcpy(g_panel, pth); g_screen = sc; logmsg("screen asked: %s = %s", pth, u);
            if (sc == SC_MM) g_mmClicked = 1;           // Find a Game since before we knew: a search may be on - taken as on
            return sc; }
    }
    return -1;
}

static int is_menu_movie(void *mv) {
    if (!mv || IsBadReadPtr(mv, 0x600)) return 0;
    DWORD vt = *(DWORD *)mv;                                          // GFx is linked into DOW2.exe: its vtable is there
    if (vt < 0x00F87000 || vt >= 0x0124F000 || IsBadReadPtr((void *)vt, 0x34)) return 0;
    GFxValue v; return as_get(mv, "_level0.display_item._visible", &v) && v.type == GFX_BOOL;
}
static void menu_seen(void *mv, void *handler, const char *how) {   // the menu movie (and the handler its buttons use)
    if (g_fsMovie == mv && (g_fsSelf || !handler)) return;
    g_fsMovie = mv; if (handler) g_fsSelf = handler;
    logmsg("the menu movie %p (handler %p) from %s", mv, g_fsSelf, how);
}
// MMFind's cover ("Searching" / "Match Found": it walks the menus itself then) - in the menu movie, as MMFind makes it
static int mm_cover(void) { GFxValue v; return g_fsMovie && !g_loading && as_get(g_fsMovie, "_level0.mmcover._visible", &v) && v.type == GFX_BOOL; }
// a search on Find a Game: the game's own (MMFind's native automatch) or MMFind's queue (the player pressed Start Searching)
static int mm_search(void) { return g_screen == SC_MM && (g_engSearch == 1 || g_mmClicked); }

// ---------------- the game browser's join by room id (MMFind 0.34-0.37: research_direct/directjoin.c) ----------------
// (*0x01335014)->vtbl[0x20]()->vtbl[0x34](this, midLo, midHi, &joiners, -1, -1), joiners = a vector of one 0x30-byte entry
// {own Relic profile id, ...}; the room we are in: (*0x01335014)->vtbl[0x24]() -> GameInfo, +0x8 the room id
#define FN_PROFBUF_CTOR 0x00537D40
#define FN_PROFBUF_DTOR 0x00401DE9
#define FN_JOINER_CTOR  0x00455BA9
#define FN_VEC_PUSH     0x00458416
#define FN_STR_CTOR     0x004132B2
#define FN_OP_NEW       0x00DAA9B6
#define FN_TO_SETUP     0x0047397D
typedef void (__stdcall *DjFn1)(void *);
typedef void *(__fastcall *DjObjFn)(void *self, void *edx);
typedef char (__stdcall *DjInfoFn)(void *self, void *info);
typedef void (__stdcall *DjProfFn)(void *self, void *buf);
typedef void (__stdcall *DjJoinFn)(void *self, unsigned midLo, unsigned midHi, void *joiners, unsigned frLo, unsigned frHi);
static void *dj_mgr(int off) {
    void **m = *(void ***)0x01335014; if (!m || IsBadReadPtr(m, 4)) return NULL;
    void **vt = *(void ***)m; if (IsBadReadPtr(vt, off + 4)) return NULL;
    return ((DjObjFn)vt[off / 4])(m, NULL);
}
static unsigned long long dj_room(void) {
    unsigned long long room = 0;
    void *m = dj_mgr(0x24); if (!m || IsBadReadPtr(m, 8)) return 0;
    static __attribute__((aligned(16))) unsigned char info[0x2000];
    memset(info, 0, sizeof info); ((DjFn1)0x0040172D)(info);
    void **mvt = *(void ***)m;
    if (!IsBadReadPtr(mvt, 8) && ((DjInfoFn)mvt[1])(m, info)) room = *(unsigned long long *)(info + 0x08);
    ((DjFn1)0x00401794)(info);
    return room;
}
static int dj_join(unsigned long long room) {
    static __attribute__((aligned(16))) unsigned char prof[0x400], entry[0x40]; static unsigned vec[4];
    memset(prof, 0, sizeof prof); ((DjFn1)FN_PROFBUF_CTOR)(prof);
    void *p4 = dj_mgr(0x4); if (!p4) { logmsg("join: no profile object"); return 0; }
    ((DjProfFn)(*(void ***)p4)[0x54 / 4])(p4, prof);
    unsigned pidLo = *(unsigned *)prof, pidHi = *(unsigned *)(prof + 4);
    if (!pidLo || pidLo == 0xFFFFFFFFu) { ((DjFn1)FN_PROFBUF_DTOR)(prof); return -1; }       // not signed in to Relic yet: later
    memset(entry, 0, sizeof entry); ((DjFn1)FN_JOINER_CTOR)(entry);
    *(unsigned *)entry = pidLo; *(unsigned *)(entry + 4) = pidHi; *(unsigned *)(entry + 0x10) = 0; *(unsigned *)(entry + 0x14) = 0;
    memset(vec, 0, sizeof vec);
    void *e = entry, *v = vec;
    __asm__ volatile("push %1\n\tcall *%2" : "+D"(v) : "r"(e), "r"((void *)FN_VEC_PUSH) : "eax", "ecx", "edx", "memory");   // EDI = vector
    void *j = dj_mgr(0x20); if (!j) { logmsg("join: no join object"); ((DjFn1)FN_PROFBUF_DTOR)(prof); return 0; }
    logmsg("join: room %llu (my profile %u)", room, pidLo);
    ((DjJoinFn)(*(void ***)j)[0x34 / 4])(j, (unsigned)room, (unsigned)(room >> 32), vec, 0xffffffff, 0xffffffff);
    ((DjFn1)FN_PROFBUF_DTOR)(prof);
    return 1;
}
static void dj_to_lobby(void) {                                // the menu to the lobby screen, as after a join from the browser
    static __attribute__((aligned(16))) unsigned char str[0x40]; static char alloc_dummy;
    memset(str, 0, sizeof str);
    void *sp = str; const char *name = "game_list_movie"; void *al = &alloc_dummy;
    __asm__ volatile("push %2\n\tpush %1\n\tcall *%3" : "+c"(sp) : "r"(name), "r"(al), "r"((void *)FN_STR_CTOR) : "eax", "edx", "memory");
    typedef void *(__cdecl *NewFn)(size_t);
    unsigned *data = (unsigned *)((NewFn)FN_OP_NEW)(8); if (!data) return;
    data[0] = 0x010935FC; data[1] = 3;
    ((void (__stdcall *)(unsigned, void *, void *))FN_TO_SETUP)(0, str, data);
}

// ---------------- the cover (as MMFind's "Match Found" screen) ----------------
static int g_coverWant, g_coverOn; static wchar_t g_coverLine[160], g_coverTitle[64] = L"Joining the Game"; static DWORD g_noticeUntil; static int g_noticeList; static double g_coverH = 1200; static DWORD g_coverTextTick;
static double g_btnX, g_btnY, g_btnW, g_btnH; static int g_btnHover, g_uiHidden;
static void rj_cancel(void);
static void cover_update(void) {
    void *m = g_fsMovie; if (!m) return;
    GFxValue v;
    int exists = as_get(m, "_level0.rjcover._visible", &v) && v.type == GFX_BOOL;
    if (g_coverWant && !exists) {
        as_fnv(m, "_level0.createEmptyMovieClip", "sn", "rjcover", 30001.0);
        double W = 1280, H = 800;
        if (as_get(m, "Stage.width", &v) && v.type == GFX_NUMBER && v.v.num > 100) W = v.v.num;
        if (as_get(m, "Stage.height", &v) && v.type == GFX_NUMBER && v.v.num > 100) H = v.v.num;
        as_fnv(m, "_level0.rjcover.beginFill", "nn", (double)0x07100d, 100.0);
        as_fnv(m, "_level0.rjcover.moveTo", "nn", -3000.0, -3000.0); as_fnv(m, "_level0.rjcover.lineTo", "nn", 6000.0, -3000.0);
        as_fnv(m, "_level0.rjcover.lineTo", "nn", 6000.0, 6000.0); as_fnv(m, "_level0.rjcover.lineTo", "nn", -3000.0, 6000.0);
        as_fnv(m, "_level0.rjcover.lineTo", "nn", -3000.0, -3000.0); as_fnv(m, "_level0.rjcover.endFill", "");
        as_fnv(m, "_level0.rjcover.createTextField", "snnnnn", "t1", 1.0, W * 0.1, H / 2 - H * 0.12, W * 0.8, H * 0.3);
        as_setb(m, "_level0.rjcover.t1.multiline", 1); as_setb(m, "_level0.rjcover.t1.wordWrap", 1);
        as_setb(m, "_level0.rjcover.t1.embedFonts", 1); as_setb(m, "_level0.rjcover.t1.html", 1); as_setb(m, "_level0.rjcover.t1.selectable", 0);
        g_coverH = H;
        g_btnW = W * 0.16; g_btnH = H * 0.06; g_btnX = W / 2 - g_btnW / 2; g_btnY = H / 2 + H * 0.2;
        as_fnv(m, "_level0.rjcover.createEmptyMovieClip", "sn", "b", 2.0);
        as_fnv(m, "_level0.rjcover.b.lineStyle", "nnn", 2.0, (double)0xE2C36B, 100.0);
        as_fnv(m, "_level0.rjcover.b.beginFill", "nn", (double)0x3a2a14, 100.0);
        as_fnv(m, "_level0.rjcover.b.moveTo", "nn", g_btnX, g_btnY); as_fnv(m, "_level0.rjcover.b.lineTo", "nn", g_btnX + g_btnW, g_btnY);
        as_fnv(m, "_level0.rjcover.b.lineTo", "nn", g_btnX + g_btnW, g_btnY + g_btnH); as_fnv(m, "_level0.rjcover.b.lineTo", "nn", g_btnX, g_btnY + g_btnH);
        as_fnv(m, "_level0.rjcover.b.lineTo", "nn", g_btnX, g_btnY); as_fnv(m, "_level0.rjcover.b.endFill", "");
        as_fnv(m, "_level0.rjcover.b.createTextField", "snnnnn", "t", 1.0, g_btnX, g_btnY + g_btnH * 0.18, g_btnW, g_btnH * 0.8);
        as_setb(m, "_level0.rjcover.b.t.embedFonts", 1); as_setb(m, "_level0.rjcover.b.t.html", 1); as_setb(m, "_level0.rjcover.b.t.selectable", 0);
        { wchar_t bt[200]; _snwprintf(bt, 199, L"<p align='center'><font face='$NormalFont' size='%d' color='#E2C36B'>Cancel</font></p>", (int)(H * 0.03));
          as_setw(m, "_level0.rjcover.b.t.htmlText", bt); }
        as_setn(m, "_level0.rjcover.b._alpha", 75); g_btnHover = 0; GetAsyncKeyState(VK_LBUTTON);
        logmsg("cover shown (stage %.0fx%.0f)", W, H); g_coverOn = 1; g_coverTextTick = 0;
    }
    if (!g_coverWant && exists) { as_fnv(m, "_level0.rjcover.removeMovieClip", ""); g_coverOn = 0; logmsg("cover removed"); return; }
    if (!g_coverWant || !exists) return;
    if (GetTickCount() - g_coverTextTick > 500) {
        g_coverTextTick = GetTickCount();
        static int dots; dots = (dots + 1) % 4; wchar_t dd[5] = L"...."; dd[dots] = 0;
        wchar_t h[700]; int big = (int)(g_coverH * 0.07), small = (int)(g_coverH * 0.032);
        _snwprintf(h, 699, L"<p align='center'><font face='$TitleFont' size='%d' color='#E2C36B'>%ls</font></p>"
                           L"<p align='center'><font face='$NormalFont' size='%d' color='#C9D6CF'>%ls%ls</font></p>", big, g_coverTitle, small, g_coverLine, g_noticeUntil ? L"" : dd);
        as_setw(m, "_level0.rjcover.t1.htmlText", h);
    }
    // Cancel: hover look + a left click inside it (the game window in front)
    GFxValue x, y; if (!as_get(m, "_level0.rjcover._xmouse", &x) || x.type != GFX_NUMBER || !as_get(m, "_level0.rjcover._ymouse", &y) || y.type != GFX_NUMBER) return;
    int in = x.v.num >= g_btnX && x.v.num <= g_btnX + g_btnW && y.v.num >= g_btnY && y.v.num <= g_btnY + g_btnH;
    if (in != g_btnHover) { g_btnHover = in; as_setn(m, "_level0.rjcover.b._alpha", in ? 100 : 75); }
    static int wasDown; HWND fg = GetForegroundWindow(); DWORD pid = 0; if (fg) GetWindowThreadProcessId(fg, &pid);
    int down = pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_LBUTTON) & 0x8001);
    if (down && !wasDown && in) { as_setb(m, "_level0.display_item._visible", 0); g_uiHidden = 1; rj_cancel(); }   // the menu must not take this click
    wasDown = down;
}

// ---------------- the join ----------------
enum { RJ_NONE, RJ_MENU, RJ_JOIN, RJ_LOBBY };
static int g_rj; static unsigned long long g_rjRoom; static wchar_t g_rjName[64];
// the game's own message boxes while we join (Receive_CreateMessageBox): the text of the latest one, and how many came.
// The first one can be the game's "Connecting..." - a box after it, or one that is still up when the join gives up, is the
// reason (the room is full, closed, a version mismatch...)
static wchar_t g_mbText[200]; static volatile int g_mbCount; static DWORD g_mbAt;
static unsigned long long g_okRoom; static DWORD g_okAt;             // the room joined last (a second Join for it is no new request)
static void stage(const wchar_t *title, const wchar_t *what) {          // what the cover says now (siritos 03.11: what is going on)
    wcsncpy(g_coverTitle, title, 63);
    const wchar_t *n = g_rjName[0] ? g_rjName : L"the custom game";
    _snwprintf(g_coverLine, 159, what, n); g_coverTextTick = 0;
}
static void rj_fail_notice(const wchar_t *why) {                       // the reason on the cover for 3 s, then the public games list
    wcsncpy(g_coverTitle, L"Could Not Join", 63);
    _snwprintf(g_coverLine, 159, L"%ls", why); g_coverTextTick = 0;
    g_coverWant = 1; g_noticeUntil = GetTickCount() + 3000; g_noticeList = 1;
} static DWORD g_rjAt, g_rjTry, g_rjCheck, g_rjKnown, g_rjProbed, g_rjBack; static int g_rjTries;
static void rj_end(const char *res, int toList) {
    logmsg("%s (room %llu)%s", res, g_rjRoom, toList ? " - the public games list" : "");
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsRoomJoin_result.txt", g_dir);
    FILE *f = _wfopen(p, L"w"); if (f) { fprintf(f, "%s %llu\n", res, g_rjRoom); fclose(f); }
    g_rj = RJ_NONE;
    if (!strcmp(res, "ok")) { g_okRoom = g_rjRoom; g_okAt = GetTickCount(); }
    if (toList && strcmp(res, "fail cancelled")) {               // a moment to read why, then the public games list
        wchar_t w[200];
        if (g_mbText[0]) _snwprintf(w, 199, L"%ls", g_mbText);
        else _snwprintf(w, 199, L"%ls did not let us in. Opening the public games list", g_rjName[0] ? g_rjName : L"The game");
        rj_fail_notice(w); return;
    }
    g_coverWant = 0;
    if (toList && g_screen == SC_MAIN) fs(".OnClickPublicMatch", "");
}
static void rj_cancel(void) {                     // the cover's Cancel: our join off; a lobby we are in already stays (03.11: Cancel took the player out)
    if (!g_rj) return;
    logmsg("Cancel pressed");
    if (g_screen == SC_SETUP || dj_room()) { rj_end("ok", 0); return; }
    rj_end("fail cancelled", g_screen == SC_MAIN);
}
// The game was running in its main menu: that menu sends nothing while nobody touches it (03.11: 37 s to the cover, until the
// player moved the mouse). Once the game's window is in front (the launcher brings it there on Join), a few mouse moves over
// the right side of the window - where the menu's buttons are - are posted to it: the menu answers with its hover commands
// and they tell us the movie. Only messages to the game's window: the real cursor does not move; the cover comes right after.
static HWND game_window(void) {
    HWND w = GetForegroundWindow(); DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId() ? w : NULL;
}
static void nudge_menu(DWORD now) {
    static DWORD last; static int n;
    if (g_fsMovie || now - last < 400) return;
    HWND w = game_window(); if (!w) return;                           // not in front: nothing to show the cover on anyway
    last = now; RECT r; if (!GetClientRect(w, &r) || r.right < 200 || r.bottom < 200) return;
    int x = r.right - r.right / 8 - (n % 3) * (r.right / 20), y = r.bottom / 4 + (n % 7) * (r.bottom / 14);
    PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
    if (++n % 10 == 1) logmsg("the menu movie not known yet: mouse moves over the menu's buttons (%d)", n);
    if (n % 21 == 20) {                                                // put the game's idea of the cursor back where the cursor is
        POINT c; if (GetCursorPos(&c) && ScreenToClient(w, &c)) PostMessageW(w, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y)); }
}
static void rj_read(DWORD now) {
    if (now - g_rjCheck < 1000) return;
    g_rjCheck = now;
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsRoomJoin.txt", g_dir);
    WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExW(p, GetFileExInfoStandard, &a)) return;
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER n, w; n.LowPart = ft.dwLowDateTime; n.HighPart = ft.dwHighDateTime; w.LowPart = a.ftLastWriteTime.dwLowDateTime; w.HighPart = a.ftLastWriteTime.dwHighDateTime;
    unsigned long long age = n.QuadPart > w.QuadPart ? (n.QuadPart - w.QuadPart) / 10000000ULL : 0;
    char l1[64] = "", l2[192] = ""; FILE *f = _wfopen(p, L"r");
    if (f) { if (fgets(l1, sizeof l1, f)) { if (!fgets(l2, sizeof l2, f)) l2[0] = 0; } fclose(f); }
    DeleteFileW(p);
    l2[strcspn(l2, "\r\n")] = 0;
    unsigned long long room = strtoull(l1, NULL, 10);
    if (!room) { logmsg("a request without a room id - dropped"); return; }
    if (age > 180) { logmsg("the request for room %llu is %llu s old - dropped", room, age); return; }
    if (room == g_okRoom && now - g_okAt < 120000) { logmsg("room %llu was joined %lu s ago - this request dropped", room, (now - g_okAt) / 1000); return; }
    if (g_screen == SC_SETUP && dj_room() == room) { logmsg("already in room %llu - nothing to do", room); g_rjRoom = room; rj_end("ok", 0); return; }
    g_rjRoom = room; MultiByteToWideChar(CP_UTF8, 0, l2, -1, g_rjName, 63);
    logmsg("request: room %llu \"%s\" (%llu s old) - waiting for the main menu", room, l2, age);
    g_rj = RJ_MENU; g_rjAt = now; g_rjKnown = g_rjProbed = g_rjBack = 0;   // per request (03.11 review: was reset only by a frame within 50 ms)
}
// the automatch goes first: MMFind's cover up or a search on Find a Game - the join is off and the screen is not touched
// (03.11 review: Back on Find a Game could break a match MMFind was walking the menus to)
static int rj_automatch(void) {
    if (mm_cover()) {
        logmsg("MMFind's cover is up (the automatch has the menus) - no join, the screen not touched");
        rj_end("fail automatch", 0); return 1; }
    if (mm_search()) {
        logmsg("a search on Find a Game (engine %d, Start pressed %d) - no join, no Back", g_engSearch, g_mmClicked);
        rj_end("fail automatch", 0);
        rj_fail_notice(L"Find a Game is open with a search on. Leave it (Back), then press Join again"); g_noticeList = 0;
        return 1; }
    return 0;
}
static void rj_step(DWORD now) {
    if (g_rj == RJ_NONE) { if (!g_loading) rj_read(now); return; }
    if (g_loading) { rj_end("fail a match is loading", 0); return; }
    if ((g_rj == RJ_MENU || g_rj == RJ_JOIN) && rj_automatch()) return;
    switch (g_rj) {
    case RJ_MENU: {                                                   // the game may still be starting: up to 90 s
        if (!g_fsMovie && now - g_rjAt > 800) nudge_menu(now);
        if (g_fsMovie && !g_rjKnown) { g_rjKnown = now;
            { HWND gw = game_window(); POINT c; if (gw && GetCursorPos(&c) && ScreenToClient(gw, &c)) PostMessageW(gw, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y)); }   // the game's cursor back
            stage(L"Joining the Game", L"Opening the lobby: %ls"); g_coverWant = 1; }   // the cover at once
        if (g_screen == SC_OTHER && g_fsMovie && now - g_rjProbed > 250) { g_rjProbed = now; if (probe_screen() == SC_MM && rj_automatch()) break; }
        // still not told after 6 s with the menu movie known, no match loading, in no room: the main menu the game starts on
        if (g_screen == SC_OTHER && g_rjKnown && now - g_rjKnown > 6000 && !dj_room()) {
            for (int k = 1; k <= 3 && !g_panel[0]; k++) { char q[96]; GFxValue v; snprintf(q, sizeof q, "_level0.display_item.panel%d_mc.panel_mc._visible", k);
                if (as_get(g_fsMovie, q, &v) && v.type == GFX_BOOL && v.v.b) snprintf(g_panel, sizeof g_panel, "_level0.display_item.panel%d_mc.panel_mc", k); }
            g_screen = SC_MAIN; logmsg("the screen not known after 6 s - taken as the main menu (panel %s)", g_panel[0] ? g_panel : "?");
        }
        if ((g_screen == SC_LIST || g_screen == SC_MM) && g_fsMovie && now - g_rjBack > 1500) {   // the game was running: back to the main menu
            g_rjBack = now; logmsg("on screen %d - Back to the main menu", g_screen); fs(".OnClickBack", ""); }   // (Find a Game: no search, rj_automatch)
        if (g_screen == SC_SETUP && dj_room() == g_rjRoom) { rj_end("ok", 0); break; }
        if (g_screen == SC_SETUP && now - g_rjAt > 1500 && dj_room() != g_rjRoom) {
            logmsg("in another lobby - not leaving it"); g_rj = RJ_NONE;
            rj_fail_notice(L"You are in another lobby. Leave it first, then press Join again"); g_noticeList = 0; break; }
        if (g_screen == SC_MAIN && g_fsMovie) {
            stage(L"Joining the Game", L"Opening the lobby: %ls");
            g_mbText[0] = 0; g_mbCount = 0;
            g_coverWant = 1; g_rj = RJ_JOIN; g_rjAt = now; g_rjTries = 0; g_rjTry = 0;
        } else if (now - g_rjAt > 90000) rj_end(g_fsMovie ? "fail no main menu" : "fail the menu movie never seen", 0);
        break; }
    case RJ_JOIN: {
        if (g_screen != SC_MAIN && g_screen != SC_SETUP) { rj_end("fail the player went elsewhere", 0); break; }   // e.g. Find a Game
        unsigned long long in = dj_room();
        if (in && in == g_rjRoom) { logmsg("in the room after %lu ms", now - g_rjAt); stage(L"Joining the Game", L"In %ls. Opening the lobby screen"); dj_to_lobby(); g_rj = RJ_LOBBY; g_rjAt = now; break; }
        if (in) { rj_end("fail in another room", 0); break; }
        if ((g_mbCount >= 2 && now - g_mbAt > 1500) || (g_mbCount && g_rjTries >= 1 && now - g_mbAt > 6000)) {     // the game said no (full, closed, ...)
            char u[200]; WideCharToMultiByte(CP_UTF8, 0, g_mbText, -1, u, sizeof u, NULL, NULL);
            logmsg("the game said: %s", u); rj_end("fail the game said no", 1); break; }
        if (g_screen == SC_MAIN && !g_mbCount && g_rjTries < 3 && (!g_rjTry || now - g_rjTry > 8000)) {          // 03.11: a join took 5 s - no second one on top
            g_rjTries++; g_rjTry = now;
            stage(L"Joining the Game", g_rjTries == 1 ? L"Connecting to the game: %ls" : g_rjTries == 2 ? L"Connecting to the game: %ls (try 2 of 3)" : L"Connecting to the game: %ls (try 3 of 3)");
            int r = dj_join(g_rjRoom);
            if (r < 0) { g_rjTries--; g_rjTry = now - 7000; break; }              // the profile not there yet: again in 1 s
            if (!r) { rj_end("fail join call", 1); break; }
        }
        if (now - g_rjAt > 25000) rj_end("fail timeout", 1);
        break; }
    case RJ_LOBBY:
        if (g_screen == SC_SETUP) rj_end("ok", 0);
        else if (now - g_rjAt > 10000) rj_end("fail no lobby screen", 1);
        break;
    }
}

// ---------------- hooks ----------------
static void __fastcall fs_hook(void *self, void *edx, void *movie, const char *cmd, const char *args) {
    if (cmd && !IsBadReadPtr(cmd, 1)) {
        if (!strncmp(cmd, "_level0.display_item", 20) || (!strcmp(cmd, "OnLoadComplete") && args && !IsBadReadPtr(args, 1) && strstr(args, "name:=_level0.display_item"))) {
            if (movie != g_fsMovie && g_loading) { g_loading = 0; logmsg("back in the menu"); }
            if (movie != g_fsMovie || g_fsSelf != self) menu_seen(movie, self, "a menu command");
        } else if (movie != g_fsMovie && !g_loading) {
            static void *tried[16]; static int nt; int seen = 0;
            for (int i = 0; i < nt; i++) if (tried[i] == movie) seen = 1;
            if (!seen) { if (nt < 16) tried[nt++] = movie; if (is_menu_movie(movie)) menu_seen(movie, self, cmd); }
        }
        if (!strcmp(cmd, "OnLoadMovieComplete") && args && !IsBadReadPtr(args, 6) && !strncmp(args, "/mp_mc", 6) && !g_loading) {
            g_loading = 1; g_coverWant = 0; screen_reset("a match is loading"); }   // 03.11 review: the lobby screen stayed known after the match
        if (!g_sending && g_screen == SC_MM && strlen(cmd) > 13 && !strcmp(cmd + strlen(cmd) - 13, ".OnClickStart")) {
            g_mmClicked = 1; logmsg("Start / Cancel Searching pressed on Find a Game"); }
        if (!strcmp(cmd, "OnLoadComplete") && args && !IsBadReadPtr(args, 1)) {     // which full-screen panel is up
            const char *nm = strstr(args, "name:="), *url = strstr(args, "url:=");
            if (nm && url) {
                char path[160] = ""; const char *e = strchr(nm, ';'); int len = e ? (int)(e - nm - 6) : 0;
                if (len > 0 && len < 159) { memcpy(path, nm + 6, len); path[len] = 0; }
                int sc = -1;
                if (strstr(url, "main_menu.gfx")) sc = SC_MAIN; else if (strstr(url, "matchmaking.gfx")) sc = SC_MM;
                else if (strstr(url, "game_list_movie.gfx")) sc = SC_LIST; else if (strstr(url, "game_setup_movie.gfx")) sc = SC_SETUP;
                if (sc >= 0 && strlen(path) >= 9 && !strcmp(path + strlen(path) - 9, ".panel_mc")) {
                    if (sc != g_screen) { g_engSearch = -1; g_mmClicked = 0; }     // a new panel: the search state of the old one goes
                    strcpy(g_panel, path); g_screen = sc; if (g_rj) logmsg("screen %d", sc); }
            }
        }
    }
    g_fsOrig(self, edx, movie, cmd, args);
}
static char __fastcall inv_hook(void *self, void *edx, const char *method, int flags, GFxValue *args, unsigned count) {
    if (method && !IsBadReadPtr(method, 31) && !strcmp(method, "Receive_ShowSearchingAnimation") && args && count == 1 && !IsBadReadPtr(args, sizeof *args)
        && (args[0].type == GFX_BOOL || args[0].type == GFX_NUMBER)) {          // the search on Find a Game, as the engine sends it
        int on = args[0].type == GFX_BOOL ? args[0].v.b != 0 : args[0].v.num != 0;
        if (on != g_engSearch && g_screen == SC_MM) { g_engSearch = on; logmsg("the game's search %s", on ? "on" : "off"); }
    }
    if (g_rj && method && !IsBadReadPtr(method, 24) && !strcmp(method, "Receive_CreateMessageBox") && args && count && count < 32 && !IsBadReadPtr(args, count * sizeof(GFxValue))) {
        wchar_t t[200] = L""; int n = 0; char dbg[400]; int dl = 0;
        for (unsigned i = 0; i < count; i++) {               // the box's strings: title, text, buttons - the longest one is the text
            if (dl > (int)sizeof dbg - 1) dl = (int)sizeof dbg - 1;   // snprintf returns the full length: never past the buffer (review 04.11)
            const wchar_t *w = args[i].type == GFX_WSTRING && args[i].v.ws && !IsBadReadPtr(args[i].v.ws, 2) ? args[i].v.ws : NULL;
            if (args[i].type == GFX_STRING && args[i].v.s && !IsBadReadPtr(args[i].v.s, 1)) dl += snprintf(dbg + dl, dl < (int)sizeof dbg ? sizeof dbg - dl : 0, " [%u]\"%.60s\"", i, args[i].v.s);
            if (!w) { if (args[i].type == GFX_NUMBER && dl < 380) dl += snprintf(dbg + dl, sizeof dbg - dl, " [%u]%g", i, args[i].v.num); continue; }
            int len = (int)wcsnlen(w, 400);
            char u[120]; WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL); u[119] = 0;
            if (dl < 380) dl += snprintf(dbg + dl, sizeof dbg - dl, " [%u]'%s'", i, u);
            if (len > n) { n = len; wcsncpy(t, w, 199); t[199] = 0; }
        }
        logmsg("message box while joining (%u args):%s", count, dl ? dbg : "");
        if (t[0]) wcsncpy(g_mbText, t, 199);
        g_mbCount++; g_mbAt = GetTickCount();
    }
    // the menu's SFMovie: its movie (+4) has _level0.display_item; its fscommand handler (+0x18) as the buttons use it
    if (!g_fsMovie && self && !IsBadReadPtr(self, 0x1C)) {
        void *mv = *(void **)((char *)self + 4), *h = *(void **)((char *)self + 0x18);
        static void *tried[16]; static int nt; int seen = 0;
        for (int i = 0; i < nt; i++) if (tried[i] == mv) seen = 1;
        if (!seen && nt < 16) {
            tried[nt++] = mv;
            int menu = is_menu_movie(mv);
            if (g_rj || nt <= 6) logmsg("engine call %s: object %p movie %p - %s", method && !IsBadReadPtr(method, 8) ? method : "?", self, mv, menu ? "the menu" : "not the menu");
            if (menu) menu_seen(mv, h && !IsBadReadPtr(h, 0x20) ? h : NULL, "the engine's calls");
        }
    }
    return g_invOrig(self, edx, method, flags, args, count);
}
static void __stdcall upd_hook(float dt);
static HANDLE patch_lock(void) {
    char n[64]; wsprintfA(n, "Local\\dow2patch_iat_%lu", GetCurrentProcessId());
    HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx) WaitForSingleObject(mx, 5000); return mx;
}
static void patch_unlock(HANDLE mx) { if (mx) { ReleaseMutex(mx); CloseHandle(mx); } }
// the fscommand handler through its vtable slot (the function's own bytes stay as they are, for MMFind / LobbyStats - they
// patch those, whenever they load; we call the slot's old value = the function's entry, so their hooks run after ours)
static int is_code(DWORD a) {
    MEMORY_BASIC_INFORMATION mi;
    return a && VirtualQuery((void *)a, &mi, sizeof mi) && mi.State == MEM_COMMIT
        && (mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
static int hook_fscmd(void) {
    volatile DWORD *slot = (volatile DWORD *)VT_FSCMD;
    if (IsBadReadPtr((void *)(VT_FSCMD - 4), 8) || slot[-1] != FN_FSCMD_VT0) { logmsg("the fscommand vtable not as in DOW2.exe 3.19 - not starting"); return 0; }
    HANDLE mx = patch_lock(); DWORD o; int ok = 0;
    DWORD cur = *slot;                                                 // read under the lock all our DLLs share
    if (cur == (DWORD)fs_hook) { patch_unlock(mx); return 1; }
    if (cur != FN_FSCMD && !is_code(cur)) { patch_unlock(mx); logmsg("the fscommand vtable slot holds %08lX - not starting", cur); return 0; }
    g_fsOrig = (FsCmdFn)cur;
    if (VirtualProtect((void *)slot, 4, PAGE_READWRITE, &o)) { InterlockedExchange((volatile LONG *)slot, (LONG)(DWORD)fs_hook); ok = 1; VirtualProtect((void *)slot, 4, o, &o); }
    patch_unlock(mx);
    logmsg("hooked fscommand (vtable slot %08X%s)", VT_FSCMD, cur != FN_FSCMD ? ", after another add-on's" : "");
    return ok;
}
// Invoke: its own bytes, or another add-on's jump (LobbyStats, MMFind) - chained either way, as MMFind does
static int hook_invoke(void) {
    unsigned char *t = (unsigned char *)HOOK_ADDR;
    if (IsBadReadPtr(t, 5)) return 0;
    int chained = t[0] == 0xE9;
    if (!chained && memcmp(t, INV_PRO, 5)) { logmsg("Invoke not as in DOW2.exe 3.19 - the menu movie only from its commands"); return 0; }
    unsigned char *tr = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); if (!tr) return 0;
    if (chained) { DWORD dest = (DWORD)(t + 5) + *(DWORD *)(t + 1); tr[0] = 0xE9; *(DWORD *)(tr + 1) = dest - (DWORD)(tr + 5); }
    else { memcpy(tr, INV_PRO, 5); tr[5] = 0xE9; *(DWORD *)(tr + 6) = HOOK_RESUME - (DWORD)(tr + 10); }
    g_invOrig = (InvokeFn)tr;
    unsigned char b[5]; b[0] = 0xE9; *(DWORD *)(b + 1) = (DWORD)inv_hook - (DWORD)(t + 5);
    HANDLE mx = patch_lock(); DWORD o; SIZE_T w = 0; int ok = 0;
    if (VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &o)) { ok = WriteProcessMemory(GetCurrentProcess(), t, b, 5, &w) && w == 5; VirtualProtect(t, 5, o, &o); }
    FlushInstructionCache(GetCurrentProcess(), t, 5); patch_unlock(mx);
    logmsg("hooked Invoke%s", chained ? " (after another add-on's hook)" : "");
    return ok;
}
static int g_fsHooked;
static void __stdcall upd_hook(float dt) {
    g_updOrig(dt);
    DWORD now = GetTickCount();
    if (g_noticeUntil && (int)(now - g_noticeUntil) > 0) {          // the notice was read: cover off (and the list when it was a failed join)
        g_noticeUntil = 0; g_coverWant = 0;
        if (g_noticeList && g_screen == SC_MAIN) fs(".OnClickPublicMatch", "");
        g_noticeList = 0;
    }
    if (g_fsHooked && !g_noticeUntil) rj_step(now);
    if (g_uiHidden && g_fsMovie && !g_loading && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {   // the Cancel click is over: the menu back
        static DWORD up; if (!up) up = now;
        if (now - up > 300) { as_setb(g_fsMovie, "_level0.display_item._visible", 1); g_uiHidden = 0; up = 0; }
    }
    if (g_fsMovie && !g_loading && (g_coverWant || g_coverOn)) cover_update();
}
static int iat_hook(DWORD slot, void *fn, void **orig) {          // chained: whatever is in the import slot now is called first
    if (IsBadReadPtr((void *)slot, 4)) return 0;
    HANDLE mx = patch_lock(); int ok = 0; DWORD cur = *(volatile DWORD *)slot, old;
    if (VirtualProtect((void *)slot, 4, PAGE_READWRITE, &old)) {
        SIZE_T w = 0;
        if (WriteProcessMemory(GetCurrentProcess(), (void *)slot, &fn, 4, &w) && w == 4) { *orig = (void *)cur; ok = 1; }
        VirtualProtect((void *)slot, 4, old, &old);
    }
    patch_unlock(mx); return ok;
}
static DWORD WINAPI installer(LPVOID _) {
    for (int i = 0; i < 180; i++) {
        if (GetModuleHandleA("Shark.dll") && iat_hook(IAT_SHARK_UPDATE, (void *)upd_hook, (void **)&g_updOrig)) { logmsg("frame hook installed"); return 0; }
        Sleep(1000);
    }
    logmsg("frame hook NOT installed"); return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID _) {
    if (r != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_lk);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0;
    logmsg("RoomJoin 8 (experiment: joining a room from the launcher's Lobbies tab)");
    { char n[64]; wsprintfA(n, "Local\\dow2patch_RoomJoin_%lu", GetCurrentProcessId());
      HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx && GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("another RoomJoin copy is active, this one stays idle"); return TRUE; } }
    { wchar_t ini[MAX_PATH]; _snwprintf(ini, MAX_PATH, L"%lsRoomJoin.ini", g_dir);
      if (!GetPrivateProfileIntW(L"RoomJoin", L"Enabled", 1, ini)) { logmsg("disabled in RoomJoin.ini - no hooks"); return TRUE; } }
    g_fsHooked = hook_fscmd();                    // the vtable slot: the same whether MMFind is in the game already or comes later
    if (!g_fsHooked) return TRUE;                 // without it no join can be done: nothing else either
    hook_invoke();
    CloseHandle(CreateThread(NULL, 0, installer, NULL, 0, NULL));
    return TRUE;
}
