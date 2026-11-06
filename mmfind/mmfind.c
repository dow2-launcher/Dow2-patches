// MMFind.dll - the Elite automatch on our servers for DoW2 Retribution / Elite (DOW2.exe 3.19).
//
// 0.46 (owner 05.11.2026: "the automatch always goes through our servers and over UDP"; the separate tick "Games through our
// servers" - relay_r2, Relay.dll - is gone from the store): Relay.dll is part of the automatch package now (store automatch:
// MMFind.dll + OwnLobby.dll + Relay.dll + the early winmm.dll) and MMFind loads it from its own folder at the start (Elite's
// injector only loads the one DLL each add-on names in InjectedFiles - for the automatch that is MMFind.dll; in vanilla the
// launcher's loader loads every DLL of dow2-patches\ anyway, then this is only one more reference). Relay.dll is the same code
// as before (relay/relay.c, Relay 23): a game of our lobby server's room (the game connects to one of our nodes on the battle
// port) goes over our UDP tunnel to that node, or through the player's nearest node when he can't reach it; its pings to our
// nodes go to the matchmaker with mm/native (relay_ping), so the ladder puts the match on the node best for all. Relic's games
// stay Relic's: forwarding them is RelayForward.dll's job (relay_fwd); whichever of the two loads first carries the connections
// of both (one hook, never two).
// 0.45 (owner 03.11.2026: "the automatch is tested - remove the old one, keep only the new one"): only the game's OWN Find a Game
// search runs, served by our lobby server (OwnLobby.dll + own_lobby.py) from the ladder's matchmaker: the game hosts / joins the
// AUTOMATCH room and starts by itself. MMFind walks no menus, draws no cover, makes no public rooms - the classic flow of 0.12-0.44
// (menus walked, MM-<match> rooms, mm/join) and the 0.43 menu rematch are gone.
// Start Searching first asks the ladder (mm/native). "No" (the game is not signed in through our lobby server, the lobby server or
// the ladder does not answer, the native automatch is off on the server) = the click is NOT passed to the game: Relic's own
// matchmaker matches nobody any more (owner: never start it). The Find a Game screen says why and what to do; the next click asks
// again. A search of the game that runs anyway without our lobby server behind it is stopped at once (Stop Searching).
// What MMFind still does: the ladder's word before the search, the faster poll (1 s) and countdown (2 s) while the native search
// runs, the kind of search / queue / online on the screen, the saved race / hero / mode / map wishes, searching again after a
// match the server called off, the replay of every multiplayer match to the replay site.
//
// 0.47 (owner 05.11.2026): a match from a launcher room - the launcher orders it over the pipe \\.\pipe\EliteMMFind-<pid>: MMFind
// walks to Find a Game, sets the room's mode, race and hero and searches with the room's id (the ladder makes the match from the
// room). See "a match from a launcher room" below.
//
// Hooks: Invoke 0x45DB6C (engine -> Flash, to read lists and to show our status), fscommand handler 0x681AB0 (Flash -> engine, to
// catch the button and to send menu commands), Shark.dll shark::update (main-thread tick).
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600                       // 0.47: CancelIoEx (the pipe) - Vista+, as RoomJoin
#endif
#include <windows.h>
#include <winhttp.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>
#include <shlobj.h>
#include <sddl.h>

#define HOOK_ADDR   0x0045DB6C
#define HOOK_RESUME 0x0045DB71
static const unsigned char PROLOGUE[5] = {0xB8, 0x59, 0x70, 0xEF, 0x00};
#define FN_FSCMD    0x00681AB0
static const unsigned char FSCMD_PRO[7] = {0x51, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x14};
#define IAT_SHARK_UPDATE 0x00F88068

#ifndef MM_HOST
#define MM_HOST L"rts.fintebtc.de;dow2.fintebtc.de;dow2ru.fintebtc.de"   // mirrors in order; dow2ru for players without Hetzner
#endif
#define MM_PATH L"/ladder/api/mm/"

typedef struct { int type; int pad; union { double num; unsigned char b; const wchar_t *ws; const char *s; } v; } GFxValue;
enum { GFX_BOOL = 2, GFX_NUMBER = 3, GFX_STRING = 4, GFX_WSTRING = 5 };
typedef char (__fastcall *InvokeFn)(void *self, void *edx, const char *method, int flags, GFxValue *args, unsigned count);
typedef void (__fastcall *FsCmdFn)(void *self, void *edx, void *movie, const char *cmd, const char *args);
typedef void (__stdcall *UpdFn)(float dt);
static InvokeFn g_invOrig; static FsCmdFn g_fsOrig; static UpdFn g_updOrig;

static wchar_t g_dir[MAX_PATH], g_server[128] = MM_HOST; static CRITICAL_SECTION g_lk; static int g_verbose, g_logInvoke;
static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsMMFind.log", g_dir);
    EnterCriticalSection(&g_lk);
    FILE *f = _wfopen(p, L"a"); if (f) {
        SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_lk);
}

// ---------------- UI tracking (filled from the hooks, read on the main thread) ----------------
enum { SC_OTHER, SC_MAIN, SC_MM, SC_LIST, SC_SETUP };
static volatile int g_screen;
static char g_panel[160];                 // path of the current full-screen panel, e.g. _level0.display_item.panel2_mc.panel_mc
static void *g_fsSelf, *g_fsMovie;        // fscommand handler + frontend movie, taken from real button presses
static volatile int g_heroShown;          // hero selection is on screen
static volatile DWORD g_heroTick, g_scrTick;   // the hero panel's last message / the last screen change
static volatile int g_humans;             // people in the last Custom Games lobby (its player list): a match with people = a replay
static wchar_t g_myName[64];              // our gamer tag
// modes: Match Type list on Find a Game = Head to Head / Two on Two / Team Battle / Team Free For All (the game's own, not ours)
int g_mode;                               // 0 1v1, 1 2v2, 2 3v3, 3 Team FFA (no Elite automatch for it)
static const char *MODE_KEY[] = {"1v1", "2v2", "3v3", "ffa"};
static const wchar_t *MODE_W[] = {L"1v1", L"2v2", L"3v3", L"Watch"};   // [Maps] Off<mode> keys of MMFind.ini (as before)
static const wchar_t *MODE_PREFIX[] = {L"(2p)", L"(4p)", L"(6p)", L"(6p)"};
// map catalog: the lobby map list as the game sends it (all Elite maps, same order as the lobby drop-down)
#define MAX_BANS 3                // maps a player may uncheck per mode (ladder rule)
#define MAXMAPS 160
static wchar_t g_maps[MAXMAPS][64]; static int g_nmaps;
static unsigned char g_off[4][MAXMAPS];  // 1 = player unchecked this map for the mode
static int map_index(const wchar_t *n) { for (int i = 0; i < g_nmaps; i++) if (!wcscmp(g_maps[i], n)) return i; return -1; }
static int mode_map(int i, int mode) { return !wcsncmp(g_maps[i], MODE_PREFIX[mode], 4); }

// ---------------- ActionScript access (Scaleform 2.x inside DOW2.exe 3.19, as in LobbyStats 0.6) ----------------
#define FN_STR_NEW   0x00DB64D0
#define FN_STR_FREE  0x00DB62D0
#define FN_RESOLVE   0x00DF9520
#define FN_AS_CALL   0x00DD27E0
#define FN_STK_GROW  0x00DB7950
#define FN_STK_POP   0x00DB7BE0
#define FN_VAL_FREE  0x00DBA000
typedef struct { unsigned char b[16]; } ASValue;
typedef void *(__fastcall *StrNewFn)(void *mgr, void *edx, const char *s);
typedef void  (__fastcall *ThisFn)(void *self, void *edx);
typedef void *(__fastcall *EnvFn)(void *sprite, void *edx);
typedef char  (__fastcall *ResolveFn)(void *env, void *edx, void **node, ASValue *out, int, int, int, int);
typedef char  (__fastcall *AsCallFn)(void *sprite, void *edx, const char *path, ASValue *res, int n);
typedef void  (__fastcall *PopFn)(void *stk, void *edx, int n);
typedef char  (__fastcall *GetVarFn)(void *movie, void *edx, GFxValue *out, const char *path);              // GFxMovie vtbl+0x30
typedef char  (__fastcall *SetVarFn)(void *movie, void *edx, const char *path, const GFxValue *v, int type); // GFxMovie vtbl+0x2C
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
static void as_setb(void *movie, const char *path, int b) { GFxValue v; memset(&v, 0, sizeof v); v.type = GFX_BOOL; v.v.b = b; as_set(movie, path, &v); }

// ---------------- the search ----------------
// idle -> (Start Searching) native_ask: the ladder's word (mm/native) -> native: the game's own search on our lobby server, MMFind
// only watches the ladder -> done: the match loads (back to idle in the menu after it). A "no" leaves the game where it was.
enum { S_IDLE, S_NASK, S_NATIVE, S_DONE };
static const char *SN[] = {"idle", "native_ask", "native", "done"};
static volatile int g_state;
static DWORD g_stateTick, g_searchTick;
static volatile int g_queue = -1;
static int jstr(const char *j, const char *key, char *out, int cap);
unsigned long long g_mySteam;
static volatile int g_wantLeave, g_wantEnded, g_loading;
static int g_maxDiff; static char g_group[32]; static char g_game[24] = "elite";
// 0.47: a match from a launcher room (see "a match from a launcher room" below); declared here, the search body and the screen use it
enum { RS_NONE, RS_STOP, RS_WALK, RS_ARMY, RS_SEARCH, RS_DONE };
static const char *RSN[] = {"none", "stop", "walk", "army", "search", "done"};
static volatile int g_rs; static char g_room[24]; static int g_rMode, g_rRace = -1, g_rHero = -1, g_rTries, g_rWalked;
static DWORD g_rAt, g_rStepAt; static volatile int g_rEvMatched, g_rEvRoom;
static CRITICAL_SECTION g_qlk; static HANDLE g_outEv;
static int g_pipeCfg = 1, g_debugPipe;
static int g_rOpt[4] = {-1, -1, -1, -1};      // 0.47: the room's options for the host: victory condition, VPTK, SLOC, RSST (-1 = the game's)
static void room_set(int rs); static void room_ev(const char *ev);
// 0.38 (owner 03.11.2026: "Start Searching straight to the loading screen, fully on our servers"): with OwnLobby.dll in the game and
// our lobby server able to do it (the ladder says so: mm/native), Start Searching goes to the game: its own Find a Game search runs
// against our lobby server, which answers it from the ladder's matchmaker. MMFind watches the ladder meanwhile: no native search
// there for 10 s (our lobby server away, the ladder away) -> 0.45: the game's search is stopped and the screen says so.
static volatile int g_natAnswer;            // 0 waiting, 1 native, 2 not native (g_natWhy)
static volatile DWORD g_natPass, g_natGood; static volatile int g_natSeen, g_natCancelled;   // pass-through time, last native answer of the ladder
static volatile int g_engSearch = -1;       // the engine's own search state (Receive_ShowSearchingAnimation as the engine sends it), -1 unknown
static DWORD g_stopGrace;                   // 0.45: no stray-search stop before this (the game is still ending a native search of ours)
static char g_natLobby[48], g_natWhy[96], g_natLastCancel[32];
// 0.42 (owner 03.11.2026: "when we press search, show right away which search it is"): the kind of search, text fields of ours in
// the game's own Find a Game panel (docs/UI_RULES.md: the game's interface, no Direct3D): a head right of the screen title and an
// info block under its left box - "Automatch: our servers", or 0.45 "The Elite automatch is not available:" with the reason and
// what to do, "MMFind 0.45", the queue and who is online. Shown from the moment the screen opens (a peek at mm/native: the ladder
// answers as for Start Searching and changes nothing; again every 20 s and on a match type change while the screen is open), after
// the click by what really happened. [MMFind] ShowMode=1 (default) / 0 = off, Lang=auto (the game's own text: Cyrillic = Russian) / ru / en.
#define MMF_VER "0.47"
#define MMF_VER_W L"0.47"
static int g_showMode = 1, g_langCfg = -1, g_lang = -1, g_modeProbe;
static double g_mx = 1065, g_my = 590, g_msz = 30;           // panel units, see mode_make
static volatile int g_peekWant, g_peekAns; static volatile DWORD g_peekLast; static char g_peekWhy[96];
static char g_runWhy[96];                                     // 0.45: why the last Start Searching did not search (shown until it can)
static volatile int g_liveOnline = -1, g_liveQueue = -1;      // mm/hello (net_worker, every 5 s on Find a Game)
// the panel's own dialogs over it, from the engine's calls and the buttons (the hero panel keeps _visible true when it closes,
// and no HideHeroSelect comes on Find a Game): Receive_ShowHeroSelect .. hero_selection_mc.OnClickAccept, Map Preferences .. Save / Cancel
static volatile int g_heroUp, g_mapUp;
// a native match called off by the server while the game was setting it up (host never came, a guest never joined, ...): the game
// ends its own search and goes back to Find a Game; the ladder says {"state":"cancelled","again":true} to everyone who was not the
// reason - MMFind presses Start Searching again for them (the native path), at most once per 30 s and 3 times in a row
static volatile int g_natAgain, g_reWant, g_reBoxOk; static volatile DWORD g_natWatchUntil; static DWORD g_reSince, g_reLast; static int g_reRow;
// 0.33: vanilla and Elite players never share a queue (the ladder keeps one queue per game). Every Elite start has "-modname Elite"
// on the command line (players' warnings.txt: RUN OPTIONS); a start without it is vanilla even when an Elite copy of this DLL got
// loaded (Elite's own Profiler.dll replaces the game's one and is loaded in vanilla too - not a sign of Elite).
static void detect_game(void) {
    wchar_t cmd[MAX_PATH]; wcsncpy(cmd, GetCommandLineW(), MAX_PATH - 1); cmd[MAX_PATH - 1] = 0; _wcslwr(cmd);
    int elite = wcsstr(cmd, L"-modname elite") != NULL;
    const char *g = elite ? "elite" : "vanilla";
    if (strcmp(g, g_game)) logmsg("game: %s", g);
    strcpy(g_game, g);
}
static const char *RACES[] = {"chaos_space_marines","eldar","imperial_guard","ordo_malleus","orks","space_marines","tau","tyranids","necron"};
static HANDLE g_wake;
static int g_sending;                       // our own fscommand to the engine is on the way (not the player's)
static DWORD g_banNote;                     // Map Preferences: more than MAX_BANS unchecked - a note on the status line

static void native_fast(int on);
static void set_state(int s) {
    logmsg("state %s -> %s", SN[g_state], SN[s]);
    if (g_state == S_NATIVE && s != S_NATIVE) { native_fast(0); g_stopGrace = GetTickCount() + 5000; }   // the game ends its search by itself
    g_state = s; g_stateTick = GetTickCount();
}
static DWORD in_state(void) { return GetTickCount() - g_stateTick; }
// Every patch of the game's code or import table goes through here: the mutex all our DLLs share (one patch at a time in the
// game process - two threads opening a page for writing and closing it again crashed the game at start), the bytes written
// with WriteProcessMemory (fails instead of crashing if the page is closed under us), the instruction cache flushed.
static HANDLE patch_lock(void) {
    char n[64]; wsprintfA(n, "Local\\dow2patch_iat_%lu", GetCurrentProcessId());
    HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx) WaitForSingleObject(mx, 5000); return mx;
}
static void patch_unlock(HANDLE mx) { if (mx) { ReleaseMutex(mx); CloseHandle(mx); } }
static int code_jmp(unsigned char *at, void *dest, int len) {   // "jmp dest" at `at`, the rest of `len` bytes NOPs
    unsigned char b[16]; if (len < 5 || len > 16) return 0;
    b[0] = 0xE9; *(DWORD *)(b + 1) = (DWORD)dest - (DWORD)(at + 5); for (int i = 5; i < len; i++) b[i] = 0x90;
    HANDLE mx = patch_lock(); DWORD o; SIZE_T w = 0; int ok = 0;
    if (VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &o)) {
        ok = WriteProcessMemory(GetCurrentProcess(), at, b, len, &w) && w == (SIZE_T)len;
        VirtualProtect(at, len, o, &o);
    }
    FlushInstructionCache(GetCurrentProcess(), at, len);
    patch_unlock(mx);
    return ok;
}

// 0.39 (owner 03.11.2026: "the ~12 s polling - make it 1 second like ours; cut the 5 s countdown to 2 s"): NativeFast=1 (default) -
// while the game's own automatch runs on our servers (S_NATIVE: from the pass-through of Start Searching to the loading screen)
// two changes of DOW2.exe 3.19, both or none (every byte checked first), two small detours that do nothing unless g_fastOn:
// 1. WorldwideAutomatchService::Process 0x5fe140 polls automatch/polling when MORE than 10 s (whole seconds, _time64) passed since
//    the last poll (`cmp dword [esp+0x10], 0xA; jbe` at 0x5fe29e) -> every second while SEARCHING (not in a room). In the room it
//    stays 10 s: stands 03.11 - a poll every second there keeps the automatcher busy (IsBusy: AutomatchPollAsync) and the host
//    readied up 5-6 s late; the room needs no polls anyway (the server pushes).
// 2. SetupArbitrator (0x44917a, "GameMode is ranked, starting countdown"): the ranked countdown length +0x48 (Find a Game's form
//    0x475b5d builds it with 5 s) -> 2 s at the moment the countdown starts (mode 1; mode 3 keeps its 30 s). The start itself is
//    untouched: at 0 the arbitrator still waits for AllPlayersValidated and AreAllPlayersMatchInfoACKed (0x44971d).
#define POLL_AT    0x005FE29E
#define POLL_POLL  0x005FE2A9     // go on to the poll
#define POLL_SKIP  0x005FE470     // no poll now
#define CD_AT      0x004491BE
#define CD_BACK    0x004491D0
static const unsigned char POLL_BYTES[11] = {0x83, 0x7C, 0x24, 0x10, 0x0A, 0x0F, 0x86, 0xC7, 0x01, 0x00, 0x00};   // jbe 0x5fe470
static const unsigned char CD_BYTES[18] = {0x83, 0x7E, 0x54, 0x03, 0x59, 0x75, 0x0B, 0x83, 0x66, 0x4C, 0x00, 0xC7, 0x46, 0x48, 0x1E, 0x00, 0x00, 0x00};
static int g_fastCfg = 1, g_fastOk = -1;     // -1 not tried, 0 not possible (nothing changed), 1 both detours in
static volatile int g_fastOn;                 // read by the detours
static int code_bytes(unsigned char *at, const unsigned char *b, int len) {
    HANDLE mx = patch_lock(); DWORD o; SIZE_T w = 0; int ok = 0;
    if (VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &o)) { ok = WriteProcessMemory(GetCurrentProcess(), at, b, len, &w) && w == (SIZE_T)len; VirtualProtect(at, len, o, &o); }
    FlushInstructionCache(GetCurrentProcess(), at, len); patch_unlock(mx); return ok;
}
static int fast_detours(void) {
    unsigned char *pa = (unsigned char *)POLL_AT, *cd = (unsigned char *)CD_AT;
    if (IsBadReadPtr(pa, sizeof POLL_BYTES) || IsBadReadPtr(cd, sizeof CD_BYTES) || memcmp(pa, POLL_BYTES, sizeof POLL_BYTES) || memcmp(cd, CD_BYTES, sizeof CD_BYTES)) {
        logmsg("native fast: DOW2.exe bytes differ (not 3.19?) - the poll and the countdown left as they are"); return 0; }
    unsigned char *c = VirtualAlloc(NULL, 160, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); int k = 0, poll, cdn;
    if (!c) { logmsg("native fast: no memory for the detours - nothing changed"); return 0; }
#define B(x) c[k++] = (unsigned char)(x)
#define D32(v) do { DWORD v_ = (DWORD)(v); memcpy(c + k, &v_, 4); k += 4; } while (0)
#define REL(target) do { DWORD r_ = (DWORD)(target) - (DWORD)(c + k + 4); memcpy(c + k, &r_, 4); k += 4; } while (0)
    poll = k;                                                         // --- the poll interval (ebx = session manager, eax/al live)
    B(0x80); B(0x3D); D32(&g_fastOn); B(0x00);                        // cmp byte [g_fastOn],0
    B(0x74); B(0x20);                                                 // je stock (+32)
    B(0x50); B(0x51); B(0x52);                                        // push eax, ecx, edx
    B(0x8B); B(0x13); B(0x53); B(0xFF); B(0x52); B(0x70);             // mov edx,[ebx]; push ebx; call [edx+70h] (IsConnected)
    B(0x84); B(0xC0);                                                 // test al,al
    B(0x5A); B(0x59); B(0x58);                                        // pop edx, ecx, eax (flags kept)
    B(0x75); B(0x10);                                                 // jne stock (+16): in a room - 10 s as the game has it
    B(0x83); B(0x7C); B(0x24); B(0x10); B(0x00);                      // cmp dword [esp+10h],0  (searching: every second)
    B(0x0F); B(0x86); REL(POLL_SKIP);                                 // jbe skip
    B(0xE9); REL(POLL_POLL);                                          // jmp poll
    B(0x83); B(0x7C); B(0x24); B(0x10); B(0x0A);                      // stock: cmp dword [esp+10h],0Ah
    B(0x0F); B(0x86); REL(POLL_SKIP);                                 // jbe skip
    B(0xE9); REL(POLL_POLL);                                          // jmp poll
    cdn = k;                                                          // --- the countdown (esi = SetupArbitrator)
    B(0x59);                                                          // pop ecx (the original's, after the trace call)
    B(0x83); B(0x7E); B(0x54); B(0x03); B(0x75); B(0x10);             // cmp [esi+54h],3 ; jne +16
    B(0x83); B(0x66); B(0x4C); B(0x00); B(0xC7); B(0x46); B(0x48); D32(0x1E);   // and [esi+4Ch],0 ; mov [esi+48h],30
    B(0xE9); REL(CD_BACK);
    B(0x80); B(0x3D); D32(&g_fastOn); B(0x00);                        // cmp byte [g_fastOn],0
    B(0x0F); B(0x84); REL(CD_BACK);                                   // je back
    B(0x83); B(0x7E); B(0x54); B(0x01);                               // cmp [esi+54h],1 (ranked)
    B(0x0F); B(0x85); REL(CD_BACK);                                   // jne back
    B(0x83); B(0x66); B(0x4C); B(0x00); B(0xC7); B(0x46); B(0x48); D32(2);      // and [esi+4Ch],0 ; mov [esi+48h],2
    B(0xE9); REL(CD_BACK);
#undef B
#undef D32
#undef REL
    unsigned char j1[sizeof POLL_BYTES], j2[sizeof CD_BYTES];
    j1[0] = 0xE9; *(DWORD *)(j1 + 1) = (DWORD)(c + poll) - (DWORD)(pa + 5); memset(j1 + 5, 0x90, sizeof j1 - 5);
    j2[0] = 0xE9; *(DWORD *)(j2 + 1) = (DWORD)(c + cdn) - (DWORD)(cd + 5); memset(j2 + 5, 0x90, sizeof j2 - 5);
    if (!code_bytes(cd, j2, sizeof j2)) { logmsg("native fast: the countdown detour could not be written - nothing changed"); return 0; }
    if (!code_bytes(pa, j1, sizeof j1)) { code_bytes(cd, CD_BYTES, sizeof CD_BYTES); logmsg("native fast: the poll detour could not be written - nothing changed"); return 0; }
    logmsg("native fast: detours in (poll 0x%X, countdown 0x%X)", POLL_AT, CD_AT);
    return 1;
}
static void native_fast(int on) {
    if (!g_fastCfg || !on == !g_fastOn) return;
    if (on && g_fastOk < 0) g_fastOk = fast_detours();
    if (g_fastOk != 1) return;
    g_fastOn = on;
    logmsg(on ? "native fast ON: automatch/polling every 1 s while searching (was 10+ s), ranked countdown 2 s (was 5 s), the start still waits for every player's ACK"
              : "native fast off: polling every 10+ s, countdown 5 s (as the game has it)");
}
// ---------------- HTTP (worker thread) ----------------
// the ladder API through the mirrors (Server=host;host;...): stay on the one that answered, go to the next when it can't be reached
static wchar_t g_hosts[4][64]; static int g_nHosts, g_curHost;
static void parse_hosts(void) {
    static wchar_t b[128]; wcscpy(b, g_server); g_nHosts = 0;
    for (wchar_t *t = wcstok(b, L";, "); t && g_nHosts < 4; t = wcstok(NULL, L";, ")) wcsncpy(g_hosts[g_nHosts++], t, 63);
    if (!g_nHosts) { wcscpy(g_hosts[0], L"rts.fintebtc.de"); g_nHosts = 1; }
}
// 0.38 test option [MMFind] TestLadder=host:port: every ladder API call (/ladder/api/mm/...) goes there over plain HTTP, no
// mirrors (the TEST ladder listens on 127.0.0.1 only, reached through an ssh tunnel); the replay upload and the relay list stay
static wchar_t g_tlHost[64]; static INTERNET_PORT g_tlPort;
static int http_post1x(const wchar_t *host, INTERNET_PORT port, int secure, const wchar_t *path, const char *body, char *out, int cap, int tmo) {
    out[0] = 0; int got = 0, answered = 0;
    HINTERNET s = WinHttpOpen(L"MMFind/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0); if (!s) return 0;
    WinHttpSetTimeouts(s, tmo, tmo, tmo, tmo);
    HINTERNET c = WinHttpConnect(s, host, port, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", path, NULL, NULL, NULL, secure ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (r && WinHttpSendRequest(r, L"Content-Type: application/json\r\n", (DWORD)-1, (void *)body, (DWORD)strlen(body), (DWORD)strlen(body), 0)
          && WinHttpReceiveResponse(r, NULL)) {
        DWORD code = 0, sz = sizeof code; WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
        answered = code && code < 500;                                // a 502 from a mirror: its way to the ladder is broken
        DWORD n; while (got < cap - 1 && WinHttpReadData(r, out + got, cap - 1 - got, &n) && n) got += n;
    }
    out[got] = 0;
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return answered && got > 0;
}
static int http_post1(const wchar_t *host, const wchar_t *path, const char *body, char *out, int cap, int tmo) {
    return http_post1x(host, INTERNET_DEFAULT_HTTPS_PORT, 1, path, body, out, cap, tmo);
}
// 0.36 (02.11.2026): the game servers from the ladder into Relay.ini [Relay] Nodes= (Relay 16 adds them to its ping set, so
// the matchmaker can pick a mirror added on the ladder without a client update). Sources: the "relays" field of the ladder's
// mm/poll ("relays": ["ip", ...]) and, at the start, GET /ladder/api/relays ({"relays":[{"ip":...}, ...]}) - both over HTTPS
// from our hosts. Only IPv4 addresses are taken, 8 at most; written only when the list changed.
static void nodes_from_json(const char *j) {
    const char *r = strstr(j, "\"relays\""); if (!r) return;
    const char *end = strchr(r, ']'); if (!end) return;
    char list[160] = ""; int L = 0, n = 0;
    for (const char *p = r + 8; p < end && n < 8; p++) {
        if (*p != '"') continue;
        const char *q = strchr(p + 1, '"'); if (!q || q > end) break;
        int len = (int)(q - p - 1), a, b, c, d; char tail;
        if (len > 6 && len < 16) {
            char ip[16]; memcpy(ip, p + 1, len); ip[len] = 0;
            if (sscanf(ip, "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) == 4 && a > 0 && a < 256 && b >= 0 && b < 256 && c >= 0 && c < 256 && d >= 0 && d < 256) {
                L += snprintf(list + L, sizeof list - L, "%s%s", n ? ";" : "", ip); n++;
            }
        }
        p = q;
    }
    if (!n) return;
    wchar_t ini[MAX_PATH], cur[200], want[200]; _snwprintf(ini, MAX_PATH, L"%lsRelay.ini", g_dir);
    MultiByteToWideChar(CP_UTF8, 0, list, -1, want, 200);
    GetPrivateProfileStringW(L"Relay", L"Nodes", L"", cur, 200, ini);
    if (!wcscmp(cur, want)) return;
    if (WritePrivateProfileStringW(L"Relay", L"Nodes", want, ini)) logmsg("game servers from the ladder: %s (Relay.ini Nodes=)", list);
}
static DWORD WINAPI nodes_fetch(LPVOID _) {               // once at the start, before the player can search
    char out[4096];
    for (int k = 0; k < g_nHosts; k++) {
        out[0] = 0; int got = 0;
        HINTERNET s = WinHttpOpen(L"MMFind/" MMF_VER_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0); if (!s) return 0;
        WinHttpSetTimeouts(s, 4000, 4000, 4000, 4000);
        HINTERNET c = WinHttpConnect(s, g_hosts[k], INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/ladder/api/relays", NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
        DWORD code = 0, sz = sizeof code;
        if (r && WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
            DWORD n; while (got < (int)sizeof out - 1 && WinHttpReadData(r, out + got, sizeof out - 1 - got, &n) && n) got += n;
        }
        out[got] = 0;
        if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
        if (code == 200 && strstr(out, "\"relays\"")) { nodes_from_json(out); return 0; }
    }
    logmsg("game servers from the ladder: not reachable now (Relay.ini Nodes= stays)");
    return 0;
}
static int http_post(const wchar_t *ep, const char *body, char *out, int cap) {
    wchar_t path[128]; _snwprintf(path, 128, L"%ls%ls", MM_PATH, ep);
    if (g_tlHost[0]) {                                                 // test ladder: plain HTTP, one place
        if (http_post1x(g_tlHost, g_tlPort, 0, path, body, out, cap, 4000)) { nodes_from_json(out); return 1; }
        out[0] = 0; return 0; }
    static const int TMO[2] = {1000, 4000};                          // first round quick (1 s per step), then a patient one (4 s)
    for (int round = 0; round < 2; round++)
        for (int k = 0; k < g_nHosts; k++) {
            int h = (g_curHost + k) % g_nHosts;
            if (http_post1(g_hosts[h], path, body, out, cap, TMO[round])) {
                if (h != g_curHost) { logmsg("server: %ls is not reachable, now using %ls", g_hosts[g_curHost], g_hosts[h]); g_curHost = h; }
                nodes_from_json(out);                                  // 0.36: the ladder's answers carry the game servers
                return 1;
            }
        }
    out[0] = 0; return 0;
}
// 0.38: a quick call (the native decision, the native watch): the mirrors in turn within about `budget` ms in all
static int http_quick(const wchar_t *ep, const char *body, char *out, int cap, DWORD budget) {
    wchar_t path[128]; _snwprintf(path, 128, L"%ls%ls", MM_PATH, ep);
    DWORD t0 = GetTickCount();
    if (g_tlHost[0]) {                                                 // test ladder: plain HTTP, one place
        if (http_post1x(g_tlHost, g_tlPort, 0, path, body, out, cap, (int)budget / 2)) { nodes_from_json(out); return 1; }
        out[0] = 0; return 0; }
    for (int k = 0; k < g_nHosts; k++) {
        DWORD used = GetTickCount() - t0; if (used + 150 > budget) break;
        int h = (g_curHost + k) % g_nHosts, tmo = (int)(budget - used) / 2;   // WinHttp's timeout is per step (connect, send, receive)
        if (http_post1(g_hosts[h], path, body, out, cap, tmo < 300 ? 300 : tmo)) {
            if (h != g_curHost) { logmsg("server: %ls is not reachable, now using %ls", g_hosts[g_curHost], g_hosts[h]); g_curHost = h; }
            nodes_from_json(out); return 1;
        }
    }
    out[0] = 0; return 0;
}
static int jtrue(const char *j, const char *key) { char a[64], b[64]; snprintf(a, sizeof a, "\"%s\":true", key); snprintf(b, sizeof b, "\"%s\": true", key); return strstr(j, a) || strstr(j, b); }
static int jstr(const char *j, const char *key, char *out, int cap) {   // "key":"value" or "key":null
    char k[64]; snprintf(k, sizeof k, "\"%s\":", key); const char *p = strstr(j, k); out[0] = 0; if (!p) return 0;
    p += strlen(k); while (*p == ' ') p++;
    if (*p != '"') return 0;
    p++; int i = 0; while (*p && *p != '"' && i < cap - 1) out[i++] = *p++; out[i] = 0; return 1;
}
static int jint(const char *j, const char *key, int def) {
    char k[64]; snprintf(k, sizeof k, "\"%s\":", key); const char *p = strstr(j, k); if (!p) return def;
    return atoi(p + strlen(k));
}
static void name_utf8(char *o, int cap) { WideCharToMultiByte(CP_UTF8, 0, g_myName, -1, o, cap, NULL, NULL); }
// the body of mm/native: my wishes for the ladder's matchmaker (maps, rating spread, test group, pings to our game servers)
static void join_body(char *body, int cap) {
    char nm[128]; name_utf8(nm, sizeof nm);
    extern int g_race, g_hero, g_random; static char maps[6000]; int L = 0; maps[0] = 0;
    for (int i = 0; i < g_nmaps && L < 5800; i++) if (mode_map(i, g_mode) && !g_off[g_mode][i]) {
        char m8[160]; WideCharToMultiByte(CP_UTF8, 0, g_maps[i], -1, m8, sizeof m8, NULL, NULL);
        L += snprintf(maps + L, sizeof maps - L, "%s\"%s\"", L ? "," : "", m8); }
    int own = GetModuleHandleA("Relay.dll") != NULL;      // our own game server: Relay 7 plays only with players who have it,
    char rv[8] = ""; GetEnvironmentVariableA("DOW2_RELAY", rv, sizeof rv);   // Relay 8 "8c": compatible, any lobby (see ladder.py)
    int compat = own && strchr(rv, 'c') != NULL;
    // Relay 9 pings our servers: "ip:ms,ip:ms" (-1 = no answer) -> "relay_ping":{"ip":ms,...}, the matchmaker picks the best for all
    char pg[256] = "", rp[320] = ""; int rl = 0;
    if (own && GetEnvironmentVariableA("DOW2_RELAY_PING", pg, sizeof pg) && pg[0]) {
        rl = snprintf(rp, sizeof rp, ",\"relay_ping\":{");
        for (char *t = strtok(pg, ","), *first = t; t && rl < 280; t = strtok(NULL, ",")) {
            char *c = strrchr(t, ':'); if (!c) continue; *c = 0;
            rl += snprintf(rp + rl, sizeof rp - rl, "%s\"%s\":%d", t == first ? "" : ",", t, atoi(c + 1));
        }
        rl += snprintf(rp + rl, sizeof rp - rl, "}");
    }
    detect_game();
    char rm[48] = ""; if (g_rs != RS_NONE && g_rs != RS_DONE && g_room[0]) snprintf(rm, sizeof rm, ",\"room\":\"%s\"", g_room);   // 0.47: a launcher room's search
    snprintf(body, cap, "{\"steam\":\"%llu\",\"game\":\"%s\",\"mode\":\"%s\",\"name\":\"%s\",\"race\":\"%s\",\"hero\":\"%d\",\"maps\":[%s],\"max_diff\":%d%s%s,\"group\":\"%s\",\"mmf\":\"" MMF_VER "\"%s}",
             g_mySteam, g_game, MODE_KEY[g_mode], nm, (g_random ? "random" : g_race >= 0 && g_race < 9 ? RACES[g_race] : ""), g_hero, maps, g_maxDiff,
             own ? (compat ? ",\"own_server\":true,\"relay_compat\":true" : ",\"own_server\":true") : "", rp, g_group, rm);
}
static DWORD WINAPI net_worker(LPVOID _) {
    static char body[8192]; char res[4096];
    for (;;) {
        WaitForSingleObject(g_wake, 2000);
        if (g_wantLeave) {                                                // Stop Searching / Back: out of the ladder's queue
            g_wantLeave = 0; snprintf(body, sizeof body, "{\"steam\":\"%llu\"}", g_mySteam);
            http_post(L"leave", body, res, sizeof res); logmsg("leave -> %s", res);
        }
        if (g_wantEnded) { g_wantEnded = 0; snprintf(body, sizeof body, "{\"steam\":\"%llu\"}", g_mySteam); http_post(L"ended", body, res, sizeof res); logmsg("ended -> %s", res); }
        if (g_loading) {                                                  // in a match: "still playing" once a minute (the ladder closes a match nobody plays)
            static DWORD lastA; if (GetTickCount() - lastA > 60000) { lastA = GetTickCount();
                snprintf(body, sizeof body, "{\"steam\":\"%llu\"}", g_mySteam); http_post(L"alive", body, res, sizeof res); }
        }
        if (g_state == S_NASK && !g_natAnswer) {                          // 0.38: may the game's own search run on our lobby server?
            join_body(body, sizeof body); DWORD t0 = GetTickCount();
            int ok = http_quick(L"native", body, res, sizeof res, 1500);
            if (ok && jtrue(res, "native")) { g_natWhy[0] = 0; g_natAnswer = 1; }
            else { if (!ok) strcpy(g_natWhy, "the ladder did not answer in time"); else if (!jstr(res, "why", g_natWhy, sizeof g_natWhy)) snprintf(g_natWhy, sizeof g_natWhy, "%.90s", res);
                   g_natAnswer = 2; }
            logmsg("native: mm/native -> %s (%lu ms): %s", g_natAnswer == 1 ? "yes" : "no", GetTickCount() - t0, ok ? res : "-");
        }
        if (g_showMode && g_screen == SC_MM && !g_loading && g_state != S_NASK) {   // 0.42: who is online / searching, for the Find a Game info block
            static DWORD lh; if (!lh || GetTickCount() - lh >= 5000) { lh = GetTickCount();
                snprintf(body, sizeof body, "{\"steam\":\"%llu\"}", g_mySteam);
                if (http_quick(L"hello", body, res, sizeof res, 1000) && strstr(res, "\"online\"")) {
                    g_liveOnline = jint(res, "online", -1);
                    const char *q = strstr(res, "\"queue\""); g_liveQueue = q ? jint(q, MODE_KEY[g_mode], -1) : jint(res, "searching", -1); } } }
        if (g_peekWant) {                                                 // 0.42: would Start Searching search now (nothing changes)
            g_peekWant = 0; join_body(body, sizeof body);
            size_t bl = strlen(body); if (bl > 1 && bl + 16 < sizeof body) strcpy(body + bl - 1, ",\"peek\":true}");
            DWORD t0 = GetTickCount(); int ok = http_quick(L"native", body, res, sizeof res, 1500); char why[96] = "";
            if (ok && jtrue(res, "native")) { g_peekAns = 1; if (g_state == S_IDLE) g_runWhy[0] = 0; }   // it can now: the old reason goes
            else { if (!ok) strcpy(why, "the ladder did not answer in time"); else if (!jstr(res, "why", why, sizeof why)) snprintf(why, sizeof why, "%.90s", res);
                   strcpy(g_peekWhy, why); g_peekAns = 2; }
            static int lastA; static char lastW[96];
            if (g_peekAns != lastA || strcmp(lastW, why)) { lastA = g_peekAns; strcpy(lastW, why);
                logmsg("search kind: mm/native peek -> %s (%lu ms): %s", g_peekAns == 1 ? "native" : "not available", GetTickCount() - t0, ok ? res : "-"); }
        }
        if ((g_state == S_NATIVE || (g_state == S_IDLE && g_natWatchUntil && (int)(g_natWatchUntil - GetTickCount()) > 0)) && !g_loading) {   // 0.38: the native search as the ladder sees it, every 2 s
            static DWORD lastn; if (GetTickCount() - lastn >= 2000) { lastn = GetTickCount();
                snprintf(body, sizeof body, "{\"steam\":\"%llu\"}", g_mySteam);
                if (http_quick(L"poll", body, res, sizeof res, 1500)) {
                    char state[32] = "", mid[32] = "", lob[48] = ""; jstr(res, "state", state, sizeof state); jstr(res, "match", mid, sizeof mid);
                    int nat = jtrue(res, "native"), good = nat && (!strcmp(state, "searching") || !strcmp(state, "matched") || !strcmp(state, "playing"));
                    static char lastSt[48]; char cur[48]; snprintf(cur, sizeof cur, "%s%s %s", nat ? "native " : "", state, mid);
                    if (strcmp(cur, lastSt)) { strcpy(lastSt, cur); logmsg("native: the ladder says %s", res); }
                    if (g_rs == RS_SEARCH && !strcmp(state, "matched")) {          // 0.47: tell the launcher
                        if (!g_rEvMatched) g_rEvMatched = 1;
                        char rm2[24] = ""; if (!g_rEvRoom && jstr(res, "room", rm2, sizeof rm2) && rm2[0] >= '1' && rm2[0] <= '9') g_rEvRoom = 1; }
                    if (good) { g_natGood = GetTickCount(); g_natSeen = 1; if (jstr(res, "lobby", lob, sizeof lob) && lob[0]) strcpy(g_natLobby, lob);
                                else { char rm[24] = ""; if (jstr(res, "room", rm, sizeof rm) && rm[0] >= '1' && rm[0] <= '9') snprintf(g_natLobby, sizeof g_natLobby, "NM-%s", rm); } }   // the ladder's lobby of a native match (mm_history) once the room is up: the replay finds its match
                    // our match called off by the server: it had the search (even when this is the first answer - a match that formed and
                    // failed within 2 s); a match id handled before is not news (the ladder repeats "cancelled" for 5 min)
                    else if (!strcmp(state, "cancelled") && mid[0] && !g_natCancelled && strcmp(mid, g_natLastCancel)) {
                        strcpy(g_natLastCancel, mid); g_natSeen = 1; g_natGood = GetTickCount(); g_natAgain = jtrue(res, "again"); g_natCancelled = 1; }
                } }
        }
    }
    return 0;
}

// ---------------- UI commands ----------------
static void fs(const char *rel, const char *args) {          // rel: ".OnClickHost" relative to the panel, or a full path
    if (!g_fsSelf || !g_fsMovie) { logmsg("fs %s: no handler yet", rel); return; }
    if (g_loading) { logmsg("fs %s skipped: the match is loading (the menu is going away)", rel); return; }   // 0.34: a lobby command then crashed the game
    char cmd[300]; if (rel[0] == '.') snprintf(cmd, sizeof cmd, "%s%s", g_panel, rel); else snprintf(cmd, sizeof cmd, "%s", rel);
    logmsg("-> fscommand %s '%s'", cmd, args ? args : "");
    g_sending++; g_fsOrig(g_fsSelf, NULL, g_fsMovie, cmd, args ? args : ""); g_sending--;
}
// ---------------- saved choice: race / chapter / hero picked by the player ----------------
int g_race = -1, g_chapter = -1, g_hero = -1, g_random; static volatile int g_heroCount; static volatile int g_curRace = -1, g_curChapter = -1, g_curHero = -1; static void *g_heroSelf; static int g_heroFlags; static volatile DWORD g_randomTick, g_autoSelTick, g_lastFsTick; static volatile int g_applyMM, g_mmApplied; static volatile DWORD g_heroAt;
static wchar_t g_ini[MAX_PATH];   // g_heroCount: heroes of the race on screen (from Receive_PopulateHeroList)
static void save_choice(void) {
    wchar_t b[16];
    _snwprintf(b, 16, L"%d", g_race); WritePrivateProfileStringW(L"Choice", L"Race", b, g_ini);
    _snwprintf(b, 16, L"%d", g_chapter); WritePrivateProfileStringW(L"Choice", L"Chapter", b, g_ini);
    _snwprintf(b, 16, L"%d", g_hero); WritePrivateProfileStringW(L"Choice", L"Hero", b, g_ini);
    _snwprintf(b, 16, L"%d", g_random); WritePrivateProfileStringW(L"Choice", L"Random", b, g_ini);
}
// unchecked maps per mode: [Maps] Off1v1=name|name|...  (new maps from Elite updates come in checked)
static void load_prefs(void) {
    for (int m = 0; m < 4; m++) {
        wchar_t key[16], buf[8192]; _snwprintf(key, 16, L"Off%ls", MODE_W[m]);
        memset(g_off[m], 0, sizeof g_off[m]);
        GetPrivateProfileStringW(L"Maps", key, L"", buf, 8192, g_ini);
        int bans = 0;                                          // older saves may hold more bans than the ladder allows now
        for (wchar_t *t = wcstok(buf, L"|"); t; t = wcstok(NULL, L"|")) { int i = map_index(t); if (i >= 0 && mode_map(i, m) && bans < MAX_BANS) { g_off[m][i] = 1; bans++; } }
    }
}
static void save_prefs(int m) {
    static wchar_t buf[8192]; int L = 0; buf[0] = 0; wchar_t key[16]; _snwprintf(key, 16, L"Off%ls", MODE_W[m]);
    for (int i = 0; i < g_nmaps && L < 8000; i++) if (g_off[m][i]) L += _snwprintf(buf + L, 8192 - L, L"%ls%ls", L ? L"|" : L"", g_maps[i]);
    WritePrivateProfileStringW(L"Maps", key, buf, g_ini);
}
static int apply_race(void) {                    // saved race / chapter into the hero panel of the current screen; 1 = changed something
    char a[16]; int ch = 0;
    if (GetPrivateProfileIntW(L"MMFind", L"DebugForceRace", 0, g_ini)) g_curRace = g_curChapter = -1;   // test: as if the shown race were unknown
    if (g_race >= 0 && g_race != g_curRace) { snprintf(a, sizeof a, "0;%d", g_race); fs(".hero_selection_mc.OnSelectDropDownItem", a); ch = 1; }
    if (g_chapter >= 0 && g_chapter != g_curChapter) { snprintf(a, sizeof a, "1;%d", g_chapter); fs(".hero_selection_mc.OnSelectDropDownItem", a); ch = 1; }
    return ch;
}
// the saved hero, as its own step some frames after the race change: picking it from inside the engine's hero-list refresh
// (nested call) killed Elite 3.2 in the lobby - "Assertion failed: i < count" in rlink vector.h
static void apply_hero(void) {
    if (g_hero < 0) return; char a[16]; snprintf(a, sizeof a, "%d", g_hero); fs(".hero_selection_mc.OnSelectHero", a);
    if (g_heroSelf) { GFxValue v; memset(&v, 0, sizeof v); v.type = GFX_NUMBER; v.v.num = g_hero; g_invOrig(g_heroSelf, NULL, "Receive_SelectHero", g_heroFlags, &v, 1); }
    logmsg("hero: set to %d", g_hero);
}

// Localizer.dll's LocString (the game's text table: the language of the game, see lang_detect)
typedef void (__fastcall *LocCtorFn)(void *self, void *edx, const wchar_t *s);
typedef void (__fastcall *LocDtorFn)(void *self, void *edx);

// ---------------- the front-end message box movie (its notice after a native match the server called off gets OK) ----------------
#define FN_SFMOVIE_CTOR 0x00681B50          // __stdcall SFMovie(this, const char *path)
static const unsigned char SFM_PRO[7] = {0x6A, 0xFF, 0x68, 0x90, 0x5C, 0xEE, 0x00};
typedef void *(__stdcall *SfmCtorFn)(void *self, const char *path);
static SfmCtorFn g_sfmOrig; static void *g_msgWrap;
// the box's buttons are fscommands OnButton1 / OnButton2 with the box's path, sent by the box's own movie
static void *g_mbFsSelf, *g_mbMovie;
static void *__stdcall sfm_hook(void *self, const char *path) {
    void *r = g_sfmOrig(self, path);
    if (path && !IsBadReadPtr(path, 4) && strstr(path, "front_end\\message_box")) { g_msgWrap = self; logmsg("message box movie wrapper %p", self); }
    return r;
}
static void install_sfm_hook(void) {
    unsigned char *t = (unsigned char *)FN_SFMOVIE_CTOR;
    if (IsBadReadPtr(t, 7) || memcmp(t, SFM_PRO, 7)) { logmsg("SFMovie ctor prologue mismatch"); return; }
    unsigned char *tr = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(tr, SFM_PRO, 7); tr[7] = 0xE9; *(DWORD *)(tr + 8) = (FN_SFMOVIE_CTOR + 7) - (DWORD)(tr + 12); g_sfmOrig = (SfmCtorFn)tr;
    code_jmp(t, (void *)sfm_hook, 7);
}

// ---------------- Map Preferences dialog: all Elite maps of the chosen mode, checked = may be played ----------------
// The stock dialog shows Relic's ranked pool; we fill it ourselves and answer its commands ourselves (the engine never sees them).
static void *g_mpSelf; static int g_mpFlags, g_mpMode = -1, g_mpIdx[MAXMAPS], g_mpN;
static GFxValue g_mpArgs[1 + 2 * MAXMAPS]; static wchar_t g_mpText[200]; static int g_relicN = 10;
static int build_maplist(void) {
    g_mpN = 0; int n = 1;
    for (int i = 0; i < g_nmaps; i++) if (mode_map(i, g_mode)) g_mpIdx[g_mpN++] = i;
    _snwprintf(g_mpText, 199, L"%ls: uncheck maps you do not want to play.", MODE_W[g_mode]);
    memset(g_mpArgs, 0, sizeof g_mpArgs);
    g_mpArgs[0].type = GFX_WSTRING; g_mpArgs[0].v.ws = g_mpText;
    for (int k = 0; k < g_mpN; k++) {
        g_mpArgs[n].type = GFX_WSTRING; g_mpArgs[n].v.ws = g_maps[g_mpIdx[k]]; n++;
        g_mpArgs[n].type = GFX_NUMBER; g_mpArgs[n].v.num = g_off[g_mode][g_mpIdx[k]] ? 2 : 0; n++;
    }
    g_mpMode = g_mode; return n;
}
static int parse_states(const char *a, unsigned char *st) {       // "0,2,0,..." -> st[k] = 1 if unchecked
    int k = 0; for (const char *p = a; *p && k < g_mpN; ) { st[k++] = atoi(p) == 2; p = strchr(p, ','); if (!p) break; p++; }
    return k;
}

// ---------------- replay upload after a multiplayer match ----------------
// The game writes the last match to Documents\My Games\Dawn of War II - Retribution\Playback\temp.rec. Back in the menu after a match
// with other people we send it to the replay site (rts.fintebtc.de/replay, same mirrors as the ladder); its id is in MMFind.log.
static volatile int g_recHumans, g_recBots; static FILETIME g_recStart; static char g_recName[48];
// 0.40 (03.11.2026, players: "replays do not always go out"): logs of 2-3.11 showed 429 (the replay site's per-address limit,
// which counted every upload as one address - fixed on the server) and 413 (the Moscow mirror took 1 MB at most - fixed) taken as
// success, so no other mirror was tried and the replay was lost. Now only 2xx is success (400 = not a replay: dropped); anything
// else tries the next mirror. No mirror took it: nothing more (owner: no queues - every player of the match sends his own copy).
static int send_rec(const char *buf, DWORD size, const char *fname, const char *lobby) {   // 1 = done (sent or not a replay)
    wchar_t hdr[400]; _snwprintf(hdr, 400, L"Content-Type: application/octet-stream\r\nX-Filename: %hs\r\nX-Lobby: %hs\r\n", fname, lobby);
    char out[512];
    for (int k = 0; k < g_nHosts; k++) {                              // the analysis runs while we wait: long receive timeout
        int h = (g_curHost + k) % g_nHosts; out[0] = 0; int n = 0; DWORD code = 0, sz = sizeof code;
        HINTERNET s = WinHttpOpen(L"MMFind/" MMF_VER_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0); if (!s) break;
        WinHttpSetTimeouts(s, 4000, 8000, 60000, 300000);
        HINTERNET c = WinHttpConnect(s, g_hosts[h], INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", L"/replay/api/upload", NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
        if (r && WinHttpSendRequest(r, hdr, (DWORD)-1, (LPVOID)buf, size, size, 0) && WinHttpReceiveResponse(r, NULL)) {
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
            DWORD m; while (n < (int)sizeof out - 1 && WinHttpReadData(r, out + n, sizeof out - 1 - n, &m) && m) n += m;
            out[n] = 0;
        }
        if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
        logmsg("replay: %lu bytes -> %ls: HTTP %lu %s", size, g_hosts[h], code, out);
        if (code >= 200 && code < 300) return 1;
        if (code == 400) return 1;                                     // the site says it is not a replay: sending again won't help
    }
    return 0;
}
static DWORD WINAPI replay_upload(LPVOID _) {
    Sleep(3000);                                                      // let the game finish writing the file
    wchar_t doc[MAX_PATH], path[MAX_PATH]; if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, doc) != S_OK) return 0;
    _snwprintf(path, MAX_PATH, L"%ls\\My Games\\Dawn of War II - Retribution\\Playback\\temp.rec", doc);
    WIN32_FILE_ATTRIBUTE_DATA fa; if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fa)) { logmsg("replay: no temp.rec"); return 0; }
    if (CompareFileTime(&fa.ftLastWriteTime, &g_recStart) < 0) { logmsg("replay: temp.rec is older than the match - not sent"); return 0; }
    DWORD size = fa.nFileSizeLow; if (fa.nFileSizeHigh || size < 64 || size > 30u << 20) { logmsg("replay: odd size %lu - not sent", size); return 0; }
    char *buf = malloc(size); if (!buf) return 0; DWORD got = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { ReadFile(f, buf, size, &got, NULL); CloseHandle(f); }
    if (got != size || memcmp(buf + 12, "DOW2_REC", 8)) { logmsg("replay: could not read temp.rec"); free(buf); return 0; }
    SYSTEMTIME t; GetLocalTime(&t); char fname[200];                 // file name on the site: MM-m2-xxxx_2026-10-27_12-31.rec / match_...
    snprintf(fname, sizeof fname, "%s_%04d-%02d-%02d_%02d-%02d.rec", g_recName[0] ? g_recName : "match", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute);
    if (!send_rec(buf, size, fname, g_recName)) logmsg("replay: no mirror took it - not sent");   // lobby: the ladder finds its replay
    free(buf);
    return 0;
}
static void replay_after_match(void) {
    if ((g_recHumans < 2 && !g_recBots) || !GetPrivateProfileIntW(L"Replays", L"Upload", 1, g_ini)) return;   // skirmish vs AI: not needed (our AI tests: yes)
    CloseHandle(CreateThread(NULL, 0, replay_upload, NULL, 0, NULL));
}

// ---------------- hooks ----------------
static void on_player_list(GFxValue *a, unsigned n) {     // a Custom Games lobby: how many people (the replay after its match)
    if (n < 5 || (n - 5) % 20) return;
    int humans = 0;
    for (unsigned i = 0; i < (n - 5) / 20; i++) {
        GFxValue *s = a + 5 + i * 20;
        if (s[0].type != GFX_BOOL || !s[0].v.b || s[4].type != GFX_NUMBER) continue;
        int st = (int)s[4].v.num; if (st == 0 || st == 1) humans++;      // 0 me, 1 other human (2.. AI / open / closed)
    }
    if (humans != g_humans) logmsg("lobby: humans=%d", humans);
    g_humans = humans;
}

// ---------------- 0.42: the kind of search on Find a Game (see g_showMode) ----------------
static const char *native_no(void);
static int has_cyr(const wchar_t *w) { for (; w && *w; w++) if (*w >= 0x400 && *w < 0x500) return 1; return 0; }
// the language of the game's own texts, as EliteAI does it: Russian if they are Cyrillic (Localizer.dll, the game's text table)
static void lang_detect(void) {
    if (g_lang >= 0) return;
    if (g_langCfg >= 0) { g_lang = g_langCfg; logmsg("search kind: language %s (Lang=)", g_lang ? "ru" : "en"); return; }
    HMODULE lz = GetModuleHandleA("Localizer.dll");
    typedef unsigned char (__stdcall *GetStrB)(int id, void *out); typedef const wchar_t *(__fastcall *CstrFn)(const void *self, void *edx);
    LocCtorFn ctor = lz ? (LocCtorFn)GetProcAddress(lz, "??0LocString@@QAE@PB_W@Z") : NULL;
    LocDtorFn dtor = lz ? (LocDtorFn)GetProcAddress(lz, "??1LocString@@UAE@XZ") : NULL;
    GetStrB get = lz ? (GetStrB)GetProcAddress(lz, "?GetString@Localizer@@SG_NHAAVLocString@@@Z") : NULL;
    CstrFn cstr = lz ? (CstrFn)GetProcAddress(lz, "?c_str@LocString@@QBEPB_WXZ") : NULL;
    int ru = 0, got = 0;
    if (ctor && dtor && get && cstr) {
        static const int IDS[] = {555027, 555036};                   // "Add Normal Computer", "Normal Computer Player" (as EliteAI)
        static __attribute__((aligned(16))) unsigned char ls[64];
        for (int i = 0; i < 2; i++) { memset(ls, 0, sizeof ls); ctor(ls, NULL, L"");
            if (get(IDS[i], ls)) { got = 1; ru |= has_cyr(cstr(ls, NULL)); } dtor(ls, NULL); }
    }
    g_lang = ru;
    logmsg("search kind: language %s (%s)", ru ? "ru" : "en", got ? "the game's text table" : "no text table - English");
}
// the ladder's / our own reasons in the player's words, with what to do (0.45: no other search runs instead)
static const wchar_t *why_text(const char *w, int ru) {
    static const struct { const char *k; const wchar_t *ru, *en; } T[] = {
        {"not signed in through our lobby", L"перезапустите игру через лаунчер Elite (игра вошла не через наш сервер лобби)",
                                            L"restart the game from the Elite launcher (the game did not sign in through our lobby server)"},
        {"no OwnLobby.dll",                 L"перезапустите игру через лаунчер Elite", L"restart the game from the Elite launcher"},
        {"lobby server unreachable",        L"наш сервер лобби не отвечает \x2014 попробуйте через минуту", L"our lobby server does not answer - try again in a minute"},
        {"did not answer",                  L"наш сервер не отвечает \x2014 попробуйте через минуту", L"our server does not answer - try again in a minute"},
        {"native automatch off",            L"выключен на сервере \x2014 попробуйте позже", L"switched off on the server - try again later"},
        {"native search",                   L"сервер не увидел поиск \x2014 нажмите Start Searching ещё раз", L"the server did not see the search - press Start Searching again"},
        {"was not ours",                    L"поиск шёл мимо нашего сервера \x2014 нажмите Start Searching ещё раз", L"the search did not go through our server - press Start Searching again"},
        {"mode",                            L"Team FFA не входит в автоматч \x2014 выберите 1v1, 2v2 или 3v3", L"Team FFA is not in the automatch - pick 1v1, 2v2 or 3v3"},
    };
    if (!w || !w[0]) return L"";
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (strstr(w, T[i].k)) return ru ? T[i].ru : T[i].en;
    static wchar_t raw[96]; MultiByteToWideChar(CP_UTF8, 0, w, -1, raw, 96); return raw;
}
static const char *native_no(void);
// 1 our servers, 2 not available (*why), 0 not known yet
static int mode_kind(const char **why) {
    *why = NULL;
    if (g_state == S_NATIVE) return 1;
    const char *no = native_no(); if (no) { *why = no; return 2; }                    // nothing to ask: what Start Searching would get
    if (g_state == S_IDLE && g_runWhy[0]) { *why = g_runWhy; return 2; }              // the last Start Searching could not search
    if (g_peekAns == 2) *why = g_peekWhy;
    return g_peekAns;
}
// Two text fields of the Find a Game panel itself (they go and come with the panel; its units = the stage, 2134 x 1200, seen
// with ModeProbe=1 on the stand): the head right of the screen's own title "Find a Game", and the info block in the free space
// under "Your Party" (no button of the game there): the kind of search, MMFind's version, the queue and who is online.
// [MMFind] HeadX/HeadY/HeadSize, ModeX/ModeY/ModeSize move them (read again on every new field).
static double g_hx = 600, g_hy = 30, g_hsz = 34;
static void head_html(wchar_t *h, int cap) {
    const char *why; int k = mode_kind(&why), ru = g_lang == 1;
    const wchar_t *t = g_rs != RS_NONE && g_rs != RS_DONE ? (ru ? L"ELITE: МАТЧ ИЗ КОМНАТЫ ЛАУНЧЕРА" : L"ELITE: MATCH FROM THE LAUNCHER ROOM")
                     : k == 1 ? (ru ? L"ELITE: АВТОМАТЧ НА НАШИХ СЕРВЕРАХ" : L"ELITE: AUTOMATCH ON OUR SERVERS")
                     : k == 2 ? (ru ? L"ELITE: АВТОМАТЧ V2.0 НЕДОСТУПЕН" : L"ELITE: AUTOMATCH V2.0 NOT AVAILABLE") : (ru ? L"ELITE: АВТОМАТЧ" : L"ELITE: AUTOMATCH");
    _snwprintf(h, cap - 1, L"<font face='%ls' size='%d' color='%ls'>%ls</font>", ru ? L"$NormalFont" : L"$TitleFont", (int)(g_hsz + 0.5),
               k == 2 ? L"#E8A06A" : L"#E2C36B", t);
    h[cap - 1] = 0;   // the game's title font has no Cyrillic in every install: Russian in $NormalFont
}
static void mode_html(wchar_t *h, int cap) {
    const char *why; int k = mode_kind(&why), ru = g_lang == 1;
    int s1 = (int)(g_msz + 0.5), s2 = (int)(g_msz * 0.78 + 0.5);
    if (k == 2) {                                                     // 0.45: nothing searches - why, and what to do
        _snwprintf(h, cap - 1, L"<font face='$NormalFont' size='%d' color='#E8A06A'>%ls</font><br>"
                               L"<font face='$NormalFont' size='%d' color='#D8D2BC'>%ls</font><br>"
                               L"<font face='$NormalFont' size='%d' color='#8E9A94'>MMFind " MMF_VER_W L"</font>",
                   s1, ru ? L"Автоматч v2.0 недоступен:" : L"Automatch v2.0 is not available:", s2, why_text(why, ru), s2);
        h[cap - 1] = 0; return;
    }
    const wchar_t *lab = ru ? L"Автопоиск:" : L"Automatch:";
    const wchar_t *val = k == 1 ? (ru ? L"наши серверы" : L"our servers") : (ru ? L"проверяем..." : L"checking...");
    const wchar_t *col = k == 1 ? L"#8FE3B4" : L"#A9B4AE";
    wchar_t q[160] = L"", n[64] = L"", o[64] = L"";
    int searching = g_state == S_NASK || g_state == S_NATIVE;
    int qn = g_liveQueue >= 0 ? g_liveQueue : g_queue;
    if (qn >= 0) _snwprintf(n, 63, ru ? L"в очереди: %d" : L"in the queue: %d", qn);
    if (g_liveOnline >= 0) _snwprintf(o, 63, ru ? L"онлайн: %d" : L"online: %d", g_liveOnline);
    if (searching) { DWORD s = (GetTickCount() - g_searchTick) / 1000;
        _snwprintf(q, 159, L"%ls %u:%02u%ls%ls%ls%ls", ru ? L"Ищем..." : L"Searching...", (unsigned)(s / 60), (unsigned)(s % 60), n[0] ? L" \x00B7 " : L"", n, o[0] ? L" \x00B7 " : L"", o); }
    else if (n[0] || o[0]) { _snwprintf(q, 159, L"%ls%ls%ls", n, n[0] && o[0] ? L" \x00B7 " : L"", o); if (q[0] >= L'a' && q[0] <= L'z') q[0] -= 32; else if (q[0] >= 0x430 && q[0] <= 0x44F) q[0] -= 32; }
    _snwprintf(h, cap - 1, L"<font face='$NormalFont' size='%d' color='#D8D2BC'>%ls</font><font face='$NormalFont' size='%d' color='%ls'> %ls</font><br>"
                           L"<font face='$NormalFont' size='%d' color='#8E9A94'>MMFind " MMF_VER_W L"</font><br>"
                           L"<font face='$NormalFont' size='%d' color='#C9D6CF'>%ls</font>",
               s1, lab, s1, col, val, s2, s2, q);
    h[cap - 1] = 0;
}
static void field_make(void *m, const char *nm, double depth, double x, double y, double w, double hgt) {
    char fn[240], p[240]; snprintf(fn, sizeof fn, "%s.createTextField", g_panel);
    as_fnv(m, fn, "snnnnn", nm, depth, x, y, w, hgt);
    static const char *B[] = {"embedFonts", "html", "multiline", "wordWrap", "selectable"}; static const int BV[] = {1, 1, 1, 1, 0};
    for (int i = 0; i < 5; i++) { snprintf(p, sizeof p, "%s.%s.%s", g_panel, nm, B[i]); as_setb(m, p, BV[i]); }
}
static void mode_make(void *m) {
    { wchar_t t[16]; static const wchar_t *K[6] = {L"ModeX", L"ModeY", L"ModeSize", L"HeadX", L"HeadY", L"HeadSize"};
      double *V[6] = {&g_mx, &g_my, &g_msz, &g_hx, &g_hy, &g_hsz};
      for (int i = 0; i < 6; i++) { GetPrivateProfileStringW(L"MMFind", K[i], L"", t, 16, g_ini); if (t[0]) *V[i] = _wtof(t); }
      g_modeProbe = GetPrivateProfileIntW(L"MMFind", L"ModeProbe", 0, g_ini); }
    field_make(m, "mmf_head", 16122.0, g_hx, g_hy, 1500, g_hsz * 1.6);
    field_make(m, "mmf_mode", 16123.0, g_mx, g_my, 1000, g_msz * 6.4);   // 0.45: room for a wrapped reason
    logmsg("search kind: fields made in %s (head %g,%g, info %g,%g)", g_panel, g_hx, g_hy, g_mx, g_my);
    if (g_modeProbe) {                                          // debug (ModeProbe=1): labels every 200 x 100 panel units
        int n = 0; char fn[240], p[240]; snprintf(fn, sizeof fn, "%s.createTextField", g_panel);
        for (int y = 0; y <= 1200; y += 100) for (int x = 0; x <= 2000; x += 200) {
            char nm[32]; wchar_t w[96]; snprintf(nm, sizeof nm, "mmf_p%d", n);
            as_fnv(m, fn, "snnnnn", nm, 16200.0 + n, (double)x, (double)y, 160.0, 30.0);
            snprintf(p, sizeof p, "%s.%s.html", g_panel, nm); as_setb(m, p, 1);
            snprintf(p, sizeof p, "%s.%s.embedFonts", g_panel, nm); as_setb(m, p, 1);
            _snwprintf(w, 95, L"<font face='$NormalFont' size='14' color='#FF60FF'>+%d,%d</font>", x, y);
            snprintf(p, sizeof p, "%s.%s.htmlText", g_panel, nm); as_setw(m, p, w); n++;
        }
    }
}
// right after the engine's own per-frame call into the Find a Game panel (Receive_ShowSearchingAnimation): the panel and its
// movie are alive at that moment, on the game's UI thread
static void mode_ui(void) {
    static DWORD last, tries, triesScr; DWORD now = GetTickCount();
    if (!g_showMode || now - last < 250) return; last = now;
    void *m = g_fsMovie;
    if (!m || g_loading || g_screen != SC_MM || !g_panel[0] || now - g_scrTick < 300) return;
    lang_detect();
    char p[240], ph[240]; GFxValue v; snprintf(p, sizeof p, "%s.mmf_mode._visible", g_panel); snprintf(ph, sizeof ph, "%s.mmf_head._visible", g_panel);
    static int shown = -1; static wchar_t lastH[900], lastT[300];
    if (!(as_get(m, p, &v) && v.type == GFX_BOOL)) {
        if (triesScr != g_scrTick) { triesScr = g_scrTick; tries = 0; }
        if (tries >= 3) return;
        tries++; mode_make(m); shown = -1; lastH[0] = lastT[0] = 0;
        if (!(as_get(m, p, &v) && v.type == GFX_BOOL)) { logmsg("search kind: the fields were not made (try %lu)", tries); return; }
    }
    int hv = g_heroUp, mv = g_mapUp, over = hv || mv;
    if ((!over) != shown) { shown = !over; as_setb(m, p, shown); as_setb(m, ph, shown);
        logmsg("search kind: %s (hero panel %d, map preferences %d, hero shown %d)", shown ? "shown" : "hidden", hv, mv, g_heroShown); }
    wchar_t h[900], t[300]; mode_html(h, 900); head_html(t, 300);
    if (wcscmp(t, lastT)) { wcscpy(lastT, t); snprintf(ph, sizeof ph, "%s.mmf_head.htmlText", g_panel); as_setw(m, ph, t); }
    if (wcscmp(h, lastH)) {
        static int lastK = -1; const char *why; int k = mode_kind(&why);
        wcscpy(lastH, h); snprintf(p, sizeof p, "%s.mmf_mode.htmlText", g_panel); as_setw(m, p, h);
        if (k != lastK) { lastK = k; logmsg("search kind shown: %s%s%s (state %s)", k == 1 ? "our servers" : k == 2 ? "not available" : "checking",
                                            why && why[0] ? " - " : "", why ? why : "", SN[g_state]); }
    }
}
// on the game's thread (tick): ask the ladder which search Start Searching would take - on entering Find a Game, every 20 s
// there and on a match type change; only while idle and nothing of a native search is pending (an older ladder without "peek"
// would drop the "cancelled" note the search again needs)
static void mode_peek(DWORD now) {
    static DWORD scr;
    if (!g_showMode || g_screen != SC_MM || g_loading || !g_mySteam) return;
    if (scr != g_scrTick) { scr = g_scrTick; g_peekAns = 0; g_peekLast = 0; }
    int idle = g_state == S_IDLE || g_state == S_DONE;
    if (!idle || native_no() || g_reWant || g_natCancelled || (g_natWatchUntil && (int)(g_natWatchUntil - now) > 0) || now - g_scrTick < 300) return;
    if (g_peekLast && now - g_peekLast < 20000) return;
    g_peekLast = now; g_peekWant = 1; SetEvent(g_wake);
}

// 0.47: the menu movie and its fscommand handler from the engine's own calls (RoomJoin 9's way): a game whose MMFind came in after
// its main menu had loaded saw no screen at all (stand 06.11: only the intro movie's OnLoadComplete) - a launcher room's walk
// needs the menu to send its commands to. Asked only while such a walk waits for it (g_rWantMenu).
static volatile int g_rWantMenu, g_rMenuOk; static void *g_triedInv[16]; static int g_ntInv;
static int is_menu_movie(void *mv) {
    if (!mv || IsBadReadPtr(mv, 0x600)) return 0;
    DWORD vt = *(DWORD *)mv;                                          // GFx is linked into DOW2.exe: its vtable is there
    if (vt < 0x00F87000 || vt >= 0x0124F000 || IsBadReadPtr((void *)vt, 0x34)) return 0;
    GFxValue v; return as_get(mv, "_level0.display_item._visible", &v) && v.type == GFX_BOOL;
}
static void menu_from_call(void *self) {
    if (!self || IsBadReadPtr(self, 0x1C)) return;
    void *mv = *(void **)((char *)self + 4), *h = *(void **)((char *)self + 0x18);
    // the menu movie known already (from a panel's OnLoadComplete): the handler its own buttons use is this one - the one taken
    // with the intro's OnLoadComplete was not (stand 2, 06.11: every Multiplayer / Find a Game command of the walk did nothing)
    if (g_fsMovie && mv == g_fsMovie) { if (h && h != g_fsSelf && !IsBadReadPtr(h, 0x20)) { logmsg("room: the menu's own handler %p (was %p) from the engine's calls", h, g_fsSelf); g_fsSelf = h; } g_rMenuOk = 1; return; }
    if (g_fsMovie) return;
    for (int i = 0; i < g_ntInv; i++) if (g_triedInv[i] == mv) return;
    if (g_ntInv >= 16) return;
    g_triedInv[g_ntInv++] = mv;
    if (is_menu_movie(mv) && h && !IsBadReadPtr(h, 0x20)) { g_fsMovie = mv; g_fsSelf = h; g_rMenuOk = 1; logmsg("room: the menu movie %p (handler %p) from the engine's calls", mv, h); }
}
// which full-screen panel is up, from the menu movie itself (the panel clips' _url) - the screen a walk starts on
static int probe_screen(void) {
    void *m = g_fsMovie; if (!m) return -1;
    int found = -1; char pth[96] = "", fu[200] = "";
    for (int k = 1; k <= 3; k++) {
        char pp[96], q[128]; GFxValue v; snprintf(pp, sizeof pp, "_level0.display_item.panel%d_mc.panel_mc", k);
        snprintf(q, sizeof q, "%s._visible", pp); if (!as_get(m, q, &v) || v.type != GFX_BOOL || !v.v.b) continue;
        snprintf(q, sizeof q, "%s._url", pp); if (!as_get(m, q, &v)) continue;
        char u[200] = "";
        if (v.type == GFX_STRING && v.v.s && !IsBadReadPtr(v.v.s, 1)) snprintf(u, sizeof u, "%s", v.v.s);
        else if (v.type == GFX_WSTRING && v.v.ws && !IsBadReadPtr(v.v.ws, 2)) WideCharToMultiByte(CP_UTF8, 0, v.v.ws, -1, u, sizeof u, NULL, NULL);
        u[sizeof u - 1] = 0;
        if (!u[0]) continue;
        for (char *c; (c = strstr(u, "%5F")) || (c = strstr(u, "%5f")); ) { *c = '_'; memmove(c + 1, c + 3, strlen(c + 3) + 1); }   // main%5Fmenu.gfx
        if (strstr(u, "transition_movie.gfx")) continue;
        int sc = strstr(u, "main_menu.gfx") ? SC_MAIN : strstr(u, "matchmaking.gfx") ? SC_MM : strstr(u, "game_list_movie.gfx") ? SC_LIST
               : strstr(u, "game_setup_movie.gfx") ? SC_SETUP : SC_OTHER;
        if (found < 0 || sc == SC_OTHER) { found = sc; strcpy(pth, pp); strcpy(fu, u); }
        if (sc == SC_OTHER) break;
    }
    if (found < 0) return -1;
    if (found != g_screen || strcmp(pth, g_panel)) logmsg("room: screen asked - %s = %s (%d)", pth, fu, found);
    strcpy(g_panel, pth); g_screen = found; g_scrTick = GetTickCount() - 2000;
    return found;
}
static char __fastcall inv_hook(void *self, void *edx, const char *method, int flags, GFxValue *args, unsigned count) {
    if (g_rWantMenu && !g_loading) menu_from_call(self);
    if (g_logInvoke && method && args && count < 64 && !IsBadReadPtr(args, count * sizeof(GFxValue))     // debug: engine -> Flash calls
        && strcmp(method, "Receive_ShowSearchingAnimation") && strcmp(method, "Receive_UpdateSearchStatus") && strncmp(method, "Receive_SetFrameRate", 20)) {
        char b[700]; int L = snprintf(b, sizeof b, "inv %s(%u):", method, count);
        for (unsigned i = 0; i < count && i < 12 && L < 600; i++) {
            if (args[i].type == GFX_NUMBER) L += snprintf(b + L, sizeof b - L, " %g", args[i].v.num);
            else if (args[i].type == GFX_BOOL) L += snprintf(b + L, sizeof b - L, " %s", args[i].v.b ? "T" : "F");
            else if (args[i].type == GFX_WSTRING && args[i].v.ws) { char u[80]; WideCharToMultiByte(CP_UTF8, 0, args[i].v.ws, -1, u, sizeof u, NULL, NULL); u[79] = 0; L += snprintf(b + L, sizeof b - L, " '%s'", u); }
            else if (args[i].type == GFX_STRING && args[i].v.s && !IsBadReadPtr(args[i].v.s, 1)) L += snprintf(b + L, sizeof b - L, " \"%.70s\"", args[i].v.s);
            else L += snprintf(b + L, sizeof b - L, " t%d", args[i].type);
        }
        static char last[700]; if (strcmp(b, last)) { strcpy(last, b); logmsg("%s", b); }
    }
    if (method && args && count < 4096 && !IsBadReadPtr(args, count * sizeof(GFxValue))) {
        if (!strcmp(method, "Receive_ShowSearchingAnimation")) {
            if (count == 1 && (args[0].type == GFX_BOOL || args[0].type == GFX_NUMBER)) {      // 0.38: the engine's own search on / off
                int on = args[0].type == GFX_BOOL ? args[0].v.b != 0 : args[0].v.num != 0;
                if (on != g_engSearch) logmsg("native: the game's own search %s (state %s)", on ? "ON" : "off", SN[g_state]);
                g_engSearch = on; } }
        if (!strcmp(method, "Receive_CreateMessageBox")) {
            logmsg("message box created (%u args)", count);
            // 0.47: the message box movie made before MMFind came in (its SFMovie constructor not seen): the engine's call into it is
            // that SFMovie (+4 its movie, +0x18 its fscommand handler) - the launcher room's walk answers the CPU question with it
            if (!g_msgWrap && self && !IsBadReadPtr(self, 0x1C)) { g_msgWrap = self; logmsg("message box movie wrapper %p (from its first box)", self); } }
        if (!strcmp(method, "Receive_PopulateDropDownListEx") && count > 2 && args[0].type == GFX_NUMBER && args[0].v.num == 0 && g_screen != SC_SETUP) {
            int n = 0;                                                  // map list: name, enabled, name, enabled...
            for (unsigned i = 0; 2 + i * 2 < count && n < MAXMAPS; i++)
                if (args[2 + i * 2].type == GFX_WSTRING && args[2 + i * 2].v.ws) { wcsncpy(g_maps[n], args[2 + i * 2].v.ws, 63); g_maps[n][63] = 0; n++; }
            if (n != g_nmaps) { g_nmaps = n; load_prefs(); logmsg("map catalog: %d maps", n); }
        }
        if (!strcmp(method, "Receive_CreateMapList") && g_nmaps) {
            g_relicN = count > 1 ? (int)(count - 1) / 2 : 10;
            g_mpSelf = self; g_mpFlags = flags; int n = build_maplist();
            logmsg("map preferences: %d maps for %s", g_mpN, MODE_KEY[g_mode]);
            return g_invOrig(self, edx, method, flags, g_mpArgs, n);
        }
        if (!strcmp(method, "Receive_SetGamerTag") && count >= 1 && args[0].type == GFX_WSTRING && args[0].v.ws) wcsncpy(g_myName, args[0].v.ws, 63);
        else if (!strcmp(method, "Receive_PopulatePlayerList")) on_player_list(args, count);
        else if (!strcmp(method, "Receive_SelectHero")) { g_autoSelTick = g_heroTick = GetTickCount(); g_heroSelf = self; g_heroFlags = flags;
            if (count >= 1 && args[0].type == GFX_NUMBER) g_curHero = (int)args[0].v.num; }
        else if (!strcmp(method, "Receive_PopulateHeroList")) { g_heroSelf = self; g_heroFlags = flags; g_heroTick = GetTickCount(); }
        else if (!strcmp(method, "Receive_PopulateDropDownListWithImage") && count >= 4 &&      // the race list (race_*_x icons) arrives before
                 ((args[3].type == GFX_WSTRING && args[3].v.ws && wcsstr(args[3].v.ws, L"\\race_")) ||   // the race selection and the hero list
                  (args[3].type == GFX_STRING && args[3].v.s && strstr(args[3].v.s, "\\race_")))) { g_heroSelf = self; g_heroFlags = flags; g_heroTick = GetTickCount(); }
        else if (!strcmp(method, "Receive_SetDropDownSelection") && count == 2 && args[0].type == GFX_NUMBER && args[1].type == GFX_NUMBER) {
            int dd = (int)args[0].v.num, v = (int)args[1].v.num;
            if (self == g_heroSelf) { if (dd == 0) { if (v != g_curRace) g_curChapter = -1; g_curRace = v; } else if (dd == 1) g_curChapter = v; }
            else if (g_screen == SC_MM && dd == 0 && v != g_mode && v >= 0 && v < 4) {   // Match Type: the engine keeps resetting it - show the player's choice
                static int n; if (n++ < 20) logmsg("match type: engine set %d, keeping %d", v, g_mode);
                args[1].v.num = g_mode;
            }
        }
        else if (!strcmp(method, "Receive_ShowHeroSelect")) { g_heroShown = 1; g_heroUp = 1; g_heroTick = GetTickCount(); if (g_screen == SC_MM && g_state == S_IDLE && !g_mmApplied) { g_applyMM = 1; g_mmApplied = 1; } }
        else if (!strcmp(method, "Receive_HideHeroSelect")) g_heroShown = g_heroUp = 0;
    }
    if (method && !strcmp(method, "Receive_SetStartButtonText") && count == 1 && args && args[0].type == GFX_WSTRING && args[0].v.ws
        && !IsBadReadPtr(args[0].v.ws, 2) && g_langCfg < 0 && g_lang != 1 && has_cyr(args[0].v.ws)) { g_lang = 1; logmsg("search kind: language ru (the Start button)"); }
    char rc = g_invOrig(self, edx, method, flags, args, count);
    if (method && !strcmp(method, "Receive_ShowSearchingAnimation") && g_screen == SC_MM) mode_ui();   // 0.42
    // a note on the game's own status line, right after the engine's own per-frame call into the Find a Game panel, on its thread
    if (method && !strcmp(method, "Receive_ShowSearchingAnimation") && g_screen == SC_MM && g_banNote && GetTickCount() - g_banNote < 8000 && g_state == S_IDLE) {
        GFxValue v[3]; memset(v, 0, sizeof v); static wchar_t a1[] = L"Only 3 maps can be banned per mode.", a2[] = L"Можно убрать не больше 3 карт на режим.", a3[] = L"The first 3 unchecked maps are kept.";
        v[0].type = v[1].type = v[2].type = GFX_WSTRING; v[0].v.ws = a1; v[1].v.ws = a2; v[2].v.ws = a3;
        g_invOrig(self, edx, "Receive_UpdateSearchStatus", flags, v, 3);
    }
    return rc;
}
// 0.38: why Start Searching can not search (NULL = ask the ladder)
static const char *native_no(void) {
    if (g_mode < 0 || g_mode > 2) return "not a ladder mode (Team FFA)";
    if (!GetModuleHandleW(L"OwnLobby.dll")) return "no OwnLobby.dll in the game";
    return NULL;
}
// 0.45: no automatch now - the click stays here (Relic's matchmaker matches nobody: the game's search would only hang)
static void search_blocked(const char *why) {
    logmsg("native: not possible (%s) - Start Searching NOT passed to the game, the reason is on the screen", why);
    snprintf(g_runWhy, sizeof g_runWhy, "%s", why);
    snprintf(g_peekWhy, sizeof g_peekWhy, "%s", why); g_peekAns = 2;
}
// Start Searching on Find a Game (the player's click, or 0.38's search again after a cancelled native match)
static void search_click(void) {
    if (g_curRace >= 0 && !g_random) { g_race = g_curRace; g_chapter = g_curChapter; if (g_curHero >= 0) g_hero = g_curHero; save_choice();
        logmsg("search with race %d chapter %d hero %d (as shown on Find a Game)", g_race, g_chapter, g_hero); }
    g_searchTick = GetTickCount(); g_queue = -1; g_runWhy[0] = 0;
    const char *no = native_no();
    if (!no) { g_natAnswer = 0; g_natSeen = g_natCancelled = 0; g_natLobby[0] = 0; g_natWatchUntil = 0; set_state(S_NASK); SetEvent(g_wake); }   // 0.38: ask the ladder
    else search_blocked(no);
}
static int ends_with(const char *s, const char *t) { size_t a = strlen(s), b = strlen(t); return a >= b && !strcmp(s + a - b, t); }
static void __fastcall fs_hook(void *self, void *edx, void *movie, const char *cmd, const char *args) {
    if (cmd && !IsBadReadPtr(cmd, 1)) {
        if (g_verbose && strcmp(cmd, "OnPlaySound")) logmsg("fs '%s' '%s'", cmd, args && !IsBadReadPtr(args, 1) ? args : "");

        if (!strncmp(cmd, "_level0.display_item", 20) || (!strcmp(cmd, "OnLoadComplete") && args && strstr(args, "name:=_level0.display_item"))) {
            if (movie != g_fsMovie && (g_state == S_DONE || g_loading)) { g_state = S_IDLE; g_stateTick = GetTickCount(); g_loading = 0; g_wantEnded = 1; replay_after_match(); SetEvent(g_wake); logmsg("back in the menu after the match");
                if (g_rs == RS_DONE) room_set(RS_NONE); }
            g_fsSelf = self; g_fsMovie = movie;
        }
        if (!strncmp(cmd, "OnButton", 8) && args && !IsBadReadPtr(args, 1) && strstr(args, "message_box")) { g_mbFsSelf = self; g_mbMovie = movie;
            logmsg("message box button: movie %s (%p), handler %p (menu %p), wrapper %p", movie == g_fsMovie ? "= menu" : "other", movie, self, g_fsSelf, g_msgWrap); }
        if (!strcmp(cmd, "OnLoadMovieComplete") && args && !strncmp(args, "/mp_mc", 6) && !g_loading) {
            int natLoad = g_state == S_NATIVE || g_state == S_NASK || (g_state == S_IDLE && g_natWatchUntil && (int)(g_natWatchUntil - GetTickCount()) > 0);
            if (natLoad) {                                            // 0.38: the game's own automatch got here by itself
                logmsg("native: loading %lu ms after Start Searching (lobby %s)", GetTickCount() - g_searchTick, g_natLobby[0] ? g_natLobby : "-");
                if (g_rs == RS_SEARCH) { room_ev("loading"); room_set(RS_DONE); }   // 0.47: the launcher room's match loads
                native_fast(0);                                       // 0.39: the game as it was (the countdown is over, nothing polls now)
                g_state = S_DONE; g_stateTick = GetTickCount(); g_reRow = 0; g_natWatchUntil = 0;      // after the match: ended + replay as usual
            }
            // release gate 03.11.2026: the native automatch never shows the Custom Games lobby, so g_humans (counted from its player
            // list) stayed 0 and replay_after_match() skipped every native match - a native match always has people
            g_loading = 1; g_recHumans = natLoad && g_humans < 2 ? 2 : g_humans; g_recBots = 0; GetSystemTimeAsFileTime(&g_recStart);
            if (natLoad && g_natLobby[0]) strcpy(g_recName, g_natLobby); else g_recName[0] = 0;   // X-Lobby NM-<room>: the ladder finds its replay
            logmsg("loading screen - hands off the menu (%d people in the lobby)", g_humans); }
        if (!strcmp(cmd, "OnLoadComplete") && args) {                 // which full-screen panel is up
            const char *nm = strstr(args, "name:="), *url = strstr(args, "url:=");
            if (nm && url) {
                char path[160] = ""; const char *e = strchr(nm, ';'); int len = e ? (int)(e - nm - 6) : 0;
                if (len > 0 && len < 159) { memcpy(path, nm + 6, len); path[len] = 0; }
                int sc = -1;
                if (strstr(url, "main_menu.gfx")) sc = SC_MAIN; else if (strstr(url, "matchmaking.gfx")) sc = SC_MM;
                else if (strstr(url, "game_list_movie.gfx")) sc = SC_LIST; else if (strstr(url, "game_setup_movie.gfx")) sc = SC_SETUP;
                else if (strstr(url, "mp_postgame.gfx")) sc = SC_OTHER;
                if (sc >= 0 && !strcmp(path + strlen(path) - 9, ".panel_mc")) {
                    strcpy(g_panel, path); g_screen = sc; g_scrTick = GetTickCount(); g_engSearch = -1; g_mapUp = 0; if (sc != SC_MM) g_heroUp = 0; else g_mmApplied = 0;
                    logmsg("screen %d panel %s", sc, g_panel);
                }
            }
        }
        // the player's own pick - remember it
        if (!g_sending && strstr(cmd, "hero_selection_mc.") && args) {
            int a = -1, b = -1;
            if (ends_with(cmd, ".OnClickRandom")) { g_randomTick = GetTickCount(); if (!g_random) { g_random = 1; save_choice(); logmsg("choice: random race and hero"); } }
            else if (ends_with(cmd, ".OnSelectDropDownItem") && sscanf(args, "%d;%d", &a, &b) == 2) {
                if (g_random) { g_random = 0; save_choice(); logmsg("choice: random off"); }
                if (a == 0 && b != g_race) { g_race = b; g_chapter = -1; g_hero = -1; save_choice(); logmsg("choice: race %d", b); }
                else if (a == 1 && b != g_chapter) { g_chapter = b; save_choice(); logmsg("choice: chapter %d", b); }
            } else if (ends_with(cmd, ".OnSelectHero") && sscanf(args, "%d", &a) == 1 && !(GetTickCount() - g_autoSelTick <= 400 && a == g_curHero) && GetTickCount() - g_randomTick > 1000) {
                // 0.32: only the panel's echo of the hero the game itself just selected is skipped (same hero, within 0.4 s); a quick
                // pick of ANOTHER hero right after a race change counted as that echo before - the lobby then got the default hero
                if (g_random) { g_random = 0; save_choice(); logmsg("choice: random off"); }
                g_curHero = a; if (a != g_hero) { g_hero = a; save_choice(); logmsg("choice: hero %d", a); }
            }
        }
        if (g_screen == SC_MM) {                                      // 0.42: the panel's dialogs over our search kind fields
            if (ends_with(cmd, "hero_selection_mc.OnClickAccept")) g_heroUp = 0;
            else if (ends_with(cmd, ".OnClickMapPreferences")) g_mapUp = 1;
            else if (ends_with(cmd, "map_preferences_mc.OnClickSave") || ends_with(cmd, "map_preferences_mc.OnClickCancel")) g_mapUp = 0;
        }
        if (strstr(cmd, "map_preferences_mc.") && g_mpSelf && g_mpMode >= 0) {          // our dialog - the engine must not see it
            static unsigned char tmp[MAXMAPS]; GFxValue v; memset(&v, 0, sizeof v);
            if (ends_with(cmd, ".OnClickMap") && args) {
                int k = parse_states(args, tmp), on = 0; for (int i = 0; i < k; i++) on += !tmp[i];
                v.type = GFX_BOOL; v.v.b = on > 0; g_invOrig(g_mpSelf, NULL, "Receive_SetSaveButtonEnabled", g_mpFlags, &v, 1);
            } else if (ends_with(cmd, ".OnClickSave") && args) {
                int k = parse_states(args, tmp);
                { int bans = 0; for (int i = 0; i < k; i++) if (tmp[i] && ++bans > MAX_BANS) tmp[i] = 0;      // ladder rule: a few bans per mode
                  if (bans > MAX_BANS) { g_banNote = GetTickCount(); logmsg("map preferences: %d maps unchecked, only %d kept", bans, MAX_BANS); } } for (int i = 0; i < k; i++) g_off[g_mpMode][g_mpIdx[i]] = tmp[i];
                save_prefs(g_mpMode); int on = 0; for (int i = 0; i < k; i++) on += !tmp[i];
                logmsg("map preferences saved for %s: %d of %d maps", MODE_KEY[g_mpMode], on, k);
                char z[256] = ""; for (int i = 0; i < g_relicN && i < 100; i++) strcat(z, i ? ",0" : "0");     // the engine closes the dialog
                g_fsOrig(self, edx, movie, cmd, z);
            } else if (ends_with(cmd, ".OnClickCancel")) {
                g_fsOrig(self, edx, movie, cmd, args);                                   // the engine closes the dialog (and resets its own list)
                GFxValue st[MAXMAPS]; memset(st, 0, sizeof st);
                for (int i = 0; i < g_mpN; i++) { st[i].type = GFX_NUMBER; st[i].v.num = g_off[g_mpMode][g_mpIdx[i]] ? 2 : 0; }
                g_invOrig(g_mpSelf, NULL, "Receive_UpdateMapList", g_mpFlags, st, g_mpN);
            } else { g_fsOrig(self, edx, movie, cmd, args); }
            return;
        }
        if (g_screen == SC_MM && ends_with(cmd, ".OnClickMapPreferences") && g_mpSelf && g_mpMode != g_mode && g_nmaps) {
            g_fsOrig(self, edx, movie, cmd, args);                                      // mode changed - refill the list
            int n = build_maplist(); g_invOrig(g_mpSelf, NULL, "Receive_CreateMapList", g_mpFlags, g_mpArgs, n);
            logmsg("map preferences refilled for %s", MODE_KEY[g_mode]); return;
        }
        if (g_screen == SC_MM && !g_sending && args && !strncmp(cmd, g_panel, strlen(g_panel)) && !strcmp(cmd + strlen(g_panel), ".OnSelectDropDownItem")) {
            int a = -1, b = -1;
            if (sscanf(args, "%d;%d", &a, &b) == 2 && a == 0 && b >= 0 && b < 4 && b != g_mode) {
                g_mode = b; g_peekLast = 0; g_runWhy[0] = 0; wchar_t w[8]; _snwprintf(w, 8, L"%d", b); WritePrivateProfileStringW(L"Choice", L"Mode", w, g_ini);
                logmsg("mode: %s", MODE_KEY[b]);
            }
        }
        // the button: "Start Searching" / "Stop Searching" on the Find a Game screen (fs() calls the engine directly - only real clicks here)
        if (g_screen == SC_MM && ends_with(cmd, ".OnClickStart")) {
            if (g_state == S_NATIVE) {                                // 0.38: Stop Searching of the game's own search - the engine does it
                logmsg("native: Stop Searching by the player"); set_state(S_IDLE); g_wantLeave = 1; SetEvent(g_wake);
                g_fsOrig(self, edx, movie, cmd, args); return;
            }
            if (g_state == S_NASK) { logmsg("native: cancelled by the player before the answer"); set_state(S_IDLE); return; }
            if (g_engSearch == 1) {                                   // the game searches without us (should not be): this click stops it
                logmsg("Stop Searching by the player of a search that is not ours"); g_fsOrig(self, edx, movie, cmd, args); return; }
            g_reWant = 0; g_reRow = 0; search_click();                // the player's own click
            return;                                                   // the game's search starts only after the ladder's "yes" (tick)
        }
        if (g_screen == SC_MM && ends_with(cmd, ".OnClickBack") && g_reWant) { g_reWant = 0; logmsg("native: the player left Find a Game - not searching again"); }
        if (g_screen == SC_MM && ends_with(cmd, ".OnClickBack") && (g_state == S_NATIVE || g_state == S_NASK)) { set_state(S_IDLE); g_wantLeave = 1; SetEvent(g_wake); }
    }
    g_fsOrig(self, edx, movie, cmd, args);
}
// 0.38: the message boxes that are up (visible) - count, and press the first button (OK of a notice) on them
static int msgbox_scan(int press) {
    void *w = g_msgWrap && !IsBadReadPtr(g_msgWrap, 0x1C) ? g_msgWrap : NULL;
    void *m = w ? *(void **)((char *)w + 4) : g_mbMovie;               // the newest message box movie first (as msgbox_hide)
    void *h = w ? *(void **)((char *)w + 0x18) : g_mbFsSelf;
    if (h && IsBadReadPtr(h, 0x20)) h = NULL;
    if (!m || IsBadReadPtr(m, 8)) return 0;
    int n = 0;
    for (int k = 0; k < 8; k++) {
        char path[64], vis[80]; GFxValue v; snprintf(path, sizeof path, "_level0.container.message_box_%d", k); snprintf(vis, sizeof vis, "%s._visible", path);
        if (!as_get(m, vis, &v) || v.type != GFX_BOOL || !v.v.b) continue;
        n++; if (press && h) { const char *btn = press == 2 ? "OnButton2" : "OnButton1";
            logmsg("message box %d: %s (%s)", k, btn, press == 2 ? "No - the launcher room's walk" : "OK - native: searching again"); g_sending++; g_fsOrig(h, NULL, m, btn, path); g_sending--; }
    }
    return n;
}
static int msgbox_up(void) { return msgbox_scan(0); }
static void msgbox_ok(void) { if (!msgbox_scan(1)) logmsg("message box: none up to press"); }
// 0.45: a search of the game on Find a Game that is not our native search (should not happen - MMFind passes Start Searching to
// the game only after the ladder's "yes"; a click that got past us, an old binding): Relic's matchmaker matches nobody, the
// player would hang in it - Stop Searching (the same button) once it has been on for 1.5 s, 3 times at most until it is off.
// Not while the game still ends a native search of ours (g_stopGrace after it) and not while it is in a native match.
static void stray_stop(DWORD now) {
    static DWORD on, last; static int n;
    int stray = g_engSearch == 1 && g_screen == SC_MM && !g_loading && (g_state == S_IDLE || g_state == S_DONE) && !g_reWant
                && (int)(now - g_stopGrace) > 0 && !(g_natWatchUntil && (int)(g_natWatchUntil - now) > 0);
    if (!stray) { on = 0; if (g_engSearch == 0) n = 0; return; }
    if (!on) on = now;
    if (now - on < 1500 || (last && now - last < 2500) || n >= 3) return;
    last = now; n++;
    logmsg("the game searches without our lobby server (Relic matches nobody) - Stop Searching (%d)", n);
    snprintf(g_runWhy, sizeof g_runWhy, "%s", GetModuleHandleW(L"OwnLobby.dll") ? "the native search was not ours" : "no OwnLobby.dll in the game");
    fs(".OnClickStart", "");
}
// ---------------- 0.47: a match from a launcher room, ordered over the launcher's pipe ----------------
// Owner 05.11.2026: "a lobby in the launcher like the game's own - the map, who plays where with which army, everybody Ready, a
// 10 s countdown, then the game starts for everyone and they all land in one match". The room lives on the ladder (lroom.py);
// at the end of its countdown every launcher tells its game, over \\.\pipe\EliteMMFind-<pid of DOW2.exe> (as RoomJoin's pipe:
// only the user the game runs as, no remote clients; one JSON object per line):
//   launcher -> game  {"cmd":"status"}
//                     {"cmd":"room","room":"<id>","mode":"1v1|2v2|3v3","race":<the race drop-down index, -1 = Random>,"hero":<0..2>}
//                     {"cmd":"cancel","room":"<id>"}
//   game -> launcher  {"ev":"status","ver":"0.47","screen":"main|find_game|games_list|lobby|other|unknown","match":b,"state":"idle|..",
//                       "room":"<id>","step":"none|stop|walk|army|search|done","own_lobby":b,"pid":n}   (on connect and on "status")
//                     {"ev":"accepted|walking|army|searching|matched|in_room|loading","room":id}
//                     {"ev":"failed","room":id,"reason":"busy_match|busy_room|in_lobby|not_menu|no_ownlobby|walk_timeout|race|left_screen|
//                       refused|server_cancelled|stopped|cancelled","text":"..."}
// The walk: the main menu -> Multiplayer -> Find a Game (the main menu's own button commands, as MMFind 0.12-0.44 sent them); there
// the room's mode, race and hero (the saved-choice code of 0.5+), then Start Searching with the room's id in mm/native: the ladder
// keeps this search in the room's own pool and makes the match from the room (its host, map, teams). A normal search that runs is
// stopped first. Never from a Custom Games lobby (the player is in one - his call), a match, or another screen.
// The pipe thread never touches the engine: commands go into a queue the game's thread takes in its tick (shark::update).
enum { QC_ROOM = 1, QC_CANCEL, QC_FS };
typedef struct { int what, mode, race, hero, opt[4]; char room[24]; char path[160], args[48]; } QCmd;
typedef struct { char s[600]; } QOut;
static QCmd g_cq[8]; static int g_cqH, g_cqN; static QOut g_oq[32]; static int g_oqH, g_oqN;
static void out_push(const char *s) {
    if (!g_outEv) return;
    EnterCriticalSection(&g_qlk);
    if (g_oqN == 32) { g_oqH = (g_oqH + 1) % 32; g_oqN--; }           // nobody reads: the oldest goes
    QOut *o = &g_oq[(g_oqH + g_oqN) % 32]; strncpy(o->s, s, sizeof o->s - 2); o->s[sizeof o->s - 2] = 0; g_oqN++;
    LeaveCriticalSection(&g_qlk);
    SetEvent(g_outEv);
}
static void emitf(const char *fmt, ...) {                             // an event to every launcher connected
    char b[600]; va_list a; va_start(a, fmt); int n = _vsnprintf(b, sizeof b - 2, fmt, a); va_end(a);
    if (n < 0 || n > (int)sizeof b - 2) return;
    b[n] = 0; logmsg("room: event %s", b); strcat(b, "\n"); out_push(b);
}
static void room_set(int rs) { if (rs != g_rs) logmsg("room %s: step %s -> %s", g_room, RSN[g_rs], RSN[rs]); g_rs = rs; g_rStepAt = GetTickCount(); if (rs != RS_WALK && rs != RS_STOP) g_rWantMenu = 0; }
static void room_fail(const char *reason, const char *text) {
    char t[160] = ""; for (int i = 0; text && text[i] && i < 150; i++) t[i] = text[i] == '"' || text[i] == '\\' || (unsigned char)text[i] < 0x20 ? ' ' : text[i];
    emitf("{\"ev\":\"failed\",\"room\":\"%s\",\"reason\":\"%s\",\"text\":\"%s\"}", g_room, reason, t);
    room_set(RS_NONE);
}
static void room_ev(const char *ev) { emitf("{\"ev\":\"%s\",\"room\":\"%s\"}", ev, g_room); }
static void room_start(const QCmd *c, DWORD now) {
    if (g_rs != RS_NONE && g_rs != RS_DONE && strcmp(c->room, g_room)) {      // another room's launch runs: that one goes on
        emitf("{\"ev\":\"failed\",\"room\":\"%s\",\"reason\":\"busy_room\",\"text\":\"%s\"}", c->room, g_room); return; }
    if (g_rs != RS_NONE && g_rs != RS_DONE) { room_ev("accepted"); return; }      // the same room again: it goes on
    if (g_rs == RS_DONE && !strcmp(c->room, g_room)) { room_ev("loading"); return; }  // its match loads / runs already (a launcher restarted)
    EnterCriticalSection(&g_qlk); strcpy(g_room, c->room); LeaveCriticalSection(&g_qlk);
    for (int i = 0; i < 4; i++) g_rOpt[i] = c->opt[i];
    g_rMode = c->mode; g_rRace = c->race; g_rHero = c->hero; g_rAt = now; g_rTries = 0; g_rWalked = 0; g_rEvMatched = g_rEvRoom = 0; g_rMenuOk = 0; g_rWantMenu = 1;
    logmsg("room %s: the launcher's match - %s, race %d, hero %d, options win %d vptk %d sloc %d rsst %d (screen %d, state %s)", g_room, MODE_KEY[g_rMode], g_rRace, g_rHero,
           g_rOpt[0], g_rOpt[1], g_rOpt[2], g_rOpt[3], g_screen, SN[g_state]);
    room_ev("accepted");
    if (g_loading) { room_fail("busy_match", "the game is in a match"); return; }
    if (!GetModuleHandleW(L"OwnLobby.dll")) { room_fail("no_ownlobby", "no OwnLobby.dll in the game (Automatch v2.0 off?)"); return; }
    room_set(g_state == S_NATIVE || g_state == S_NASK ? RS_STOP : RS_WALK);
}
static void room_cancel(const char *room) {
    if (g_rs == RS_NONE || (room[0] && strcmp(room, g_room))) { logmsg("room: cancel for %s - not the launch that runs", room[0] ? room : "-"); return; }
    logmsg("room %s: cancelled by the launcher (step %s, state %s)", g_room, RSN[g_rs], SN[g_state]);
    if (g_rs == RS_SEARCH || g_rs == RS_DONE) {
        if (g_state == S_NATIVE) { set_state(S_IDLE); g_wantLeave = 1; SetEvent(g_wake); if (g_engSearch == 1 && !g_loading) fs(".OnClickStart", ""); }   // Stop Searching
        else if (g_state == S_NASK) set_state(S_IDLE);
    }
    if (g_rs != RS_DONE) room_fail("cancelled", "");
}
// the room's army into the saved choice the Find a Game code applies (apply_race / apply_hero / Random)
static void room_choice(void) {
    g_mode = g_rMode;
    if (g_rRace < 0) g_random = 1;
    else { g_random = 0; if (g_rRace != g_race) g_chapter = -1; g_race = g_rRace; g_hero = g_rHero; }
}
static void room_step(DWORD now) {
    if (g_rEvMatched == 1) { g_rEvMatched = 2; room_ev("matched"); }
    if (g_rEvRoom == 1) { g_rEvRoom = 2; room_ev("in_room"); }
    switch (g_rs) {
    case RS_STOP:                                                             // a normal search first: Stop Searching
        if (g_state == S_NASK) set_state(S_IDLE);
        if (g_state == S_NATIVE) { logmsg("room %s: stopping the search that runs", g_room); set_state(S_IDLE); g_wantLeave = 1; SetEvent(g_wake); if (g_engSearch == 1) fs(".OnClickStart", ""); }
        if ((g_engSearch != 1 && now - g_rStepAt > 1500) || now - g_rStepAt > 6000) room_set(RS_WALK);
        break;
    case RS_WALK:
        if (g_loading) { room_fail("busy_match", "the game is in a match"); break; }
        if (now - g_rAt > 60000) { room_fail("walk_timeout", "Find a Game did not open in 60 s"); break; }
        g_rWantMenu = 1;                                                     // the menu and its own handler from the engine's calls
        if (!g_fsSelf || !g_fsMovie) break;
        if (!g_rMenuOk && now - g_rAt < 3000) break;
        if (!g_scrTick || now - g_rStepAt > 2500) { static DWORD lp; if (now - lp > 500) { lp = now; probe_screen(); } }   // the screen we came in on
        if (!g_scrTick) break;                                               // the menu is not up yet (the game is still starting)
        if (g_screen == SC_MM && now - g_scrTick > 800) {
            g_rWantMenu = 0; room_choice(); g_applyMM = 1; g_mmApplied = 1; room_set(RS_ARMY); room_ev("army"); break; }
        // the game's question on the first Multiplayer of an account ("Play against CPU? ... an offline game against the CPU?"):
        // its menu ignores every command while a box is up (stand 2, 06.11) - No (OnButton2), then Multiplayer again
        if (g_screen == SC_MAIN && g_rWalked && now - g_rStepAt > 700 && msgbox_up()) { g_rStepAt = now; msgbox_scan(2); break; }
        if (g_screen == SC_MAIN && now - g_scrTick > 1000 && now - g_rStepAt > 1500) {
            if (!g_rWalked) room_ev("walking");
            // Find a Game straight from the main menu (MainMenuForm::OnClickMatchMaking): Multiplayer first asks an account that never
            // played online "Play against CPU?" (stand 2, 06.11) and the menu then ignores every command - so Multiplayer only on the
            // 4th try, when the straight way did not open it
            g_rWalked++; g_rStepAt = now; if (g_rWalked == 4) fs(".OnClickMultiplayer", ""); fs(".OnClickMatchMaking", ""); break; }
        if (g_screen == SC_LIST && now - g_rStepAt > 1500) { g_rStepAt = now; fs(".OnClickBack", ""); break; }   // the games list: back to the menu
        if (g_screen == SC_SETUP) { room_fail("in_lobby", "the game is in a Custom Games lobby - leave it first"); break; }
        if (g_screen == SC_OTHER && now - g_scrTick > 4000 && now - g_rAt > 4000) room_fail("not_menu", "the game is on another screen - go to the main menu");
        break;
    case RS_ARMY:                                                             // the choice applied (upd_hook: race, then hero 0.8 s on)
        if (g_screen != SC_MM) { room_fail("left_screen", "Find a Game was left"); break; }
        if (g_applyMM || g_heroAt || now - g_rStepAt < 1800) break;
        if (g_rRace >= 0 && g_curRace != g_rRace) {
            if (g_rTries++ < 2) { logmsg("room %s: race shown %d, wanted %d - again", g_room, g_curRace, g_rRace); room_choice(); g_applyMM = 1; g_rStepAt = now; break; }
            room_fail("race", "the race could not be set on Find a Game"); break; }
        if (g_rRace >= 0 && g_rHero >= 0 && g_curHero >= 0 && g_curHero != g_rHero && g_rTries < 3) {
            g_rTries++; logmsg("room %s: hero shown %d, wanted %d - again", g_room, g_curHero, g_rHero); apply_hero(); g_rStepAt = now; break; }
        if (g_heroUp) {                                                       // the hero panel's own Ready: the panel closes, as after a click
            logmsg("room %s: army set - the hero panel's Ready", g_room); fs(".hero_selection_mc.OnClickAccept", ""); g_heroUp = 0; g_rStepAt = now - 1000; break; }
        logmsg("room %s: army on screen race %d hero %d (random %d) - Start Searching", g_room, g_curRace, g_curHero, g_random);
        g_reWant = 0; g_reRow = 0; search_click(); room_set(RS_SEARCH); room_ev("searching");
        break;
    case RS_SEARCH:
        if (g_loading) break;
        if (g_state == S_IDLE && now - g_rStepAt > 1500) room_fail(g_runWhy[0] ? "refused" : "stopped", g_runWhy);
        break;
    }
}
static void room_take(DWORD now) {
    for (;;) {
        QCmd c; int have = 0;
        EnterCriticalSection(&g_qlk);
        if (g_cqN) { c = g_cq[g_cqH]; g_cqH = (g_cqH + 1) % 8; g_cqN--; have = 1; }
        LeaveCriticalSection(&g_qlk);
        if (!have) return;
        if (c.what == QC_ROOM) room_start(&c, now); else if (c.what == QC_CANCEL) room_cancel(c.room);
        else if (c.what == QC_FS) { g_rWantMenu = 1; probe_screen(); fs(c.path, c.args); emitf("{\"ev\":\"fs_done\",\"screen\":%d}", g_screen); }
    }
}

// ---------------- the pipe (its own thread; never the engine) - RoomJoin 9's, for MMFind's commands ----------------
#define MAXC 4
typedef struct { HANDLE h; OVERLAPPED ov; char rb[512]; char line[1024]; int len, drop, rd; DWORD id; } Cli;
static Cli g_c[MAXC]; static DWORD g_nextId = 1; static wchar_t g_pipeName[80];
static const char *SC_NM[] = {"other", "main", "find_game", "games_list", "lobby"};
static void cli_close(Cli *c, const char *why) {
    if (!c->h) return;
    logmsg("pipe: client %lu gone (%s)", c->id, why);
    if (c->rd) { DWORD n; CancelIoEx(c->h, &c->ov); GetOverlappedResult(c->h, &c->ov, &n, TRUE); c->rd = 0; }
    DisconnectNamedPipe(c->h); CloseHandle(c->h); c->h = NULL;
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
    char room[24]; EnterCriticalSection(&g_qlk); strcpy(room, g_room); LeaveCriticalSection(&g_qlk);
    int sc = g_screen;
    _snprintf(b, cap - 1, "{\"ev\":\"status\",\"ver\":\"" MMF_VER "\",\"screen\":\"%s\",\"match\":%s,\"state\":\"%s\",\"room\":\"%s\",\"step\":\"%s\","
              "\"own_lobby\":%s,\"game\":\"%s\",\"pid\":%lu}\n",
              !g_scrTick ? "unknown" : sc >= 0 && sc < 5 ? SC_NM[sc] : "unknown", g_loading ? "true" : "false", SN[g_state], g_rs ? room : "",
              RSN[g_rs], GetModuleHandleW(L"OwnLobby.dll") ? "true" : "false", g_game, GetCurrentProcessId());
    b[cap - 1] = 0;
}
static void cli_line(Cli *c, char *s) {
    char cmd[16] = "", room[24] = "", mode[8] = "";
    jstr(s, "cmd", cmd, sizeof cmd); jstr(s, "room", room, sizeof room);
    for (char *p = room; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9'))) { room[0] = 0; break; }
    if (!strcmp(cmd, "status")) { char b[400]; status_line(b, sizeof b); cli_write(c, b); return; }
    QCmd q; memset(&q, 0, sizeof q);
    if (!strcmp(cmd, "room") && room[0]) {
        jstr(s, "mode", mode, sizeof mode); q.mode = !strcmp(mode, "3v3") ? 2 : !strcmp(mode, "2v2") ? 1 : 0;
        q.race = jint(s, "race", -1); q.hero = jint(s, "hero", -1);
        if (q.race < -1 || q.race > 8) q.race = -1;
        if (q.hero < -1 || q.hero > 2) q.hero = -1;
        static const char *OK[4] = {"win", "vptk", "sloc", "rsst"};
        for (int i = 0; i < 4; i++) { q.opt[i] = jint(s, OK[i], -1); if (q.opt[i] < -1 || q.opt[i] > 20) q.opt[i] = -1; }
        q.what = QC_ROOM; strcpy(q.room, room);
    } else if (!strcmp(cmd, "cancel")) { q.what = QC_CANCEL; strcpy(q.room, room); }
    else if (!strcmp(cmd, "fs") && g_debugPipe) {           // [MMFind] DebugPipe=1 (stands only): a menu command as its button sends it
        jstr(s, "path", q.path, sizeof q.path); jstr(s, "args", q.args, sizeof q.args); if (!q.path[0]) { cli_write(c, "{\"ev\":\"error\"}\n"); return; }
        q.what = QC_FS; }
    else { cli_write(c, "{\"ev\":\"error\",\"reason\":\"bad_command\"}\n"); return; }
    logmsg("pipe: client %lu: %s %s (queued for the game's thread)", c->id, cmd, room);
    EnterCriticalSection(&g_qlk);
    if (g_cqN < 8) { g_cq[(g_cqH + g_cqN) % 8] = q; g_cqN++; } else q.what = 0;
    LeaveCriticalSection(&g_qlk);
    if (!q.what) cli_write(c, "{\"ev\":\"error\",\"reason\":\"queue_full\"}\n");
}
static int cli_read(Cli *c) {
    ResetEvent(c->ov.hEvent);
    DWORD n = 0;
    if (!ReadFile(c->h, c->rb, sizeof c->rb, &n, &c->ov) && GetLastError() != ERROR_IO_PENDING) { cli_close(c, "read failed"); return 0; }
    c->rd = 1; return 1;
}
static void cli_got(Cli *c) {
    DWORD n = 0; c->rd = 0;
    if (!GetOverlappedResult(c->h, &c->ov, &n, FALSE)) { DWORD e = GetLastError(); if (e != ERROR_MORE_DATA) { cli_close(c, e == ERROR_BROKEN_PIPE || e == ERROR_PIPE_NOT_CONNECTED || e == ERROR_OPERATION_ABORTED ? "closed" : "read failed"); return; } }
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
    if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_INVALID_PARAMETER)
        h = CreateNamedPipeW(g_pipeName, open, mode, PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, sa);
    return h;
}
static DWORD WINAPI pipe_thread(LPVOID _) {
    _snwprintf(g_pipeName, 80, L"\\\\.\\pipe\\EliteMMFind-%lu", GetCurrentProcessId()); g_pipeName[79] = 0;
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, FALSE}; HANDLE tok; static unsigned char tu[256]; DWORD len = 0; wchar_t *sid = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        if (GetTokenInformation(tok, TokenUser, tu, sizeof tu, &len)) ConvertSidToStringSidW(((TOKEN_USER *)tu)->User.Sid, &sid);
        CloseHandle(tok);
    }
    if (sid) { wchar_t sddl[200]; _snwprintf(sddl, 199, L"D:P(A;;GA;;;%ls)", sid); sddl[199] = 0; LocalFree(sid);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL)) sa.lpSecurityDescriptor = NULL; }
    if (!sa.lpSecurityDescriptor) { logmsg("pipe: no security descriptor for the current user - no pipe"); return 0; }
    HANDLE lis = pipe_new(&sa, 1);
    if (lis == INVALID_HANDLE_VALUE) { logmsg("pipe: cannot create the pipe (error %lu) - no launcher rooms", GetLastError()); return 0; }
    OVERLAPPED lov; memset(&lov, 0, sizeof lov); lov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < MAXC; i++) g_c[i].ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    logmsg("pipe: EliteMMFind-%lu open (only this user, no remote clients)", GetCurrentProcessId());
    int pending = 0, connNow = 0;
    for (;;) {
        if (!pending && lis != INVALID_HANDLE_VALUE) {
            ResetEvent(lov.hEvent); connNow = 0;
            if (ConnectNamedPipe(lis, &lov)) { connNow = 1; SetEvent(lov.hEvent); }
            else { DWORD e = GetLastError(); if (e == ERROR_PIPE_CONNECTED) { connNow = 1; SetEvent(lov.hEvent); } else if (e != ERROR_IO_PENDING) { logmsg("pipe: ConnectNamedPipe error %lu", e); CloseHandle(lis); lis = INVALID_HANDLE_VALUE; } }
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
            if (s < 0) { logmsg("pipe: a fifth client - refused"); DisconnectNamedPipe(lis); continue; }
            Cli *c = &g_c[s]; c->h = lis; c->len = 0; c->drop = 0; c->rd = 0; c->id = g_nextId++;
            logmsg("pipe: client %lu connected", c->id);
            lis = pipe_new(&sa, 0);
            char b[400]; status_line(b, sizeof b);
            if (cli_write(c, b)) cli_read(c);
        } else if (k == -2) {
            for (;;) {
                QOut o; int have = 0;
                EnterCriticalSection(&g_qlk);
                if (g_oqN) { o = g_oq[g_oqH]; g_oqH = (g_oqH + 1) % 32; g_oqN--; have = 1; }
                LeaveCriticalSection(&g_qlk);
                if (!have) break;
                for (int i = 0; i < MAXC; i++) if (g_c[i].h) cli_write(&g_c[i], o.s);
            }
        } else cli_got(&g_c[k]);
    }
}

// ---------------- 0.47: the match options of a launcher room's match (only the host's count) ----------------
// The host owns the options: the game builds "<victory condition> -1 <n> <key> <value> ... <extra>" (keys as fourcc numbers: VPTK
// 1448105035, SLOC 1397509955, RSST 1381192532, MATY 1296127065) and gives it to the match object (*0x01335014)->vtbl[0x24]() ->
// match->vtbl[0x48] (stdcall this, {char *p, cap, len}; 0x5F74A0 in DOW2.exe 3.19); the host sends that matchinfo to everybody.
// The native automatch writes its defaults there (research 04.11.2026: lmatch/MatchOpts, RSST=1 -> High Resources in the match).
// The entry is swapped once (only when it is 0x5F74A0); a string is rewritten only while this game searches for a launcher room
// that has options, and only one with MATY (a match's options) - everything else passes as it is (and is logged with Verbose=1).
typedef struct { char *p; unsigned cap; unsigned len; } OptBlob;
typedef int (__stdcall *SetOptFn)(void *self, OptBlob *b);
static SetOptFn g_optOrig; static volatile int g_optHooked;
#define FN_SETOPTS 0x005F74A0
static int __stdcall opts_hook(void *self, OptBlob *b) {
    char in[512] = {0}; unsigned n = (b && b->p && b->len < sizeof in && !IsBadReadPtr(b->p, b->len)) ? b->len : 0;
    if (n) memcpy(in, b->p, n);
    int want = (g_rs == RS_SEARCH || g_rs == RS_ARMY) && (g_rOpt[0] >= 0 || g_rOpt[1] >= 0 || g_rOpt[2] >= 0 || g_rOpt[3] >= 0);
    if (g_verbose || want) logmsg("options: \"%s\"", in);
    if (!want || !n || !strstr(in, "1296127065")) return g_optOrig(self, b);
    long v[64]; int k = 0; char *q = in, *e;
    while (k < 64) { long x = strtol(q, &e, 10); if (e == q) break; v[k++] = x; q = e; }
    if (k < 4 || v[2] < 0 || v[2] > 20 || 3 + 2 * v[2] + 1 > k) { logmsg("options: not parsed (%d numbers) - left as the game made them", k); return g_optOrig(self, b); }
    if (g_rOpt[0] >= 0) v[0] = g_rOpt[0];
    for (int i = 0; i < v[2]; i++) {
        long key = v[3 + 2 * i], *val = &v[4 + 2 * i];
        if (key == 0x5650544B && g_rOpt[1] >= 0) *val = g_rOpt[1];
        if (key == 0x534C4F43 && g_rOpt[2] >= 0) *val = g_rOpt[2];
        if (key == 0x52535354 && g_rOpt[3] >= 0) *val = g_rOpt[3];
    }
    char *out = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 512); int o = 0;   // the game keeps it (a few bytes, never freed)
    if (!out) return g_optOrig(self, b);
    for (int i = 0; i < 3 + 2 * v[2] + 1 && o < 480; i++) o += _snprintf(out + o, 512 - o, "%ld ", v[i]);
    OptBlob nb = { out, 0, (unsigned)o };
    logmsg("options: the launcher room's -> \"%s\"", out);
    return g_optOrig(self, &nb);
}
static void opts_install(void) {                     // on the game's thread, once the RLink manager is there
    static DWORD last; static int tries;
    if (g_optHooked || tries > 600 || GetTickCount() - last < 1000) return; last = GetTickCount(); tries++;
    void **m = *(void ***)0x01335014; if (!m || IsBadReadPtr(m, 4) || IsBadReadPtr(*m, 0x28)) return;
    typedef void *(__fastcall *GetFn)(void *self, void *edx);
    void *match = ((GetFn)((*(void ***)m)[0x24 / 4]))(m, 0);
    if (!match || IsBadReadPtr(match, 4)) return;
    void **vt = *(void ***)match; if (IsBadReadPtr(vt, 0x4C)) return;
    if ((DWORD)vt[0x48 / 4] != FN_SETOPTS) { logmsg("options: the match's set-options entry is %p, not 0x%X (not DOW2.exe 3.19?) - room options off", vt[0x48 / 4], FN_SETOPTS); g_optHooked = -1; return; }
    g_optOrig = (SetOptFn)vt[0x48 / 4]; void *fn = (void *)opts_hook;
    HANDLE mx = patch_lock(); DWORD o; SIZE_T w = 0;
    if (VirtualProtect(&vt[0x48 / 4], 4, PAGE_READWRITE, &o)) { WriteProcessMemory(GetCurrentProcess(), &vt[0x48 / 4], &fn, 4, &w); VirtualProtect(&vt[0x48 / 4], 4, o, &o); }
    patch_unlock(mx);
    g_optHooked = w == 4 ? 1 : -1;
    logmsg(w == 4 ? "options: the match's set-options entry hooked (vtable %p) - a launcher room's host sets its options" : "options: the entry could not be written (vtable %p)", vt);
}

static void tick(void) {
    static DWORD last; DWORD now = GetTickCount(); if (now - last < 250) return; last = now;
    { static DWORD hb; if (now - hb > 30000) { hb = now; logmsg("tick alive, state %s screen %d", SN[g_state], g_screen); } }
    mode_peek(now);                                                   // 0.42
    stray_stop(now);                                                  // 0.45
    room_take(now); room_step(now);                                   // 0.47: the launcher's room
    opts_install();
    if (g_natCancelled) {                                             // 0.38: the server called our native match off
        g_natCancelled = 0;
        if (g_rs == RS_SEARCH) { g_natAgain = 0; room_fail("server_cancelled", "the server called the match off (the launcher room says why)"); }
        if (g_state == S_NATIVE || (g_state == S_IDLE && g_natWatchUntil)) {
            g_natWatchUntil = 0;
            if (!g_natAgain) logmsg("native: the match was cancelled by the server (I was the reason) - idle");
            else if (g_reRow >= 3) logmsg("native: the match was cancelled by the server - %d searches again in a row already, not once more", g_reRow);
            else { g_reWant = 1; g_reBoxOk = 0; g_reSince = now; logmsg("native: the match was cancelled by the server, not because of me - searching again when the game is back on Find a Game"); }
            if (g_state == S_NATIVE) set_state(S_IDLE);
        }
    }
    if (g_reWant && (g_state != S_IDLE || g_loading)) g_reWant = 0;      // something else is going on (the player searched himself)
    if (g_reWant) {
        int box = msgbox_up();
        if (box && !g_reBoxOk && now - g_reSince > 3000) { g_reBoxOk = 1; msgbox_ok(); }      // the notice ("Countdown was stopped") away
        int engOff = g_engSearch == 0 || (g_engSearch < 0 && now - g_scrTick > 3000);
        if (now - g_reSince > 45000) { logmsg("native: not searching again - the game was not back on Find a Game with its search off in 45 s"); g_reWant = 0; }
        else if (g_screen == SC_MM && engOff && now - g_scrTick > 1000 && (!box || now - g_reSince > 10000) && (!g_reLast || now - g_reLast >= 30000)) {
            g_reWant = 0; g_reLast = now; g_reRow++; logmsg("native: Start Searching again by itself (%d in a row)", g_reRow); search_click();
        }
    }
    switch (g_state) {
    case S_NASK:                                                      // 0.38: waiting for the ladder's word (net_worker, <= 1.5 s)
        if (g_screen != SC_MM) { logmsg("native: left Find a Game while asking"); set_state(S_IDLE); break; }
        if (g_natAnswer == 1) {
            g_natAnswer = 0; char a[8]; snprintf(a, sizeof a, "0;%d", g_mode);
            fs(".OnSelectDropDownItem", a);                           // the engine's own match type = the one on screen (it resets it by itself)
            native_fast(1);                                           // 0.39: poll every 1 s, countdown 2 s - before the game's search starts
            fs(".OnClickStart", "");                                  // the game's own Start Searching -> automatch/startpolling on our server
            g_natPass = now; logmsg("native: Start Searching passed to the game %lu ms after the click (%s)", now - g_searchTick, MODE_KEY[g_mode]);
            set_state(S_NATIVE);
        } else if (g_natAnswer == 2 || in_state() > 3000) {           // 0.45: no other search - the reason on the screen
            const char *why = g_natAnswer == 2 ? g_natWhy : "the ladder did not answer in time"; g_natAnswer = 0;
            set_state(S_IDLE); search_blocked(why);
        }
        break;
    case S_NATIVE: {                                                  // 0.38: the game searches by itself; we only watch the ladder
        DWORD quiet = g_natSeen && (int)(g_natGood - g_natPass) > 0 ? now - g_natGood : now - g_natPass;
        if (quiet <= 10000) break;
        const char *why = g_natSeen ? "the ladder lost the native search" : "the ladder never saw the native search";
        if (g_screen != SC_MM) { logmsg("native: %s (%lu ms) and not on Find a Game - idle (the game goes on by itself)", why, quiet); set_state(S_IDLE); g_natWatchUntil = now + 30000; break; }   // no leave: it would call off a match the game may still be setting up
        // 0.45: nothing else to search with - the game's search stopped (it would hang), the reason on the screen
        logmsg("native: %s (%lu ms quiet) - the game's own search %s, the reason on the screen", why, quiet, g_engSearch == 1 ? "on: stopping it" : g_engSearch == 0 ? "off" : "unknown");
        if (g_engSearch == 1) fs(".OnClickStart", "");                // Stop Searching (the same button); stray_stop checks it again after 5 s
        g_queue = -1; set_state(S_IDLE); g_wantLeave = 1; SetEvent(g_wake); search_blocked(why);
        break; }
    }
}
static void __stdcall upd_hook(float dt) {
    g_updOrig(dt);
    DWORD now = GetTickCount(); int alive = g_fsMovie && g_state != S_DONE;            // after Start the frontend goes away - hands off
    if (g_applyMM && alive) { static DWORD t; if (!t) t = now; if (now - t > 400) { g_applyMM = 0; t = 0;      // Find a Game: the saved army and mode
        if (g_random) { g_autoSelTick = now; fs(".hero_selection_mc.OnClickRandom", ""); g_heroAt = 0; } else g_heroAt = now + (apply_race() ? 800 : 1);
        char a[8]; snprintf(a, sizeof a, "0;%d", g_mode); fs(".OnSelectDropDownItem", a); } }
    if (g_heroAt && now >= g_heroAt) { g_heroAt = 0; if (alive && g_screen == SC_MM) apply_hero(); }
    tick();
}

// ---------------- install ----------------
static int iat_hook(DWORD slot, const char *dll, void *fn, void **orig) {
    HMODULE m = GetModuleHandleA(dll); MODULEINFO mi;
    if (!m || !GetModuleInformation(GetCurrentProcess(), m, &mi, sizeof mi) || IsBadReadPtr((void *)slot, 4)) return 0;
    // one import-table write at a time across all our DLLs (LobbyStats hooks this same Shark slot from its own thread; two
    // threads opening and closing the page at once crashed the game at start): the shared mutex, the slot read again under
    // it (the other hook may be in by now - we chain onto it), the write through WriteProcessMemory (fails, never crashes)
    HANDLE mx = patch_lock();
    int ok = 0; DWORD cur = *(volatile DWORD *)slot, old;
    if (VirtualProtect((void *)slot, 4, PAGE_READWRITE, &old)) {
        SIZE_T w = 0;
        if (WriteProcessMemory(GetCurrentProcess(), (void *)slot, &fn, 4, &w) && w == 4) { *orig = (void *)cur; ok = 1; }
        VirtualProtect((void *)slot, 4, old, &old);
    }
    patch_unlock(mx);
    return ok;
}
static DWORD WINAPI installer(LPVOID _) {
    for (int i = 0; i < 180; i++) {
        if (GetModuleHandleA("Shark.dll") && iat_hook(IAT_SHARK_UPDATE, "Shark.dll", (void *)upd_hook, (void **)&g_updOrig)) {
            logmsg("frame hook installed");
            if (g_pipeCfg) return pipe_thread(NULL);                   // 0.47: this thread now serves the launcher's pipe
            return 0; }
        Sleep(1000);
    }
    logmsg("frame hook NOT installed"); return 0;
}
static void install(void) {
    unsigned char *t = (unsigned char *)HOOK_ADDR;
    int chained = !IsBadReadPtr(t, 5) && t[0] == 0xE9;                // another add-on (LobbyStats) already hooked Invoke: wrap it
    if (IsBadReadPtr(t, 5) || (memcmp(t, PROLOGUE, 5) && !chained)) { logmsg("Invoke prologue not recognised - not starting"); return; }
    unsigned char *f = (unsigned char *)FN_FSCMD;
    if (IsBadReadPtr(f, 7) || memcmp(f, FSCMD_PRO, 7)) { logmsg("fscommand prologue mismatch - not starting"); return; }
    unsigned char *tr = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (chained) { DWORD dest = (DWORD)(t + 5) + *(DWORD *)(t + 1); tr[0] = 0xE9; *(DWORD *)(tr + 1) = dest - (DWORD)(tr + 5); logmsg("Invoke already hooked by another add-on (-> %08lX), chaining", dest); }
    else { memcpy(tr, PROLOGUE, 5); tr[5] = 0xE9; *(DWORD *)(tr + 6) = HOOK_RESUME - (DWORD)(tr + 10); }
    g_invOrig = (InvokeFn)tr;
    memcpy(tr + 16, FSCMD_PRO, 7); tr[23] = 0xE9; *(DWORD *)(tr + 24) = (FN_FSCMD + 7) - (DWORD)(tr + 28); g_fsOrig = (FsCmdFn)(tr + 16);
    code_jmp(t, (void *)inv_hook, 5);
    code_jmp(f, (void *)fs_hook, 7);
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    logmsg("hooked Invoke + fscommand");
    install_sfm_hook();
    CloseHandle(CreateThread(NULL, 0, installer, NULL, 0, NULL));
    CloseHandle(CreateThread(NULL, 0, net_worker, NULL, 0, NULL));
}
static unsigned long long my_steam(void) {                         // Steam writes the active account id here
    DWORD v = 0, sz = sizeof v; HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"ActiveUser", NULL, NULL, (BYTE *)&v, &sz); RegCloseKey(k); }
    return v ? 76561197960265728ULL + v : 0;
}
// 0.46: Relay.dll of the automatch package, from MMFind's own folder (Elite_DLLs\ or dow2-patches\). In a thread: never a
// LoadLibrary under the loader lock of our own DllMain (the thread runs once the injector's LoadLibrary of MMFind is over).
static DWORD WINAPI relay_load(LPVOID _) {
    if (GetModuleHandleW(L"Relay.dll")) { logmsg("Relay.dll already loaded - the automatch's games go to our servers over UDP"); return 0; }
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsRelay.dll", g_dir);
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) { logmsg("no Relay.dll next to MMFind.dll - a match on our servers connects without our UDP tunnel"); return 0; }
    HMODULE m = LoadLibraryW(p);
    if (m) logmsg("Relay.dll loaded - the automatch's games go to our servers over UDP (its log: Relay.log)");
    else logmsg("Relay.dll did not load (error %lu) - a match on our servers connects without our UDP tunnel", GetLastError());
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID _) {
    if (r != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_lk); g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    InitializeCriticalSection(&g_qlk); g_outEv = CreateEventW(NULL, FALSE, FALSE, NULL);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0;
    wchar_t ini[MAX_PATH]; _snwprintf(ini, MAX_PATH, L"%lsMMFind.ini", g_dir);
    GetPrivateProfileStringW(L"MMFind", L"Server", MM_HOST, g_server, 128, ini); if (!wcscmp(g_server, L"rts.fintebtc.de")) wcscpy(g_server, MM_HOST);   // old single-host ini -> all mirrors
    parse_hosts();
    g_mode = GetPrivateProfileIntW(L"Choice", L"Mode", 0, ini); if (g_mode < 0 || g_mode > 3) g_mode = 0;
    g_maxDiff = GetPrivateProfileIntW(L"MMFind", L"MaxRatingDiff", 0, ini);
    g_fastCfg = GetPrivateProfileIntW(L"MMFind", L"NativeFast", 1, ini) != 0;  // 0.39, see native_fast
    g_showMode = GetPrivateProfileIntW(L"MMFind", L"ShowMode", 1, ini) != 0;   // 0.42, see g_showMode
    { wchar_t t[16]; GetPrivateProfileStringW(L"MMFind", L"Lang", L"auto", t, 16, ini); g_langCfg = !_wcsicmp(t, L"ru") ? 1 : !_wcsicmp(t, L"en") ? 0 : -1; }
    { wchar_t t[80]; GetPrivateProfileStringW(L"MMFind", L"TestLadder", L"", t, 80, ini); wchar_t *c = wcsrchr(t, L':');   // 0.38 tests, see g_tlHost
      if (t[0] && c && _wtoi(c + 1) > 0 && _wtoi(c + 1) < 65536) { *c = 0; wcsncpy(g_tlHost, t, 63); g_tlPort = (INTERNET_PORT)_wtoi(c + 1); } }
    g_verbose = GetPrivateProfileIntW(L"MMFind", L"Verbose", 0, ini);
    g_pipeCfg = GetPrivateProfileIntW(L"MMFind", L"Pipe", 1, ini) != 0;
    g_debugPipe = GetPrivateProfileIntW(L"MMFind", L"DebugPipe", 0, ini) != 0;   // stands only: menu commands over the pipe   // 0.47: the launcher rooms' pipe (0 = none)
    g_logInvoke = GetPrivateProfileIntW(L"MMFind", L"LogInvoke", 0, ini);   // debug: every engine -> Flash call
    { wchar_t g[32]; GetPrivateProfileStringW(L"MMFind", L"TestGroup", L"", g, 32, ini); WideCharToMultiByte(CP_UTF8, 0, g, -1, g_group, sizeof g_group, NULL, NULL); }
    wcscpy(g_ini, ini);
    g_race = GetPrivateProfileIntW(L"Choice", L"Race", -1, ini); g_chapter = GetPrivateProfileIntW(L"Choice", L"Chapter", -1, ini); g_hero = GetPrivateProfileIntW(L"Choice", L"Hero", -1, ini); g_random = GetPrivateProfileIntW(L"Choice", L"Random", 0, ini);
    g_mySteam = my_steam();
    { wchar_t p[MAX_PATH], o[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsMMFind.log", g_dir); _snwprintf(o, MAX_PATH, L"%lsMMFind.log.old", g_dir);
      WIN32_FILE_ATTRIBUTE_DATA a; if (GetFileAttributesExW(p, GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > (1u << 20))) MoveFileExW(p, o, MOVEFILE_REPLACE_EXISTING); }
    logmsg("MMFind " MMF_VER " start, steam=%llu server=%ls - only the native automatch (the game's own search on our lobby server, OwnLobby.dll %s)",
           g_mySteam, g_server, GetModuleHandleW(L"OwnLobby.dll") ? "loaded" : "not loaded yet");
    if (g_tlHost[0]) logmsg("TEST LADDER: every ladder API call goes to http://%ls:%u (TestLadder=)", g_tlHost, (unsigned)g_tlPort);
    { char n[64]; wsprintfA(n, "Local\\dow2patch_MMFind_%lu", GetCurrentProcessId());          // one copy per game (launcher + dow2-patches)
      HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx && GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("another MMFind copy is active, this one stays idle"); return TRUE; } }
    CloseHandle(CreateThread(NULL, 0, relay_load, NULL, 0, NULL));   // 0.46: the UDP transport of the automatch
    install(); CloseHandle(CreateThread(NULL, 0, nodes_fetch, NULL, 0, NULL)); return TRUE;
}
