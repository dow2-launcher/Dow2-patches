// LiveReplay.dll - watch a running match of our battle servers in the game, as a replay that keeps growing (DoW2: Retribution
// 3.19, Elite and vanilla). Owner 06.11.2026: "зритель открывает матч в лаунчере и смотрит его как реплей, который не
// заканчивается и подтягивается вживую, с задержкой, заданной сервером".
//
// Nothing runs in the players' games: our battle server (battle_relay.py + live_rec.py) builds the .rec of the match from the
// turns it carries and the host's lobby state, the ladder serves it LIVE_DELAY s behind (2 min by default; the server's setting).
// This add-on is only the spectator's side:
//   - the launcher's "Watch" on a running match -> the pipe \\.\pipe\EliteLiveReplay-<pid> (one JSON object per line, as
//     RoomJoin: {"cmd":"watch","match":"<id>","base":"https://<ladder host>"}, {"cmd":"status"}, {"cmd":"stop"}; events
//     accepted / fetching / starting / playing / waiting / over / failed (reason) and status lines);
//   - its network thread asks the ladder for the replay so far (GET <base>/ladder/api/live/<match>/rec?from=N, the node's
//     "DWLR" envelope), writes it as Playback\live_watch.rec and keeps asking every 2 s for the records after it;
//   - the game's thread starts that replay as its own Game History -> Load Replay does (0x40F526: the playback setup for a
//     file, the front end then loads it) when the menu is up (not in a match, a lobby or a loading screen);
//   - while it plays, the replay does not end where the file ended: the playback's reader (vtable 0x0108BA30, slot 1 "next
//     record", 0x42BE09) gets the new records from the network, parsed by the game's own reader 0x42B451 from a byte stream of
//     ours; with nothing new yet, PlaybackSynchronization::GetSyncCommand (vtable 0x010B9E2C slot 4) answers "no step now"
//     instead of "recorded game finished", and SimulateInternal's end of a replay (call 0x42761A at 0x43809B) is skipped for
//     that step - the picture stands until the next records come; when the ladder says the match is over and everything is
//     played, the replay ends as any replay;
//   - at the start it runs the replay faster (CatchUp, 4x) up to the live edge, then normal speed (the player's own speed
//     buttons work as in any replay);
//   - sync check: every step's checksum of this game (PlaybackSynchronization slot 7, 0x66B000: (crc, tick)) against the
//     players' own (the turn's crc the server carried, in the record) - LiveReplay.log says how many matched; the launcher
//     gets them in the status.
// No overlay (only the game's own replay screen), no files read during the replay, the pipe instead of files for the launcher.
// LiveReplay.ini [LiveReplay] Enabled=0: off; CatchUp=0: no faster start; CatchUpSpeed=40 (ticks/s, 10 = normal);
// SyncOffset=0 (the record whose crc a step is compared with); Server= the ladder when the launcher sends none.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600                 // CancelIoEx (Vista+) with older mingw headers
#endif
#include <windows.h>
#include <sddl.h>
#include <shlobj.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <wchar.h>

#define LR_VERSION "2"
// ---- DOW2.exe 3.19
#define G_SETTINGS     0x01335A78         // +0x118 game type (0x800 playback)
#define G_GAMEOBJ_PTR  0x01335A74         // what the replay start takes in EDX ([+4] = the settings)
#define G_WORLD        0x013356A4         // +0x174 the playback's length (ticks), +0x178 the current tick
#define G_HUD          0x01335720         // +8: +0x68 the sim rate (vtbl+0x28 set, +0x2C get; 10 = normal)
#define VT_READER      0x0108BA30         // the recorder / playback reader; slot 1 = next record (0x42BE09)
#define FN_NEXTREC     0x0042BE09
#define VT_PLAYBACK    0x010B9E2C         // PlaybackSynchronization; slot 4 GetSyncCommand, slot 7 the step's crc
#define FN_GETSYNC     0x0066ADF0
#define FN_PBCRC       0x0066B000
#define AT_PB_END      0x0043809B         // SimulateInternal: call 0x42761A when a replay has no more steps
#define FN_PB_END      0x0042761A
#define FN_READREC     0x0042B451         // a record from a ByteStream (ECX), the game's own allocation; 0 = none
#define FN_START_PB    0x0040F526         // the replay of a file (ECX "playback:<name>.rec", EDX 0x01335A74) -> AL
#define IAT_SHARK_UPDATE 0x00F88068
#define IAT_SIM_PRE    0x00F88154
#define IAT_SIM_POST   0x00F88100
#define VT_FSCMD       0x010BAC9C
#define FN_FSCMD       0x00681AB0
#define FN_FSCMD_VT0   0x00681E80
#define T_PLAYBACK     0x800

typedef void *(__fastcall *ThisRet_t)(void *self, void *edx);
typedef char (__fastcall *GetSync_t)(void *self, void *edx, void *out, unsigned tick);
typedef void (__fastcall *PbCrc_t)(void *self, void *edx, unsigned crc, unsigned tick);
typedef void *(__fastcall *ReadRec_t)(void *stream, void *edx);
typedef char (__fastcall *StartPb_t)(const char *path, void *gs);
typedef void (__fastcall *PbEnd_t)(void *gameobj, void *edx);
typedef void (__stdcall *UpdFn)(float dt);
typedef void (__fastcall *ThisFn)(void *self, void *edx);
typedef void (__fastcall *FsCmdFn)(void *self, void *edx, void *movie, const char *cmd, const char *args);
typedef void (__fastcall *SetF_t)(void *o, void *edx, float f);

static ThisRet_t g_nextOrig = (ThisRet_t)FN_NEXTREC; static GetSync_t g_gsOrig = (GetSync_t)FN_GETSYNC; static PbCrc_t g_crcOrig = (PbCrc_t)FN_PBCRC;
static UpdFn g_updOrig; static ThisFn g_preOrig, g_postOrig; static FsCmdFn g_fsOrig;

// ---------------- the log ----------------
static wchar_t g_dir[MAX_PATH]; static CRITICAL_SECTION g_lk; static HANDLE g_log = INVALID_HANDLE_VALUE;
static void log_open(void) {
    wchar_t p[MAX_PATH], o[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsLiveReplay.log", g_dir); p[MAX_PATH - 1] = 0;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(p, GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > 512 * 1024)) {
        _snwprintf(o, MAX_PATH, L"%lsLiveReplay.log.old", g_dir); o[MAX_PATH - 1] = 0; MoveFileExW(p, o, MOVEFILE_REPLACE_EXISTING); }
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
static int ok(const void *p, int n) { return p && !IsBadReadPtr(p, n); }
#define I32(p, o) (*(int *)((char *)(p) + (o)))
#define P32(p, o) (*(char **)((char *)(p) + (o)))

// ---------------- state ----------------
// the session (one at a time): what the launcher asked, what the network thread got, what the game's thread does
enum { ST_IDLE, ST_FETCH, ST_START, ST_PLAY, ST_OVER, ST_FAILED };
static const char *ST_NAME[] = {"idle", "fetching", "starting", "playing", "over", "failed"};
static CRITICAL_SECTION g_qlk;                      // the session, the record buffer, the events
static volatile int g_state;                        // ST_*
static char g_match[32], g_base[200], g_reason[40];
static volatile LONG g_session;                     // +1 per watch: the network thread of an older one stops
static unsigned char *g_buf; static size_t g_bufLen, g_bufCap, g_bufPos;   // records not yet given to the playback
static volatile LONG g_bufTicks;                    // tick records in it
#define JITTER 30                                   // steps (3 s): after a wait the replay goes on only with this much ahead
static volatile LONG g_avail;                       // the records the ladder has given so far (the next "from")
static volatile LONG g_availTicks, g_fileTicks;     // tick records so far / in the starting file
static volatile int g_over;                         // the ladder: the match is over and everything is given
static volatile int g_fileReady, g_live, g_waiting; // the file is written / the replay that plays is ours / no step now
static volatile int g_inWorld, g_loading;           // a match or replay runs / loads (World::SimulatePre..Post)
static int g_catchUp = 1, g_cuDone; static float g_cuSpeed = 40.0f; static int g_syncOff;
static volatile LONG g_syncOk, g_syncBad, g_syncFirstBad = -1;
static unsigned *g_crc; static unsigned g_crcN;     // tick -> the players' crc + 1 (0 = not known), from the records' headers
static wchar_t g_file[MAX_PATH];
static DWORD g_startAt;

// ---------------- the pipe's events (any thread -> the pipe thread) ----------------
#define MAXC 4
static HANDLE g_outEv; static char *g_oq[32]; static int g_oqH, g_oqN;
static void emitf(const char *fmt, ...) {
    char b[600]; va_list a; va_start(a, fmt); int n = _vsnprintf(b, sizeof b - 2, fmt, a); va_end(a);
    if (n < 0 || n > (int)sizeof b - 2) n = (int)sizeof b - 2;
    b[n++] = '\n'; b[n] = 0;
    EnterCriticalSection(&g_qlk);
    if (g_oqN < 32) { g_oq[(g_oqH + g_oqN) % 32] = _strdup(b); g_oqN++; }
    LeaveCriticalSection(&g_qlk);
    if (g_outEv) SetEvent(g_outEv);
}
static void set_state(int st, const char *reason) {
    EnterCriticalSection(&g_qlk); g_state = st; if (reason) snprintf(g_reason, sizeof g_reason, "%s", reason); LeaveCriticalSection(&g_qlk);
    if (st == ST_FAILED) { logmsg("failed: %s", reason ? reason : "?"); emitf("{\"ev\":\"failed\",\"match\":\"%s\",\"reason\":\"%s\"}", g_match, reason ? reason : "error"); }
    else emitf("{\"ev\":\"%s\",\"match\":\"%s\"}", ST_NAME[st], g_match);
}

// ---------------- the records ----------------
static void crc_note(const unsigned char *rec, size_t len) {        // a tick record: its tick and the players' crc
    if (len < 8 + 17 || *(const unsigned *)rec != 0) return;
    unsigned tick = *(const unsigned *)(rec + 9), crc = *(const unsigned *)(rec + 17);
    if (!tick || tick > 4000000) return;
    if (tick >= g_crcN) {
        unsigned n = g_crcN ? g_crcN : 4096; while (n <= tick) n *= 2;
        unsigned *p = realloc(g_crc, n * sizeof *p); if (!p) return;
        memset(p + g_crcN, 0, (n - g_crcN) * sizeof *p); g_crc = p; g_crcN = n;
    }
    g_crc[tick] = crc + 1;
}
// the records of a block (u32 type, u32 len, body)*: how many bytes of whole records, how many of them are tick records
static size_t whole(const unsigned char *b, size_t n, LONG *recs, LONG *ticks) {
    size_t o = 0; LONG r = 0, t = 0;
    while (o + 8 <= n) {
        unsigned len = *(const unsigned *)(b + o + 4);
        if (len > (16u << 20) || o + 8 + len > n) break;
        if (*(const unsigned *)(b + o) == 0) t++;
        r++; o += 8 + len;
    }
    if (recs) *recs = r; if (ticks) *ticks = t;
    return o;
}
static void buf_add(const unsigned char *b, size_t n) {             // under g_qlk
    if (g_bufPos > (1u << 20) && g_bufPos * 2 > g_bufLen) { memmove(g_buf, g_buf + g_bufPos, g_bufLen - g_bufPos); g_bufLen -= g_bufPos; g_bufPos = 0; }
    if (g_bufLen + n > g_bufCap) {
        size_t c = g_bufCap ? g_bufCap : 65536; while (c < g_bufLen + n) c *= 2;
        unsigned char *p = realloc(g_buf, c); if (!p) return; g_buf = p; g_bufCap = c;
    }
    memcpy(g_buf + g_bufLen, b, n); g_bufLen += n;
    for (size_t o = 0; o + 8 <= n; o += 8 + *(const unsigned *)(b + o + 4)) {
        crc_note(b + o, 8 + *(const unsigned *)(b + o + 4)); if (*(const unsigned *)(b + o) == 0) g_bufTicks++; }
}
static int buf_has_tick(void) {                                    // under g_qlk: a whole tick record waits
    size_t o = g_bufPos;
    while (o + 8 <= g_bufLen) {
        unsigned len = *(unsigned *)(g_buf + o + 4); if (o + 8 + len > g_bufLen) return 0;
        if (*(unsigned *)(g_buf + o) == 0) return 1;
        o += 8 + len;
    }
    return 0;
}
// a byte stream the game's record reader can read (ByteStream: vtable slot 2 = Read(buffer, n) -> bytes read)
typedef struct { void **vt; const unsigned char *p; unsigned n, pos; } Bs;
static unsigned __fastcall bs_read(Bs *s, void *edx, void *dst, unsigned n) {
    (void)edx; unsigned k = s->n - s->pos < n ? s->n - s->pos : n;
    memcpy(dst, s->p + s->pos, k); s->pos += k; return k;
}
static unsigned __fastcall bs_zero(Bs *s, void *edx) { (void)s; (void)edx; return 0; }
static void *g_bsVt[16];
static void *buf_take(void) {                                       // the next whole record as the game's record object, or NULL
    void *r = NULL;
    EnterCriticalSection(&g_qlk);
    if (g_bufPos + 8 <= g_bufLen) {
        unsigned len = *(unsigned *)(g_buf + g_bufPos + 4);
        if (g_bufPos + 8 + len <= g_bufLen) {
            Bs s = {g_bsVt, g_buf + g_bufPos, 8 + len, 0};
            r = ((ReadRec_t)FN_READREC)(&s, NULL);
            if (*(unsigned *)(g_buf + g_bufPos) == 0 && g_bufTicks > 0) g_bufTicks--;
            g_bufPos += 8 + len;
            if (!r) logmsg("the game could not read a record of %u bytes (type %u) - skipped", len, *(unsigned *)(g_buf + g_bufPos - 8 - len));
        }
    }
    LeaveCriticalSection(&g_qlk);
    return r;
}

// ---------------- the playback hooks (the simulation's thread) ----------------
static int our_playback(void) {
    char *st = ok((void *)G_SETTINGS, 4) ? *(char **)G_SETTINGS : NULL;
    return g_live && st && ok(st, 0x120) && I32(st, 0x118) == T_PLAYBACK;
}
static void *__fastcall my_next(void *reader, void *edx) {
    void *r = g_nextOrig(reader, edx);
    if (!r && our_playback()) r = buf_take();
    return r;
}
static char __fastcall my_getsync(void *self, void *edx, void *out, unsigned tick) {
    if (our_playback() && ok(self, 0x50)) {
        char *rd = P32(self, 0x4C);
        int fileLeft = ok(rd, 0x18) && I32(rd, 4) != I32(rd, 0x14);
        if (!fileLeft) {
            EnterCriticalSection(&g_qlk); int have = buf_has_tick(), over = g_over; LONG ahead = g_bufTicks; LeaveCriticalSection(&g_qlk);
            if (g_waiting && !over && ahead < JITTER) have = 0;          // after a wait: a few seconds ahead first (no stutter at the edge)
            if (!have && !over) {
                if (!g_waiting) { g_waiting = 1; logmsg("waiting for the next records (tick %u)", tick); }
                return 0;                                             // no step now; SimulateInternal's replay end is skipped (my_pb_end)
            }
            if (!have && over && g_state != ST_OVER) { logmsg("the match is over - the replay ends here (tick %u)", tick); }
        }
        if (g_waiting) { g_waiting = 0; logmsg("records again, going on (tick %u)", tick); }
    }
    return g_gsOrig(self, edx, out, tick);
}
static void __fastcall my_pb_end(void *gameobj, void *edx) {          // the call at 0x43809B (ECX = GameObj)
    if (g_waiting && our_playback()) return;                           // only "no records yet": not the end
    ((PbEnd_t)FN_PB_END)(gameobj, edx);
}
static int g_crcLogged;
static void __fastcall my_pbcrc(void *self, void *edx, unsigned crc, unsigned tick) {
    if (our_playback()) {
        unsigned t = tick + g_syncOff, want, a, b2, c2;
        EnterCriticalSection(&g_qlk);                                  // (the network thread may grow the table)
        want = t < g_crcN ? g_crc[t] : 0; a = tick - 1 < g_crcN ? g_crc[tick - 1] : 0; b2 = tick < g_crcN ? g_crc[tick] : 0; c2 = tick + 1 < g_crcN ? g_crc[tick + 1] : 0;
        LeaveCriticalSection(&g_qlk);
        if (g_crcLogged < 12) { g_crcLogged++; logmsg("sync: step %u crc %08X, players' %u: %08X, %u: %08X, %u: %08X", tick, crc,
            tick - 1, a ? a - 1 : 0, tick, b2 ? b2 - 1 : 0, tick + 1, c2 ? c2 - 1 : 0); }
        if (want) {
            if (want - 1 == crc) InterlockedIncrement(&g_syncOk);
            else { LONG b = InterlockedIncrement(&g_syncBad); if (g_syncFirstBad < 0) g_syncFirstBad = (LONG)tick;
                   if (b <= 5) logmsg("sync: step %u - this game %08X, the players %08X: OUT OF SYNC", tick, crc, want - 1); }
            if (((g_syncOk + g_syncBad) % 600) == 0) logmsg("sync: %ld steps checked, %ld matched, %ld not", g_syncOk + g_syncBad, g_syncOk, g_syncBad);
        }
    }
    g_crcOrig(self, edx, crc, tick);
}
// SimulateInternal's call of 0x42761A: a stub with ECX kept (fastcall: ECX, EDX)
static int patch_call(DWORD at, DWORD expect, void *fn) {
    unsigned char *t = (unsigned char *)at; DWORD old;
    if (IsBadReadPtr(t, 5) || t[0] != 0xE8 || (DWORD)(t + 5) + *(int *)(t + 1) != expect) return 0;
    VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old); *(int *)(t + 1) = (DWORD)fn - (DWORD)(t + 5);
    VirtualProtect(t, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 5);
    return 1;
}
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

// ---------------- the menu (which full-screen panel is up: as RoomJoin) ----------------
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
                    if (sc != g_screen) logmsg("screen %s", SC_NAME[sc]);
                    g_screen = sc;
                }
            }
        }
    }
    g_fsOrig(self, edx, movie, cmd, args);
}
static void __fastcall pre_hook(void *self, void *edx) { g_inWorld = 1; g_loading = 1; g_preOrig(self, edx); }
static void __fastcall post_hook(void *self, void *edx) {
    g_inWorld = 0; g_loading = 0;
    if (g_live) { g_live = 0; g_waiting = 0; logmsg("the replay is over (left or ended); sync: %ld matched, %ld not", g_syncOk, g_syncBad);
        if (g_state == ST_PLAY) set_state(ST_OVER, NULL); }
    g_postOrig(self, edx);
}

// ---------------- the network (its own thread per watch) ----------------
typedef struct { unsigned flags, avail, nticks; const unsigned char *head; unsigned headLen; const unsigned char *body; unsigned bodyLen; } Env;
static int env_parse(const unsigned char *b, size_t n, Env *e) {
    if (n < 28 || memcmp(b, "DWLR", 4)) return 0;
    e->flags = *(unsigned *)(b + 8); e->avail = *(unsigned *)(b + 12); e->nticks = *(unsigned *)(b + 16);
    size_t o = 20; e->headLen = *(unsigned *)(b + o); o += 4; if (o + e->headLen + 4 > n) return 0;
    e->head = b + o; o += e->headLen; e->bodyLen = *(unsigned *)(b + o); o += 4; if (o + e->bodyLen > n) return 0;
    e->body = b + o; return 1;
}
// GET <base><path> -> the body (malloc'd) and the HTTP status; NULL on a network error
static unsigned char *http_get(const char *base, const char *path, size_t *len, DWORD *status) {
    wchar_t host[160] = L"", wpath[300]; INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT; int secure = 1;
    const char *h = base;
    if (!strncmp(h, "https://", 8)) h += 8; else if (!strncmp(h, "http://", 7)) { h += 7; secure = 0; port = INTERNET_DEFAULT_HTTP_PORT; }
    char hb[160]; snprintf(hb, sizeof hb, "%s", h); char *sl = strchr(hb, '/'); if (sl) *sl = 0;
    char *co = strchr(hb, ':'); if (co) { *co = 0; port = (INTERNET_PORT)atoi(co + 1); }
    MultiByteToWideChar(CP_UTF8, 0, hb, -1, host, 160); MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 300);
    unsigned char *out = NULL; *len = 0; *status = 0;
    HINTERNET s = WinHttpOpen(L"DoW2-LiveReplay/" LR_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return NULL;
    WinHttpSetTimeouts(s, 8000, 8000, 15000, 30000);
    HINTERNET c = WinHttpConnect(s, host, port, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", wpath, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD sc = 0, sz = sizeof sc;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &sc, &sz, WINHTTP_NO_HEADER_INDEX);
        *status = sc; size_t cap = 65536, n = 0; out = malloc(cap);
        for (;;) {
            DWORD av = 0; if (!WinHttpQueryDataAvailable(r, &av) || !av) break;
            if (n + av > cap) { while (n + av > cap) cap *= 2; if (cap > (64u << 20)) break; unsigned char *p = realloc(out, cap); if (!p) break; out = p; }
            DWORD got = 0; if (!WinHttpReadData(r, out + n, av, &got) || !got) break; n += got;
        }
        *len = n;
    }
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return out;
}
static int write_file(const wchar_t *p, const unsigned char *a, size_t an, const unsigned char *b, size_t bn) {
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w; int r = WriteFile(f, a, (DWORD)an, &w, NULL) && w == an && (!bn || (WriteFile(f, b, (DWORD)bn, &w, NULL) && w == bn));
    CloseHandle(f); return r;
}
static DWORD WINAPI net_thread(LPVOID arg) {
    LONG me = (LONG)(INT_PTR)arg; char path[200], base[200], match[32];
    EnterCriticalSection(&g_qlk); snprintf(base, sizeof base, "%s", g_base); snprintf(match, sizeof match, "%s", g_match); LeaveCriticalSection(&g_qlk);
    int fails = 0; DWORD lastOk = GetTickCount();
    while (g_session == me) {
        if (g_state != ST_FETCH && g_state != ST_START && g_state != ST_PLAY) break;   // left the replay, failed, stopped
        LONG from = g_avail;
        snprintf(path, sizeof path, "/ladder/api/live/%s/rec?from=%ld", match, from);
        size_t n = 0; DWORD st = 0; unsigned char *b = http_get(base, path, &n, &st); Env e;
        if (g_session != me) { free(b); break; }
        if (!b || st != 200 || !env_parse(b, n, &e)) {
            free(b);
            if (st == 404) { set_state(ST_FAILED, "no_match"); break; }
            fails++; logmsg("the ladder: %s (HTTP %lu, %u bytes)", b ? "no answer we read" : "no connection", st, (unsigned)n);
            // LiveReplay 2: the first answer may take a while (the server's way to a far node): 90 s before "server", the player sees "fetching"
            if (g_state == ST_FETCH && GetTickCount() - g_startAt > 90000) { set_state(ST_FAILED, "server"); break; }
            if (GetTickCount() - lastOk > 120000) { set_state(ST_FAILED, "server"); break; }
            Sleep(3000); continue;
        }
        fails = 0; lastOk = GetTickCount();
        if (e.flags & 4) { free(b); set_state(ST_FAILED, "no_match"); break; }
        if (e.flags & 2) { free(b); Sleep(3000); continue; }          // the match is not started on the server yet
        LONG recs = 0, ticks = 0; size_t w = whole(e.body, e.bodyLen, &recs, &ticks);
        if (from == 0) {                                               // the start: header + the records so far = the replay file
            if (!e.headLen) { free(b); Sleep(2000); continue; }
            if (!write_file(g_file, e.head, e.headLen, e.body, w)) { free(b); set_state(ST_FAILED, "file"); break; }
            EnterCriticalSection(&g_qlk);
            for (size_t o = 0; o + 8 <= w; o += 8 + *(const unsigned *)(e.body + o + 4)) crc_note(e.body + o, 8 + *(const unsigned *)(e.body + o + 4));
            g_avail = from + recs; g_availTicks = ticks; g_fileTicks = ticks; g_over = (e.flags & 1) && g_avail >= (LONG)e.avail;
            LeaveCriticalSection(&g_qlk);
            logmsg("the replay so far: %ld records (%ld steps), %u bytes of header -> live_watch.rec%s", recs, ticks, e.headLen, g_over ? " (the match is over)" : "");
            g_fileReady = 1;
        } else if (recs) {
            EnterCriticalSection(&g_qlk); buf_add(e.body, w); g_avail = from + recs; g_availTicks += ticks; LeaveCriticalSection(&g_qlk);
        }
        if ((e.flags & 1) && g_avail >= (LONG)e.avail && !g_over) { g_over = 1; logmsg("the ladder: the match is over, all %ld records here", g_avail); }
        int more = g_avail < (LONG)e.avail;
        free(b);
        if (g_over) break;
        Sleep(more ? 200 : 2000);
    }
    logmsg("network: done (session %ld)", me);
    return 0;
}

// ---------------- the game's thread ----------------
static void start_watch(const char *match, const char *base) {
    if (g_inWorld || g_loading) { snprintf(g_match, sizeof g_match, "%s", match); set_state(ST_FAILED, "busy_match"); return; }
    if (g_screen == SC_SETUP) { snprintf(g_match, sizeof g_match, "%s", match); set_state(ST_FAILED, "busy_lobby"); return; }
    LONG me = InterlockedIncrement(&g_session);
    EnterCriticalSection(&g_qlk);
    snprintf(g_match, sizeof g_match, "%s", match); snprintf(g_base, sizeof g_base, "%s", base);
    free(g_buf); g_buf = NULL; g_bufLen = g_bufCap = g_bufPos = 0; g_bufTicks = 0; g_avail = 0; g_availTicks = 0; g_fileTicks = 0; g_over = 0;
    free(g_crc); g_crc = NULL; g_crcN = 0;
    LeaveCriticalSection(&g_qlk);
    g_fileReady = 0; g_live = 0; g_waiting = 0; g_cuDone = 0; g_syncOk = g_syncBad = 0; g_syncFirstBad = -1; g_crcLogged = 0;
    g_startAt = GetTickCount();
    logmsg("watch %s from %s", match, base);
    set_state(ST_FETCH, NULL);
    HANDLE t = CreateThread(NULL, 256 * 1024, net_thread, (LPVOID)(INT_PTR)me, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL); if (t) CloseHandle(t);
}
static void game_step(void) {
    DWORD now = GetTickCount();
    if (g_state == ST_FETCH && g_fileReady) {
        if (g_inWorld || g_loading) { set_state(ST_FAILED, "busy_match"); g_session++; return; }
        if (g_screen == SC_SETUP) { set_state(ST_FAILED, "busy_lobby"); g_session++; return; }
        // no panel seen yet (Elite's loader brings the add-ons in once the front end runs - its first panel may be up already):
        // the menu after 3 s of frames without a world
        if (g_screen == SC_UNKNOWN && now - g_startAt < 3000) return;
        g_live = 1;
        char pb[] = "playback:live_watch.rec";
        char r = ((StartPb_t)FN_START_PB)(pb, (void *)G_GAMEOBJ_PTR);
        logmsg("replay start: %s", r ? "ok - the game loads it" : "REFUSED by the game");
        if (!r) { g_live = 0; set_state(ST_FAILED, "start"); g_session++; return; }
        set_state(ST_START, NULL);
    }
    if (g_state == ST_START && g_inWorld && our_playback()) {
        char *w = ok((void *)G_WORLD, 4) ? *(char **)G_WORLD : NULL;
        if (w && ok(w, 0x180) && I32(w, 0x178) > 0) set_state(ST_PLAY, NULL);
    }
    if ((g_state == ST_START || g_state == ST_PLAY) && our_playback()) {
        char *w = ok((void *)G_WORLD, 4) ? *(char **)G_WORLD : NULL;
        if (w && ok(w, 0x180) && g_over && g_state == ST_PLAY && I32(w, 0x178) >= g_availTicks - 1) {
            logmsg("the end of the match reached (step %d); sync: %ld matched, %ld not", I32(w, 0x178), g_syncOk, g_syncBad); set_state(ST_OVER, NULL); }
        if (w && ok(w, 0x180)) {
            I32(w, 0x174) = g_availTicks > g_fileTicks ? g_availTicks : g_fileTicks;   // the replay panel's length: what is here
            int cur = I32(w, 0x178), behind = g_availTicks - cur;
            char *hud = ok((void *)G_HUD, 4) ? *(char **)G_HUD : NULL, *h = ok(hud, 12) ? P32(hud, 8) : NULL, *sr = ok(h, 0x70) ? P32(h, 0x68) : NULL;
            if (g_catchUp && !g_cuDone && cur > 0 && ok(sr, 4)) {
                static int fast;
                if (!fast && behind > 300) { fast = 1; ((SetF_t)(*(void ***)sr)[0x28 / 4])(sr, 0, g_cuSpeed); logmsg("catching up: %d steps behind, faster", behind); }
                else if (fast && behind < 60) { fast = 0; g_cuDone = 1; ((SetF_t)(*(void ***)sr)[0x28 / 4])(sr, 0, 10.0f); logmsg("caught up (step %d of %ld): normal speed", cur, g_availTicks); }
                else if (!fast && behind <= 300) g_cuDone = 1;
            }
        }
    }
}
// the commands from the pipe (the pipe thread puts them here, the game's thread takes them)
typedef struct { int what; char match[32], base[200]; } QCmd;
enum { CMD_WATCH = 1, CMD_STOP };
static QCmd g_cq[4]; static int g_cqH, g_cqN;
static void __stdcall upd_hook(float dt) {
    g_updOrig(dt);
    for (;;) {
        QCmd c; int have = 0;
        EnterCriticalSection(&g_qlk); if (g_cqN) { c = g_cq[g_cqH]; g_cqH = (g_cqH + 1) % 4; g_cqN--; have = 1; } LeaveCriticalSection(&g_qlk);
        if (!have) break;
        if (c.what == CMD_WATCH) start_watch(c.match, c.base);
        else if (c.what == CMD_STOP && g_state == ST_FETCH) { g_session++; set_state(ST_IDLE, NULL); logmsg("stop (the launcher): no replay"); }
        else if (c.what == CMD_STOP) logmsg("stop (the launcher): %s - nothing to stop (a replay that plays is left in the game)", ST_NAME[g_state]);
    }
    game_step();
}

// ---------------- the pipe (as RoomJoin's: overlapped, several clients, never the engine) ----------------
typedef struct { HANDLE h; OVERLAPPED ov; char rb[512]; char line[1024]; int len, drop, rd; DWORD id; } Cli;
static Cli g_c[MAXC]; static DWORD g_nextId = 1; static wchar_t g_pipeName[80]; static char g_defBase[200];
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
    EnterCriticalSection(&g_qlk);
    _snprintf(b, cap - 1, "{\"ev\":\"status\",\"ver\":\"" LR_VERSION "\",\"pid\":%lu,\"state\":\"%s\",\"match\":\"%s\",\"reason\":\"%s\",\"screen\":\"%s\","
              "\"in_world\":%s,\"waiting\":%s,\"records\":%ld,\"steps\":%ld,\"sync_ok\":%ld,\"sync_bad\":%ld}\n",
              GetCurrentProcessId(), ST_NAME[g_state], g_match, g_state == ST_FAILED ? g_reason : "", SC_NAME[g_screen],
              g_inWorld ? "true" : "false", g_waiting ? "true" : "false", g_avail, g_availTicks, g_syncOk, g_syncBad);
    LeaveCriticalSection(&g_qlk);
    b[cap - 1] = 0;
}
static const char *js_str(const char *s, const char *key, char *out, int cap) {   // "key":"value" (no escapes needed for ours)
    char k[40]; snprintf(k, sizeof k, "\"%s\"", key); const char *p = strstr(s, k); out[0] = 0;
    if (!p) return NULL; p += strlen(k); while (*p == ' ' || *p == ':') p++; if (*p != '"') return NULL; p++;
    int n = 0; while (*p && *p != '"' && n < cap - 1) { out[n++] = *p++; } out[n] = 0; return out;
}
static void cli_line(Cli *c, char *s) {
    char cmd[16], match[32], base[200];
    if (!js_str(s, "cmd", cmd, sizeof cmd)) { cli_write(c, "{\"ev\":\"error\",\"reason\":\"bad_command\"}\n"); return; }
    if (!strcmp(cmd, "status")) { char b[600]; status_line(b, sizeof b); cli_write(c, b); return; }
    QCmd q; memset(&q, 0, sizeof q);
    if (!strcmp(cmd, "watch") && js_str(s, "match", match, sizeof match) && match[0]) {
        for (char *p = match; *p; p++) if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) *p = '_';
        if (!js_str(s, "base", base, sizeof base) || strncmp(base, "http", 4)) snprintf(base, sizeof base, "%s", g_defBase);
        q.what = CMD_WATCH; snprintf(q.match, sizeof q.match, "%s", match); snprintf(q.base, sizeof q.base, "%s", base);
    } else if (!strcmp(cmd, "stop")) q.what = CMD_STOP;
    else { cli_write(c, "{\"ev\":\"error\",\"reason\":\"bad_command\"}\n"); return; }
    logmsg("pipe: client %lu: %s %s", c->id, cmd, q.match);
    EnterCriticalSection(&g_qlk); if (g_cqN < 4) { g_cq[(g_cqH + g_cqN) % 4] = q; g_cqN++; } else q.what = 0; LeaveCriticalSection(&g_qlk);
    if (!q.what) cli_write(c, "{\"ev\":\"error\",\"reason\":\"queue_full\"}\n");
    else if (q.what == CMD_WATCH) { char b[120]; snprintf(b, sizeof b, "{\"ev\":\"accepted\",\"match\":\"%s\"}\n", q.match); cli_write(c, b); }
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
    _snwprintf(g_pipeName, 80, L"\\\\.\\pipe\\EliteLiveReplay-%lu", GetCurrentProcessId()); g_pipeName[79] = 0;
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
    logmsg("pipe: EliteLiveReplay-%lu open (only this user, no remote clients)", GetCurrentProcessId());
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
    if (r != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_lk); InitializeCriticalSection(&g_qlk);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0; else g_dir[0] = 0;
    log_open();
    logmsg("LiveReplay " LR_VERSION " (watch a running match of our servers: the pipe EliteLiveReplay-%lu)", GetCurrentProcessId());
    { char n[64]; wsprintfA(n, "Local\\dow2patch_LiveReplay_%lu", GetCurrentProcessId());
      HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx && GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("another LiveReplay copy is active, this one stays idle"); return TRUE; } }
    wchar_t ini[MAX_PATH], v[200]; _snwprintf(ini, MAX_PATH, L"%lsLiveReplay.ini", g_dir); ini[MAX_PATH - 1] = 0;
    if (!GetPrivateProfileIntW(L"LiveReplay", L"Enabled", 1, ini)) { logmsg("disabled in LiveReplay.ini - no hooks"); return TRUE; }
    g_catchUp = GetPrivateProfileIntW(L"LiveReplay", L"CatchUp", 1, ini) != 0;
    g_cuSpeed = (float)GetPrivateProfileIntW(L"LiveReplay", L"CatchUpSpeed", 40, ini); if (g_cuSpeed < 10 || g_cuSpeed > 160) g_cuSpeed = 40;
    g_syncOff = GetPrivateProfileIntW(L"LiveReplay", L"SyncOffset", 0, ini);
    GetPrivateProfileStringW(L"LiveReplay", L"Server", L"https://rts.fintebtc.de", v, 200, ini);
    WideCharToMultiByte(CP_UTF8, 0, v, -1, g_defBase, sizeof g_defBase, NULL, NULL); g_defBase[sizeof g_defBase - 1] = 0;
    // the replay file: <Documents>\My Games\Dawn of War II - Retribution\Playback\live_watch.rec (the game's "playback:")
    wchar_t docs[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs) != S_OK) { logmsg("no Documents folder - nothing changed"); return TRUE; }
    _snwprintf(g_file, MAX_PATH, L"%ls\\My Games\\Dawn of War II - Retribution\\Playback\\live_watch.rec", docs); g_file[MAX_PATH - 1] = 0;
    // all or nothing: DOW2.exe 3.19 where we hook
    unsigned char *e = (unsigned char *)AT_PB_END;
    int good = !IsBadReadPtr((void *)VT_READER, 8) && ((DWORD *)VT_READER)[1] == FN_NEXTREC
        && !IsBadReadPtr((void *)VT_PLAYBACK, 0x20) && ((DWORD *)VT_PLAYBACK)[0] == 0x0066AD20
        && !IsBadReadPtr(e, 5) && e[0] == 0xE8 && (DWORD)(e + 5) + *(int *)(e + 1) == FN_PB_END
        && !IsBadReadPtr((void *)FN_READREC, 3) && *(unsigned char *)FN_READREC == 0xB8 && *(unsigned char *)FN_START_PB == 0xB8;
    if (!good) { logmsg("not DOW2.exe 3.19 (or patched there by something else) - nothing changed"); return TRUE; }
    for (int i = 0; i < 16; i++) g_bsVt[i] = (void *)bs_zero;
    g_bsVt[2] = (void *)bs_read;
    int a = slot_hook(VT_READER + 4, FN_NEXTREC, (void *)my_next, (void **)&g_nextOrig);
    int b = slot_hook(VT_PLAYBACK + 0x10, FN_GETSYNC, (void *)my_getsync, (void **)&g_gsOrig);
    int c = slot_hook(VT_PLAYBACK + 0x1C, FN_PBCRC, (void *)my_pbcrc, (void **)&g_crcOrig);
    int d = patch_call(AT_PB_END, FN_PB_END, (void *)my_pb_end);
    logmsg("playback hooks: reader %d, sync %d, crc %d, end %d; screens %d", a, b, c, d, hook_fscmd());
    g_outEv = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 64 * 1024, installer, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL); if (t) CloseHandle(t);
    return TRUE;
}
