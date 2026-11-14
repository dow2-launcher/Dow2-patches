// ReplayOpen.dll - "Play" of a replay from the launcher (siritos, 14.11.2026; DoW2: Retribution 3.19, Elite and vanilla).
// The player presses the button on a replay in the launcher: a game that is not running is started by the launcher, which then
// sends the command as soon as the pipe is there; a game that is open shows the game's own "Connecting" box at once
// (common/connbox.h, as RoomJoin / MMFind / LiveReplay) and the replay starts - the same start as the game's Game History ->
// Load Replay (0x40F526, as LiveReplay and ReplayTakeover's tests use it). Its own add-on and tick (docs/ADDON_RULES.md).
//
// The pipe \\.\pipe\EliteReplayOpen-<pid of DOW2.exe> (only the user the game runs as, no remote clients; as RoomJoin's / LiveReplay's;
// robridge.exe = rjbridge.c for a launcher outside Wine). One JSON object per line:
//   launcher -> game  {"cmd":"play","file":"<full path of the .rec, or a name in the game's Playback folder>"}
//                     {"cmd":"status"}   {"cmd":"cancel"}
//   game -> launcher  {"ev":"status","ver":"1","pid":n,"state":"idle|waiting|loading|playing|failed","file":"...","reason":"...",
//                       "screen":"main|find_game|games_list|lobby|other|unknown","in_world":b}   (on connect and on "status")
//                     {"ev":"accepted","file":"..."}  {"ev":"loading","file":"..."}  {"ev":"playing","file":"..."}  {"ev":"over","file":"..."}
//                     {"ev":"data_mismatch","file":"...","replay":"<hex>","game":"<hex>"}   (see below; the replay still starts)
//                     {"ev":"failed","file":"...","reason":"not_found|bad_file|version|data_mismatch|busy_match|busy_lobby|busy|start|
//                       cancelled|timeout|error","text":"..."}
//                     {"ev":"error","reason":"bad_command|queue_full"}
// The file is copied to <Documents>\My Games\Dawn of War II - Retribution\Playback\launcher_replay.rec (the game opens replays only
// from that folder by name, "playback:<name>"); nothing else is written.
//
// "Micro-patches of the same game version do not start each other's replays" (siritos): the replay header (0x54 bytes, read by
// 0x42B259) is accepted only with build 10320 (0x2850, = 3.19), a zero third word, the "DOW2_REC" mark, and its second word equal to
// 0x7A0A30() - the game data's checksum (Relic's lobby "dataChecksum"): every Elite build with other attrib archives has another one
// (3.2 0xD6805B7D, 3.2.1 0x819567F8, 3.2.1.0.0 ...) although the game is the same 3.19, so the game's own Load Replay of such a file
// silently does nothing. ReplayOpen reads the header first: another build of the game (not 10320) or no DOW2_REC = "version" /
// "bad_file" with the reason in the log and the event; another data checksum = the copy gets this game's checksum (as LiveReplay 3
// does for live replays) and the event data_mismatch tells the launcher ("recorded with another Elite build: it may go out of
// sync where the builds differ in data") - ReplayOpen.ini Mismatch=refuse fails it instead (reason data_mismatch).
//
// Rules (as RoomJoin): the engine only from the game's own thread (shark::update - the pipe thread only queues), nothing in
// DLL_PROCESS_DETACH, the code places checked for 3.19 (else: logged, nothing hooked), the hooks under the shared patch lock
// (Local\dow2patch_iat_<pid>, MMFind / LobbyStats / RoomJoin / LiveReplay), every step in ReplayOpen.log.
// ReplayOpen.ini [ReplayOpen] Enabled=0: off; ConnectingBox=0: no box; Mismatch=play|refuse.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600                 // CancelIoEx (Vista+) with older mingw headers
#endif
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>

#define RO_VERSION "1"
#define G_SETTINGS     0x01335A78         // +0x118 game type (0x800 playback)
#define G_GAMEOBJ_PTR  0x01335A74         // what the replay start takes in EDX
#define FN_START_PB    0x0040F526         // the replay of a file (ECX "playback:<name>.rec", EDX 0x01335A74) -> AL
#define FN_DATACRC     0x007A0A30         // cdecl () -> the game data's checksum
#define IAT_SHARK_UPDATE 0x00F88068
#define IAT_SIM_PRE    0x00F88154
#define IAT_SIM_POST   0x00F88100
#define VT_FSCMD       0x010BAC9C
#define FN_FSCMD       0x00681AB0
#define FN_FSCMD_VT0   0x00681E80
#define T_PLAYBACK     0x800
#define REC_BUILD      10320

typedef char (__fastcall *StartPb_t)(const char *path, void *gs);
typedef void (__stdcall *UpdFn)(float dt);
typedef void (__fastcall *ThisFn)(void *self, void *edx);
typedef void (__fastcall *FsCmdFn)(void *self, void *edx, void *movie, const char *cmd, const char *args);
static UpdFn g_updOrig; static ThisFn g_preOrig, g_postOrig; static FsCmdFn g_fsOrig;

// ---------------- the log ----------------
static wchar_t g_dir[MAX_PATH]; static CRITICAL_SECTION g_lk; static HANDLE g_log = INVALID_HANDLE_VALUE;
static void log_open(void) {
    wchar_t p[MAX_PATH], o[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsReplayOpen.log", g_dir); p[MAX_PATH - 1] = 0;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(p, GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > 512 * 1024)) {
        _snwprintf(o, MAX_PATH, L"%lsReplayOpen.log.old", g_dir); o[MAX_PATH - 1] = 0; MoveFileExW(p, o, MOVEFILE_REPLACE_EXISTING); }
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
#define CONNBOX_LOG logmsg
#include "../common/connbox.h"
#define I32(p, o) (*(int *)((char *)(p) + (o)))
#define P32(p, o) (*(char **)((char *)(p) + (o)))
static int ok(const void *p, int n) { return p && !IsBadReadPtr(p, n); }
#define CONNBOX_LOG logmsg
#include "../common/connbox.h"

// ---------------- state ----------------
enum { ST_IDLE, ST_WAIT, ST_LOAD, ST_PLAY, ST_FAILED };
static const char *ST_NAME[] = {"idle", "waiting", "loading", "playing", "failed"};
static CRITICAL_SECTION g_qlk;
static volatile int g_state, g_inWorld, g_loading;
static char g_fileU8[400], g_reason[40];
static wchar_t g_src[MAX_PATH], g_dst[MAX_PATH], g_pbDir[MAX_PATH];
static DWORD g_reqAt, g_boxAt, g_firstFrame; static int g_boxCfg = 1, g_mismatchRefuse, g_ours;

#define MAXC 4
static HANDLE g_outEv; static char *g_oq[32]; static int g_oqH, g_oqN;
static void js_esc(char *o, int cap, const char *s) {                // a string into JSON
    int n = 0; for (; *s && n < cap - 7; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { o[n++] = '\\'; o[n++] = ch; } else if (ch < 0x20) n += snprintf(o + n, cap - n, "\\u%04x", ch); else o[n++] = ch; }
    o[n] = 0;
}
static void emitf(const char *fmt, ...) {
    char b[900]; va_list a; va_start(a, fmt); int n = _vsnprintf(b, sizeof b - 2, fmt, a); va_end(a);
    if (n < 0 || n > (int)sizeof b - 2) n = (int)sizeof b - 2;
    b[n++] = '\n'; b[n] = 0;
    EnterCriticalSection(&g_qlk);
    if (g_oqN < 32) { g_oq[(g_oqH + g_oqN) % 32] = _strdup(b); g_oqN++; }
    LeaveCriticalSection(&g_qlk);
    if (g_outEv) SetEvent(g_outEv);
}
static void ev(const char *e) { char f[820]; js_esc(f, sizeof f, g_fileU8); emitf("{\"ev\":\"%s\",\"file\":\"%s\"}", e, f); }
static void set_state(int st) { EnterCriticalSection(&g_qlk); g_state = st; LeaveCriticalSection(&g_qlk); }
static void ro_fail(const char *reason, const char *text) {          // the game's thread
    connbox_close(reason);
    char f[820], t[300]; js_esc(f, sizeof f, g_fileU8); js_esc(t, sizeof t, text ? text : "");
    logmsg("failed: %s%s%s", reason, text && text[0] ? " - " : "", text ? text : "");
    EnterCriticalSection(&g_qlk); snprintf(g_reason, sizeof g_reason, "%s", reason); g_state = ST_FAILED; LeaveCriticalSection(&g_qlk);
    emitf("{\"ev\":\"failed\",\"file\":\"%s\",\"reason\":\"%s\",\"text\":\"%s\"}", f, reason, t);
}

// ---------------- hooks ----------------
static HANDLE patch_lock(void) {
    char n[64]; wsprintfA(n, "Local\\dow2patch_iat_%lu", GetCurrentProcessId());
    HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx) WaitForSingleObject(mx, 5000); return mx;
}
static void patch_unlock(HANDLE mx) { if (mx) { ReleaseMutex(mx); CloseHandle(mx); } }
static int is_code(DWORD a) {
    MEMORY_BASIC_INFORMATION mi;
    return a && VirtualQuery((void *)a, &mi, sizeof mi) && mi.State == MEM_COMMIT
        && (mi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}
static int slot_hook(DWORD slot, DWORD expect, void *fn, void **orig) {   // a vtable slot (chained when another add-on has it)
    if (IsBadReadPtr((void *)slot, 4)) return 0;
    HANDLE mx = patch_lock(); DWORD cur = *(volatile DWORD *)slot, o; int r = 0;
    if (cur == (DWORD)fn) { patch_unlock(mx); return 1; }
    if (cur == expect || is_code(cur)) {
        *orig = (void *)cur;
        if (VirtualProtect((void *)slot, 4, PAGE_READWRITE, &o)) { InterlockedExchange((volatile LONG *)slot, (LONG)(DWORD)fn); VirtualProtect((void *)slot, 4, o, &o); r = 1; }
        if (cur != expect) logmsg("slot %08lX held %08lX (another add-on's) - chained", slot, cur);
    }
    patch_unlock(mx); return r;
}
static int iat_hook(DWORD slot, void *fn, void **orig) {
    if (IsBadReadPtr((void *)slot, 4)) return 0;
    HANDLE mx = patch_lock(); int r = 0; DWORD cur = *(volatile DWORD *)slot, old;
    if (cur == (DWORD)fn) { patch_unlock(mx); return 1; }
    if (!is_code(cur)) { patch_unlock(mx); return 0; }
    *orig = (void *)cur;
    if (VirtualProtect((void *)slot, 4, PAGE_READWRITE, &old)) {
        SIZE_T w = 0; if (WriteProcessMemory(GetCurrentProcess(), (void *)slot, &fn, 4, &w) && w == 4) r = 1;
        VirtualProtect((void *)slot, 4, old, &old);
    }
    patch_unlock(mx); return r;
}

// ---------------- the menu (which full-screen panel is up: as RoomJoin / LiveReplay) ----------------
enum { SC_UNKNOWN, SC_OTHER, SC_MAIN, SC_MM, SC_LIST, SC_SETUP };
static const char *SC_NAME[] = {"unknown", "other", "main", "find_game", "games_list", "lobby"};
static volatile int g_screen;
static int url_screen(const char *u) {
    return strstr(u, "main_menu.gfx") ? SC_MAIN : strstr(u, "matchmaking.gfx") ? SC_MM : strstr(u, "game_list_movie.gfx") ? SC_LIST
         : strstr(u, "game_setup_movie.gfx") ? SC_SETUP : SC_OTHER;
}
static void __fastcall fs_hook(void *self, void *edx, void *movie, const char *cmd, const char *args) {
    if (cmd && !IsBadReadPtr(cmd, 1)) {
        if (!strcmp(cmd, "OnLoadMovieComplete") && args && !IsBadReadPtr(args, 6) && !strncmp(args, "/mp_mc", 6)) g_loading = 1;
        if (!strcmp(cmd, "OnLoadComplete") && args && !IsBadReadPtr(args, 1)) {
            const char *nm = strstr(args, "name:="), *url = strstr(args, "url:=");
            if (nm && url) {
                const char *e = strchr(nm, ';'); int len = e ? (int)(e - nm - 6) : 0;
                if (len >= 9 && !strncmp(nm + 6 + len - 9, ".panel_mc", 9)) {
                    int sc = url_screen(url);
                    if (sc != SC_OTHER && (g_inWorld || g_loading)) { g_inWorld = 0; g_loading = 0; }
                    if (sc != g_screen && g_state == ST_WAIT) logmsg("screen %s", SC_NAME[sc]);
                    g_screen = sc;
                }
            }
        }
    }
    g_fsOrig(self, edx, movie, cmd, args);
}
static void __fastcall pre_hook(void *self, void *edx) { g_inWorld = 1; g_loading = 1; g_preOrig(self, edx); }
static void __fastcall post_hook(void *self, void *edx) { g_inWorld = 0; g_loading = 0; g_postOrig(self, edx); }

// ---------------- the file ----------------
// the header (as 0x42B259 reads it): u32 build, u32 data checksum, u32 0, "DOW2_REC" ... -> 0 fine, else the reason
static const char *rec_check(const unsigned char *h, DWORD n, unsigned mine, unsigned *theirs) {
    if (n < 0x54) return "bad_file";
    *theirs = *(const unsigned *)(h + 4);
    if (memcmp(h + 12, "DOW2_REC", 8)) return "bad_file";
    if (*(const unsigned *)h != REC_BUILD || *(const unsigned *)(h + 8) != 0) return "version";
    return *theirs == mine ? NULL : "data_mismatch";
}
static int copy_rec(unsigned mine) {                                  // the game's thread; 1 = ready to start
    HANDLE f = CreateFileW(g_src, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { char t[40]; snprintf(t, sizeof t, "error %lu", GetLastError()); ro_fail("not_found", t); return 0; }
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    if (sz.QuadPart < 0x54 || sz.QuadPart > (256LL << 20)) { CloseHandle(f); ro_fail("bad_file", "not a replay (size)"); return 0; }
    unsigned char *b = malloc((size_t)sz.QuadPart); DWORD got = 0;
    int r = b && ReadFile(f, b, (DWORD)sz.QuadPart, &got, NULL) && got == (DWORD)sz.QuadPart; CloseHandle(f);
    if (!r) { free(b); ro_fail("error", "could not read the file"); return 0; }
    unsigned theirs = 0; const char *why = rec_check(b, got, mine, &theirs);
    if (why && !strcmp(why, "data_mismatch")) {
        logmsg("the replay was recorded with game data %08X, this game has %08X (another Elite build of the same game 3.19) - the game itself refuses it", theirs, mine);
        char f8[820]; js_esc(f8, sizeof f8, g_fileU8);
        if (g_mismatchRefuse) { free(b); ro_fail("data_mismatch", "recorded with another build of the game's data (ReplayOpen.ini Mismatch=refuse)"); return 0; }
        emitf("{\"ev\":\"data_mismatch\",\"file\":\"%s\",\"replay\":\"%08X\",\"game\":\"%08X\"}", f8, theirs, mine);
        *(unsigned *)(b + 4) = mine; why = NULL;
        logmsg("played anyway with this game's checksum in the copy: it may go out of sync where the two builds differ in data");
    }
    if (why) {
        char t[120]; if (!strcmp(why, "version")) snprintf(t, sizeof t, "recorded by game build %u, this is %u (3.19)", *(unsigned *)b, REC_BUILD); else snprintf(t, sizeof t, "no DOW2 replay header");
        free(b); ro_fail(why, t); return 0; }
    HANDLE o = CreateFileW(g_dst, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); DWORD w = 0;
    r = o != INVALID_HANDLE_VALUE && WriteFile(o, b, got, &w, NULL) && w == got; if (o != INVALID_HANDLE_VALUE) CloseHandle(o);
    free(b);
    if (!r) { ro_fail("error", "could not write Playback\\launcher_replay.rec"); return 0; }
    logmsg("copied to Playback\\launcher_replay.rec (%lu bytes)", got);
    return 1;
}

// ---------------- the game's thread ----------------
typedef struct { int what; wchar_t file[MAX_PATH]; char u8[400]; } QCmd;
enum { CMD_PLAY = 1, CMD_CANCEL };
static QCmd g_cq[4]; static int g_cqH, g_cqN;
static void play_req(const QCmd *c, DWORD now) {
    if (g_state == ST_WAIT) { char f[820]; js_esc(f, sizeof f, c->u8); emitf("{\"ev\":\"failed\",\"file\":\"%s\",\"reason\":\"busy\",\"text\":\"another replay is starting\"}", f); return; }
    snprintf(g_fileU8, sizeof g_fileU8, "%s", c->u8);
    if (wcschr(c->file, L'\\') || wcschr(c->file, L'/') || wcschr(c->file, L':')) { wcsncpy(g_src, c->file, MAX_PATH - 1); g_src[MAX_PATH - 1] = 0; }
    else { _snwprintf(g_src, MAX_PATH, L"%ls\\%ls", g_pbDir, c->file); g_src[MAX_PATH - 1] = 0; }
    logmsg("play: %s", g_fileU8);
    ev("accepted");
    if (g_inWorld || g_loading) { ro_fail("busy_match", "the game is in a match or a replay"); return; }
    if (g_screen == SC_SETUP) { ro_fail("busy_lobby", "the game is in a lobby"); return; }
    if (GetFileAttributesW(g_src) == INVALID_FILE_ATTRIBUTES) { char t[40]; snprintf(t, sizeof t, "error %lu", GetLastError()); ro_fail("not_found", t); return; }
    set_state(ST_WAIT); g_reqAt = now; g_boxAt = 0;
}
static void game_step(DWORD now) {
    if (!g_firstFrame) g_firstFrame = now ? now : 1;
    if (g_state == ST_WAIT) {
        if (g_inWorld || g_loading) { ro_fail("busy_match", "a match or replay started meanwhile"); return; }
        if (g_screen == SC_SETUP) { ro_fail("busy_lobby", "the game went into a lobby"); return; }
        // the menu: a full-screen panel seen, or (Elite loads the add-ons after the menu's first panel: usually none seen) 10 s of frames
        if (g_screen == SC_UNKNOWN && now - g_firstFrame < 10000) return;
        if (now - g_reqAt > 120000) { ro_fail("timeout", "the game's menu did not come in 2 minutes"); return; }
        if (g_boxCfg && !g_boxAt) { g_boxAt = now; if (g_screen != SC_UNKNOWN || now - g_firstFrame >= 10000) connbox_open(0, "a replay from the launcher starts"); }
        if (connbox_is_open() && now - g_boxAt < 700) return;          // the player sees the box before the screen changes
        unsigned mine = ((unsigned (__cdecl *)(void))FN_DATACRC)();
        if (!copy_rec(mine)) return;
        connbox_close("the replay loads");
        char pb[] = "playback:launcher_replay.rec";
        g_ours = 1;
        char r = ((StartPb_t)FN_START_PB)(pb, (void *)G_GAMEOBJ_PTR);
        logmsg("replay start: %s", r ? "ok - the game loads it" : "REFUSED by the game");
        if (!r) { g_ours = 0; ro_fail("start", "the game refused the replay"); return; }
        set_state(ST_LOAD); ev("loading");
    } else if (g_state == ST_LOAD && g_inWorld) {
        char *st = ok((void *)G_SETTINGS, 4) ? *(char **)G_SETTINGS : NULL;
        if (st && ok(st, 0x120) && (*(int *)(st + 0x118) & T_PLAYBACK)) { set_state(ST_PLAY); ev("playing"); logmsg("the replay plays"); }
    } else if (g_state == ST_PLAY && !g_inWorld && !g_loading) { set_state(ST_IDLE); g_ours = 0; ev("over"); logmsg("the replay is over (left or ended)"); }
    else if (g_state == ST_LOAD && now - g_reqAt > 180000) { ro_fail("timeout", "the replay did not load in 3 minutes"); }
}
static void __stdcall upd_hook(float dt) {
    g_updOrig(dt);
    DWORD now = GetTickCount();
    for (;;) {
        QCmd c; int have = 0;
        EnterCriticalSection(&g_qlk); if (g_cqN) { c = g_cq[g_cqH]; g_cqH = (g_cqH + 1) % 4; g_cqN--; have = 1; } LeaveCriticalSection(&g_qlk);
        if (!have) break;
        if (c.what == CMD_PLAY) play_req(&c, now);
        else if (c.what == CMD_CANCEL && g_state == ST_WAIT) { logmsg("cancel (the launcher)"); ro_fail("cancelled", ""); }
        else if (c.what == CMD_CANCEL) logmsg("cancel (the launcher): %s - nothing to cancel (a replay that loads or plays is left in the game)", ST_NAME[g_state]);
    }
    game_step(now);
    if (connbox_is_open() && (g_inWorld || g_loading)) connbox_close("a world loads");
}

// ---------------- the pipe (as RoomJoin's / LiveReplay's: overlapped, several clients, never the engine) ----------------
// ---------------- the pipe (as RoomJoin's: overlapped, several clients, never the engine) ----------------
typedef struct { HANDLE h; OVERLAPPED ov; char rb[512]; char line[1024]; int len, drop, rd; DWORD id; } Cli;
static Cli g_c[MAXC]; static DWORD g_nextId = 1; static wchar_t g_pipeName[80];
static void cli_close(Cli *c, const char *why) {
    if (!c->h) return;
    logmsg("pipe: client %lu gone (%s)", c->id, why);
    if (c->rd) { DWORD n; CancelIoEx(c->h, &c->ov); GetOverlappedResult(c->h, &c->ov, &n, TRUE); c->rd = 0; }
    DisconnectNamedPipe(c->h); CloseHandle(c->h); c->h = NULL;
}
static int cli_write(Cli *c, const char *s) {
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
    char f[820]; EnterCriticalSection(&g_qlk); js_esc(f, sizeof f, g_fileU8);
    _snprintf(b, cap - 1, "{\"ev\":\"status\",\"ver\":\"" RO_VERSION "\",\"pid\":%lu,\"state\":\"%s\",\"file\":\"%s\",\"reason\":\"%s\",\"screen\":\"%s\",\"in_world\":%s}\n",
              GetCurrentProcessId(), ST_NAME[g_state], f, g_state == ST_FAILED ? g_reason : "", SC_NAME[g_screen], g_inWorld ? "true" : "false");
    LeaveCriticalSection(&g_qlk);
    b[cap - 1] = 0;
}
// a JSON string value with its escapes (\" \\ \/ \n \uXXXX, surrogate pairs) -> UTF-8; NULL = no such key / broken
static char *js_get(const char *s, const char *key, char *out, int cap) {
    char k[40]; snprintf(k, sizeof k, "\"%s\"", key); const char *p = strstr(s, k); if (!p) return NULL;
    p += strlen(k); while (*p == ' ' || *p == '\t') p++; if (*p != ':') return NULL; p++; while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return NULL; p++;
    int n = 0;
    while (*p && *p != '"') {
        unsigned c = (unsigned char)*p++;
        if (c == '\\') {
            char e = *p++; if (!e) return NULL;
            if (e == 'u') {
                unsigned u = 0; for (int i = 0; i < 4; i++) { char h = *p++; u = u * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 99); if (u > 0xFFFF) return NULL; }
                if (u >= 0xD800 && u < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
                    unsigned l = 0; for (int i = 2; i < 6; i++) { char h = p[i]; l = l * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 99); if (l > 0xFFFF) return NULL; }
                    if (l >= 0xDC00 && l < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (l - 0xDC00); p += 6; } }
                if (n + 4 >= cap) return NULL;
                if (u < 0x80) out[n++] = (char)u;
                else if (u < 0x800) { out[n++] = (char)(0xC0 | u >> 6); out[n++] = (char)(0x80 | (u & 63)); }
                else if (u < 0x10000) { out[n++] = (char)(0xE0 | u >> 12); out[n++] = (char)(0x80 | ((u >> 6) & 63)); out[n++] = (char)(0x80 | (u & 63)); }
                else { out[n++] = (char)(0xF0 | u >> 18); out[n++] = (char)(0x80 | ((u >> 12) & 63)); out[n++] = (char)(0x80 | ((u >> 6) & 63)); out[n++] = (char)(0x80 | (u & 63)); }
                continue;
            }
            c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'b' ? '\b' : e == 'f' ? '\f' : (unsigned char)e;
        }
        if (n + 1 >= cap) return NULL;
        out[n++] = (char)c;
    }
    if (*p != '"') return NULL;
    out[n] = 0; return out;
}
static void cli_line(Cli *c, char *s) {
    char cmd[16], file[400];
    if (!js_get(s, "cmd", cmd, sizeof cmd)) { cli_write(c, "{\"ev\":\"error\",\"reason\":\"bad_command\"}\n"); return; }
    if (!strcmp(cmd, "status")) { char b[1100]; status_line(b, sizeof b); cli_write(c, b); return; }
    QCmd q; memset(&q, 0, sizeof q);
    if (!strcmp(cmd, "play") && js_get(s, "file", file, sizeof file) && file[0]) {
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, file, -1, q.file, MAX_PATH)) { cli_write(c, "{\"ev\":\"error\",\"reason\":\"bad_command\"}\n"); return; }
        q.what = CMD_PLAY; snprintf(q.u8, sizeof q.u8, "%s", file);
    } else if (!strcmp(cmd, "cancel")) q.what = CMD_CANCEL;
    else { cli_write(c, "{\"ev\":\"error\",\"reason\":\"bad_command\"}\n"); return; }
    logmsg("pipe: client %lu: %s %s", c->id, cmd, q.u8);
    EnterCriticalSection(&g_qlk); if (g_cqN < 4) { g_cq[(g_cqH + g_cqN) % 4] = q; g_cqN++; } else q.what = 0; LeaveCriticalSection(&g_qlk);
    if (!q.what) cli_write(c, "{\"ev\":\"error\",\"reason\":\"queue_full\"}\n");
}
static int cli_read(Cli *c) {
    ResetEvent(c->ov.hEvent); DWORD n = 0;
    if (!ReadFile(c->h, c->rb, sizeof c->rb, &n, &c->ov) && GetLastError() != ERROR_IO_PENDING) { cli_close(c, "read failed"); return 0; }
    c->rd = 1; return 1;
}
static void cli_got(Cli *c) {
    DWORD n = 0; c->rd = 0;
    if (!GetOverlappedResult(c->h, &c->ov, &n, FALSE)) { DWORD e = GetLastError(); if (e != ERROR_MORE_DATA) { cli_close(c, "closed"); return; } }
    for (DWORD i = 0; i < n && c->h; i++) {
        char ch = c->rb[i];
        if (ch == '\n') { c->line[c->len] = 0; if (!c->drop && c->len) cli_line(c, c->line); c->len = 0; c->drop = 0; continue; }
        if (c->len < (int)sizeof c->line - 1) c->line[c->len++] = ch; else c->drop = 1;
    }
    if (c->h) cli_read(c);
}
static HANDLE pipe_new(SECURITY_ATTRIBUTES *sa, int first) {
    DWORD open = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
    DWORD mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT;
    HANDLE h = CreateNamedPipeW(g_pipeName, open, mode | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, sa);
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER) h = CreateNamedPipeW(g_pipeName, open, mode, PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, sa);
    return h;
}
static DWORD WINAPI pipe_thread(LPVOID _) {
    (void)_;
    _snwprintf(g_pipeName, 80, L"\\\\.\\pipe\\EliteReplayOpen-%lu", GetCurrentProcessId()); g_pipeName[79] = 0;
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, FALSE}; HANDLE tok; static unsigned char tu[256]; DWORD len = 0; wchar_t *sid = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        if (GetTokenInformation(tok, TokenUser, tu, sizeof tu, &len)) ConvertSidToStringSidW(((TOKEN_USER *)tu)->User.Sid, &sid);
        CloseHandle(tok);
    }
    if (sid) { wchar_t sddl[200]; _snwprintf(sddl, 199, L"D:P(A;;GA;;;%ls)", sid); sddl[199] = 0; LocalFree(sid);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL)) sa.lpSecurityDescriptor = NULL; }
    if (!sa.lpSecurityDescriptor) { logmsg("pipe: no security descriptor for the current user - no pipe"); return 0; }
    HANDLE lis = pipe_new(&sa, 1);
    if (lis == INVALID_HANDLE_VALUE) { logmsg("pipe: cannot create it (error %lu) - no pipe", GetLastError()); return 0; }
    OVERLAPPED lov; memset(&lov, 0, sizeof lov); lov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < MAXC; i++) g_c[i].ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    logmsg("pipe: EliteReplayOpen-%lu open (only this user, no remote clients)", GetCurrentProcessId());
    int pending = 0, connNow = 0;
    for (;;) {
        if (!pending && lis != INVALID_HANDLE_VALUE) {
            ResetEvent(lov.hEvent); connNow = 0;
            if (ConnectNamedPipe(lis, &lov)) { connNow = 1; SetEvent(lov.hEvent); }
            else { DWORD e = GetLastError(); if (e == ERROR_PIPE_CONNECTED) { connNow = 1; SetEvent(lov.hEvent); } else if (e != ERROR_IO_PENDING) { CloseHandle(lis); lis = INVALID_HANDLE_VALUE; } }
            pending = 1;
        }
        if (lis == INVALID_HANDLE_VALUE) { Sleep(1000); lis = pipe_new(&sa, 0); pending = 0; continue; }
        HANDLE hs[2 + MAXC]; int idx[2 + MAXC], n = 0;
        hs[n] = g_outEv; idx[n++] = -2; hs[n] = lov.hEvent; idx[n++] = -1;
        for (int i = 0; i < MAXC; i++) if (g_c[i].h) { hs[n] = g_c[i].ov.hEvent; idx[n++] = i; }
        DWORD r = WaitForMultipleObjects(n, hs, FALSE, INFINITE);
        if (r >= WAIT_OBJECT_0 + (DWORD)n) { Sleep(100); continue; }
        int k = idx[r - WAIT_OBJECT_0];
        if (k == -1) {
            DWORD dummy; pending = 0;
            if (!connNow && !GetOverlappedResult(lis, &lov, &dummy, FALSE)) { DisconnectNamedPipe(lis); continue; }
            int s = -1; for (int i = 0; i < MAXC; i++) if (!g_c[i].h) { s = i; break; }
            if (s < 0) { DisconnectNamedPipe(lis); continue; }
            Cli *c = &g_c[s]; c->h = lis; c->len = 0; c->drop = 0; c->rd = 0; c->id = g_nextId++;
            logmsg("pipe: client %lu connected", c->id);
            lis = pipe_new(&sa, 0);
            char b[600]; status_line(b, sizeof b);
            if (cli_write(c, b)) cli_read(c);
        } else if (k == -2) {
            for (;;) {
                char *o = NULL;
                EnterCriticalSection(&g_qlk); if (g_oqN) { o = g_oq[g_oqH]; g_oqH = (g_oqH + 1) % 32; g_oqN--; } LeaveCriticalSection(&g_qlk);
                if (!o) break;
                for (int i = 0; i < MAXC; i++) if (g_c[i].h) cli_write(&g_c[i], o);
                free(o);
            }
        } else cli_got(&g_c[k]);
    }
}

// ---------------- install ----------------
static int hook_fscmd(void) {
    volatile DWORD *slot = (volatile DWORD *)VT_FSCMD;
    if (IsBadReadPtr((void *)(VT_FSCMD - 4), 8) || slot[-1] != FN_FSCMD_VT0) return 0;
    return slot_hook(VT_FSCMD, FN_FSCMD, (void *)fs_hook, (void **)&g_fsOrig);
}
static DWORD WINAPI installer(LPVOID _) {
    (void)_; int sim = 0;
    for (int i = 0; i < 180; i++) {
        if (!sim && GetModuleHandleA("SimEngine.dll")) {
            int post = iat_hook(IAT_SIM_POST, (void *)post_hook, (void **)&g_postOrig);
            sim = iat_hook(IAT_SIM_PRE, (void *)pre_hook, (void **)&g_preOrig);
            logmsg("world hooks: %s / %s", sim ? "start" : "NO start", post ? "end" : "NO end"); sim = 1;
        }
        if (GetModuleHandleA("Shark.dll") && iat_hook(IAT_SHARK_UPDATE, (void *)upd_hook, (void **)&g_updOrig)) {
            logmsg("frame hook installed"); return pipe_thread(NULL);
        }
        Sleep(1000);
    }
    logmsg("frame hook NOT installed - no pipe"); return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID _) {
    (void)_;
    if (r != DLL_PROCESS_ATTACH) return TRUE;                          // DETACH: nothing (the pipe thread is never waited for)
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_lk); InitializeCriticalSection(&g_qlk);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0; else g_dir[0] = 0;
    log_open();
    logmsg("ReplayOpen " RO_VERSION " (a replay from the launcher: the pipe EliteReplayOpen-%lu)", GetCurrentProcessId());
    { char n[64]; wsprintfA(n, "Local\\dow2patch_ReplayOpen_%lu", GetCurrentProcessId());
      HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx && GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("another ReplayOpen copy is active, this one stays idle"); return TRUE; } }
    wchar_t ini[MAX_PATH], v[32]; _snwprintf(ini, MAX_PATH, L"%lsReplayOpen.ini", g_dir); ini[MAX_PATH - 1] = 0;
    if (!GetPrivateProfileIntW(L"ReplayOpen", L"Enabled", 1, ini)) { logmsg("disabled in ReplayOpen.ini - no hooks"); return TRUE; }
    g_boxCfg = GetPrivateProfileIntW(L"ReplayOpen", L"ConnectingBox", 1, ini) != 0;
    GetPrivateProfileStringW(L"ReplayOpen", L"Mismatch", L"play", v, 32, ini); g_mismatchRefuse = !_wcsicmp(v, L"refuse");
    wchar_t docs[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs) != S_OK) { logmsg("no Documents folder - nothing changed"); return TRUE; }
    _snwprintf(g_pbDir, MAX_PATH, L"%ls\\My Games\\Dawn of War II - Retribution\\Playback", docs); g_pbDir[MAX_PATH - 1] = 0;
    _snwprintf(g_dst, MAX_PATH, L"%ls\\launcher_replay.rec", g_pbDir); g_dst[MAX_PATH - 1] = 0;
    // DOW2.exe 3.19 where we call: the replay start, the checksum, the fscommand vtable
    int good = !IsBadReadPtr((void *)FN_START_PB, 5) && !memcmp((void *)FN_START_PB, "\xB8\x72\x80\xF1\x00", 5)
        && !IsBadReadPtr((void *)FN_DATACRC, 7) && !memcmp((void *)FN_DATACRC, "\x6A\xFF\x68\x69\xF9\xEB\x00", 7);
    if (!good) { logmsg("not DOW2.exe 3.19 (the replay start / data checksum code differ) - nothing changed"); return TRUE; }
    logmsg("screens %d; Connecting box %s; another build's replay: %s", hook_fscmd(), g_boxCfg && connbox_check() ? "on" : "off",
           g_mismatchRefuse ? "refused (Mismatch=refuse)" : "played with this game's checksum (Mismatch=play)");
    g_outEv = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 64 * 1024, installer, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL); if (t) CloseHandle(t);
    return TRUE;
}
