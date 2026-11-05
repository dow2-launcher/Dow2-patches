// RoomJoin.dll (experiment, siritos 03.11.2026; RoomJoin 9 05.11.2026) - "Join" from the launcher's Lobbies tab, for DoW2
// Retribution / Elite (DOW2.exe 3.19).
//
// RoomJoin 9: the launcher talks to the game over a named pipe, \\.\pipe\EliteRoomJoin-<pid of DOW2.exe> (one per game
// process: two copies of the game = two pipes; only the current user may open it, no remote clients). One JSON object per line:
//   launcher -> game  {"cmd":"join","room":<id>,"name":"..."}   {"cmd":"status"}   {"cmd":"cancel"}
//   game -> launcher  {"ev":"status","screen":"main|find_game|games_list|lobby|other|unknown|match","in_match":b,
//                       "searching":b,"room":<id or 0>,"joining":<id or 0>,"version":"9","proto":1,"pid":n,"stalled":b}
//                     {"ev":"accepted","room":id} {"ev":"walking","room":id} {"ev":"joining","room":id}
//                     {"ev":"in_lobby","room":id[,"late":true]}
//                     {"ev":"failed","room":id,"reason":"full|closed|password|version|timeout|busy_search|busy_match|
//                       busy_join|not_menu|other_lobby|cancelled|error","text":"what the game said"}
//                     {"ev":"error","reason":"bad_command"}
// A status line goes to a client as soon as it connects (the launcher's heartbeat: the add-on is in this game and works).
// Events go to every connected client. The pipe exists only once the frame hook is in: no pipe = no RoomJoin in that game.
// The pipe thread never touches the engine: a join / cancel goes into a queue that the game's own thread takes every frame
// (shark::update). It answers "status" itself from what that thread noted last ("stalled": no frame for 3 s - a loading screen).
// The exit of the game: nothing waits for that thread - no work in DLL_PROCESS_DETACH, all its I/O is overlapped (it only ever
// waits in WaitForMultipleObjects), it holds no lock of the C runtime or of the heap while it waits (logging goes through one
// file handle and WriteFile), so ExitProcess ends it wherever it is.
// RoomJoin 10 (05.11.2026, owner): no request file at all - Elite_DLLs/RoomJoin.txt of launcher 1.4.8 is not read any more (no
// disk access in the game's main thread, nothing to be locked), and nothing runs on a timer while no launcher is connected.
//
// In the game, once it is in its main menu: a cover ("Joining the Game", the room's name, Cancel; the menu under it hidden, so
// clicks and Esc don't reach it), the game browser's own join by the room id (the call MMFind's fast automatch guests use since
// 0.37), the lobby screen - then the cover goes and the lobby is the player's (hero, ready: as always). The game says no (full,
// closed, ...): the reason for 3 s, then the public games list. No answer in 25 s, or Cancel: the room is watched 15 s more
// (a late answer: the lobby screen after all), no second join on top of the first.
// Never touched: Find a Game (no Back there, review 04.11 p.4: a search or the native automatch may be building its room), a
// lobby the player is in, a match. Another screen (Options, Army Painter, Campaign, Replays, the match summary): "not_menu".
//
// Its own add-on next to MMFind (the automatch is not touched). Hooks, all chained onto whatever is there already:
//   - the fscommand handler (Flash -> engine) through its vtable slot 0x010BAC9C (not a patch of its code 0x681AB0 that
//     MMFind and LobbyStats patch): which full-screen panel is up, the menu movie and its handler, a match loading;
//   - Shark.dll shark::update (import 0x00F88068): the main-thread tick;
//   - World::SimulatePre / SimulatePost (imports 0x00F88154 / 0x00F88100): a world (match, replay) runs - the menu movie is
//     forgotten at its start (review 04.11 p.5: a dangling movie pointer after a replay);
//   - the engine -> Flash Invoke 0x45DB6C (chained with MMFind / LobbyStats): the menu movie and its handler at once, the
//     game's search animation on Find a Game, the game's message boxes while we join;
//   - while the cover is up, the game window's procedure (subclass): Esc and mouse clicks go to the cover, not to the menu.
// RoomJoin.ini [RoomJoin] Enabled=0: off (no hooks, no pipe).
#include <windows.h>
#include <sddl.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <wctype.h>

#define RJ_VERSION  "10"
#define HOOK_ADDR   0x0045DB6C                    // Invoke (engine -> Flash), this = SFMovie: +4 the movie, +0x18 its fscommand handler
#define HOOK_RESUME 0x0045DB71
static const unsigned char INV_PRO[5] = {0xB8, 0x59, 0x70, 0xEF, 0x00};
#define FN_FSCMD    0x00681AB0                    // SFMovie's FSCommandHandler::Callback
#define VT_FSCMD    0x010BAC9C                    // its vtable slot (.rdata; the vtable starts at 0x010BAC98, slot 0 = 0x00681E80)
#define FN_FSCMD_VT0 0x00681E80
#define IAT_SHARK_UPDATE 0x00F88068
#define IAT_SIM_PRE  0x00F88154                   // World::SimulatePre  (__thiscall): a world starts (match, replay)
#define IAT_SIM_POST 0x00F88100                   // World::SimulatePost (__thiscall): it ends (late: end of the loading screen after it)

typedef struct { int type; int pad; union { double num; unsigned char b; const wchar_t *ws; const char *s; } v; } GFxValue;
enum { GFX_BOOL = 2, GFX_NUMBER = 3, GFX_STRING = 4, GFX_WSTRING = 5 };
typedef void (__fastcall *FsCmdFn)(void *self, void *edx, void *movie, const char *cmd, const char *args);
typedef void (__stdcall *UpdFn)(float dt);
typedef char (__fastcall *InvokeFn)(void *self, void *edx, const char *method, int flags, GFxValue *args, unsigned count);
typedef void (__fastcall *ThisFn)(void *self, void *edx);
static FsCmdFn g_fsOrig; static UpdFn g_updOrig; static InvokeFn g_invOrig; static ThisFn g_preOrig, g_postOrig;

// ---------------- the log: one handle, WriteFile (no C runtime lock held by the pipe thread), kept under 512 KB ----------------
static wchar_t g_dir[MAX_PATH]; static CRITICAL_SECTION g_lk; static HANDLE g_log = INVALID_HANDLE_VALUE;
static void log_open(void) {
    wchar_t p[MAX_PATH], o[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsRoomJoin.log", g_dir); p[MAX_PATH - 1] = 0;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(p, GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > 512 * 1024)) {
        _snwprintf(o, MAX_PATH, L"%lsRoomJoin.log.old", g_dir); o[MAX_PATH - 1] = 0; MoveFileExW(p, o, MOVEFILE_REPLACE_EXISTING); }
    g_log = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}
static void logmsg(const char *fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char b[1100]; SYSTEMTIME t; GetLocalTime(&t);
    int n = _snprintf(b, 32, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds); if (n < 0 || n > 31) n = 0;
    va_list a; va_start(a, fmt); int m = _vsnprintf(b + n, sizeof b - n - 3, fmt, a); va_end(a);
    if (m < 0 || m > (int)sizeof b - n - 3) m = (int)sizeof b - n - 3;
    n += m; b[n++] = '\r'; b[n++] = '\n';
    EnterCriticalSection(&g_lk); DWORD w; WriteFile(g_log, b, n, &w, NULL); LeaveCriticalSection(&g_lk);
}

// ---------------- the menu, as the fscommands show it ----------------
enum { SC_UNKNOWN, SC_OTHER, SC_MAIN, SC_MM, SC_LIST, SC_SETUP };
static const char *SC_NAME[] = {"unknown", "other", "main", "find_game", "games_list", "lobby"};
static volatile int g_screen, g_loading, g_inWorld;
static char g_panel[160];
static void *g_fsSelf, *g_fsMovie; static int g_sending;
static void *g_triedFs[16], *g_triedInv[16]; static int g_ntFs, g_ntInv;     // movies already asked "are you the menu?"
// the automatch (MMFind / the game's own search) on Find a Game: the engine's search animation as it is sent (-1 not seen),
// the player's Start / Cancel Searching since the panel came up. Both forgotten when another panel comes or a match loads.
static volatile int g_engSearch = -1, g_mmClicked;
static int g_coverWant, g_coverOn, g_uiHidden;
static void screen_reset(const char *why) {
    if (g_screen != SC_UNKNOWN || g_panel[0]) logmsg("screen forgotten (%s)", why);
    g_screen = SC_UNKNOWN; g_panel[0] = 0; g_engSearch = -1; g_mmClicked = 0;
}
// a world starts (a match or a replay loads): the menu movie goes with it - never used again (review 04.11 p.5)
static void menu_forget(const char *why) {
    if (g_fsMovie || g_fsSelf) logmsg("the menu movie forgotten (%s)", why);
    g_fsMovie = NULL; g_fsSelf = NULL; g_ntFs = g_ntInv = 0; g_coverOn = 0; g_uiHidden = 0;
    screen_reset(why);
}
static int url_screen(const char *u) {
    return strstr(u, "main_menu.gfx") ? SC_MAIN : strstr(u, "matchmaking.gfx") ? SC_MM : strstr(u, "game_list_movie.gfx") ? SC_LIST
         : strstr(u, "game_setup_movie.gfx") ? SC_SETUP : SC_OTHER;           // any other full-screen panel: not ours (review p.1)
}

// ---------------- ActionScript access (Scaleform 2.x inside DOW2.exe 3.19; as in MMFind) ----------------
#define FN_STR_NEW   0x00DB64D0
#define FN_AS_CALL   0x00DD27E0
#define FN_STK_GROW  0x00DB7950
#define FN_STK_POP   0x00DB7BE0
#define FN_VAL_FREE  0x00DBA000
typedef struct { unsigned char b[16]; } ASValue;
typedef void *(__fastcall *StrNewFn)(void *mgr, void *edx, const char *s);
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
    if (!g_fsSelf || !g_fsMovie || g_loading || g_inWorld) return;
    char cmd[300]; if (rel[0] == '.') snprintf(cmd, sizeof cmd, "%s%s", g_panel, rel); else snprintf(cmd, sizeof cmd, "%s", rel);
    logmsg("-> fscommand %s", cmd);
    g_sending++; g_fsOrig(g_fsSelf, NULL, g_fsMovie, cmd, args ? args : ""); g_sending--;
}

// which full-screen panel is up, asked from the menu movie itself (the panel clips' _url) - for the screen we came in on.
// Every visible panel counts (Options sits in panel2 over a transition movie in panel1): one that is not ours -> SC_OTHER,
// else the one of ours. -> the screen, SC_UNKNOWN when no panel is visible
static int probe_screen(void) {
    void *m = g_fsMovie; if (!m) return SC_UNKNOWN;
    int found = SC_UNKNOWN; char pth[96] = "", fu[200] = "";
    for (int k = 1; k <= 3; k++) {
        char p[96], q[128]; GFxValue v; snprintf(p, sizeof p, "_level0.display_item.panel%d_mc.panel_mc", k);
        snprintf(q, sizeof q, "%s._visible", p); if (!as_get(m, q, &v) || v.type != GFX_BOOL || !v.v.b) continue;
        snprintf(q, sizeof q, "%s._url", p); if (!as_get(m, q, &v)) continue;
        char u[200] = "";
        if (v.type == GFX_STRING && v.v.s && !IsBadReadPtr(v.v.s, 1)) snprintf(u, sizeof u, "%s", v.v.s);
        else if (v.type == GFX_WSTRING && v.v.ws && !IsBadReadPtr(v.v.ws, 2)) WideCharToMultiByte(CP_UTF8, 0, v.v.ws, -1, u, sizeof u, NULL, NULL);
        u[sizeof u - 1] = 0;
        if (!u[0]) continue;
        for (char *c = u; (c = strstr(c, "%5F")) || (c = strstr(u, "%5f")); ) { *c = '_'; memmove(c + 1, c + 3, strlen(c + 3) + 1); }   // the URL comes encoded (main%5Fmenu.gfx)
        if (strstr(u, "transition_movie.gfx")) continue;               // the fade between panels, not a screen
        int sc = url_screen(u);
        if (found == SC_UNKNOWN || sc == SC_OTHER) { found = sc; strcpy(pth, p); strcpy(fu, u); }
        if (sc == SC_OTHER) break;
    }
    if (found == SC_UNKNOWN) return SC_UNKNOWN;
    if (found != g_screen || strcmp(pth, g_panel)) logmsg("screen asked: %s = %s (%s)", pth, fu, SC_NAME[found]);
    strcpy(g_panel, pth); g_screen = found;
    if (found == SC_MM) g_mmClicked = 1;                              // Find a Game since before we knew: a search may be on - taken as on
    return found;
}

static int is_menu_movie(void *mv) {
    if (!mv || IsBadReadPtr(mv, 0x600)) return 0;
    DWORD vt = *(DWORD *)mv;                                          // GFx is linked into DOW2.exe: its vtable is there
    if (vt < 0x00F87000 || vt >= 0x0124F000 || IsBadReadPtr((void *)vt, 0x34)) return 0;
    GFxValue v; return as_get(mv, "_level0.display_item._visible", &v) && v.type == GFX_BOOL;
}
// The menu movie's own end: its class's deleting destructor (vtable slot 0; DOW2.exe 3.19: 0x00DCE310 in the vtable 0x0104BBFC)
// is wrapped once the movie is known. The front end is rebuilt without any match (05.11.2026, stand: Options reloaded the sound
// and the whole front end; RoomJoin 9 before this kept the old movie and the next Join crashed in it) - the movie is forgotten
// the moment it is deleted, whatever deleted it.
typedef void *(__fastcall *DtorFn)(void *self, void *edx, unsigned flags);
static DWORD g_dtVt[4]; static DtorFn g_dtOrig[4]; static int g_dtN; static volatile int g_movieGone;
static void *__fastcall movie_dtor(void *self, void *edx, unsigned flags) {
    DtorFn orig = NULL; DWORD vt = self && !IsBadReadPtr(self, 4) ? *(DWORD *)self : 0;
    for (int i = 0; i < g_dtN; i++) if (g_dtVt[i] == vt) orig = g_dtOrig[i];
    if (!orig) orig = g_dtOrig[0];                                     // (a derived class's destructor sets its own vtable first)
    if (self && self == g_fsMovie) { g_fsMovie = NULL; g_fsSelf = NULL; g_movieGone = 1; logmsg("the menu movie %p is being deleted", self); }
    for (int i = 0; i < g_ntFs; i++) if (g_triedFs[i] == self) g_triedFs[i] = NULL;     // its address may come back as another movie
    for (int i = 0; i < g_ntInv; i++) if (g_triedInv[i] == self) g_triedInv[i] = NULL;
    return orig(self, edx, flags);
}
static HANDLE patch_lock(void); static void patch_unlock(HANDLE mx); static int is_code(DWORD a);
static void hook_movie_dtor(void *mv) {
    DWORD vt = *(DWORD *)mv;
    for (int i = 0; i < g_dtN; i++) if (g_dtVt[i] == vt) return;
    if (g_dtN >= 4 || IsBadReadPtr((void *)vt, 4)) return;
    HANDLE mx = patch_lock(); DWORD cur = *(volatile DWORD *)vt, o;
    if (cur != (DWORD)movie_dtor && is_code(cur)) {
        g_dtVt[g_dtN] = vt; g_dtOrig[g_dtN] = (DtorFn)cur;             // before the slot points to us
        if (VirtualProtect((void *)vt, 4, PAGE_READWRITE, &o)) {
            InterlockedExchange((volatile LONG *)vt, (LONG)(DWORD)movie_dtor); VirtualProtect((void *)vt, 4, o, &o); g_dtN++;
            logmsg("the menu movie's destructor wrapped (vtable %08lX, slot 0 was %08lX%s)", vt, cur, cur == 0x00DCE310 ? "" : " - not as in DOW2.exe 3.19");
        }
    }
    patch_unlock(mx);
}
static void menu_seen(void *mv, void *handler, const char *how) {   // the menu movie (and the handler its buttons use)
    if (g_fsMovie == mv && (g_fsSelf || !handler)) return;
    if (g_fsMovie != mv) { g_coverOn = 0; g_uiHidden = 0; }           // a new movie: no cover of ours in it
    g_fsMovie = mv; if (handler) g_fsSelf = handler;
    logmsg("the menu movie %p (vtable %08lX, handler %p) from %s", mv, *(DWORD *)mv, g_fsSelf, how);
    hook_movie_dtor(mv);
}
// a search on Find a Game: the game's own (MMFind's native automatch) or the player pressed Start Searching
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
    logmsg("join: room %llu", room);                                   // (RoomJoin 8 logged the profile id too: not needed)
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

// ---------------- the game's own texts for a "no" (its language: Localizer::GetString of the DOW2.ucs ids) ----------------
static const struct { int id; const char *why; } NO_IDS[] = {
    {550050, "full"}, {550113, "full"}, {550100, "closed"}, {550116, "closed"}, {550063, "password"}, {550105, "password"},
    {512500, "password"}, {602003, "password"}, {550101, "timeout"}, {550102, "timeout"}, {550053, "timeout"}, {550104, "timeout"},
    {550010, "version"}, {550011, "version"}, {550106, "version"}, {550107, "version"}, {550108, "version"}, {550109, "version"},
    {550110, "version"}, {550111, "version"}, {550112, "version"}, {550051, "closed"}, {550115, "closed"}, {550052, "closed"},
};
#define N_NO (int)(sizeof NO_IDS / sizeof NO_IDS[0])
static wchar_t g_noText[N_NO][96]; static int g_noLoaded;
static void no_texts_load(void) {
    if (g_noLoaded) return; g_noLoaded = 1;
    HMODULE l = GetModuleHandleA("Localizer.dll"); if (!l) return;
    typedef char (__stdcall *GetStrFn)(int, void *);
    typedef void (__fastcall *CtorFn)(void *, void *);
    typedef const wchar_t *(__fastcall *CStrFn)(void *, void *);
    GetStrFn get = (GetStrFn)GetProcAddress(l, "?GetString@Localizer@@SG_NHAAVLocString@@@Z");
    CtorFn ctor = (CtorFn)GetProcAddress(l, "??0LocString@@QAE@XZ"), dtor = (CtorFn)GetProcAddress(l, "??1LocString@@UAE@XZ");
    CStrFn cstr = (CStrFn)GetProcAddress(l, "?c_str@LocString@@QBEPB_WXZ");
    if (!get || !ctor || !dtor || !cstr) { logmsg("Localizer: no GetString - the reasons from English words only"); return; }
    int n = 0;
    for (int i = 0; i < N_NO; i++) {
        static __attribute__((aligned(16))) unsigned char ls[64]; memset(ls, 0, sizeof ls);
        ctor(ls, NULL);
        if (get(NO_IDS[i].id, ls)) { const wchar_t *w = cstr(ls, NULL); if (w && !IsBadReadPtr(w, 2)) { wcsncpy(g_noText[i], w, 95); g_noText[i][95] = 0; n++; } }
        dtor(ls, NULL);
    }
    logmsg("Localizer: %d of %d texts of a no", n, N_NO);
}
static const char *no_reason(const wchar_t *t) {                       // the game's message -> our reason
    if (!t || !t[0]) return "error";
    no_texts_load();
    for (int i = 0; i < N_NO; i++) if (g_noText[i][0] && wcsstr(t, g_noText[i])) return NO_IDS[i].why;
    wchar_t l[200]; int k = 0; for (; t[k] && k < 199; k++) l[k] = towlower(t[k]); l[k] = 0;
    if (wcsstr(l, L"full")) return "full";
    if (wcsstr(l, L"password")) return "password";
    if (wcsstr(l, L"version") || wcsstr(l, L"different mod")) return "version";
    if (wcsstr(l, L"timed out") || wcsstr(l, L"did not respond") || wcsstr(l, L"nat negotiation")) return "timeout";
    if (wcsstr(l, L"no longer exists") || wcsstr(l, L"being started") || wcsstr(l, L"banned") || wcsstr(l, L"kicked")) return "closed";
    return "error";
}

// ---------------- JSON lines (both threads; no heap) ----------------
static int u8_put(char *o, int cap, int n, unsigned c) {
    if (c < 0x80) { if (n + 1 < cap) o[n++] = (char)c; }
    else if (c < 0x800) { if (n + 2 < cap) { o[n++] = 0xC0 | (c >> 6); o[n++] = 0x80 | (c & 63); } }
    else if (c < 0x10000) { if (n + 3 < cap) { o[n++] = 0xE0 | (c >> 12); o[n++] = 0x80 | ((c >> 6) & 63); o[n++] = 0x80 | (c & 63); } }
    else if (n + 4 < cap) { o[n++] = 0xF0 | (c >> 18); o[n++] = 0x80 | ((c >> 12) & 63); o[n++] = 0x80 | ((c >> 6) & 63); o[n++] = 0x80 | (c & 63); }
    return n;
}
static void js_esc(char *o, int cap, const wchar_t *w) {              // UTF-16 -> a JSON string body (UTF-8, escaped)
    char u[600]; int m = WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, u, sizeof u, NULL, NULL); if (m <= 0) u[0] = 0; u[sizeof u - 1] = 0;
    int n = 0;
    for (unsigned char *c = (unsigned char *)u; *c && n < cap - 7; c++) {
        if (*c == '"' || *c == '\\') { o[n++] = '\\'; o[n++] = *c; }
        else if (*c < 0x20) n += _snprintf(o + n, 7, "\\u%04x", *c);
        else o[n++] = *c;
    }
    o[n] = 0;
}
static const char *js_ws(const char *p) { while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++; return p; }
// a JSON string at p ('"' first) into UTF-8 (escapes decoded); -> past it, NULL if broken
static const char *js_str(const char *p, char *o, int cap) {
    if (*p != '"') return NULL;
    int n = 0; p++;
    while (*p && *p != '"') {
        unsigned c = (unsigned char)*p++;
        if (c != '\\') { if (n + 1 < cap) o[n++] = (char)c; continue; }
        c = (unsigned char)*p++;
        switch (c) {
        case 'n': c = '\n'; break; case 't': c = '\t'; break; case 'r': c = '\r'; break; case 'b': c = 8; break; case 'f': c = 12; break;
        case 'u': {
            unsigned v = 0; for (int i = 0; i < 4; i++) { char h = *p++; v <<= 4;
                if (h >= '0' && h <= '9') v |= h - '0'; else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10; else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10; else return NULL; }
            if (v >= 0xD800 && v < 0xDC00 && p[0] == '\\' && p[1] == 'u') {          // a surrogate pair
                unsigned v2 = 0; const char *q = p + 2; int ok = 1;
                for (int i = 0; i < 4; i++) { char h = *q++; v2 <<= 4;
                    if (h >= '0' && h <= '9') v2 |= h - '0'; else if (h >= 'a' && h <= 'f') v2 |= h - 'a' + 10; else if (h >= 'A' && h <= 'F') v2 |= h - 'A' + 10; else ok = 0; }
                if (ok && v2 >= 0xDC00 && v2 < 0xE000) { v = 0x10000 + ((v - 0xD800) << 10) + (v2 - 0xDC00); p = q; }
            }
            n = u8_put(o, cap, n, v); continue; }
        case 0: return NULL;
        default: break;                                               // \" \\ \/
        }
        if (n + 1 < cap) o[n++] = (char)c;
    }
    if (*p != '"') return NULL;
    o[n] = 0; return p + 1;
}
static const char *js_skip(const char *p) {                           // past one value (nested objects / arrays too)
    p = js_ws(p);
    if (*p == '"') { char d[8]; return js_str(p, d, sizeof d); }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') { char d[8]; p = js_str(p, d, sizeof d); if (!p) return NULL; continue; }
            if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
            p++;
        }
        return NULL;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}
typedef struct { char cmd[16]; unsigned long long room; int hasRoom; char name[200]; } Cmd;
static int js_command(const char *s, Cmd *c) {                        // one line -> the command; 0 = not an object we read
    memset(c, 0, sizeof *c);
    const char *p = js_ws(s); if (*p != '{') return 0; p = js_ws(p + 1);
    if (*p == '}') return 0;
    for (;;) {
        char k[24]; p = js_str(p, k, sizeof k); if (!p) return 0;
        p = js_ws(p); if (*p != ':') return 0; p = js_ws(p + 1);
        if (!strcmp(k, "cmd") && *p == '"') { p = js_str(p, c->cmd, sizeof c->cmd); if (!p) return 0; }
        else if (!strcmp(k, "name") && *p == '"') { p = js_str(p, c->name, sizeof c->name); if (!p) return 0; }
        else if (!strcmp(k, "room") && (*p == '"' || (*p >= '0' && *p <= '9'))) {   // a number, or a string of digits
            char d[32] = ""; int i = 0;
            if (*p == '"') { p = js_str(p, d, sizeof d); if (!p) return 0; }
            else { while (*p >= '0' && *p <= '9' && i < 30) d[i++] = *p++; d[i] = 0; if (*p >= '0' && *p <= '9') return 0; }
            for (i = 0; d[i]; i++) if (d[i] < '0' || d[i] > '9') return 0;
            if (!d[0] || i > 20) return 0;
            c->room = strtoull(d, NULL, 10); c->hasRoom = 1;
        } else { p = js_skip(p); if (!p) return 0; }
        p = js_ws(p);
        if (*p == ',') { p = js_ws(p + 1); continue; }
        if (*p == '}') break;
        return 0;
    }
    return c->cmd[0] != 0;
}

// ---------------- the queues between the pipe thread and the game's thread ----------------
enum { CMD_JOIN = 1, CMD_CANCEL };
typedef struct { int what; unsigned long long room; wchar_t name[64]; DWORD from; } QCmd;
typedef struct { DWORD to; char s[700]; } QOut;                       // to 0 = every client
static CRITICAL_SECTION g_qlk; static QCmd g_cq[8]; static int g_cqH, g_cqN; static QOut g_oq[32]; static int g_oqH, g_oqN;
static HANDLE g_outEv;                                                // auto-reset: there is something to send
// what the game's thread saw last, for "status" without waiting for a frame
static volatile struct { int screen, inMatch, searching; unsigned long long room, joining; DWORD at; } g_snap;
static void out_push(DWORD to, const char *s) {
    if (!g_outEv) return;
    EnterCriticalSection(&g_qlk);
    if (g_oqN == 32) { g_oqH = (g_oqH + 1) % 32; g_oqN--; }           // nobody reads: the oldest goes
    QOut *o = &g_oq[(g_oqH + g_oqN) % 32]; o->to = to; strncpy(o->s, s, sizeof o->s - 2); o->s[sizeof o->s - 2] = 0; g_oqN++;
    LeaveCriticalSection(&g_qlk);
    SetEvent(g_outEv);
}
static void emitf(const char *fmt, ...) {                             // an event to every client
    char b[700]; va_list a; va_start(a, fmt); int n = _vsnprintf(b, sizeof b - 2, fmt, a); va_end(a);
    if (n < 0 || n > (int)sizeof b - 2) return;
    b[n] = 0; logmsg("event %s", b); strcat(b, "\n"); out_push(0, b);
}

// ---------------- the cover (as MMFind's "Match Found" screen) ----------------
static wchar_t g_coverLine[200], g_coverTitle[64] = L"Joining the Game"; static DWORD g_noticeUntil; static int g_noticeList; static double g_coverH = 1200; static DWORD g_coverTextTick;
static double g_btnX, g_btnY, g_btnW, g_btnH; static int g_btnHover;
static void rj_cancel(const char *how);
static void html_esc(wchar_t *o, int cap, const wchar_t *s) {          // a text for htmlText (review 04.11 p.7: a room name with <, &)
    int n = 0;
    for (; s && *s && n < cap - 7; s++) {
        const wchar_t *r = *s == L'<' ? L"&lt;" : *s == L'>' ? L"&gt;" : *s == L'&' ? L"&amp;" : *s == L'"' ? L"&quot;" : *s == L'\'' ? L"&#39;" : NULL;
        if (r) { while (*r) o[n++] = *r++; } else o[n++] = *s;
    }
    o[n] = 0;
}
// clicks and Esc while the cover is up go to the cover, not to the menu under it (review 04.11 p.8): the game window's
// procedure is wrapped while the cover is up; a click = the logical primary button (left-handed mouse: Windows swaps it)
static HWND g_wnd; static WNDPROC g_wndOrig; static int g_wndUni; static volatile int g_click, g_esc;
static LRESULT CALLBACK wnd_hook(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (g_coverOn) {
        if ((m == WM_KEYDOWN || m == WM_KEYUP || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP || m == WM_CHAR) && wp == VK_ESCAPE) { if (m == WM_KEYDOWN) g_esc = 1; return 0; }
        if (m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK) { g_click = 1; return 0; }
        if (m == WM_LBUTTONUP || m == WM_RBUTTONDOWN || m == WM_RBUTTONUP || m == WM_RBUTTONDBLCLK || m == WM_MBUTTONDOWN || m == WM_MBUTTONUP) return 0;
    }
    return g_wndUni ? CallWindowProcW(g_wndOrig, w, m, wp, lp) : CallWindowProcA(g_wndOrig, w, m, wp, lp);
}
static BOOL CALLBACK find_wnd(HWND w, LPARAM l) {                     // the game's main window by our process id (review p.10)
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
    RECT r; if (!GetClientRect(w, &r)) return TRUE;
    HWND *best = (HWND *)l; RECT b;
    if (!*best || (GetClientRect(*best, &b) && (r.right * r.bottom > b.right * b.bottom))) *best = w;
    return TRUE;
}
static HWND main_window(void) { HWND w = NULL; EnumWindows(find_wnd, (LPARAM)&w); return w; }
static void wnd_wrap(int on) {
    if (on && !g_wndOrig) {
        HWND w = main_window(); if (!w) return;
        g_wndUni = IsWindowUnicode(w);
        LONG_PTR o = g_wndUni ? SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)wnd_hook) : SetWindowLongPtrA(w, GWLP_WNDPROC, (LONG_PTR)wnd_hook);
        if (o) { g_wnd = w; g_wndOrig = (WNDPROC)o; g_click = g_esc = 0; logmsg("cover: the game window %p takes clicks and Esc for the cover", w); }
    } else if (!on && g_wndOrig) {
        LONG_PTR cur = g_wndUni ? GetWindowLongPtrW(g_wnd, GWLP_WNDPROC) : GetWindowLongPtrA(g_wnd, GWLP_WNDPROC);
        if (cur == (LONG_PTR)wnd_hook) {                                // ours on top: put the old one back; else stay in the chain (passing all on)
            if (g_wndUni) SetWindowLongPtrW(g_wnd, GWLP_WNDPROC, (LONG_PTR)g_wndOrig); else SetWindowLongPtrA(g_wnd, GWLP_WNDPROC, (LONG_PTR)g_wndOrig);
            g_wndOrig = NULL; g_wnd = NULL;
        }
    }
}
static void cover_off(void *m) {
    as_fnv(m, "_level0.rjcover.removeMovieClip", "");
    if (g_uiHidden) { as_setb(m, "_level0.display_item._visible", 1); g_uiHidden = 0; }
    g_coverOn = 0; wnd_wrap(0); logmsg("cover removed");
}
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
        { wchar_t bt[200]; _snwprintf(bt, 199, L"<p align='center'><font face='$NormalFont' size='%d' color='#E2C36B'>Cancel</font></p>", (int)(H * 0.03)); bt[199] = 0;
          as_setw(m, "_level0.rjcover.b.t.htmlText", bt); }
        as_setn(m, "_level0.rjcover.b._alpha", 75); g_btnHover = 0;
        as_setb(m, "_level0.display_item._visible", 0); g_uiHidden = 1;   // the menu under the cover takes no click
        logmsg("cover shown (stage %.0fx%.0f)", W, H); g_coverOn = 1; g_coverTextTick = 0;
        wnd_wrap(1); GetAsyncKeyState(VK_LBUTTON); GetAsyncKeyState(VK_RBUTTON);
        return;                                                        // (exists above is from before: not "gone by itself")
    }
    if (!g_coverWant && exists) { cover_off(m); return; }
    if (!g_coverWant || !exists) { if (!exists && g_coverOn) { g_coverOn = 0; wnd_wrap(0); } return; }
    if (GetTickCount() - g_coverTextTick > 500) {
        g_coverTextTick = GetTickCount();
        static int dots; dots = (dots + 1) % 4; wchar_t dd[5] = L"...."; dd[dots] = 0;
        wchar_t h[900], t[100], l[400]; int big = (int)(g_coverH * 0.07), small = (int)(g_coverH * 0.032);
        html_esc(t, 100, g_coverTitle); html_esc(l, 400, g_coverLine);
        _snwprintf(h, 899, L"<p align='center'><font face='$TitleFont' size='%d' color='#E2C36B'>%ls</font></p>"
                           L"<p align='center'><font face='$NormalFont' size='%d' color='#C9D6CF'>%ls%ls</font></p>", big, t, small, l, g_noticeUntil ? L"" : dd);
        h[899] = 0;
        as_setw(m, "_level0.rjcover.t1.htmlText", h);
    }
    // Cancel: hover look + a click inside it; Esc = Cancel
    GFxValue x, y; int in = 0;
    if (as_get(m, "_level0.rjcover._xmouse", &x) && x.type == GFX_NUMBER && as_get(m, "_level0.rjcover._ymouse", &y) && y.type == GFX_NUMBER)
        in = x.v.num >= g_btnX && x.v.num <= g_btnX + g_btnW && y.v.num >= g_btnY && y.v.num <= g_btnY + g_btnH;
    if (in != g_btnHover) { g_btnHover = in; as_setn(m, "_level0.rjcover.b._alpha", in ? 100 : 75); }
    int click = 0;
    if (g_wndOrig) { click = g_click; g_click = 0; }
    else {                                                             // no window wrap: the primary button's state (left-handed: the right one)
        static int wasDown; HWND fg = GetForegroundWindow(); DWORD pid = 0; if (fg) GetWindowThreadProcessId(fg, &pid);
        int down = pid == GetCurrentProcessId() && (GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8001);
        click = down && !wasDown; wasDown = down;
    }
    int esc = g_esc; g_esc = 0;
    if ((click && in) || esc) {
        if (g_noticeUntil) g_noticeUntil = GetTickCount();              // a notice: read, close it now
        else rj_cancel(esc ? "Esc" : "the cover's Cancel");
    }
}

// ---------------- the join ----------------
enum { RJ_NONE, RJ_MENU, RJ_JOIN, RJ_LOBBY };
static int g_rj; static unsigned long long g_rjRoom; static wchar_t g_rjName[64];
// the game's own message boxes while we join (Receive_CreateMessageBox): the text of the latest one, and how many came.
// The first one can be the game's "Connecting..." - a box after it, or one that is still up when the join gives up, is the reason
static wchar_t g_mbText[200]; static volatile int g_mbCount; static DWORD g_mbAt;
static unsigned long long g_okRoom; static DWORD g_okAt;             // the room joined last (a second Join for it is no new request)
static DWORD g_rjAt, g_rjCalled, g_rjKnown, g_rjProbed, g_rjBack, g_rjOther; static int g_rjBacks, g_rjWalk;
// after a timeout or a Cancel with the join call made: the room is watched for a late answer (review 04.11 p.3)
static unsigned long long g_watchRoom; static DWORD g_watchUntil;
static void stage(const wchar_t *title, const wchar_t *what) {          // what the cover says now (siritos 03.11: what is going on)
    wcsncpy(g_coverTitle, title, 63); g_coverTitle[63] = 0;
    const wchar_t *n = g_rjName[0] ? g_rjName : L"the custom game";
    _snwprintf(g_coverLine, 199, what, n); g_coverLine[199] = 0; g_coverTextTick = 0;
}
static void notice(const wchar_t *title, const wchar_t *why, int toList) {   // a text on the cover for 3 s (then the public games list)
    wcsncpy(g_coverTitle, title, 63); g_coverTitle[63] = 0;
    _snwprintf(g_coverLine, 199, L"%ls", why); g_coverLine[199] = 0; g_coverTextTick = 0;
    g_coverWant = 1; g_noticeUntil = GetTickCount() + 3000; g_noticeList = toList;
}
static void rj_ok(int late) {
    logmsg("in the lobby of room %llu%s", g_rjRoom, late ? " (a late answer)" : "");
    emitf("{\"ev\":\"in_lobby\",\"room\":%llu%s}", g_rjRoom, late ? ",\"late\":true" : "");
    g_okRoom = g_rjRoom; g_okAt = GetTickCount(); g_rj = RJ_NONE; g_coverWant = 0; g_noticeUntil = 0; g_watchRoom = 0;
}
// the join failed: the event, and on the cover the reason (show = 1: 3 s, then the public games list if toList)
static void rj_fail(const char *reason, const wchar_t *shown, int toList) {
    char t[400]; js_esc(t, sizeof t, g_mbText);                         // what the game itself said, if anything
    logmsg("failed (room %llu): %s", g_rjRoom, reason);
    emitf("{\"ev\":\"failed\",\"room\":%llu,\"reason\":\"%s\",\"text\":\"%s\"}", g_rjRoom, reason, t);
    int called = g_rj == RJ_JOIN && g_rjCalled;
    g_rj = RJ_NONE;
    if (called && (!strcmp(reason, "timeout") || !strcmp(reason, "cancelled"))) { g_watchRoom = g_rjRoom; g_watchUntil = GetTickCount() + 15000; logmsg("room %llu watched 15 s for a late answer", g_rjRoom); }
    if (shown && g_fsMovie && !g_inWorld && !g_loading) notice(L"Could Not Join", g_mbText[0] ? g_mbText : shown, toList);
    else g_coverWant = 0;
}
static void rj_cancel(const char *how) {             // Cancel: our join off; a lobby we are in already stays (03.11: Cancel took the player out)
    if (!g_rj) return;
    logmsg("Cancel (%s)", how);
    if (dj_room() == g_rjRoom) { rj_ok(0); return; }
    rj_fail("cancelled", NULL, 0);
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
// a Join (from the pipe)
static void rj_start(unsigned long long room, const wchar_t *name, DWORD now, const char *from) {
    char nm[200]; WideCharToMultiByte(CP_UTF8, 0, name, -1, nm, sizeof nm, NULL, NULL); nm[sizeof nm - 1] = 0;
    logmsg("request (%s): room %llu \"%s\"", from, room, nm);
    if (!room) { emitf("{\"ev\":\"failed\",\"room\":0,\"reason\":\"error\",\"text\":\"no room id\"}"); return; }
    if (g_rj) {
        if (room == g_rjRoom) { emitf("{\"ev\":\"accepted\",\"room\":%llu}", room); return; }      // the same Join again: it goes on
        emitf("{\"ev\":\"failed\",\"room\":%llu,\"reason\":\"busy_join\",\"text\":\"\"}", room); return;
    }
    if (g_inWorld || g_loading) { emitf("{\"ev\":\"failed\",\"room\":%llu,\"reason\":\"busy_match\",\"text\":\"\"}", room); return; }
    if (g_watchRoom && (int)(g_watchUntil - now) > 0 && room != g_watchRoom) {     // the last room may still let us in
        emitf("{\"ev\":\"failed\",\"room\":%llu,\"reason\":\"busy_join\",\"text\":\"waiting for the previous room's answer\"}", room); return; }
    g_watchRoom = 0;
    g_rjRoom = room; wcsncpy(g_rjName, name, 63); g_rjName[63] = 0;
    emitf("{\"ev\":\"accepted\",\"room\":%llu}", room);
    if (g_fsMovie && dj_room() == room) {
        logmsg("already in room %llu", room);
        if (g_screen != SC_SETUP) dj_to_lobby();
        rj_ok(0); return;
    }
    g_noticeUntil = 0; g_noticeList = 0;
    g_rj = RJ_MENU; g_rjAt = now; g_rjKnown = g_rjProbed = g_rjBack = g_rjOther = g_rjCalled = 0; g_rjBacks = 0; g_rjWalk = 0;
    g_mbText[0] = 0; g_mbCount = 0;
}
static void rj_step(DWORD now) {
    if (g_watchRoom) {                                                // a late answer to a join we gave up on
        if ((int)(now - g_watchUntil) > 0 || g_inWorld || g_loading) { logmsg("room %llu: no late answer", g_watchRoom); g_watchRoom = 0; }
        else if (g_fsMovie) {
            static DWORD lastW;
            if (now - lastW > 400) {
                lastW = now;
                if (dj_room() == g_watchRoom) {
                    g_rjRoom = g_watchRoom; g_watchRoom = 0; logmsg("room %llu let us in late - the lobby screen", g_rjRoom);
                    if (g_screen != SC_SETUP) dj_to_lobby();
                    rj_ok(1);
                }
            }
        }
    }
    if (g_rj == RJ_NONE) return;
    if (g_inWorld || g_loading) { rj_fail("busy_match", NULL, 0); return; }
    switch (g_rj) {
    case RJ_MENU: {                                                   // the game may still be starting: up to 90 s
        if (!g_rjWalk) { g_rjWalk = 1; emitf("{\"ev\":\"walking\",\"room\":%llu}", g_rjRoom); }
        if (!g_fsMovie) {
            if (now - g_rjAt > 800) nudge_menu(now);
            if (now - g_rjAt > 90000) rj_fail("not_menu", NULL, 0);
            break;
        }
        if (!g_rjKnown) { g_rjKnown = now;
            { HWND gw = game_window(); POINT c; if (gw && GetCursorPos(&c) && ScreenToClient(gw, &c)) PostMessageW(gw, WM_MOUSEMOVE, 0, MAKELPARAM(c.x, c.y)); }   // the game's cursor back
            stage(L"Joining the Game", L"Opening the lobby: %ls"); g_coverWant = 1; }   // the cover at once
        if ((g_screen == SC_UNKNOWN || g_screen == SC_OTHER) && now - g_rjProbed > 250) { g_rjProbed = now; probe_screen(); }
        unsigned long long in = dj_room();
        if (g_screen == SC_UNKNOWN) {                                  // no panel visible yet (review 04.11 p.1: no "taken as the main menu")
            if (now - g_rjKnown > 15000) rj_fail("not_menu", L"The game is not in its main menu. Go there, then press Join again", 0);
            break;
        }
        if (g_screen != SC_OTHER) g_rjOther = 0;
        switch (g_screen) {
        case SC_OTHER:                                                 // Options, Army Painter, Campaign, Replays, a match summary...
            if (!g_rjOther) g_rjOther = now;
            if (now - g_rjOther > 4000) rj_fail("not_menu", L"Go to the main menu, then press Join again", 0);
            break;
        case SC_MM:                                                    // Find a Game: never Back there (review 04.11 p.4)
            if (mm_search() || in) rj_fail("busy_search", L"A search is on in Find a Game. Stop it, then press Join again", 0);
            else rj_fail("not_menu", L"Leave Find a Game (Back), then press Join again", 0);
            break;
        case SC_LIST:                                                  // the public games list: Back to the main menu, if in no room
            if (in) { rj_fail("other_lobby", L"You are in another lobby. Leave it first, then press Join again", 0); break; }
            if (now - g_rjBack > 1500) {
                if (g_rjBacks >= 3) { rj_fail("not_menu", L"Go to the main menu, then press Join again", 0); break; }
                g_rjBack = now; g_rjBacks++; logmsg("on the public games list - Back to the main menu"); fs(".OnClickBack", ""); }
            break;
        case SC_SETUP:
            if (in == g_rjRoom) { rj_ok(0); break; }
            if (in || now - g_rjKnown > 1500) rj_fail("other_lobby", L"You are in another lobby. Leave it first, then press Join again", 0);
            break;
        case SC_MAIN:
            if (in) { rj_fail("busy_search", L"The game is in a room of the automatch. Wait, then press Join again", 0); break; }
            stage(L"Joining the Game", L"Opening the lobby: %ls");
            g_mbText[0] = 0; g_mbCount = 0;
            g_coverWant = 1; g_rj = RJ_JOIN; g_rjAt = now; g_rjCalled = 0;
            break;
        }
        break; }
    case RJ_JOIN: {
        unsigned long long in = dj_room();
        if (in && in == g_rjRoom) {
            logmsg("in the room after %lu ms", g_rjCalled ? now - g_rjCalled : 0); stage(L"Joining the Game", L"In %ls. Opening the lobby screen");
            if (g_screen != SC_SETUP) dj_to_lobby();
            g_rj = RJ_LOBBY; g_rjAt = now; break; }
        if (in) { rj_fail("other_lobby", L"You are in another lobby", 0); break; }
        if (g_screen != SC_MAIN && g_screen != SC_SETUP) { rj_fail(g_screen == SC_MM ? "busy_search" : "not_menu", NULL, 0); break; }   // the player went elsewhere
        if ((g_mbCount >= 2 && now - g_mbAt > 1500) || (g_mbCount && g_rjCalled && now - g_mbAt > 6000)) {     // the game said no (full, closed, ...)
            char u[400]; WideCharToMultiByte(CP_UTF8, 0, g_mbText, -1, u, sizeof u, NULL, NULL); u[sizeof u - 1] = 0;
            logmsg("the game said: %s", u);
            const wchar_t *dflt = L"The game did not let us in. Opening the public games list";
            rj_fail(no_reason(g_mbText), dflt, 1); break; }
        if (!g_rjCalled && g_screen == SC_MAIN) {                      // one call only: no second join on top of a slow first (review p.3)
            stage(L"Joining the Game", L"Connecting to the game: %ls");
            int r = dj_join(g_rjRoom);
            if (r > 0) { g_rjCalled = now; emitf("{\"ev\":\"joining\",\"room\":%llu}", g_rjRoom); }
            else if (!r) { rj_fail("error", L"The game's join did not start. Opening the public games list", 1); break; }
            else if (now - g_rjAt > 60000) { rj_fail("timeout", L"Not signed in to the game's online service. Try again later", 0); break; }   // r < 0: Relic sign-in not done
        }
        if (g_rjCalled && now - g_rjCalled > 25000) rj_fail("timeout", L"The room did not answer", 0);   // no list: it may still answer (watched)
        break; }
    case RJ_LOBBY:
        if (g_screen == SC_SETUP) rj_ok(0);
        else if (now - g_rjAt > 10000) rj_fail("timeout", L"In the room, but its lobby screen did not open. Opening the public games list", 1);
        break;
    }
}
static void snap_update(DWORD now) {                                  // for "status" on the pipe thread
    static DWORD lastRoom; static unsigned long long room;
    int inMatch = g_inWorld || g_loading;
    if (inMatch) room = 0;
    else if (g_fsMovie && now - lastRoom > 1000) { lastRoom = now; room = dj_room(); }
    EnterCriticalSection(&g_qlk);
    g_snap.screen = inMatch ? -1 : g_screen; g_snap.inMatch = inMatch; g_snap.searching = mm_search() || (g_screen == SC_MM && room);
    g_snap.room = room; g_snap.joining = g_rj ? g_rjRoom : 0; g_snap.at = now;
    LeaveCriticalSection(&g_qlk);
}
static void cmd_take(DWORD now) {
    for (;;) {
        QCmd c; int have = 0;
        EnterCriticalSection(&g_qlk);
        if (g_cqN) { c = g_cq[g_cqH]; g_cqH = (g_cqH + 1) % 8; g_cqN--; have = 1; }
        LeaveCriticalSection(&g_qlk);
        if (!have) return;
        if (c.what == CMD_JOIN) rj_start(c.room, c.name, now, "the launcher's pipe");
        else if (c.what == CMD_CANCEL) {
            if (g_rj) rj_cancel("the launcher");
            else if (g_watchRoom) { logmsg("Cancel (the launcher): no more watching room %llu", g_watchRoom); g_watchRoom = 0; }
        }
    }
}

// ---------------- the pipe (its own thread; never the engine) ----------------
#define MAXC 4
typedef struct { HANDLE h; OVERLAPPED ov; char rb[512]; char line[1024]; int len, drop, rd; DWORD id; } Cli;
static Cli g_c[MAXC]; static DWORD g_nextId = 1;
static volatile LONG g_nCli;                      // launchers connected now (the main thread probes the screen only for them)
static wchar_t g_pipeName[80];
static void cli_close(Cli *c, const char *why) {
    if (!c->h) return;
    logmsg("pipe: client %lu gone (%s)", c->id, why);
    if (c->rd) { DWORD n; CancelIoEx(c->h, &c->ov); GetOverlappedResult(c->h, &c->ov, &n, TRUE); c->rd = 0; }   // the cancelled read is over before the slot is reused
    DisconnectNamedPipe(c->h); CloseHandle(c->h); c->h = NULL; InterlockedDecrement(&g_nCli);
}
static int cli_write(Cli *c, const char *s) {                         // overlapped, at most 1 s - a client that does not read is dropped
    static OVERLAPPED wo; static HANDLE we;
    if (!we) we = CreateEventW(NULL, TRUE, FALSE, NULL);
    memset(&wo, 0, sizeof wo); wo.hEvent = we; ResetEvent(we);
    DWORD n = (DWORD)strlen(s), w = 0;
    if (!WriteFile(c->h, s, n, &w, &wo)) {
        if (GetLastError() != ERROR_IO_PENDING) { cli_close(c, "write failed"); return 0; }
        if (WaitForSingleObject(we, 1000) != WAIT_OBJECT_0) { CancelIoEx(c->h, &wo); GetOverlappedResult(c->h, &wo, &w, TRUE); cli_close(c, "does not read"); return 0; }
        if (!GetOverlappedResult(c->h, &wo, &w, FALSE)) { cli_close(c, "write failed"); return 0; }
    }
    return 1;
}
static void status_line(char *b, int cap) {
    EnterCriticalSection(&g_qlk);
    int sc = g_snap.screen, im = g_snap.inMatch, se = g_snap.searching; unsigned long long room = g_snap.room, jn = g_snap.joining; DWORD at = g_snap.at;
    LeaveCriticalSection(&g_qlk);
    _snprintf(b, cap - 1, "{\"ev\":\"status\",\"screen\":\"%s\",\"in_match\":%s,\"searching\":%s,\"room\":%llu,\"joining\":%llu,"
              "\"version\":\"" RJ_VERSION "\",\"proto\":1,\"pid\":%lu,\"stalled\":%s}\n",
              sc < 0 ? "match" : sc < (int)(sizeof SC_NAME / sizeof SC_NAME[0]) ? SC_NAME[sc] : "unknown", im ? "true" : "false", se ? "true" : "false",
              room, jn, GetCurrentProcessId(), !at || GetTickCount() - at > 3000 ? "true" : "false");
    b[cap - 1] = 0;
}
static void cli_line(Cli *c, char *s) {
    Cmd cmd;
    if (!js_command(s, &cmd)) { logmsg("pipe: client %lu sent what is not a command (%d bytes)", c->id, (int)strlen(s)); cli_write(c, "{\"ev\":\"error\",\"reason\":\"bad_command\"}\n"); return; }
    if (!strcmp(cmd.cmd, "status")) { char b[400]; status_line(b, sizeof b); cli_write(c, b); return; }
    QCmd q; memset(&q, 0, sizeof q); q.from = c->id;
    if (!strcmp(cmd.cmd, "join") && cmd.hasRoom && cmd.room) {
        q.what = CMD_JOIN; q.room = cmd.room;
        if (!MultiByteToWideChar(CP_UTF8, 0, cmd.name, -1, q.name, 64)) q.name[0] = 0;
        q.name[63] = 0;
        for (wchar_t *p = q.name; *p; p++) if (*p < 0x20) *p = L' ';
    } else if (!strcmp(cmd.cmd, "cancel")) q.what = CMD_CANCEL;
    else { cli_write(c, "{\"ev\":\"error\",\"reason\":\"bad_command\"}\n"); return; }
    logmsg("pipe: client %lu: %s%s", c->id, cmd.cmd, q.what == CMD_JOIN ? " (queued for the game's thread)" : "");
    EnterCriticalSection(&g_qlk);
    if (g_cqN < 8) { g_cq[(g_cqH + g_cqN) % 8] = q; g_cqN++; } else q.what = 0;
    LeaveCriticalSection(&g_qlk);
    if (!q.what) cli_write(c, "{\"ev\":\"error\",\"reason\":\"queue_full\"}\n");
}
static int cli_read(Cli *c) {                                         // start the next read; 0 = the client is gone
    ResetEvent(c->ov.hEvent);
    DWORD n = 0;
    if (!ReadFile(c->h, c->rb, sizeof c->rb, &n, &c->ov) && GetLastError() != ERROR_IO_PENDING) { cli_close(c, "read failed"); return 0; }
    c->rd = 1; return 1;                                                         // done at once or pending: the event says so either way
}
static void cli_got(Cli *c) {
    DWORD n = 0; c->rd = 0;
    if (!GetOverlappedResult(c->h, &c->ov, &n, FALSE)) { DWORD e = GetLastError(); if (e != ERROR_MORE_DATA) { cli_close(c, e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED || e == ERROR_OPERATION_ABORTED ? "closed" : "read failed"); return; } }
    for (DWORD i = 0; i < n && c->h; i++) {
        char ch = c->rb[i];
        if (ch == '\n') { c->line[c->len] = 0; if (!c->drop && c->len) cli_line(c, c->line); c->len = 0; c->drop = 0; continue; }
        if (c->len < (int)sizeof c->line - 1) c->line[c->len++] = ch; else c->drop = 1;      // a line too long: dropped whole
    }
    if (c->h) cli_read(c);
}
static HANDLE pipe_new(SECURITY_ATTRIBUTES *sa, int first) {
    DWORD open = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
    DWORD mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT;
    HANDLE h = CreateNamedPipeW(g_pipeName, open, mode | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, sa);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER)          // an older system without the flag
        h = CreateNamedPipeW(g_pipeName, open, mode, PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, sa);
    return h;
}
static DWORD WINAPI pipe_thread(LPVOID _) {
    _snwprintf(g_pipeName, 80, L"\\\\.\\pipe\\EliteRoomJoin-%lu", GetCurrentProcessId()); g_pipeName[79] = 0;
    // only the user the game runs as (review: ACL) - the DACL has this one entry, nothing inherited
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, FALSE}; HANDLE tok; static unsigned char tu[256]; DWORD len = 0; wchar_t *sid = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        if (GetTokenInformation(tok, TokenUser, tu, sizeof tu, &len)) ConvertSidToStringSidW(((TOKEN_USER *)tu)->User.Sid, &sid);
        CloseHandle(tok);
    }
    if (sid) { wchar_t sddl[200]; _snwprintf(sddl, 199, L"D:P(A;;GA;;;%ls)", sid); sddl[199] = 0; LocalFree(sid);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL)) sa.lpSecurityDescriptor = NULL; }
    if (!sa.lpSecurityDescriptor) { logmsg("pipe: no security descriptor for the current user - no pipe"); return 0; }
    HANDLE lis = pipe_new(&sa, 1);
    if (lis == INVALID_HANDLE_VALUE) { logmsg("pipe: cannot create %ls (error %lu) - no pipe", g_pipeName, GetLastError()); return 0; }
    OVERLAPPED lov; memset(&lov, 0, sizeof lov); lov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < MAXC; i++) g_c[i].ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    char nm[100]; WideCharToMultiByte(CP_UTF8, 0, g_pipeName, -1, nm, sizeof nm, NULL, NULL); nm[99] = 0;
    logmsg("pipe: %s open (only this user, no remote clients)", nm);
    int pending = 0, connNow = 0;
    for (;;) {
        if (!pending && lis != INVALID_HANDLE_VALUE) {                // wait for the next client
            ResetEvent(lov.hEvent); connNow = 0;
            if (ConnectNamedPipe(lis, &lov)) { connNow = 1; SetEvent(lov.hEvent); }
            else { DWORD e = GetLastError(); if (e == ERROR_PIPE_CONNECTED) { connNow = 1; SetEvent(lov.hEvent); } else if (e != ERROR_IO_PENDING) { logmsg("pipe: ConnectNamedPipe error %lu", e); CloseHandle(lis); lis = INVALID_HANDLE_VALUE; } }
            pending = 1;
        }
        if (lis == INVALID_HANDLE_VALUE) { Sleep(1000); lis = pipe_new(&sa, 0); pending = 0; continue; }
        HANDLE hs[2 + MAXC]; int idx[2 + MAXC], n = 0;
        hs[n] = g_outEv; idx[n++] = -2; hs[n] = lov.hEvent; idx[n++] = -1;
        for (int i = 0; i < MAXC; i++) if (g_c[i].h) { hs[n] = g_c[i].ov.hEvent; idx[n++] = i; }
        DWORD r = WaitForMultipleObjects(n, hs, FALSE, INFINITE);   // ExitProcess ends this thread here (no lock held)
        if (r >= WAIT_OBJECT_0 + (DWORD)n) { Sleep(100); continue; }
        int k = idx[r - WAIT_OBJECT_0];
        if (k == -1) {                                                // a client connected
            DWORD dummy; pending = 0;
            if (!connNow && !GetOverlappedResult(lis, &lov, &dummy, FALSE)) { DisconnectNamedPipe(lis); continue; }
            int s = -1; for (int i = 0; i < MAXC; i++) if (!g_c[i].h) { s = i; break; }
            if (s < 0) { logmsg("pipe: a fifth client - refused"); DisconnectNamedPipe(lis); continue; }
            Cli *c = &g_c[s]; c->h = lis; c->len = 0; c->drop = 0; c->rd = 0; c->id = g_nextId++; InterlockedIncrement(&g_nCli);
            logmsg("pipe: client %lu connected", c->id);
            lis = pipe_new(&sa, 0);                                    // the next one waits on a new instance
            char b[400]; status_line(b, sizeof b);
            if (cli_write(c, b)) cli_read(c);
        } else if (k == -2) {                                         // events from the game's thread
            for (;;) {
                QOut o; int have = 0;
                EnterCriticalSection(&g_qlk);
                if (g_oqN) { o = g_oq[g_oqH]; g_oqH = (g_oqH + 1) % 32; g_oqN--; have = 1; }
                LeaveCriticalSection(&g_qlk);
                if (!have) break;
                for (int i = 0; i < MAXC; i++) if (g_c[i].h && (!o.to || o.to == g_c[i].id)) cli_write(&g_c[i], o.s);
            }
        } else cli_got(&g_c[k]);
    }
}

// ---------------- hooks ----------------
static void __fastcall fs_hook(void *self, void *edx, void *movie, const char *cmd, const char *args) {
    if (cmd && !IsBadReadPtr(cmd, 1)) {
        if (!strncmp(cmd, "_level0.display_item", 20) || (!strcmp(cmd, "OnLoadComplete") && args && !IsBadReadPtr(args, 1) && strstr(args, "name:=_level0.display_item"))) {
            if (movie != g_fsMovie && g_loading && !g_inWorld) { g_loading = 0; logmsg("back in the menu"); }
            if (!g_loading && !g_inWorld && (movie != g_fsMovie || g_fsSelf != self)) menu_seen(movie, self, "a menu command");
        } else if (movie != g_fsMovie && !g_loading && !g_inWorld) {
            int seen = 0;
            for (int i = 0; i < g_ntFs; i++) if (g_triedFs[i] == movie) seen = 1;
            if (!seen && g_ntFs < 16) { g_triedFs[g_ntFs++] = movie; if (is_menu_movie(movie)) menu_seen(movie, self, cmd); }
        }
        if (!strcmp(cmd, "OnLoadMovieComplete") && args && !IsBadReadPtr(args, 6) && !strncmp(args, "/mp_mc", 6) && !g_loading) {
            g_loading = 1; g_coverWant = 0; menu_forget("a match is loading"); }   // 03.11 review: the lobby screen stayed known after the match
        if (!g_sending && g_screen == SC_MM && strlen(cmd) > 13 && !strcmp(cmd + strlen(cmd) - 13, ".OnClickStart")) {
            g_mmClicked = 1; logmsg("Start / Cancel Searching pressed on Find a Game"); }
        if (!strcmp(cmd, "OnLoadComplete") && args && !IsBadReadPtr(args, 1) && (movie == g_fsMovie || !g_fsMovie)) {     // which full-screen panel is up
            const char *nm = strstr(args, "name:="), *url = strstr(args, "url:=");
            if (nm && url) {
                char path[160] = ""; const char *e = strchr(nm, ';'); int len = e ? (int)(e - nm - 6) : 0;
                if (len > 0 && len < 159) { memcpy(path, nm + 6, len); path[len] = 0; }
                if (strlen(path) >= 9 && !strcmp(path + strlen(path) - 9, ".panel_mc")) {
                    int sc = url_screen(url);
                    if (sc != SC_OTHER && (g_inWorld || g_loading)) {
                        g_inWorld = 0; g_loading = 0; logmsg("back in the menu (%s)", SC_NAME[sc]);
                        if (!g_fsMovie) menu_seen(movie, self, "a menu panel"); }                          // an unknown panel = SC_OTHER, not the last one (review p.1)
                    if (sc != g_screen) { g_engSearch = -1; g_mmClicked = 0; }     // a new panel: the search state of the old one goes
                    strcpy(g_panel, path); g_screen = sc; if (g_rj) logmsg("screen %s", SC_NAME[sc]); }
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
        wchar_t t[200] = L""; int n = 0; char dbg[400]; int dl = 0; dbg[0] = 0;
        for (unsigned i = 0; i < count; i++) {               // the box's strings: title, text, buttons - the longest one is the text
            int room = (int)sizeof dbg - dl; if (room < 2) break;
            const wchar_t *w = args[i].type == GFX_WSTRING && args[i].v.ws && !IsBadReadPtr(args[i].v.ws, 2) ? args[i].v.ws : NULL;
            int k = 0;
            if (args[i].type == GFX_STRING && args[i].v.s && !IsBadReadPtr(args[i].v.s, 1)) k = _snprintf(dbg + dl, room - 1, " [%u]\"%.60s\"", i, args[i].v.s);
            else if (args[i].type == GFX_NUMBER) k = _snprintf(dbg + dl, room - 1, " [%u]%g", i, args[i].v.num);
            else if (w) { char u[120]; WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL); u[119] = 0; k = _snprintf(dbg + dl, room - 1, " [%u]'%s'", i, u); }
            if (k < 0 || k > room - 1) { dl = (int)sizeof dbg - 1; dbg[dl] = 0; } else dl += k;   // never past the buffer (review 04.11 p.2)
            if (w) { int len = (int)wcsnlen(w, 400); if (len > n) { n = len; wcsncpy(t, w, 199); t[199] = 0; } }
        }
        logmsg("message box while joining (%u args):%s", count, dbg);
        if (t[0]) { wcsncpy(g_mbText, t, 199); g_mbText[199] = 0; }
        g_mbCount++; g_mbAt = GetTickCount();
    }
    // the menu's SFMovie: its movie (+4) has _level0.display_item; its fscommand handler (+0x18) as the buttons use it
    if (!g_fsMovie && !g_loading && !g_inWorld && self && !IsBadReadPtr(self, 0x1C)) {
        void *mv = *(void **)((char *)self + 4), *h = *(void **)((char *)self + 0x18);
        int seen = 0;
        for (int i = 0; i < g_ntInv; i++) if (g_triedInv[i] == mv) seen = 1;
        if (!seen && g_ntInv < 16) {
            g_triedInv[g_ntInv++] = mv;
            int menu = is_menu_movie(mv);
            if (g_rj || g_ntInv <= 6) logmsg("engine call %s: object %p movie %p - %s", method && !IsBadReadPtr(method, 8) ? method : "?", self, mv, menu ? "the menu" : "not the menu");
            if (menu) menu_seen(mv, h && !IsBadReadPtr(h, 0x20) ? h : NULL, "the engine's calls");
        }
    }
    return g_invOrig(self, edx, method, flags, args, count);
}
// a world (match, replay): the menu is gone with its start, the screen again known once the menu talks (review 04.11 p.5)
// (These run on the simulation's thread: only flags here; the game's thread forgets the menu movie in its next frame, and
// every use of the movie there checks the flags first.) Without SimulatePost hooked the start is only "a match is loading".
static int g_postHooked; static volatile int g_worldStart;
static void __fastcall pre_hook(void *self, void *edx) {
    if (g_postHooked) g_inWorld = 1;
    g_loading = 1; g_worldStart = 1;
    g_preOrig(self, edx);
}
static void __fastcall post_hook(void *self, void *edx) {
    g_inWorld = 0;
    g_postOrig(self, edx);
}
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
    if (g_worldStart) { g_worldStart = 0; logmsg("a world starts (match or replay)"); g_coverWant = 0; g_noticeUntil = 0; menu_forget("a world starts"); }
    if (g_movieGone) { g_movieGone = 0; logmsg("the menu movie was deleted (the front end rebuilt?)"); g_noticeUntil = 0; menu_forget("the menu movie deleted"); }
    if (g_noticeUntil && (int)(now - g_noticeUntil) >= 0) {        // the notice was read: cover off (and the list when it was a failed join)
        g_noticeUntil = 0; g_coverWant = 0;
        if (g_noticeList && g_screen == SC_MAIN && !g_watchRoom) fs(".OnClickPublicMatch", "");
        g_noticeList = 0;
    }
    if (g_fsHooked) {
        cmd_take(now);
        if (!g_noticeUntil) rj_step(now);
        static DWORD idleProbe;                                        // the screen for "status" while no join runs
        if (!g_rj && g_nCli > 0 && g_fsMovie && !g_loading && !g_inWorld && g_screen == SC_UNKNOWN && now - idleProbe > 2000) { idleProbe = now; probe_screen(); }   // only for a connected launcher's status
        snap_update(now);
    }
    if (g_fsMovie && !g_loading && !g_inWorld && (g_coverWant || g_coverOn)) cover_update();
    else if (g_wndOrig && !g_coverOn) wnd_wrap(0);
}
static int iat_hook(DWORD slot, void *fn, void **orig) {          // chained: whatever is in the import slot now is called first
    if (IsBadReadPtr((void *)slot, 4)) return 0;
    HANDLE mx = patch_lock(); int ok = 0; DWORD cur = *(volatile DWORD *)slot, old;
    if (cur == (DWORD)fn) { patch_unlock(mx); return 1; }
    if (!is_code(cur)) { patch_unlock(mx); return 0; }
    *orig = (void *)cur;                                             // before the slot points to us (review 04.11 p.6)
    if (VirtualProtect((void *)slot, 4, PAGE_READWRITE, &old)) {
        SIZE_T w = 0;
        if (WriteProcessMemory(GetCurrentProcess(), (void *)slot, &fn, 4, &w) && w == 4) ok = 1;
        VirtualProtect((void *)slot, 4, old, &old);
    }
    patch_unlock(mx); return ok;
}
static DWORD WINAPI installer(LPVOID _) {
    int sim = 0;
    for (int i = 0; i < 180; i++) {
        if (!sim && GetModuleHandleA("SimEngine.dll")) {
            g_postHooked = iat_hook(IAT_SIM_POST, (void *)post_hook, (void **)&g_postOrig);   // the end first: no world flag that never clears
            sim = iat_hook(IAT_SIM_PRE, (void *)pre_hook, (void **)&g_preOrig);
            if (sim && !g_postHooked) logmsg("World::SimulatePost not hooked - a world's end is seen from the menu");
            logmsg(sim ? "world hooks installed" : "World::SimulatePre not hooked - a world is seen from the match movie only");
            sim = 1;
        }
        if (GetModuleHandleA("Shark.dll") && iat_hook(IAT_SHARK_UPDATE, (void *)upd_hook, (void **)&g_updOrig)) {
            logmsg("frame hook installed");
            return pipe_thread(NULL);                                  // this thread now serves the pipe until the process ends
        }
        Sleep(1000);
    }
    logmsg("frame hook NOT installed - no pipe"); return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID _) {
    if (r != DLL_PROCESS_ATTACH) return TRUE;                          // DETACH: nothing (the pipe thread is never waited for)
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_lk); InitializeCriticalSection(&g_qlk);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0; else g_dir[0] = 0;
    log_open();
    logmsg("RoomJoin " RJ_VERSION " (joining a room from the launcher: the pipe EliteRoomJoin-%lu)", GetCurrentProcessId());
    { char n[64]; wsprintfA(n, "Local\\dow2patch_RoomJoin_%lu", GetCurrentProcessId());
      HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx && GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("another RoomJoin copy is active, this one stays idle"); return TRUE; } }
    { wchar_t ini[MAX_PATH]; _snwprintf(ini, MAX_PATH, L"%lsRoomJoin.ini", g_dir); ini[MAX_PATH - 1] = 0;
      if (!GetPrivateProfileIntW(L"RoomJoin", L"Enabled", 1, ini)) { logmsg("disabled in RoomJoin.ini - no hooks"); return TRUE; } }
    g_fsHooked = hook_fscmd();                    // the vtable slot: the same whether MMFind is in the game already or comes later
    if (!g_fsHooked) return TRUE;                 // without it no join can be done: nothing else either
    hook_invoke();
    g_outEv = CreateEventW(NULL, FALSE, FALSE, NULL);
    // 64 KB of stack reserved, not 1 MB (address space: see ShellFix, CHANGELOG 0.2.3)
    HANDLE t = CreateThread(NULL, 64 * 1024, installer, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL); if (t) CloseHandle(t);
    return TRUE;
}
