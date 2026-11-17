// EliteAI.dll - new computer AI for DoW2 Retribution / Elite without touching the game archives.
// Every Lua chunk goes through lua_load (LuaConfig.dll, stdcall build). When the engine loads an AI script and
// Elite_DLLs\EliteAI\<same path> exists, the engine gets our text instead of the one from the archive.
// No file in the folder = the original Elite AI; the archives and the lobby data (datacrc) stay as they are.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <winhttp.h>
#include <shlobj.h>
#include "moveloop_guard.h"     // the engine's melee freeze (= MoveLoopFix 1; fastbench_1113 HANG_12da6e_RU.md, brain step 7 hangs)

typedef const char *(__stdcall *LuaReader)(void *L, void *data, size_t *size);
typedef int (__stdcall *LuaLoadFn)(void *L, LuaReader reader, void *data, const char *chunkname);
static LuaLoadFn g_loadOrig;
// a few Lua 5.1 calls from LuaConfig.dll (stdcall build): to give the AI scripts a log - aitrace writes nothing without -dev
typedef int (__stdcall *LuaCFn)(void *L);
static void (__stdcall *p_pushcclosure)(void *L, LuaCFn f, int n);
static void (__stdcall *p_setfield)(void *L, int idx, const char *k);
static const char *(__stdcall *p_tolstring)(void *L, int idx, size_t *len);
static int (__stdcall *p_gettop)(void *L);
static void (__stdcall *p_pushstring)(void *L, const char *s);
static void (__stdcall *p_pushnil)(void *L);
static void (__stdcall *p_getfield)(void *L, int idx, const char *k);
static void (__stdcall *p_settop)(void *L, int idx);
static double (__stdcall *p_tonumber)(void *L, int idx);
static wchar_t g_ini[MAX_PATH];
#define LUA_GLOBALSINDEX (-10002)
static wchar_t g_dir[MAX_PATH];                 // ...\Elite_DLLs\ (where this DLL lives)
static CRITICAL_SECTION g_lk;
static int g_verbose;

// THE OWNER'S RULE FOR THIS DLL (14.11.2026): whatever here touches the AI's control or the simulation must be deterministic
// and INTEGER (or fixed-point) - no float / double arithmetic, no wall-clock time, no randomness of ours, a stable order.
// The Mac (Feral) build computes floats differently. (14.11: in a network game ONE machine - the host - computes the AI and
// its orders reach the others as recorded commands, so the AI's decisions need not be bit-equal across machines; the rule
// stays, so that nothing here has to be ported twice.)
// Decisions live in the Lua scripts; the DLL is plumbing - loading, the log, the cfg, the order pipe, the move loop guard.
// State of this file against the rule:
//   * the log (below), the replay upload and the labels use the wall clock and never feed anything back to the scripts;
//   * the order pipe (lua_order / lua_take) does NO arithmetic, but it carries the scripts' numbers as doubles and prints
//     them with "%.2f". Since 0.15 (brain step 9) NO ORDER GOES THROUGH IT by default: it carries only what elite.scar
//     watches (an order already sent, with mode + 1000) and the scripts' words to it (modes 9-15) - nothing of it reaches
//     the simulation. It is the control path again only with [Lua] Net_Orders=0 (the comparison path, single player);
//   * eliteai_cmd / eliteai_retlock (0.15, below): integers, bytes and pointer compares only - see there; the table of
//     the retreat locks (retlock.h, 0.16) is emptied at a match start and by the simulation's own state, never by a timer;
//   * lua_cfg hands the scripts strings of the local EliteAI.ini. In a network match (0.16) only the station that computes
//     a computer runs its scripts, so the keys need not be equal across stations - but the keys of the SCAR path are not read;
//   * the marks' file (0.17, EliteAI_Mark): the world's tick is read to choose which lines go into EliteAI.marks.log - ints
//     and byte counts, nothing goes back to a script or an order (0.19: how the file is shared among the marks - byte counts
//     and an integer bisection, still logging only);
//   * the move loop guard (moveloop_guard.h) changes the simulation only where the engine would hang; it counts passes
//     whose 'time left' is BIT-equal to the pass before (an integer compare of the four bytes) - the float is read as a
//     number only for the log line.
// 0.13 (14.11.2026): ONE LOG PER MATCH. (Integers only - byte counts and GetTickCount; it decides which FILE a line goes
// to and nothing else: no script and no order ever sees it.) The launcher's "send logs" carries the LAST 256 KB of every *.log of this folder
// (bundle 20261114-121730: 'EliteAI.log (669590 bytes, +387141 new)', 262146 bytes of it in the bundle - the match began
// at 4:07 of the game there, the tester's marks of 1:07 and 1:30 had no line of the AI). Now:
//   * at the start of a match OF THE NEW AI (0.14: the first AI script of ours the game is given for it - not a replay,
//     not a SCAR load; see log_match) EliteAI.log becomes EliteAI.prev.log and EliteAI.more.log becomes
//     EliteAI.more.prev.log; EliteAI.log holds the current match (and the few lines of the replays watched after it);
//   * EliteAI.log never grows beyond LOG_MAIN_MAX (250 KB), so the launcher's tail is the whole file and THE BEGINNING OF
//     THE MATCH IS NEVER CUT. What does not fit goes on in EliteAI.more.log, the least needed lines first: the order
//     lines ('BR ORD', the counters) from LOG_ALL_MAX (140 KB) on, the fight / fist / capture lines from LOG_B_MAX
//     (235 KB) on, everything from 250 KB on. One brain writes ~15 KB a minute, 6 KB of it orders: a 1v1 of 25 minutes
//     fits whole (orders of its last minutes in the second file, which the launcher sends as well).
#define LOG_ALL_MAX  (140 * 1024)
#define LOG_B_MAX    (235 * 1024)
#define LOG_MAIN_MAX (250 * 1024)
static long g_logBytes = -1;                    // bytes in EliteAI.log (-1 = not measured yet)
static int g_logSaid;                           // the line "goes on in EliteAI.more.log" was written (bits: 1 orders, 2 fights, 4 all)
static DWORD g_lastLoadTick;                    // the last script load of a match (GetTickCount)
static int log_class(const char *s) {           // 0 = always in the match's log, 1 = fights / fist / captures, 2 = orders / counters
    if (strstr(s, " BR ORD ") || strstr(s, " stats ") || strstr(s, "CAST seen") || strstr(s, "RETREAT seen")) return 2;
    if (strstr(s, " BR ARMY ")) return (strstr(s, " squad ") || strstr(s, " zone ") || strstr(s, " stage ")) ? 0 : 1;
    if (strstr(s, " BR W ") || strstr(s, " BR CAP ") || strstr(s, " BR SEE ") || strstr(s, " BR COMP ")) return 1;
    return 0;
}
static void log_write(const wchar_t *name, const char *stamp, const char *line) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%ls%ls", g_dir, name);
    FILE *f = _wfopen(p, L"a");
    if (f) { fputs(stamp, f); fputs(line, f); fputc('\n', f); fclose(f); }
}
// ---- 0.17 (15.11.2026): THE TESTERS' MARKS -> EliteAI.marks.log ------------------------------------------------------------
// A tester presses the mark hotkey / button of Marks.dll; Marks.log says "MARK n | game m:ss ...". The launcher's "send logs"
// carries the last 256 KB of every *.log, and the new AI writes megabytes a match (a 3v3 of six brains ~2.5 KB a second of
// game), so three bundles in a row had no AI line at the early marks (20261115-082422-09df0f: marks from 0:34, the AI's lines
// from 17:49 on). Now every mark keeps the AI's lines of 20 s of game before it and 20 s after it in a small file of its own:
//   * HOW EliteAI LEARNS OF A MARK: Marks.dll 2 calls the export EliteAI_Mark(n, tick, line) of every module named
//     EliteAI.dll (the copy that is not active - Elite's injector and dow2-patches may both load one - answers 0) at the
//     moment of the mark, on the game's UI thread. No file is watched, nothing is polled. The call writes Marks' line into
//     EliteAI.log through logmsg (Marks 1 appended it to the file itself, past the size count of the split) and opens the
//     mark's window here. Without Marks.dll the export is never called and nothing of this runs; Marks 1 (no call) leaves
//     EliteAI.marks.log empty and appends its line as before; Marks 2 with an older EliteAI (no export) does the same;
//   * every line of logmsg also goes into a ring in memory (MK_RING_N lines / MK_RING_BYTES) with the world object and the
//     world's tick (World + 0x178, an int; a tick = 0.1 s of game) it was written at;
//   * a mark at tick T: its header, the ring's lines of the same world from T - 200 on (those not in the file yet: a mark
//     inside the window of the one before goes on from where that one is), then every new line up to tick T + 200;
//   * EliteAI.marks.log stays under MK_FILE_MAX (240 KB, the launcher takes 256 KB). One mark takes at most MK_SIDE_MAX
//     (60 KB) of lines before it (the nearest ones) and as much after it - and a side at most half its equal share of the
//     file (the share = (MK_FILE_MAX - 2 KB) / the marks in the file, the new one counted) or half the room still free;
//   * 0.19 (a tester's 18-min 3v3 with 15 marks: 0.17 squeezed each older mark once to 10 KB a side, then dropped the oldest
//     whole - marks 1-6 had no AI line): when the next lines do not fit, EVERY older mark is squeezed to the same level, its
//     equal share (lower when that is not enough), again and again as marks come: it keeps its header, its trailer and the
//     lines nearest to the mark, half the room on each side; the note of each side says how many KB went in all. A mark is
//     dropped whole (the oldest, a note line in its place says which and how much) only when the level would fall under
//     MK_FLOOR (2 KB) - with 128 marks at most in the file that does not happen. The first line of the file counts both.
//     Sizes: one brain ~0.5 KB a second of game, so a 1v1 mark is ~20 KB and ~11 marks stay whole; a 3v3 of six brains
//     ~2.5 KB a second, a mark ~100 KB: with the 14 marks of the bundle 20261115-082422-09df0f every mark keeps ~16 KB
//     (about +-3 s of game around it) - test/marks_test.c;
//   * one file per match: the first mark of another match (another world object, or the tick went back) moves the file of
//     the match before to EliteAI.marks.prev.log - also when that match had no new AI (Elite's AI, a network match);
//   * every line of the file starts with its game time ("g 15:26.3") and then the real time of EliteAI.log.
// (Integers only - ticks, byte counts, pointer compares; it decides which FILE a line goes to and nothing else: no script
// and no order ever sees it. The world's tick is read, never written.)
#define MK_FILE_MAX   (240 * 1024)
#define MK_SIDE_MAX   (60 * 1024)
#define MK_FLOOR      (2 * 1024)                // under this a mark is dropped whole rather than squeezed
#define MK_SLACK      (4 * 1024)                // room made beyond the line that needs it (the file is rewritten at each squeeze)
#define MK_WIN_TICKS  200
#define MK_RING_N     8192
#define MK_RING_BYTES (1024 * 1024)
#define MK_SEC_MAX    128
#define MK_G_WORLD    0x013356A4                // DOW2.exe 3.19.10320: the World (= SimEngine's g_World), + 0x178 its tick
typedef struct { char *s; int len; void *world; int tick; unsigned seq; } MkLine;
static MkLine g_mkRing[MK_RING_N];
static int g_mkHead, g_mkCount; static long g_mkRingBytes; static unsigned g_mkSeq;
static char g_mkFile[MK_FILE_MAX + 2048];       // = EliteAI.marks.log (the file is only ever written from here)
// a mark's section: [start, body) its header lines (+ the note of lines cut before it, bnote bytes at the end), [body, mid) the
// lines before the mark, [mid, tail) the lines after it, [tail, end) its trailer: the note of lines cut after it (anote bytes),
// the "stop here" note, the "---- end of MARK" line (tail 0 = the mark is still open and has no trailer yet)
typedef struct { int start, body, mid, tail, bnote, anote, squeezed, n, tick, cutB, cutA; } MkSec;
static MkSec g_mkSec[MK_SEC_MAX];
static int g_mkLen, g_mkNSec, g_mkDropped, g_mkSqueezed;     // the file's length, its marks, marks dropped / squeezed in this match
static void *g_mkWorld; static int g_mkLastTick = -1;            // the match the file holds
static int g_mkOpen, g_mkN, g_mkT, g_mkUntil, g_mkAfter, g_mkCut, g_mkSide; static unsigned g_mkDone;   // the window of the last mark
static volatile int g_active;                   // this copy is the one that works (DllMain: the other copy stays idle)
static void mk_world(void **w, int *tick) {
    void **g = (void **)MK_G_WORLD; *w = NULL; *tick = -1;
    if (IsBadReadPtr(g, 4) || !*g || IsBadReadPtr(*g, 0x17C)) return;
    *w = *g; *tick = *(int *)((char *)*g + 0x178);
}
static void mk_path(wchar_t *p, int prev) { _snwprintf(p, MAX_PATH, L"%lsEliteAI.marks%ls.log", g_dir, prev ? L".prev" : L""); }
// 0.19: THE FIRST 32 BYTES OF THE FILE CHANGE AT EVERY REWRITE. The launcher sends a .log in increments (logs.rs: it keeps
// [size, the first 32 bytes]; the same 32 bytes and a size not smaller -> only the bytes past the old size go). The file
// always began with the same header, so after a squeeze / a drop / a new match only the tail went (Orca's bundle of 17.11:
// 25 marks in Marks.log, "244518 bytes, +17373 new" = MARK 25 alone). Now the first line starts with "=EAI mk <match>.<n> ":
// <match> = the Unix time of the match's first mark in hex, <n> = the rewrites of this process (every mk_rewrite counts) -
// both integers, plain ASCII. A plain append (mk_put without making room) keeps the line as it is: those still go as increments.
static unsigned g_mkMatchId, g_mkRew;           // the match of the file (Unix time of its first mark), the rewrites so far
static int mk_head(char *o, int cap);
static void mk_newhead(void);
static void mk_rewrite(void) {                  // the whole file again: a new first line (prefix), then every byte of it
    g_mkRew++; mk_newhead();
    wchar_t p[MAX_PATH]; mk_path(p, 0); FILE *f = _wfopen(p, L"wb");
    if (f) { fwrite(g_mkFile, 1, g_mkLen, f); fclose(f); }
}
static unsigned mk_unix(void) {                 // the Unix time in seconds (integers; it only names the file's version)
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (unsigned)(t / 10000000ULL - 11644473600ULL);
}
static int mk_head(char *o, int cap) {          // the first line of the file
    return snprintf(o, cap, "=EAI mk %08x.%u === EliteAI 0.19: the AI's lines %d s of game before and after each mark of Marks.dll (Marks.log has the players); "
                    "at most %d KB: older marks are squeezed toward an equal share each (the lines nearest to them kept), a mark is dropped "
                    "only below %d KB - in this match %d squeezed, %d dropped ===\n",
                    g_mkMatchId, g_mkRew, MK_WIN_TICKS / 10, MK_FILE_MAX / 1024, MK_FLOOR / 1024, g_mkSqueezed, g_mkDropped);
}
static char g_mkTmp[MK_FILE_MAX + 2048];
// the file = A + B + C -> A + B' + C, every section offset after the change moved by the difference
static void mk_splice(int at, int len, const char *by, int n) {
    int tail = g_mkLen - at - len, d = n - len;
    memcpy(g_mkTmp, g_mkFile + at + len, tail); memcpy(g_mkFile + at, by, n); memcpy(g_mkFile + at + n, g_mkTmp, tail); g_mkLen += d;
    for (int i = 0; i < g_mkNSec; i++) {
        MkSec *m = &g_mkSec[i];
        if (m->start >= at + len) m->start += d;
        if (m->body >= at + len) m->body += d;
        if (m->mid >= at + len) m->mid += d;
        if (m->tail && m->tail >= at + len) m->tail += d;
    }
}
static int mk_end(int i) { return i + 1 < g_mkNSec ? g_mkSec[i + 1].start : g_mkLen; }
static int mk_size(int i) { return mk_end(i) - g_mkSec[i].start; }
static int mk_share(void) { return (MK_FILE_MAX - 2048) / (g_mkNSec > 0 ? g_mkNSec : 1); }   // one mark's equal share of the file
static void mk_newhead(void) {                  // the first line again (the counts changed); notes of dropped marks under it stay
    char h[512]; int hl = mk_head(h, sizeof h), old = 0;
    while (old < g_mkLen && g_mkFile[old] != '\n') old++;
    mk_splice(0, old < g_mkLen ? old + 1 : g_mkLen, h, hl);
}
static void mk_drop(void) {                     // the oldest mark of the file goes (whole); a note line says which
    MkSec *m = &g_mkSec[0]; int a = m->start, e = mk_end(0);
    char note[200];
    int k = snprintf(note, sizeof note, "(MARK %d (game %d:%02d, tick %d) dropped whole for later marks: its %d KB did not fit, every mark kept at least %d KB)\n",
                     m->n, m->tick / 600, (m->tick / 10) % 60, m->tick, (e - a + 1023) / 1024, MK_FLOOR / 1024);
    g_mkDropped++;
    mk_splice(a, e - a, note, k);
    memmove(g_mkSec, g_mkSec + 1, sizeof g_mkSec[0] * (g_mkNSec - 1)); g_mkNSec--;
    mk_newhead();
}
// an older (closed) mark is squeezed to `to` bytes at most: its header and trailer stay, of its lines the ones nearest to the
// mark stay, half the room on each side (a side that has less gives the rest to the other). Again and again as the file fills:
// the note of each side says how much went in all. -> bytes freed (0 = nothing to cut)
#define MK_NOTE_ROOM 100
static int mk_squeeze(int i, int to) {
    MkSec *m = &g_mkSec[i]; int end = mk_end(i), tail = m->tail ? m->tail : end, size = end - m->start;
    if (size <= to) return 0;
    int fixed = (m->body - m->bnote - m->start) + (end - tail - m->anote);
    int room = to - fixed - 2 * MK_NOTE_ROOM; if (room < 0) room = 0;
    int before = m->mid - m->body, after = tail - m->mid;
    if (before + after <= room) return 0;
    int kb = room / 2, ka = room - kb;
    if (before < kb) { ka += kb - before; kb = before; }
    if (after < ka) { kb += ka - after; ka = after; if (kb > before) kb = before; }
    int q = m->mid + ka;                            // after it: the lines that end by mid + ka
    while (q > m->mid && g_mkFile[q - 1] != '\n') q--;
    int p = m->mid - kb;                            // before it: the lines that start from mid - kb on
    while (p < m->mid && p > m->body && g_mkFile[p - 1] != '\n') p++;
    char note[200]; int k;
    if (!m->squeezed) { m->squeezed = 1; g_mkSqueezed++; }
    if (q < tail) {
        m->cutA += tail - q;
        k = snprintf(note, sizeof note, "(squeezed for later marks: %d KB of lines farther after this mark dropped)\n", (m->cutA + 1023) / 1024);
        mk_splice(q, tail + m->anote - q, note, k);
        m->tail = q; m->anote = k;
    }
    if (p > m->body) {
        int a = m->body - m->bnote;
        m->cutB += p - m->body;
        k = snprintf(note, sizeof note, "(squeezed for later marks: %d KB of lines farther before this mark dropped)\n", (m->cutB + 1023) / 1024);
        mk_splice(a, p - a, note, k);
        m->body = a + k; m->bnote = k;
    }
    return size - mk_size(i);
}
static long mk_over(int level) {                // bytes the older marks hold above `level`
    long o = 0;
    for (int i = 0; i < g_mkNSec - 1; i++) { int s = mk_size(i); if (s > level) o += s - level; }
    return o;
}
static int mk_put(const char *s, int n) {       // append to the file; room is made from the older marks -> 0 = not written
    while (g_mkLen + n > MK_FILE_MAX && g_mkNSec > 1) {
        // never the last mark (the one being written). Every older mark is squeezed to the same level: its equal share of the
        // file, or lower when that does not free enough (+ MK_SLACK, so that the file is not rewritten at every line); a level
        // under MK_FLOOR drops the oldest mark whole instead
        long need = g_mkLen + n - MK_FILE_MAX + MK_SLACK;
        int lv = mk_share(), freed = 0;
        if (mk_over(lv) < need) {                   // the highest level that frees enough (integers: bisection)
            int lo = 0, hi = lv;
            if (mk_over(0) < need) hi = lo = -1;
            while (lo < hi) { int md = (lo + hi + 1) / 2; if (mk_over(md) >= need) lo = md; else hi = md - 1; }
            lv = lo;
        }
        if (lv >= MK_FLOOR) for (int i = 0; i < g_mkNSec - 1; i++) freed += mk_squeeze(i, lv);
        if (freed <= 0) mk_drop();
        mk_rewrite();                               // (a new first line: the launcher sends the whole file again)
    }
    if (g_mkLen + n > MK_FILE_MAX) return 0;
    memcpy(g_mkFile + g_mkLen, s, n); g_mkLen += n;
    wchar_t p[MAX_PATH]; mk_path(p, 0); FILE *f = _wfopen(p, L"ab");
    if (f) { fwrite(s, 1, n, f); fclose(f); }
    return 1;
}
static int mk_fmt(char *o, int cap, const MkLine *l) {      // "g 15:26.3 12:03:10.115 AI: p1000 BR ..."
    int t = l->tick < 0 ? 0 : l->tick;
    int n = snprintf(o, cap, "g %d:%02d.%d %.*s\n", t / 600, (t / 10) % 60, t % 10, l->len, l->s);
    return n < cap ? n : cap - 1;
}
static void mk_close(const char *why) {
    if (!g_mkOpen) return;
    MkSec *m = &g_mkSec[g_mkNSec - 1]; if (!m->tail) m->tail = g_mkLen;      // (the open mark is the last section)
    char b[200]; int n = snprintf(b, sizeof b, "---- end of MARK %d (game %d:%02d): %s ----\n", g_mkN, g_mkT / 600, (g_mkT / 10) % 60, why);
    mk_put(b, n); g_mkOpen = 0;
}
static void mk_line(const char *stamp, const char *line) {  // every line of logmsg (g_lk held)
    void *w; int t; mk_world(&w, &t);
    int ls = (int)strlen(stamp), ll = (int)strlen(line);
    char *s = malloc(ls + ll + 1); if (!s) return;
    memcpy(s, stamp, ls); memcpy(s + ls, line, ll); s[ls + ll] = 0;
    while (g_mkCount && (g_mkCount == MK_RING_N || g_mkRingBytes + ls + ll > MK_RING_BYTES)) {   // the oldest lines go
        MkLine *o = &g_mkRing[g_mkHead]; g_mkRingBytes -= o->len; free(o->s); o->s = NULL;
        g_mkHead = (g_mkHead + 1) % MK_RING_N; g_mkCount--;
    }
    MkLine *l = &g_mkRing[(g_mkHead + g_mkCount) % MK_RING_N]; g_mkCount++;
    l->s = s; l->len = ls + ll; l->world = w; l->tick = t; l->seq = ++g_mkSeq; g_mkRingBytes += l->len;
    if (!g_mkOpen) return;
    if (!w || w != g_mkWorld || t < g_mkT) { mk_close(!w ? "the match is over" : "another match"); return; }
    if (t > g_mkUntil) { mk_close("20 s of game after it"); return; }
    char b[1600]; int n = mk_fmt(b, sizeof b, l);
    if (g_mkAfter + n > g_mkSide) {
        if (!g_mkCut) {
            g_mkCut = 1; g_mkSec[g_mkNSec - 1].tail = g_mkLen;           // (the note is the trailer's: a squeeze keeps it)
            char c[160]; int k = snprintf(c, sizeof c, "(the lines after MARK %d stop here: %d KB after one mark at most)\n", g_mkN, (g_mkSide + 1023) / 1024); mk_put(c, k);
        }
        return;
    }
    if (mk_put(b, n)) { g_mkAfter += n; g_mkDone = l->seq; }
}
static void logmsg(const char *fmt, ...);
// Marks.dll 2: at a mark of a live match. n = Marks' number, tick = the world's tick Marks read, line = Marks' line for
// EliteAI.log ("MARK 3 game 15:26 (926.0 s, tick 9260) map ... camera ..."). -> 1 taken, 0 not this copy / no match.
__declspec(dllexport) int __cdecl EliteAI_Mark(int n, int tick, const char *line) {
    if (!g_active) return 0;
    void *w; int t; mk_world(&w, &t);
    if (!w) return 0;
    if (tick < 0) tick = t;
    EnterCriticalSection(&g_lk);
    mk_close("the next mark");
    if (w != g_mkWorld || tick < g_mkLastTick || !g_mkNSec) {          // the first mark of a match: a file of its own
        wchar_t a[MAX_PATH], b[MAX_PATH]; mk_path(a, 0); mk_path(b, 1);
        if (w != g_mkWorld || tick < g_mkLastTick) {                     // (the file of the match before -> prev)
            if (GetFileAttributesW(a) != INVALID_FILE_ATTRIBUTES) MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
            g_mkDropped = g_mkSqueezed = 0;
        }
        g_mkMatchId = mk_unix();
        g_mkWorld = w; g_mkNSec = 0; g_mkLen = mk_head(g_mkFile, 512); mk_rewrite();
    }
    g_mkLastTick = tick;
    if (g_mkNSec == MK_SEC_MAX) { mk_drop(); mk_rewrite(); }    // (128 marks in the file: the oldest goes)
    // a side of this mark: MK_SIDE_MAX at most, and not more than half its equal share of the file (this mark counted) - or
    // half the room still free in the file, when that is more (a quiet match: no mark needs the room)
    g_mkNSec++; g_mkSide = mk_share() / 2 - 512; g_mkNSec--;
    if (g_mkSide < (MK_FILE_MAX - g_mkLen - 2048) / 2) g_mkSide = (MK_FILE_MAX - g_mkLen - 2048) / 2;
    if (g_mkSide > MK_SIDE_MAX) g_mkSide = MK_SIDE_MAX;
    if (g_mkSide < MK_FLOOR / 2) g_mkSide = MK_FLOOR / 2;             // (not under the floor: older marks are dropped instead)
    // the lines before it: those of this world from T - 20 s on that are not in the file yet, the nearest g_mkSide of them
    int from = tick - MK_WIN_TICKS, first = g_mkCount, bytes = 0, older = 0, above = 0;
    char b[1600];
    for (int i = g_mkCount - 1, next = tick; i >= 0; i--) {          // (back in time: a tick that goes UP = the match before)
        MkLine *l = &g_mkRing[(g_mkHead + i) % MK_RING_N];
        if (l->world != w || l->tick < from || l->tick < 0 || l->tick > next) break;
        next = l->tick;
        if (l->seq <= g_mkDone) { above = 1; break; }
        int k = mk_fmt(b, sizeof b, l);
        if (bytes + k > g_mkSide) { older = 1; break; }
        bytes += k; first = i;
    }
    char h[700]; SYSTEMTIME st; GetLocalTime(&st);
    int hl = snprintf(h, sizeof h, "\n==== MARK %d | game %d:%02d (tick %d) | %02d:%02d:%02d | the AI's lines from game %d:%02d to %d:%02d ====\n%s",
                      n, tick / 600, (tick / 10) % 60, tick, st.wHour, st.wMinute, st.wSecond, from < 0 ? 0 : from / 600, from < 0 ? 0 : (from / 10) % 60,
                      (tick + MK_WIN_TICKS) / 600, ((tick + MK_WIN_TICKS) / 10) % 60,
                      above ? "(the lines before these, from the same 20 s, are under the mark above)\n" : "");
    if (hl >= (int)sizeof h) hl = sizeof h - 1;
    MkSec *sec = &g_mkSec[g_mkNSec++];              // this mark's section (the last one: mk_put never squeezes or drops it)
    memset(sec, 0, sizeof *sec); sec->start = sec->body = sec->mid = g_mkLen; sec->n = n; sec->tick = tick;
    if (mk_put(h, hl)) {
        if (older) { char c[160]; int k = snprintf(c, sizeof c, "(earlier lines of the 20 s before it left out: %d KB before one mark at most)\n", (g_mkSide + 1023) / 1024); mk_put(c, k); }
        sec = &g_mkSec[g_mkNSec - 1]; sec->body = g_mkLen;                       // (mk_put may have moved the file)
        for (int i = first; i < g_mkCount; i++) { MkLine *l = &g_mkRing[(g_mkHead + i) % MK_RING_N]; int k = mk_fmt(b, sizeof b, l); if (mk_put(b, k)) g_mkDone = l->seq; }
        g_mkSec[g_mkNSec - 1].mid = g_mkLen;
        g_mkOpen = 1; g_mkN = n; g_mkT = tick; g_mkUntil = tick + MK_WIN_TICKS; g_mkAfter = 0; g_mkCut = 0;
    }
    LeaveCriticalSection(&g_lk);
    logmsg("%s", line && line[0] ? line : "MARK (no text from Marks.dll)");   // EliteAI.log as before - and the first line after the mark here
    return 1;
}
static void logmsg(const char *fmt, ...) {
    char line[1400], stamp[20];
    va_list a; va_start(a, fmt); vsnprintf(line, sizeof line, fmt, a); va_end(a); line[sizeof line - 1] = 0;
    SYSTEMTIME t; GetLocalTime(&t); snprintf(stamp, sizeof stamp, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    EnterCriticalSection(&g_lk);
    if (g_logBytes < 0) {                        // (appending to the file of the last session: its size counts)
        wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsEliteAI.log", g_dir);
        WIN32_FILE_ATTRIBUTE_DATA d; g_logBytes = GetFileAttributesExW(p, GetFileExInfoStandard, &d) ? (long)d.nFileSizeLow : 0;
    }
    int c = log_class(line);
    long cap = c == 2 ? LOG_ALL_MAX : c == 1 ? LOG_B_MAX : LOG_MAIN_MAX;
    // ([EliteAI] LogWhole=1, or the scene bench's dump - [Lua] SceneTo: one file, no limits; the bench reads megabytes of it)
    static int whole = -1;
#ifdef LOG_TEST_SPLIT
    whole = 0;                                   // (a bench test of the split itself)
#else
    if (whole < 0 && g_ini[0]) whole = GetPrivateProfileIntW(L"EliteAI", L"LogWhole", 0, g_ini) || GetPrivateProfileIntW(L"Lua", L"SceneTo", 0, g_ini) > 0;
#endif
    if (whole > 0 || g_logBytes < cap) {
        log_write(L"EliteAI.log", stamp, line); g_logBytes += (long)(strlen(stamp) + strlen(line) + 2);
    } else {
        int bit = c == 2 ? 1 : c == 1 ? 2 : 4;
        if (!(g_logSaid & bit) && g_logBytes < LOG_MAIN_MAX + 400) {
            g_logSaid |= bit;
            const char *w = c == 2 ? "the order lines (BR ORD, the counters) go on in EliteAI.more.log" : c == 1 ? "the fight / fist / capture lines go on in EliteAI.more.log"
                                   : "this file is full (250 KB): everything goes on in EliteAI.more.log";
            log_write(L"EliteAI.log", stamp, w); g_logBytes += (long)(strlen(stamp) + strlen(w) + 2);
        }
        log_write(L"EliteAI.more.log", stamp, line);
    }
    mk_line(stamp, line);                        // 0.17: the ring of the marks' windows (and the open window -> EliteAI.marks.log)
    LeaveCriticalSection(&g_lk);
}
// 0.14 (14.11.2026, the verifier of brain step 8): the log is rotated ONLY WHEN A MATCH WITH THE NEW AI STARTS. 0.13
// rotated at the first script of any game the hook touches - a replay watched with ReplayTakeover in the game (its SCAR
// gets our append) and every skirmish alike - so a tester's AI match survived exactly one later game or replay. Now:
//   * a SCAR append rotates nothing (a replay never gives an AI script of ours: its lines are added to the log as it is);
//   * the first AI script that is given as OURS after LOG_MATCH_GAP_MS without one = a match of the new AI starts. The
//     log is rotated then - and only if the file already holds a match (the line "=== EliteAI" in it): a file with
//     nothing but the lines of replays / of this match's own SCAR load is kept and the match goes on in it.
// (Integers only: GetTickCount and byte compares; it chooses the FILE of a line, nothing here reaches a script.)
#define LOG_MATCH_GAP_MS 20000
#define LOG_MATCH_MARK   "=== EliteAI"
static int log_holds_match(void) {               // does EliteAI.log hold the header line of a match?
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsEliteAI.log", g_dir);
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    static char buf[65536 + 16]; const int ml = (int)sizeof(LOG_MATCH_MARK) - 1;
    int keep = 0, found = 0; DWORD got = 0;
    while (!found && ReadFile(f, buf + keep, 65536, &got, NULL) && got) {
        int n = keep + (int)got;
        for (int i = 0; i + ml <= n; i++) if (buf[i] == '=' && !memcmp(buf + i, LOG_MATCH_MARK, ml)) { found = 1; break; }
        keep = n < ml - 1 ? n : ml - 1; memmove(buf, buf + n - keep, keep);      // (the mark may lie across two reads)
    }
    CloseHandle(f); return found;
}
static int log_match(const char *chunk) {        // an AI script of OURS is being given to the engine -> 1 the first of a match
    DWORD now = GetTickCount(); int fresh, rotated = 0;
    EnterCriticalSection(&g_lk);
    fresh = (g_lastLoadTick == 0 || now - g_lastLoadTick > LOG_MATCH_GAP_MS); g_lastLoadTick = now ? now : 1;
    if (fresh && log_holds_match()) {
        static const wchar_t *R[2][2] = { { L"EliteAI.log", L"EliteAI.prev.log" }, { L"EliteAI.more.log", L"EliteAI.more.prev.log" } };
        for (int i = 0; i < 2; i++) {
            wchar_t x[MAX_PATH], y[MAX_PATH]; _snwprintf(x, MAX_PATH, L"%ls%ls", g_dir, R[i][0]); _snwprintf(y, MAX_PATH, L"%ls%ls", g_dir, R[i][1]);
            if (GetFileAttributesW(x) != INVALID_FILE_ATTRIBUTES) MoveFileExW(x, y, MOVEFILE_REPLACE_EXISTING);
            else if (i == 1) DeleteFileW(y);    // (no second file in the last match: the one before it is not its)
        }
        g_logBytes = 0; g_logSaid = 0; rotated = 1;
    }
    LeaveCriticalSection(&g_lk);
    if (fresh) {
        SYSTEMTIME t; GetLocalTime(&t);
        logmsg(LOG_MATCH_MARK " 0.17: a match of the new AI starts %04d-%02d-%02d %02d:%02d:%02d (first AI script %s); %s ===",
               t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, chunk ? chunk : "?",
               rotated ? "the match before it is in EliteAI.prev.log" : "no match in this file before it: nothing rotated");
    }
    return fresh;
}

// "data:simulation/ai/default.ai", "@Simulation\AI\Personality\orks\expert.ai" ... -> "simulation\ai\default.ai" (lower case)
static int ai_rel(const char *chunk, char *out, int cap) {
    if (!chunk) return 0;
    char b[512]; int n = 0;
    for (const char *s = chunk; *s && n < (int)sizeof b - 1; s++) b[n++] = (*s == '/') ? '\\' : (char)tolower((unsigned char)*s);
    b[n] = 0;
    const char *p = strstr(b, "simulation\\ai\\");
    if (!p || strstr(p, "..")) return 0;
    snprintf(out, cap, "%s", p); return 1;
}

static void replay_note(const char *line);       // 0.11: the replay of a match with the new AI (below)
static int __stdcall lua_log(void *L) {          // eliteai_log(a, b, ...) -> one line in EliteAI.log
    char line[1024]; int n = 0, top = p_gettop(L);
    for (int i = 1; i <= top && n < (int)sizeof line - 2; i++) {
        size_t len = 0; const char *s = p_tolstring(L, i, &len);
        n += snprintf(line + n, sizeof line - n, "%s%s", i > 1 ? " " : "", s ? s : "nil");
    }
    line[sizeof line - 1] = 0; logmsg("AI: %s", line); replay_note(line); return 0;
}
static int mp_game(void);                        // 0.16: a network match (below, MULTIPLAYER)
static int mp_scar_key(const char *k) { return k && ((!strncmp(k, "Net_", 4) && strcmp(k, "Net_Log")) || !strcmp(k, "Brain_GlobalPosScar")); }
static int __stdcall lua_cfg(void *L) {          // eliteai_cfg("Key") -> string from EliteAI.ini [Lua], or nil (quick tuning, A/B tests)
    const char *k = p_tolstring(L, 1, NULL); char v[256] = "";
    char ini[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, g_ini, -1, ini, sizeof ini, NULL, NULL);
    // 0.16: in a network match the scripts are told so ("MP" -> "1"), and the keys that would bring an order back to the SCAR
    // path (a command of this machine only = a desync) do not exist: Net_* (but Net_Log) and Brain_GlobalPosScar read as unset
    if (k && !strcmp(k, "MP")) { if (mp_game()) p_pushstring(L, "1"); else p_pushnil(L); return 1; }
    if (mp_scar_key(k) && mp_game()) {
        static long said; GetPrivateProfileStringA("Lua", k, "", v, sizeof v, ini);
        if (v[0] && said++ < 12) logmsg("MP: [Lua] %s=%s is not read in a network match (it would order through SCAR on this machine only)", k, v);
        p_pushnil(L); return 1;
    }
    if (k) GetPrivateProfileStringA("Lua", k, "", v, sizeof v, ini);
    if (v[0]) p_pushstring(L, v); else p_pushnil(L);
    return 1;
}
// combat director: the AI state can't give orders (no Command_* there); it queues them here and a SCAR rule
// (EliteAI\append\scar\elite\elite.scar) takes them once a second and issues them with Cmd_*.
typedef struct { double id, x, y, z, mode; } Order;
static Order g_orders[1024]; static int g_nOrders;
static int __stdcall lua_order(void *L) {          // eliteai_order(squad_world_id, x, y, z, mode)  mode 1 move, 2 attack-move, 3 retreat
    EnterCriticalSection(&g_lk);
    if (g_nOrders < 1024) { Order *o = &g_orders[g_nOrders++];
        o->id = p_tonumber(L, 1); o->x = p_tonumber(L, 2); o->y = p_tonumber(L, 3); o->z = p_tonumber(L, 4); o->mode = p_tonumber(L, 5); }
    LeaveCriticalSection(&g_lk); return 0;
}
static void probe_install(void);
static int __stdcall lua_take(void *L) {           // eliteai_take_orders() -> "id x y z mode;..." (empty string = none)
    static char buf[1024 * 64]; int n = 0; buf[0] = 0;
    probe_install();
    // 0.16: in a network match NOTHING that elite.scar could turn into a command of its own reaches it - only the marks of
    // the orders already sent as network commands (mode + 1000 and above: watched) and the scripts' words that order
    // nothing (9 the variant's label, 10 the dodge policy, 11 the cast watch, 13 the purse, 15 'my orders go by the
    // network'). A plain order (1-8, 14, 100-599) or the old retreat lock (12) is dropped here and counted in the log.
    int mp = mp_game(); long dropped = 0; static long droppedAll;
    EnterCriticalSection(&g_lk);
    for (int i = 0; i < g_nOrders && n < (int)sizeof buf - 128; i++) {
        if (mp) { int m = (int)g_orders[i].mode; if (!(m >= 1000 || m == 9 || m == 10 || m == 11 || m == 13 || m == 15)) { dropped++; continue; } }
        n += snprintf(buf + n, sizeof buf - n, "%.0f %.2f %.2f %.2f %.0f;", g_orders[i].id, g_orders[i].x, g_orders[i].y, g_orders[i].z, g_orders[i].mode);
    }
    g_nOrders = 0; LeaveCriticalSection(&g_lk);
    if (dropped) { long was = droppedAll; droppedAll += dropped; if (was < 20 || was / 500 != droppedAll / 500) logmsg("MP: %ld order(s) for the SCAR path dropped (a network match takes network commands only; %ld so far)", dropped, droppedAll); }
    p_pushstring(L, buf); return 1;
}
// ---- 0.15 (brain step 9): THE AI PLAYER'S ORDERS AS NETWORK COMMANDS -------------------------------------------------------
// Until 0.14 every order of the new AI went through the queue above into SCAR (Cmd_* of elite.scar): straight into the
// simulation of this machine, past the game's command stream - not in the .rec (the replay of such a match desyncs at the
// first order), and in a network game only the host would have them. The engine's own AI sends its orders another way
// (DOW2.exe 3.19.10320, AICommand_*::SendCommand, e.g. 0x00A6FF00 move, 0x00A71E40 stop):
//     cmd = WorldDoCommandSquad(type, queued, player, squad)      SimEngine.dll export: builds a WorldCommand
//     the custom data of the command (target, ability ...)        WorldCommand::SetCustomDataRaw(type byte, bytes, len)
//     proxy->DoCommand(cmd)                                       CommandProxy of the AIPlayer (vtable + 0x5C)
// and the proxy hands it to the game's command interface: the command is numbered, recorded and sent like a human's click.
// eliteai_cmd does exactly these three calls for the AI player that calls it. It is a TRANSPORT: integers and bytes in,
// no arithmetic, no float, no clock (the owner's rule above). What to order and every byte of the custom data (the
// position as IEEE bits included) is made by the Lua script (mod/netorder.ai).
//     eliteai_cmd(player id, receiver kind 1 squad | 2 entity | 3 the player, receiver id, command type, custom data
//                 type (-1 none), "hex bytes of the custom data") -> 1 sent, or 0 and a reason word
// Addresses of DOW2.exe (checked by bytes before the first use; another exe = nothing is called, the function says 0
// "exe" and the scripts keep the SCAR path): AIPlayer_GetLocal 0x008E8820 (stdcall, player id -> AIPlayer*),
// AIPlayer + 0x0C Player*, + 0x14 CommandProxy*; World + 0x154 squad manager (vtable + 8: by id), + 0xF8 entity manager
// (vtable + 0x14: by id); Squad + 0x4C its Player*.
#define FN_AIP_GETLOCAL 0x008E8820
static const unsigned char AIP_GETLOCAL_BYTES[16] = { 0x8B, 0x44, 0x24, 0x04, 0x8B, 0x0D, 0x1C, 0x91, 0x3A, 0x01, 0x50, 0xA1, 0x18, 0x91, 0x3A, 0x01 };
typedef void *(__stdcall *AipGetLocal)(unsigned long pid);
typedef void *(__stdcall *WdcObj)(unsigned long type, int queued, void *player, void *obj);
typedef void (__attribute__((thiscall)) *SetRaw)(void *cmd, unsigned char type, const void *data, unsigned len);
typedef void *(__attribute__((thiscall)) *ById)(void *mgr, unsigned long id);
typedef void (__attribute__((thiscall)) *ProxyDo)(void *proxy, void *cmd);
static WdcObj p_wdcSquad, p_wdcEntity, p_wdcPlayer; static SetRaw p_setRaw; static void **p_gWorld;
static void (__stdcall *p_pushinteger)(void *L, int n);
static int (__stdcall *p_tointeger)(void *L, int idx);
static int g_cmdState;                           // 0 not looked at, 1 usable, -1 not usable (the reason is in the log once)
static long g_cmdSent, g_cmdRefused;
// 0.18: relabel() asks it too, from the thread that draws the lobby - the first answer is worked out once, under g_lk, so a
// second thread never sees a state half set (cmd_probe only answers; g_cmdState is written once, here). Nothing here depends on the game's state: SimEngine.dll
// is an import of DOW2.exe (loaded before this DLL), its exports, the exe's bytes and the Lua calls (DllMain) are there from
// the start - so the answer in a lobby is the answer the match gets.
static int cmd_probe(void);
static int cmd_ready(void) {
    if (g_cmdState) return g_cmdState > 0;
    EnterCriticalSection(&g_lk); if (!g_cmdState) g_cmdState = cmd_probe() ? 1 : -1; LeaveCriticalSection(&g_lk);
    return g_cmdState > 0;
}
static int cmd_probe(void) {
    HMODULE se = GetModuleHandleA("SimEngine.dll");
    if (!se) { logmsg("NET cmd: SimEngine.dll not loaded"); return 0; }
    p_wdcSquad = (WdcObj)GetProcAddress(se, "?WorldDoCommandSquad@@YGPAVWorldCommand@@K_NPBVPlayer@@PBVSquad@@@Z");
    p_wdcEntity = (WdcObj)GetProcAddress(se, "?WorldDoCommandEntity@@YGPAVWorldCommand@@K_NPBVPlayer@@PBVEntity@@@Z");
    p_wdcPlayer = (WdcObj)GetProcAddress(se, "?WorldDoCommandPlayer@@YGPAVWorldCommand@@K_NPBVPlayer@@1@Z");
    p_setRaw = (SetRaw)GetProcAddress(se, "?SetCustomDataRaw@WorldCommand@@QAEXEPBXI@Z");
    p_gWorld = (void **)GetProcAddress(se, "?g_World@@3PAVWorld@@A");
    if (!p_wdcSquad || !p_wdcEntity || !p_wdcPlayer || !p_setRaw || !p_gWorld) { logmsg("NET cmd: SimEngine exports missing - the orders stay on the SCAR path"); return 0; }
    if (IsBadReadPtr((void *)FN_AIP_GETLOCAL, 16) || memcmp((void *)FN_AIP_GETLOCAL, AIP_GETLOCAL_BYTES, 16)) {
        logmsg("NET cmd: DOW2.exe differs at %08X (not 3.19.10320) - the orders stay on the SCAR path", FN_AIP_GETLOCAL); return 0; }
    if (!p_pushinteger || !p_tointeger) { logmsg("NET cmd: no lua_pushinteger / lua_tointeger"); return 0; }
    logmsg("NET cmd: ready (the AI player's orders go as network commands: WorldDoCommand* + CommandProxy::DoCommand)");
    return 1;
}
// ---- 0.16: MULTIPLAYER (the owner, 14.11.2026: the new AI may be tried in network games) ---------------------------------
// The engine computes a computer player on ONE station and the others get its orders as commands (the replay pool; the
// step 9 playback). Which station: the game's slot table - S = *(0x01335A78) + 0x118 (S + 0 is the game type), records of
// 0xA8 bytes at [S + 0x810], [S + 0x814] of them; record + 0x9C the player id, + 0x58 the team, + 0x60 the id (64 bit) of
// the station that owns the slot - for a computer, the station that simulates it; S + 0xE48 this machine's station id.
// Read from DOW2.exe 3.19.10320: a station drops -> 0x00425E.. picks the lowest remaining station id -> for every computer
// of the dropped one 0x004261D4 writes the new id to record + 0x60 and, IF IT IS [S + 0xE48], makes the AI on this
// machine (record + 0 = 1; otherwise 4) - "GameObj - AI Simulation for player [%d] on team [%d] migrated ...". The AI
// object, its Lua state and so its scripts exist only where the AI is simulated: a station that does not compute a
// computer never loads an AI script for it, and this DLL gives it none and orders nothing for it.
//   * mp_computes(pid): the slot of pid is owned by this station AND the engine has an AIPlayer of pid here. eliteai_cmd
//     and eliteai_retlock ask it at EVERY call in a network match - after a migration the answer is the engine's new one;
//   * what the new AI loads must not change the simulation on this machine only: the AI scripts order by eliteai_cmd
//     (commands of the player, sent to every station); elite.scar gets no order to execute (lua_take), the keys of the
//     SCAR path are not read (lua_cfg), and without the engine calls of eliteai_cmd (another exe) the new AI stays out;
//   * [EliteAI] Multiplayer=0 in EliteAI.ini: network matches as before 0.16 - Elite's own scripts, nothing of ours.
// Integers and pointer compares only; nothing here is timed by the wall clock.
#define MP_TYPES 0x558
typedef struct { int type, pid, team; unsigned lo, hi; } MpSlot;
static DWORD game_type(void);
// [EliteAI] / [Lua] MpSelfTest=1 (the bench only, never in a package): a single-player game is treated as a network match
// by everything below - the gate, the slot table, the per-call check, the filters - since the bench cannot run two stations.
// A single-player game has no station ids (this station FFFFFFFFFFFFFFFF, the slots 0): with 1 every order is refused as
// 'another station's' (what a station that does not compute the AI does); with 2 the missing ids are let through, so the
// AI plays under the network match's filters.
static int mp_selftest(void) { static int k = -1; if (k < 0 && g_ini[0]) { k = GetPrivateProfileIntW(L"EliteAI", L"MpSelfTest", 0, g_ini); if (!k) k = GetPrivateProfileIntW(L"Lua", L"MpSelfTest", 0, g_ini); if (k < 0) k = 0; } return k > 0 ? k : 0; }
static int mp_game(void) { DWORD t = game_type(); return ((t & MP_TYPES) && !(t & 0x800)) || (t && !(t & 0x800) && mp_selftest()); }
static int mp_on(void) { static int k = -1; if (k < 0 && g_ini[0]) k = GetPrivateProfileIntW(L"EliteAI", L"Multiplayer", 1, g_ini) ? 1 : 0; return k != 0; }
static int mp_slots(MpSlot *o, int cap, unsigned *llo, unsigned *lhi) {     // -> the number of slots read (0: no game)
    char **g = (char **)0x01335A78; *llo = *lhi = 0xFFFFFFFF;
    if (IsBadReadPtr(g, 4) || !*g || IsBadReadPtr(*g + 0x118, 0xE50)) return 0;
    char *S = *g + 0x118; unsigned n = *(unsigned *)(S + 0x814); char *b = *(char **)(S + 0x810);
    *llo = *(unsigned *)(S + 0xE48); *lhi = *(unsigned *)(S + 0xE4C);
    if (!b || !n || n > 16 || IsBadReadPtr(b, n * 0xA8)) return 0;
    if ((int)n > cap) n = cap;
    for (unsigned i = 0; i < n; i++) { char *r = b + i * 0xA8; o[i].type = *(int *)r; o[i].pid = *(int *)(r + 0x9C); o[i].team = *(int *)(r + 0x58); o[i].lo = *(unsigned *)(r + 0x60); o[i].hi = *(unsigned *)(r + 0x64); }
    return (int)n;
}
static int mp_computes(int pid) {                // 1 this station simulates the computer pid, 0 another one does, -1 no such slot
    MpSlot s[16]; unsigned lo, hi; int n = mp_slots(s, 16, &lo, &hi);
    for (int i = 0; i < n; i++) if (s[i].pid == pid)
        return ((s[i].lo == lo && s[i].hi == hi && !(lo == 0xFFFFFFFF && hi == 0xFFFFFFFF)) || (lo == 0xFFFFFFFF && hi == 0xFFFFFFFF && mp_selftest() == 2))
            && g_cmdState > 0 && ((AipGetLocal)FN_AIP_GETLOCAL)((unsigned long)pid) != NULL;
    return -1;
}
// the log: who computes what, at the start of a match and whenever the engine's answer for a player changes (a migration)
static struct { int pid, mine; } g_mpSeen[16];
static void mp_slots_line(const char *head) {
    MpSlot s[16]; unsigned lo, hi; int n = mp_slots(s, 16, &lo, &hi); char b[900]; int k = 0; char mine[200] = ""; int mk = 0;
    for (int i = 0; i < n && k < (int)sizeof b - 80; i++) {
        int c = g_cmdState > 0 && s[i].pid >= 1000 && ((AipGetLocal)FN_AIP_GETLOCAL)((unsigned long)s[i].pid) != NULL;   // an AI object of that player on this machine
        k += snprintf(b + k, sizeof b - k, " [%d: player %d type %d team %d station %08X%08X%s]", i, s[i].pid, s[i].type, s[i].team, s[i].hi, s[i].lo, c ? " AI here" : "");
        if (c && mp_computes(s[i].pid) == 1 && mk < (int)sizeof mine - 12) mk += snprintf(mine + mk, sizeof mine - mk, " %d", s[i].pid);
    }
    b[k] = 0;
    logmsg("%s; this station %08X%08X; computing AI players:%s; slots:%s", head, hi, lo, mine[0] ? mine : " none yet", n ? b : " none");
}
static void mp_watch(int pid) {                  // a network match: has the engine's answer for this player changed?
    int c = mp_computes(pid), slot = -1;
    for (int i = 0; i < 16; i++) { if (g_mpSeen[i].pid == pid) { slot = i; break; } if (slot < 0 && !g_mpSeen[i].pid) slot = i; }
    if (slot < 0 || (g_mpSeen[slot].pid == pid && g_mpSeen[slot].mine == c)) return;
    g_mpSeen[slot].pid = pid; g_mpSeen[slot].mine = c;
    { char h[160]; void *w = (p_gWorld && *p_gWorld && !IsBadReadPtr(*p_gWorld, 0x17C)) ? *p_gWorld : NULL;
      snprintf(h, sizeof h, "MP: AI player %d is %s (world tick %d)", pid, c == 1 ? "COMPUTED BY THIS STATION - the new AI orders for it" : c == 0 ? "computed by another station - nothing is ordered for it here" : "in no slot - nothing is ordered for it", w ? *(int *)((char *)w + 0x178) : -1);
      mp_slots_line(h); }
}
// 0.18: WHO COMPUTES WHICH COMPUTER, in plain words, on EVERY station of a network match - also one that computes none, or has
// the new AI off (until 0.17 only a station that computed a computer AND ran the new AI wrote the slot table, so a computer
// left to a station without the new AI was seen nowhere). Written at the match's first SCAR script (load_hook; the AI players
// are made after it, so this is the slot table's owner - the station the engine makes each AI on) and again by a station that
// gets an AI script of ours, in its new log of the match (mp_match_line). Slot types (logs of 15.11): 0 a human here, 3 a
// human elsewhere, 1 a computer simulated here, 4 a computer simulated elsewhere, 7 empty. Reads only; integers and compares.
static void mp_who_line(const char *when) {
    MpSlot s[16]; unsigned lo, hi; int n = mp_slots(s, 16, &lo, &hi); char b[1000]; int k = 0, ai = 0; b[0] = 0;
    int on = mp_on() && cmd_ready();
    for (int i = 0; i < n && k < (int)sizeof b - 160; i++) {
        if (s[i].type != 1 && s[i].type != 4) continue;
        int here = s[i].lo == lo && s[i].hi == hi && !(lo == 0xFFFFFFFF && hi == 0xFFFFFFFF), by = -1; ai++;
        for (int j = 0; j < n; j++) if (j != i && s[j].type != 1 && s[j].type != 4 && s[j].type != 7 && s[j].pid && s[j].lo == s[i].lo && s[j].hi == s[i].hi) { by = s[j].pid; break; }
        k += snprintf(b + k, sizeof b - k, " [computer player %d team %d -> station %08X%08X (%s", s[i].pid, s[i].team, s[i].hi, s[i].lo, here ? "THIS station" : "another station");
        if (by >= 0) k += snprintf(b + k, sizeof b - k, ", the station of human player %d", by);
        else k += snprintf(b + k, sizeof b - k, ", no human slot of that station");
        k += snprintf(b + k, sizeof b - k, ")%s]", here ? (on ? ": the NEW AI plays it" : ": Elite's AI plays it - the new AI is off here") : "");
    }
    logmsg("MP match start: who computes the computers (%s; game type %lX; this station %08X%08X, the new AI here %s):%s%s", when, game_type(), hi, lo,
           on ? "ON" : !mp_on() ? "OFF ([EliteAI] Multiplayer=0)" : "OFF (no network commands with this exe)", ai ? b : " no computer in the slots", ai ? "; a computer on another station plays the AI of THAT station's EliteAI (the new AI only if it has it on)" : "");
}
static void mp_match_line(int fresh) {           // an AI script of ours is given in a network match: the engine makes an AI here
    cmd_ready();
    void *w = (p_gWorld && *p_gWorld && !IsBadReadPtr(*p_gWorld, 0x17C)) ? *p_gWorld : NULL; int tick = w ? *(int *)((char *)w + 0x178) : -1; char h[400];
    if (fresh) { memset(g_mpSeen, 0, sizeof g_mpSeen);
        snprintf(h, sizeof h, "MP: a NETWORK match with the new AI (game type %lX, world tick %d): this station computes computer players - their orders go to every station as network commands; OTHER STATIONS NEED MoveLoopFix (Melee freeze fix) or this add-on: without it a client can hang in the engine's melee freeze where this machine does not. [EliteAI] Multiplayer=0 turns this off", game_type(), tick);
        mp_who_line("this station's log of the match");   // 0.18: the line of the first SCAR script went into the log before the rotation
    } else snprintf(h, sizeof h, "MP: the engine makes an AI player on this station (world tick %d%s)", tick, tick > 600 ? ": mid-match - the AI simulation MIGRATED here, the new AI takes it over" : "");
    mp_slots_line(h);
}
static int cmd_fail(void *L, const char *why) {
    if (++g_cmdRefused <= 20 || g_cmdRefused % 500 == 0) logmsg("NET cmd refused: %s (n %ld)", why, g_cmdRefused);
    p_pushinteger(L, 0); p_pushstring(L, why); return 2;
}
static int __stdcall lua_cmd(void *L) {           // eliteai_cmd(pid, kind, id, type, ctype, hex) -> 1 | 0, reason
    if (!cmd_ready()) { if (p_pushinteger) { p_pushinteger(L, 0); p_pushstring(L, "exe"); return 2; } return 0; }
    int pid = p_tointeger(L, 1), kind = p_tointeger(L, 2), id = p_tointeger(L, 3), type = p_tointeger(L, 4), ctype = p_tointeger(L, 5);
    const char *hex = p_gettop(L) >= 6 ? p_tolstring(L, 6, NULL) : NULL;
    unsigned char data[96]; unsigned n = 0;
    for (; hex && hex[0] && hex[1]; hex += 2) {
        int a = hex[0], b = hex[1];
        a = a >= '0' && a <= '9' ? a - '0' : a >= 'a' && a <= 'f' ? a - 'a' + 10 : -1; b = b >= '0' && b <= '9' ? b - '0' : b >= 'a' && b <= 'f' ? b - 'a' + 10 : -1;
        if (a < 0 || b < 0 || n >= sizeof data) return cmd_fail(L, "hex");
        data[n++] = (unsigned char)(a * 16 + b);
    }
    if (type <= 0 || type > 255 || ctype > 255) return cmd_fail(L, "type");
    if (mp_game()) {                                 // 0.16: a network match - only for a computer THIS station simulates (asked every time)
        mp_watch(pid);
        if (!mp_on()) return cmd_fail(L, "MP off (Multiplayer=0)");
        if (mp_computes(pid) != 1) return cmd_fail(L, "MP: this station does not compute that AI player");
    }
    char *aip = ((AipGetLocal)FN_AIP_GETLOCAL)((unsigned long)pid);
    if (!aip || IsBadReadPtr(aip, 0x18)) return cmd_fail(L, "no AI player");
    void *player = *(void **)(aip + 0x0C); void **proxy = *(void ***)(aip + 0x14);
    char *world = p_gWorld ? *(char **)p_gWorld : NULL;
    if (!player || !proxy || IsBadReadPtr(proxy, 4) || IsBadReadPtr(*proxy, 0x60) || !world || IsBadReadPtr(world, 0x158)) return cmd_fail(L, "no proxy / world");
    void *cmd = NULL;
    if (kind == 1) {
        void **mgr = *(void ***)(world + 0x154); if (!mgr || IsBadReadPtr(mgr, 4) || IsBadReadPtr(*mgr, 12)) return cmd_fail(L, "squad manager");
        char *sq = ((ById)((void **)*mgr)[2])(mgr, (unsigned long)id);
        if (!sq || IsBadReadPtr(sq, 0x50)) return cmd_fail(L, "no squad");
        if (*(void **)(sq + 0x4C) != player) return cmd_fail(L, "not the player's squad");
        cmd = p_wdcSquad((unsigned long)type, 0, player, sq);
    } else if (kind == 2) {
        void **mgr = *(void ***)(world + 0xF8); if (!mgr || IsBadReadPtr(mgr, 4) || IsBadReadPtr(*mgr, 0x18)) return cmd_fail(L, "entity manager");
        void *en = ((ById)((void **)*mgr)[5])(mgr, (unsigned long)id);
        if (!en) return cmd_fail(L, "no entity");
        cmd = p_wdcEntity((unsigned long)type, 0, player, en);
    } else if (kind == 3) cmd = p_wdcPlayer((unsigned long)type, 0, player, player);
    else return cmd_fail(L, "kind");
    if (!cmd) return cmd_fail(L, "the engine made no command");
    if (ctype >= 0) p_setRaw(cmd, (unsigned char)ctype, data, n);
    ((ProxyDo)((void **)*proxy)[0x5C / 4])(proxy, cmd);
    g_cmdSent++;
    p_pushinteger(L, 1); return 1;
}
// 0.15: THE ENGINE'S OWN RETREATS OFF FOR A BRAIN PLAYER, ON THE AI'S SIDE. The engine's squad tasks send squads home by
// themselves (brain step 2: 12 such retreats in 15 scenes, squads at 0.5-0.9 of their health). Until 0.14 elite.scar
// switched the retreat off IN THE SIMULATION (Squad_EnableRetreat) - a change of the simulation from SCAR, in no
// recording. What happened then: the engine's AI went on SENDING its retreat commands (bench s81_l8i: 526 commands 0x3D
// of the brain players in 48 games) and the simulation refused each. The same result without touching the simulation:
// the engine's AI sends every retreat from ONE place (DOW2.exe AICommand_SquadRetreat::SendCommand, the hand-over to the
// CommandProxy at 0x00A71B29); for the proxy of a player whose brain asked for it (eliteai_retlock) the command is not
// handed over, for every other player it goes on as before. The AI's tasks see what they saw under the old lock - an
// order that did nothing; the brain's own retreat is eliteai_cmd, which does not pass here. Integers and pointer compares.
// (The command object the engine made is left alone: ~0x90 bytes, a few hundred times a match.)
#define AT_AI_RETREAT_SEND 0x00A71B29
static const unsigned char AI_RETREAT_SEND_BYTES[11] = { 0x8B, 0x55, 0x00, 0x8B, 0x42, 0x5C, 0x56, 0x8B, 0xCD, 0xFF, 0xD0 };   // mov edx,[ebp]; mov eax,[edx+5C]; push esi; mov ecx,ebp; call eax
// 0.16 (the verifier of step 9, note 1): the table of the locks is retlock.h - it is emptied when a match starts and a
// lock ends when its AI player is gone (0.15 kept it for the life of the process: a later match of the same session
// could inherit it). World + 0x178 = the simulation's tick, an int.
static long g_retSaid;
#define RL_AIP(pid)     (((AipGetLocal)FN_AIP_GETLOCAL)((unsigned long)(pid)))
#define RL_OK(p, n)     (!IsBadReadPtr((p), (n)))
#define RL_PLAYER(aip)  (*(void **)((char *)(aip) + 0x0C))
#define RL_PROXY(aip)   (*(void **)((char *)(aip) + 0x14))
#define RL_WORLD()      ((p_gWorld && *p_gWorld && !IsBadReadPtr(*p_gWorld, 0x17C)) ? *p_gWorld : NULL)
#define RL_TICK(w)      (*(int *)((char *)(w) + 0x178))
#define RL_SAY(why, n)  do { if (++g_retSaid <= 40 || g_retSaid % 500 == 0) logmsg("NET retlock: %d lock(s) dropped - %s (n %ld)", (int)(n), (why), g_retSaid); } while (0)
#include "retlock.h"
static int g_retHook;   // 0 not tried, 1 installed, -1 cannot
static long g_retDropped;
static void __stdcall ai_retreat_send(void **proxy, void *cmd) {         // (esi = the command, ebp = the proxy at the patched place)
    if (ret_locked(proxy)) { g_retDropped++; return; }
    ((ProxyDo)((void **)*proxy)[0x5C / 4])(proxy, cmd);
}
static int __stdcall lua_retlock(void *L) {       // eliteai_retlock(player id, 1 | 0) -> 1 done, 0 not available
    if (!cmd_ready()) { if (p_pushinteger) { p_pushinteger(L, 0); return 1; } return 0; }
    int pid = p_tointeger(L, 1), on = p_tointeger(L, 2);
    if (!g_retHook) {
        unsigned char *c = (unsigned char *)AT_AI_RETREAT_SEND; g_retHook = -1;
        if (!IsBadReadPtr(c, 11) && !memcmp(c, AI_RETREAT_SEND_BYTES, 11)) {
            DWORD o; if (VirtualProtect(c, 11, PAGE_EXECUTE_READWRITE, &o)) {
                unsigned char n[11] = { 0x56, 0x55, 0xE8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 };            // push esi; push ebp; call ai_retreat_send; nop x4
                *(int *)(n + 3) = (int)((DWORD)ai_retreat_send - (AT_AI_RETREAT_SEND + 7));
                memcpy(c, n, 11); VirtualProtect(c, 11, o, &o); FlushInstructionCache(GetCurrentProcess(), c, 11); g_retHook = 1; }
        }
        { void *w = RL_WORLD(); logmsg("NET retlock: the engine AI's retreat hand-over at %08X %s (world tick %d)", AT_AI_RETREAT_SEND, g_retHook > 0 ? "redirected" : "NOT redirected (bytes differ)", w ? RL_TICK(w) : -1); }
    }
    if (g_retHook < 0) { p_pushinteger(L, 0); return 1; }
    if (on && mp_game() && (!mp_on() || mp_computes(pid) != 1)) { p_pushinteger(L, 0); return 1; }   // 0.16: not this station's AI player
    p_pushinteger(L, ret_set(pid, on)); return 1;
}
// Developer's probe ([EliteAI] CmdProbe=1, the bench only): what a SCAR command of elite.scar hands to the simulation -
// World::DoCommand (vtable + 0x78 of the world) is wrapped and every command is written to the log (type, custom data
// type, bytes). Read only; it is how the bytes mod/netorder.ai builds were learnt. Never on without the key.
typedef void (__attribute__((thiscall)) *WorldDo)(void *world, void *cmd);
static WorldDo g_probeOrig; static void *g_probeVt[256]; static int g_probeOn = -1;
static void __attribute__((thiscall)) probe_do(void *world, void *cmd) {
    static unsigned long (__attribute__((thiscall)) *gc)(void *), (__attribute__((thiscall)) *gl)(void *), (__attribute__((thiscall)) *rc)(void *);
    static unsigned char (__attribute__((thiscall)) *gt)(void *); static void *(__attribute__((thiscall)) *gd)(void *); static long n;
    HMODULE se = GetModuleHandleA("SimEngine.dll");
    if (!gc && se) { gc = (void *)GetProcAddress(se, "?GetCommand@WorldCommand@@QBEKXZ"); gl = (void *)GetProcAddress(se, "?GetCustomDataLength@WorldCommand@@QBEIXZ");
        gt = (void *)GetProcAddress(se, "?GetCustomDataType@WorldCommand@@QBEEXZ"); gd = (void *)GetProcAddress(se, "?GetCustomData@WorldCommand@@QBEPAXXZ");
        rc = (void *)GetProcAddress(se, "?GetReceiverCount@WorldCommand@@QBEIXZ"); }
    DWORD ra = (DWORD)__builtin_return_address(0);          // (only the SCAR bindings Command_*: DOW2.exe 0x937000..0x93C000)
    if (gc && gl && gt && gd && rc && cmd && ra >= 0x00937000 && ra < 0x0093C000 && ++n <= 6000) {
        unsigned len = gl(cmd); unsigned char *d = gd(cmd); char hx[200]; int k = 0;
        for (unsigned i = 0; d && i < len && i < 90; i++) k += sprintf(hx + k, "%02x", d[i]);
        hx[k] = 0; logmsg("CMDPROBE type %02lx ctype %02x len %u recv %lu data %s from %08lx", gc(cmd), gt(cmd), len, rc(cmd), hx, ra);
    }
    g_probeOrig(world, cmd);
}
static void probe_install(void) {
    if (g_probeOn >= 0) return;
    g_probeOn = (GetPrivateProfileIntW(L"EliteAI", L"CmdProbe", 0, g_ini) || GetPrivateProfileIntW(L"Lua", L"CmdProbe", 0, g_ini)) ? 1 : 0;
    if (!g_probeOn) return;
    void ***w = (void ***)0x013356A4;
    if (IsBadReadPtr(w, 4) || !*w || IsBadReadPtr(*w, 4) || IsBadReadPtr(**w, sizeof g_probeVt)) { g_probeOn = -1; return; }
    memcpy(g_probeVt, **w, sizeof g_probeVt); g_probeOrig = (WorldDo)g_probeVt[0x78 / 4]; g_probeVt[0x78 / 4] = (void *)probe_do;
    **w = (void *)g_probeVt; logmsg("CMDPROBE on: World::DoCommand wrapped (orig %p)", (void *)g_probeOrig);
}
static void give_log(void *L) {
    if (!p_pushcclosure || !p_setfield) return;
    p_pushcclosure(L, lua_log, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_log");
    p_pushcclosure(L, lua_cfg, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_cfg");
    if (p_tonumber) {
        p_pushcclosure(L, lua_order, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_order");
        p_pushcclosure(L, lua_take, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_take_orders");
        p_pushcclosure(L, lua_cmd, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_cmd");
        p_pushcclosure(L, lua_retlock, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_retlock");
    }
    if (p_getfield && p_settop) {                 // AI scripts run in their own environment: also via the shared string table
        p_getfield(L, LUA_GLOBALSINDEX, "string");
        p_pushcclosure(L, lua_log, 0); p_setfield(L, -2, "eliteai_log");
        p_pushcclosure(L, lua_cfg, 0); p_setfield(L, -2, "eliteai_cfg");
        if (p_tonumber) { p_pushcclosure(L, lua_order, 0); p_setfield(L, -2, "eliteai_order");
                          p_pushcclosure(L, lua_take, 0); p_setfield(L, -2, "eliteai_take_orders"); }
        if (p_tonumber) { p_pushcclosure(L, lua_cmd, 0); p_setfield(L, -2, "eliteai_cmd"); p_pushcclosure(L, lua_retlock, 0); p_setfield(L, -2, "eliteai_retlock"); }
        p_settop(L, -2);
    }
}

typedef struct { const char *buf; size_t len; int done; } BufState;
static const char *__stdcall buf_reader(void *L, void *data, size_t *size) {
    BufState *s = (BufState *)data; (void)L;
    if (s->done) { *size = 0; return NULL; }
    s->done = 1; *size = s->len; return s->buf;
}

static char *read_file(const wchar_t *p, size_t *len) {
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(f, NULL), got = 0; char *b = NULL;
    if (sz != INVALID_FILE_SIZE && sz < (8u << 20) && (b = malloc(sz + 1))) {
        if (!ReadFile(f, b, sz, &got, NULL) || got != sz) { free(b); b = NULL; } else { b[sz] = 0; *len = sz; }
    }
    CloseHandle(f); return b;
}

static int append_load(void *L, LuaReader reader, void *data, const char *chunkname, const char *add, size_t alen) {
    size_t cap = 1 << 16, n = 0; char *all = malloc(cap + alen + 64);
    for (;;) { size_t sz = 0; const char *part = reader(L, data, &sz); if (!part || !sz) break;
        if (n + sz + alen + 64 > cap) { cap = (n + sz) * 2; all = realloc(all, cap + alen + 64); }
        memcpy(all + n, part, sz); n += sz; }
    all[n++] = '\n'; memcpy(all + n, add, alen); n += alen;
    BufState st = { all, n, 0 }; give_log(L);
    int r = g_loadOrig(L, buf_reader, &st, chunkname); free(all); return r;
}
// Until 0.15 the new AI came on only in single-player games: its SCAR commands ran on the machine that had it, so one
// player with it would have desynced an online game. Since brain step 9 every order is a network command of the AI's
// player, and 0.16 lets it into network matches too (MULTIPLAYER above); a replay still replays with Elite's scripts. The game type
// is [*(0x01335A78) + 0x118] (reverse 21.10): SP_SKIRMISH 0x4, SP_FFA_SOLO 0x80, SP_FFA_TEAM 0x200; every MP_* is in
// 0x558, PLAYBACK 0x800. (0.5 used the network session's flags instead - they are set whenever the game is logged in
// to Steam, so the new AI never came on, not even in a skirmish.)
#define G_GAME      0x01335A78
#define SP_TYPES    (0x4 | 0x80 | 0x200)
#define NOT_SP      (0x558 | 0x800)
static DWORD game_type(void) {
    DWORD *g = (DWORD *)G_GAME;
    if (IsBadReadPtr(g, 4) || !*g || IsBadReadPtr((void *)(*g + 0x118), 4)) return 0;
    return *(DWORD *)(*g + 0x118);
}
static int single_player(void) { DWORD t = game_type(); return (t & SP_TYPES) && !(t & NOT_SP); }
// 0.10 (10.11): ReplayTakeover 3 turns a replay into a skirmish (type 0x800 -> 4) and makes the computers then, so the AI
// scripts come here as in a skirmish - but the SCAR scripts were loaded by the replay, without the order queue rule.
// With ReplayTakeover.dll in the game, a replay's SCAR also gets EliteAI\append (a rule taking the queue once a second:
// nothing in a plain replay, where no new AI runs, so it gives no command). The AI scripts of a replay stay Elite's.
#define T_PLAYBACK  0x800
static int replay_takeover_on(void) { return game_type() == T_PLAYBACK && GetModuleHandleA("ReplayTakeover.dll") != NULL; }
static int __stdcall load_hook(void *L, LuaReader reader, void *data, const char *chunkname) {
    char rel[400];
    int rt = 0;
    if (chunkname) {                                   // 0.16: a SCAR script is being loaded = a world starts (before its AI players
        char b[400]; int n = 0;                        // are made; any game type) - no retreat lock of the match before outlives it
                                                       // (0.17: or an AI player is made mid-match - retlock.h ret_scar_load)
        for (const char *s = chunkname; *s && n < 398; s++) b[n++] = (*s == '/') ? '\\' : (char)tolower((unsigned char)*s);
        b[n] = 0; if (strstr(b, "simulation\\scar\\")) { ret_scar_load();   // 0.17: a new match only (a migration keeps the locks)
            // 0.18: every station of a network match says who computes each computer - once per burst of SCAR loads (a match's
            // start; the engine loads SCAR again when it makes an AI player mid-match = a migration). The wall clock only picks
            // when the LOG line is written.
            static DWORD lastScar; DWORD now = GetTickCount();
            if (mp_game() && (!lastScar || now - lastScar > LOG_MATCH_GAP_MS)) {
                cmd_ready();                                 // (p_gWorld for the tick; it only looks at the exe)
                void *w = (p_gWorld && *p_gWorld && !IsBadReadPtr(*p_gWorld, 0x17C)) ? *p_gWorld : NULL; int tick = w ? *(int *)((char *)w + 0x178) : -1;
                mp_who_line(tick > 600 ? "a SCAR script mid-match: an AI player is made again, e.g. a migration" : "the match's first SCAR script");
            }
            lastScar = now ? now : 1;
        }
    }
    // 0.16: a network match is in too (see MULTIPLAYER above) - unless [EliteAI] Multiplayer=0, or the engine calls of
    // eliteai_cmd are not there (another exe: the orders would have to go through SCAR, on this machine only)
    int mp = 0;
    if (!single_player() && !(rt = replay_takeover_on()) && !(mp = (mp_game() && mp_on() && cmd_ready()))) {   // replay, menus, MP switched off: Elite's own scripts, untouched
        static DWORD said, lastType = 1;
        DWORD t = game_type();
        if (t && (t != lastType || GetTickCount() - said > 60000)) { said = GetTickCount(); lastType = t;
            logmsg("game type %lX (not single player%s): the new AI stays out", t, !mp_game() ? "" : !mp_on() ? "; a network match, [EliteAI] Multiplayer=0" : "; a network match, but no network commands with this exe"); }
        return g_loadOrig(L, reader, data, chunkname);
    }
    if (chunkname) {                                   // SCAR scripts: EliteAI\append\scar\<name> is appended (the order queue of the new AI)
        char b[400]; int n = 0;
        for (const char *s = chunkname; *s && n < 398; s++) b[n++] = (*s == '/') ? '\\' : (char)tolower((unsigned char)*s);
        b[n] = 0; const char *p = strstr(b, "simulation\\scar\\");
        if (p && !strstr(p, "..")) {
            wchar_t f[MAX_PATH]; _snwprintf(f, MAX_PATH, L"%lsEliteAI\\append\\%hs", g_dir, p + 11);
            size_t alen = 0; char *add = read_file(f, &alen);
            if (add) { int r = append_load(L, reader, data, chunkname, add, alen); logmsg("%s + append (%u bytes), load %d%s", chunkname, (unsigned)alen, r, rt ? " (replay: for ReplayTakeover)" : ""); free(add); return r; }
        }
    }
    if (rt) return g_loadOrig(L, reader, data, chunkname);  // a replay: only the SCAR queue rule, the AI scripts are Elite's
    if (ai_rel(chunkname, rel, sizeof rel)) {
        wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsEliteAI\\%hs", g_dir, rel);
        size_t len = 0; char *buf = read_file(p, &len);
        if (buf) {
            int fresh = log_match(chunkname);                // 0.14: a log per match of the new AI (an AI script of ours is given)
            if (!strcmp(rel, "simulation\\ai\\default.ai")) { if (mp || mp_game()) mp_match_line(fresh); else if (fresh && cmd_ready()) mp_slots_line("slots of this game (single player)"); }
            BufState st = { buf, len, 0 }; give_log(L);
            int r = g_loadOrig(L, buf_reader, &st, chunkname);
            logmsg("ai script %s -> ours (%u bytes), load %d", chunkname, (unsigned)len, r);
            free(buf); return r;
        }
        if (!strcmp(rel, "simulation\\ai\\default.ai")) {           // Elite's default.ai + our module at its end
            _snwprintf(p, MAX_PATH, L"%lsEliteAI\\main.ai", g_dir);
            size_t mlen = 0; char *mod = read_file(p, &mlen);
            if (mod) {                                               // mechanics.ai (10.11): right after main.ai, same chunk
                { int fresh = log_match(chunkname); if (mp) mp_match_line(fresh); }
                _snwprintf(p, MAX_PATH, L"%lsEliteAI\\mechanics.ai", g_dir);
                size_t xlen = 0; char *mech = read_file(p, &xlen);
                if (mech) {
                    static const char ms[] = "\n-- ===== EliteAI mechanics.ai =====\n";
                    char *both = malloc(mlen + sizeof ms + xlen);
                    memcpy(both, mod, mlen); memcpy(both + mlen, ms, sizeof ms - 1); memcpy(both + mlen + sizeof ms - 1, mech, xlen);
                    mlen += sizeof ms - 1 + xlen; free(mod); free(mech); mod = both;
                    logmsg("main.ai + mechanics.ai (%u bytes)", (unsigned)xlen);
                }
                size_t cap = 1 << 16, n = 0; char *all = malloc(cap + mlen + 64);
                for (;;) {                                           // the original chunk, as the engine's reader gives it
                    size_t sz = 0; const char *part = reader(L, data, &sz);
                    if (!part || !sz) break;
                    if (n + sz + mlen + 64 > cap) { cap = (n + sz) * 2; all = realloc(all, cap + mlen + 64); }
                    memcpy(all + n, part, sz); n += sz;
                }
                // Elite's default.ai ends with a top-level "InitializeAI()" call: our module goes right before it,
                // so it can wrap InitializeAI / adjust what the AI is built from; no such call -> at the end
                all[n] = 0; size_t at = n; char *q = all, *hit;
                while ((hit = strstr(q, "InitializeAI()"))) {
                    if ((hit == all || hit[-1] == '\n') ) at = hit - all;     // line start = the call, not "function InitializeAI()"
                    q = hit + 1;
                }
                static const char sep[] = "\n-- ===== EliteAI main.ai =====\n", sep2[] = "\n-- ===== /EliteAI =====\n";
                size_t tail = n - at; char *rest = malloc(tail + 1); memcpy(rest, all + at, tail);
                n = at; memcpy(all + n, sep, sizeof sep - 1); n += sizeof sep - 1; memcpy(all + n, mod, mlen); n += mlen;
                if (n + sizeof sep2 + tail > cap + mlen + 64) all = realloc(all, n + sizeof sep2 + tail + 1);
                memcpy(all + n, sep2, sizeof sep2 - 1); n += sizeof sep2 - 1; memcpy(all + n, rest, tail); n += tail; free(rest);
                BufState st = { all, n, 0 }; give_log(L);
                int r = g_loadOrig(L, buf_reader, &st, chunkname);
                logmsg("default.ai + main.ai (%u + %u bytes), load %d", (unsigned)(n - mlen), (unsigned)mlen, r);
                free(all); free(mod); return r;
            }
        }
        if (g_verbose) logmsg("ai script %s -> Elite's", chunkname);
    } else if (g_verbose > 1) logmsg("lua chunk %s", chunkname ? chunkname : "(null)");
    return g_loadOrig(L, reader, data, chunkname);
}

// 0.7: the new AI is visible in the lobby. It plays as the Normal computer, so the game setup menu says so: "Add Normal
// Computer" / "Normal Computer Player" (ucs 555027 / 555036) read "new AI" while this add-on is on. The game takes its
// texts from Localizer.dll (GetString by number, ConvertLocIDToString for "$555036" from the menus) - hooked in the
// import tables of every module. Russian if the original text is Cyrillic, English otherwise.
typedef struct { void *vt; void *imp; } LocString;                                     // opaque, only passed on
typedef LocString *(__stdcall *GetStrV)(LocString *ret, int id);
typedef unsigned char (__stdcall *GetStrB)(int id, LocString *out);
typedef LocString *(__stdcall *ConvId)(LocString *ret, const wchar_t *s);
typedef LocString *(__fastcall *LsAssign)(LocString *self, void *edx, const wchar_t *s);
typedef const wchar_t *(__fastcall *LsCstr)(const LocString *self, void *edx);
static GetStrV o_getv; static GetStrB o_getb; static ConvId o_conv; static LsAssign p_assign; static LsCstr p_cstr;

// 0.9 (10.11): three variants of the new AI for testers to compare - the slot's difficulty picks it: Easy = A, Normal = B,
// Hard = C (main.ai EliteAI_VARIANTS); Expert stays Elite's AI. ucs: Add Easy/Normal/Hard 555026-555028, players 555035-555037.
static void relabel(int id, LocString *ls) {
    const wchar_t *w = NULL; int v;
    if (id >= 555026 && id <= 555028) v = id - 555026; else if (id >= 555035 && id <= 555037) v = id - 555035; else return;
    // 0.18 (fb 1370/1374/1379/1381/1389, COOP_RESET_RU.md): a NETWORK lobby gets the label too. Until 0.17 a second human in
    // a custom lobby made the game type a network one and the labels fell back to "Hard Computer" (back again when he left),
    // though 0.16 plays the new AI in network matches. The test is load_hook's own for a network match - mp_game() the type
    // (the lobby has it already: Malum's "Start game" button), mp_on() EliteAI.ini, cmd_ready() the exe's engine calls
    // (nothing of the match's state: true in the lobby iff it will be true at the match's start) - so the label shows exactly
    // where a computer this station computes plays the new AI. The label is this machine's only; who computes which
    // computer is in the log at the match's start ("MP match start: who computes the computers").
    if (!single_player() && !(mp_game() && mp_on() && cmd_ready())) return;
    const wchar_t *orig = p_cstr(ls, NULL); int ru = 0;
    for (const wchar_t *c = orig; c && *c; c++) if (*c >= 0x400 && *c < 0x500) { ru = 1; break; }
    static const wchar_t *add_en[3] = { L"Add NEW AI A", L"Add NEW AI B", L"Add NEW AI C" };
    static const wchar_t *add_ru[3] = { L"Добавить НОВЫЙ ИИ A", L"Добавить НОВЫЙ ИИ B", L"Добавить НОВЫЙ ИИ C" };
    static const wchar_t *pl_en[3]  = { L"NEW AI A", L"NEW AI B", L"NEW AI C" };
    static const wchar_t *pl_ru[3]  = { L"НОВЫЙ ИИ A", L"НОВЫЙ ИИ B", L"НОВЫЙ ИИ C" };
    if (id <= 555028) w = ru ? add_ru[v] : add_en[v]; else w = ru ? pl_ru[v] : pl_en[v];
    p_assign(ls, NULL, w);
    static int said; if (said++ < 6) logmsg("label %d -> new AI %c (game type %lX)", id, 'A' + v, game_type());
}
static LocString *__stdcall h_getv(LocString *ret, int id) { LocString *r = o_getv(ret, id); relabel(id, r); return r; }
static unsigned char __stdcall h_getb(int id, LocString *out) { unsigned char ok = o_getb(id, out); if (ok) relabel(id, out); return ok; }
static LocString *__stdcall h_conv(LocString *ret, const wchar_t *s) {
    LocString *r = o_conv(ret, s);
    if (s && s[0] == L'$') relabel(_wtoi(s + 1), r);
    return r;
}
static int patch_iat(const char *dllname, void *real, void *hook) {
    HMODULE mods[512]; DWORD need = 0, n = 0;
    typedef BOOL (WINAPI *Enum_t)(HANDLE, HMODULE *, DWORD, LPDWORD);
    Enum_t en = (Enum_t)GetProcAddress(LoadLibraryA("psapi.dll"), "EnumProcessModules");
    if (!en || !en(GetCurrentProcess(), mods, sizeof mods, &need)) return 0;
    for (DWORD m = 0; m < need / sizeof(HMODULE) && m < 512; m++) {
        unsigned char *b = (unsigned char *)mods[m]; IMAGE_DOS_HEADER *d = (IMAGE_DOS_HEADER *)b;
        if (IsBadReadPtr(d, sizeof *d) || d->e_magic != IMAGE_DOS_SIGNATURE) continue;
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + d->e_lfanew);
        IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress) continue;
        for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(b + dir.VirtualAddress); imp->Name; imp++) {
            if (lstrcmpiA((const char *)(b + imp->Name), dllname)) continue;
            for (IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(b + imp->FirstThunk); t->u1.Function; t++)
                if ((void *)t->u1.Function == real) {
                    DWORD o; VirtualProtect(&t->u1.Function, 4, PAGE_READWRITE, &o); t->u1.Function = (DWORD)hook; VirtualProtect(&t->u1.Function, 4, o, &o); n++;
                }
        }
    }
    return (int)n;
}
static void install_labels(void) {
    HMODULE lz = GetModuleHandleW(L"Localizer.dll");
    if (!lz) { logmsg("Localizer.dll not loaded - no new AI label"); return; }
    o_getv = (GetStrV)GetProcAddress(lz, "?GetString@Localizer@@SG?AVLocString@@H@Z");
    o_getb = (GetStrB)GetProcAddress(lz, "?GetString@Localizer@@SG_NHAAVLocString@@@Z");
    o_conv = (ConvId)GetProcAddress(lz, "?ConvertLocIDToString@Localizer@@SG?AVLocString@@PB_W@Z");
    p_assign = (LsAssign)GetProcAddress(lz, "??4LocString@@QAEAAV0@PB_W@Z");
    p_cstr = (LsCstr)GetProcAddress(lz, "?c_str@LocString@@QBEPB_WXZ");
    if (!o_getv || !o_getb || !o_conv || !p_assign || !p_cstr) { logmsg("Localizer.dll exports missing - no new AI label"); return; }
    int a = patch_iat("Localizer.dll", (void *)o_getv, (void *)h_getv), b = patch_iat("Localizer.dll", (void *)o_getb, (void *)h_getb),
        c = patch_iat("Localizer.dll", (void *)o_conv, (void *)h_conv);
    logmsg("new AI label: GetString %d+%d, ConvertLocIDToString %d import(s)", a, b, c);
}

// ---------------- 0.11 (11.11.2026): the replay of a match with the new AI to the replay site ----------------
// MMFind sends the replay of every match with other people; a skirmish against the computer it leaves alone. Testers' matches
// against the new AI are what we need to see, so EliteAI sends those itself: back in the menu after a single-player match in
// which a new AI played, Documents\My Games\Dawn of War II - Retribution\Playback\temp.rec goes to <mirror>/replay/api/upload.
// - the new AI is known from its own log lines (main.ai InitializeAI: "p1002 init race orks ..." / "p1002 variant C ..."),
//   which come here only in a single-player game (load_hook gives the new AI nothing online or in a replay);
// - in a game / in the menus: [*(0x01335A78) + 0x114] (0 = menus, as SteamKeep); a replay (type 0x800) seen while in that game
//   (ReplayTakeover turns a replay into a skirmish) = not sent;
// - the mirrors are checked first, all at once, about 2.5 s at most (GET /replay/api/by_lobby/<x>: one indexed lookup, "{}");
//   the upload goes to the first one that answered, the next one that answered when it fails; all in a thread of its own,
//   nothing waits for it; no later than 4 minutes after the match;
// - the game closed during the match or the upload (Quit from the score screen, a crash): Elite_DLLs\EliteAI.replay keeps the
//   match, the next start of the game sends it (temp.rec still newer than the match and not older than 6 hours);
// - temp.rec not found or older than the match: the log gets the Documents path the game itself gets (SHGetFolderPathW), the
//   Playback folder, the newest .rec there and %USERPROFILE%\Documents / OneDrive\Documents when they differ.
// EliteAI.ini [Replays] Upload=0 turns it off; Server=host;host;... (default the replay site's mirrors, as MMFind).
#define RP_HOSTS   L"rts.fintebtc.de;dow2.fintebtc.de;dow2ru.fintebtc.de"
#define RP_REL     L"\\My Games\\Dawn of War II - Retribution\\Playback"
#define RP_CHECK_MS 2500
#define RP_BUDGET_MS (4 * 60 * 1000)
typedef struct { int pid; char race[24]; char var; } RpBot;
static RpBot g_rpBot[8]; static int g_rpN;                  // this match: the AI players seen (race from "init", variant = new AI)
static volatile int g_rpNew;                                // a new AI played in this match
static volatile int g_rpMp;                                 // ... and it is a network match (0.16): "mp_" in the file name
static ULONGLONG g_rpStart;                                 // FILETIME (UTC) of the match start
static volatile int g_rpPlayback;                           // a replay was on while in this game

static ULONGLONG ft_now(void) { FILETIME f; GetSystemTimeAsFileTime(&f); return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }
static ULONGLONG ft_u(const FILETIME *f) { return ((ULONGLONG)f->dwHighDateTime << 32) | f->dwLowDateTime; }
static void ft_str(ULONGLONG u, char *o, int cap) {        // local "2026-11-11 19:02:33"
    FILETIME f = { (DWORD)u, (DWORD)(u >> 32) }, l; SYSTEMTIME t;
    if (!u || !FileTimeToLocalFileTime(&f, &l) || !FileTimeToSystemTime(&l, &t)) { snprintf(o, cap, "-"); return; }
    snprintf(o, cap, "%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}
static const char *u8(const wchar_t *w, char *o, int cap) {   // paths in the log as UTF-8 (a Cyrillic user name)
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, o, cap, NULL, NULL)) snprintf(o, cap, "?"); return o;
}
static void rp_who(char *o, int cap) {                     // "orks-C_eldar-A": the new AI players of the match
    int n = 0; o[0] = 0;
    if (g_rpMp) n = snprintf(o, cap, "mp_");
    EnterCriticalSection(&g_lk);
    for (int i = 0, k = 0; i < g_rpN && k < 4; i++) {
        if (!g_rpBot[i].var) continue;
        n += snprintf(o + n, cap - n, "%s%s-%c", k++ ? "_" : "", g_rpBot[i].race[0] ? g_rpBot[i].race : "race", g_rpBot[i].var);
        if (n >= cap - 1) break;
    }
    LeaveCriticalSection(&g_lk);
    if (!o[0]) snprintf(o, cap, "match");
}
static void rp_marker_path(wchar_t *p) { _snwprintf(p, MAX_PATH, L"%lsEliteAI.replay", g_dir); }
static void rp_marker_write(void) {                        // the match is kept until its replay is dealt with
    char who[160], b[256]; rp_who(who, sizeof who); wchar_t p[MAX_PATH]; rp_marker_path(p);
    int n = snprintf(b, sizeof b, "start %llu\r\nwho %s\r\n", g_rpStart, who);
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, b, n, &w, NULL); CloseHandle(f); }
}
static int rp_marker_read(ULONGLONG *start, char *who, int cap) {
    wchar_t p[MAX_PATH]; rp_marker_path(p); size_t len = 0; char *b = read_file(p, &len);
    if (!b) return 0;
    *start = 0; who[0] = 0; char *s = strstr(b, "start "), *w = strstr(b, "who ");
    if (s) *start = _strtoui64(s + 6, NULL, 10);
    if (w) { w += 4; int i = 0; while (w[i] && w[i] != '\r' && w[i] != '\n' && i < cap - 1) { who[i] = w[i]; i++; } who[i] = 0; }
    free(b); return *start != 0;
}
static void rp_marker_done(ULONGLONG start) {              // only the marker of this match (a newer match may have written its own)
    ULONGLONG s = 0; char who[160];
    if (rp_marker_read(&s, who, sizeof who) && s == start) { wchar_t p[MAX_PATH]; rp_marker_path(p); DeleteFileW(p); }
}
// lua_log: "p1002 init race orks difficulty 1 team 1 logic new eliteai 0.19", "p1002 variant C rules C real_diff 2"
static void replay_note(const char *line) {
    int pid = 0; char race[24] = "", v = 0, w[16] = "";
    // 0.16: a network match too. These lines are written by the new AI's scripts, and the engine runs them only on the station
    // that computes the computer - so only that station sends, once, after the match (MMFind's own upload of a match with a
    // computer is not stored by the site; ours, X-Lobby newai, is kept hidden for the AI's development). The file name says mp.
    if (game_type() & MP_TYPES) g_rpMp = 1;
    // 0.17 (the network check of 15.11, MP_CHECK_1115_RU.md bug 1): a computer of the BRAIN (orks since newai-24) never wrote
    // 'variant' - only the old layer does (default.ai / main.ai under EliteAI.on, and the dispatcher sets EliteAI.on = false
    // for a brain slot), so no match of the brain was sent since newai-24, single player or network. The brain's own first
    // line is "p1001 BR INIT 5 brain 0.9 race orks profile 1 team 1 diff 2 units ..." (mod/brain/90_boot.ai): it is written
    // only by the station whose engine runs that AI player's scripts, like 'variant', and it counts the same - race from it,
    // the variant letter from its diff (0 / 1 / 2 -> A / B / C: the rules it plays; any other -> 'X').
    if (line[0] != 'p' || (!strstr(line, " init race ") && !strstr(line, " variant ") && !strstr(line, " BR INIT "))) return;   // every AI log line comes here
    if (sscanf(line, "p%d init race %23s", &pid, race) == 2) ;
    else if (sscanf(line, "p%d variant %15s", &pid, w) == 2 && w[0] >= 'A' && w[0] <= 'Z' && !w[1]) v = w[0];
    else if (sscanf(line, "p%d BR INIT %*d brain %*s race %23s", &pid, race) == 2) {
        const char *d = strstr(line, " diff "); int dv = -1;
        if (d) sscanf(d + 6, "%d", &dv);
        v = dv >= 0 && dv <= 2 ? (char)('A' + dv) : 'X';
    }
    else return;
    int first = 0;
    EnterCriticalSection(&g_lk);
    int i = 0; while (i < g_rpN && g_rpBot[i].pid != pid) i++;
    if (i == g_rpN && g_rpN < 8) { memset(&g_rpBot[i], 0, sizeof g_rpBot[i]); g_rpBot[i].pid = pid; g_rpN++; }
    if (i < g_rpN) {
        if (race[0]) { for (char *c = race; *c; c++) if (!isalnum((unsigned char)*c) && *c != '_') *c = '_'; strcpy(g_rpBot[i].race, race); }
        if (v) { g_rpBot[i].var = v; if (!g_rpNew) { first = 1; g_rpNew = 1; if (!g_rpStart) g_rpStart = ft_now() - 300000000ULL; } }   // AI init comes after the recording began: 30 s of slack
    }
    LeaveCriticalSection(&g_lk);
    if (v) rp_marker_write();
    if (first) logmsg("replay: a new AI plays this match - its replay goes to the replay site after it");
}

// Documents as the game gets it, Playback, temp.rec (diagnostics: Malum's temp.rec at the usual place was months old)
static int rp_rec_info(const wchar_t *docs, wchar_t *rec, ULONGLONG *mt, DWORD *size, int say) {
    wchar_t dir[MAX_PATH]; _snwprintf(dir, MAX_PATH, L"%ls" RP_REL, docs); _snwprintf(rec, MAX_PATH, L"%ls\\temp.rec", dir);
    DWORD da = GetFileAttributesW(dir); int pb = da != INVALID_FILE_ATTRIBUTES && (da & FILE_ATTRIBUTE_DIRECTORY);
    WIN32_FILE_ATTRIBUTE_DATA fa; int have = GetFileAttributesExW(rec, GetFileExInfoStandard, &fa);
    *mt = have ? ft_u(&fa.ftLastWriteTime) : 0; *size = have ? fa.nFileSizeLow : 0;
    if (say) {
        char t[32]; ft_str(*mt, t, sizeof t);
        char nb[200] = "-"; ULONGLONG nt = 0; int cnt = 0;           // the newest .rec of the folder (another name?)
        if (pb) { wchar_t q[MAX_PATH]; _snwprintf(q, MAX_PATH, L"%ls\\*.rec", dir); WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(q, &fd);
            if (h != INVALID_HANDLE_VALUE) { do { cnt++; ULONGLONG u = ft_u(&fd.ftLastWriteTime);
                    if (u > nt) { nt = u; char tt[32]; ft_str(u, tt, sizeof tt); char fn[120]; snprintf(nb, sizeof nb, "%s (%lu bytes, %s)", u8(fd.cFileName, fn, sizeof fn), fd.nFileSizeLow, tt); }
                } while (FindNextFileW(h, &fd)); FindClose(h); } }
        char d8[MAX_PATH * 3];
        logmsg("replay: Documents %s | Playback %s | temp.rec %s%lu bytes, written %s | .rec files %d, newest %s",
               u8(docs, d8, sizeof d8), pb ? "found" : "NOT found", have ? "" : "NOT found ", *size, t, cnt, nb);
    }
    return have;
}
static int rp_docs(wchar_t *docs) {                         // the path the game itself gets for its My Games folder
    HRESULT r = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, docs);
    if (r != S_OK) { logmsg("replay: SHGetFolderPathW(CSIDL_PERSONAL) failed (%08lX)", (unsigned long)r); return 0; }
    return 1;
}
// temp.rec newer than the match; not there: say where the game looks and what else is around
static int rp_find(ULONGLONG start, ULONGLONG notAfter, wchar_t *rec, DWORD *size) {
    wchar_t docs[MAX_PATH] = L""; ULONGLONG mt = 0; int ok = 0;
    if (rp_docs(docs) && rp_rec_info(docs, rec, &mt, size, 0) && mt >= start && (!notAfter || mt <= notAfter)) ok = 1;
    if (ok) return 1;
    char a[32], b[32]; ft_str(start, a, sizeof a); ft_str(mt, b, sizeof b);
    logmsg("replay: no temp.rec of this match (match from %s, temp.rec %s) - where the game writes:", a, mt ? b : "missing");
    if (docs[0]) rp_rec_info(docs, rec, &mt, size, 1);
    wchar_t up[MAX_PATH], alt[MAX_PATH]; DWORD n = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
    if (n && n < MAX_PATH - 40) {
        static const wchar_t *sub[2] = { L"\\Documents", L"\\OneDrive\\Documents" };
        for (int i = 0; i < 2; i++) {
            _snwprintf(alt, MAX_PATH, L"%ls%ls", up, sub[i]);
            if (!_wcsicmp(alt, docs) || GetFileAttributesW(alt) == INVALID_FILE_ATTRIBUTES) continue;
            wchar_t r2[MAX_PATH]; DWORD s2 = 0; ULONGLONG m2 = 0;
            if (rp_rec_info(alt, r2, &m2, &s2, 1) && m2 >= start && (!notAfter || m2 <= notAfter)) {
                char a8[MAX_PATH * 3]; logmsg("replay: the match's temp.rec is under %s - sending that one", u8(alt, a8, sizeof a8)); wcscpy(rec, r2); *size = s2; return 1; }
        }
    }
    return 0;
}

typedef struct { wchar_t host[64]; volatile LONG refs; volatile int ok; volatile DWORD code, ms; } RpProbe;
static void rp_unref(RpProbe *p) { if (!InterlockedDecrement(&p->refs)) free(p); }
static DWORD WINAPI rp_probe(LPVOID a) {                    // GET /replay/api/by_lobby/<x>: the site answers "{}" (404) at once
    RpProbe *p = a; DWORD t0 = GetTickCount(), code = 0, sz = sizeof code, m; char out[64]; int n = 0;
    HINTERNET s = WinHttpOpen(L"EliteAI/0.19", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (s) {
        WinHttpSetTimeouts(s, RP_CHECK_MS, RP_CHECK_MS, RP_CHECK_MS, RP_CHECK_MS);
        HINTERNET c = WinHttpConnect(s, p->host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/replay/api/by_lobby/eliteai-check", NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
        if (r && WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
            while (n < (int)sizeof out - 1 && WinHttpReadData(r, out + n, sizeof out - 1 - n, &m) && m) n += m;
        }
        out[n] = 0;
        if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    }
    p->code = code; p->ms = GetTickCount() - t0; p->ok = (code == 200 || code == 404) && n > 0 && out[0] == '{';
    rp_unref(p); return 0;
}
static DWORD rp_post(const wchar_t *host, const char *buf, DWORD size, const char *fname, char *out, int cap, DWORD tmoRecv) {
    wchar_t hdr[400]; _snwprintf(hdr, 400, L"Content-Type: application/octet-stream\r\nX-Filename: %hs\r\nX-Lobby: newai\r\n", fname);
    DWORD code = 0, sz = sizeof code, m; int n = 0; out[0] = 0;
    HINTERNET s = WinHttpOpen(L"EliteAI/0.19", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0); if (!s) return 0;
    WinHttpSetTimeouts(s, 4000, 4000, 30000, (int)tmoRecv);       // the site parses the replay before it answers
    HINTERNET c = WinHttpConnect(s, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", L"/replay/api/upload", NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    if (r && WinHttpSendRequest(r, hdr, (DWORD)-1, (LPVOID)buf, size, size, 0) && WinHttpReceiveResponse(r, NULL)) {
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
        while (n < cap - 1 && WinHttpReadData(r, out + n, cap - 1 - n, &m) && m) n += m;
    }
    out[n] = 0;
    if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
    return code;
}
typedef struct { ULONGLONG start; char who[160]; int old; } RpJob;   // old: a match of an earlier run of the game (EliteAI.replay)
static DWORD WINAPI rp_upload(LPVOID a) {
    RpJob *j = a;
    Sleep(j->old ? 20000 : 3000);                                // the game finishes writing the file / is up after its start
    DWORD t0 = GetTickCount();
    wchar_t rec[MAX_PATH]; DWORD size = 0; char *buf = NULL;
    ULONGLONG notAfter = j->old ? j->start + 6ULL * 3600 * 10000000ULL : 0;
    if (j->old && game_type() && *(DWORD *)(*(DWORD *)G_GAME + 0x114)) { logmsg("replay: the earlier match not sent - a game is already on"); goto done; }
    if (!rp_find(j->start, notAfter, rec, &size)) goto done;
    for (int i = 0; i < 10; i++) {                               // still being written: wait until the size holds
        Sleep(1000); WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(rec, GetFileExInfoStandard, &fa) || fa.nFileSizeLow == size) break;
        size = fa.nFileSizeLow;
    }
    if (size < 64 || size > 30u << 20) { logmsg("replay: odd size %lu - not sent", size); goto done; }
    if (!(buf = malloc(size))) goto done;
    DWORD got = 0; HANDLE f = CreateFileW(rec, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { ReadFile(f, buf, size, &got, NULL); CloseHandle(f); }
    if (got != size || memcmp(buf + 12, "DOW2_REC", 8)) { char r8[MAX_PATH * 3]; logmsg("replay: could not read %s (%lu of %lu bytes)", u8(rec, r8, sizeof r8), got, size); goto done; }
    { char t[32]; WIN32_FILE_ATTRIBUTE_DATA fa; ULONGLONG mt = GetFileAttributesExW(rec, GetFileExInfoStandard, &fa) ? ft_u(&fa.ftLastWriteTime) : 0;
      char r8[MAX_PATH * 3]; ft_str(mt, t, sizeof t); logmsg("replay: %s, %lu bytes, written %s", u8(rec, r8, sizeof r8), size, t); }

    // the mirrors, all at once: who answers within RP_CHECK_MS
    wchar_t list[256]; GetPrivateProfileStringW(L"Replays", L"Server", RP_HOSTS, list, 256, g_ini);
    RpProbe *pr[6]; HANDLE th[6]; int np = 0;
    for (wchar_t *t = wcstok(list, L";, "); t && np < 6; t = wcstok(NULL, L";, ")) {
        RpProbe *p = calloc(1, sizeof *p); if (!p) break; wcsncpy(p->host, t, 63); p->refs = 2;
        HANDLE h = CreateThread(NULL, 0, rp_probe, p, 0, NULL);
        if (!h) { free(p); continue; }
        pr[np] = p; th[np++] = h;
    }
    DWORD c0 = GetTickCount();
    if (np) WaitForMultipleObjects(np, th, TRUE, RP_CHECK_MS + 500);
    DWORD cms = GetTickCount() - c0; char cl[400]; int cn = 0, alive = 0; int ok[6];
    for (int i = 0; i < np; i++) {
        ok[i] = pr[i]->ok; alive += ok[i]; CloseHandle(th[i]);
        cn += snprintf(cl + cn, sizeof cl - cn, "%s%ls %s", i ? ", " : "", pr[i]->host, ok[i] ? "ok" : "-");
        if (ok[i] && cn < (int)sizeof cl - 16) cn += snprintf(cl + cn, sizeof cl - cn, " %lu ms", pr[i]->ms);
        else if (pr[i]->ms && pr[i]->code && cn < (int)sizeof cl - 16) cn += snprintf(cl + cn, sizeof cl - cn, " (HTTP %lu)", pr[i]->code);
    }
    logmsg("replay: mirror check %lu ms: %s", cms, np ? cl : "no mirrors");
    if (!alive) { logmsg("replay: no mirror answered - not sent"); for (int i = 0; i < np; i++) rp_unref(pr[i]); goto done; }

    SYSTEMTIME st; GetLocalTime(&st); char fname[220];
    snprintf(fname, sizeof fname, "newai_%s_%04d-%02d-%02d_%02d-%02d.rec", j->who, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    int sent = 0, tried = 0;
    for (int i = 0; i < np && !sent; i++) {
        if (!ok[i]) continue;
        DWORD used = GetTickCount() - t0;
        if (used > RP_BUDGET_MS - 10000) { logmsg("replay: out of time (%lu s) - not sent", used / 1000); break; }
        char out[512]; DWORD u0 = GetTickCount();
        logmsg("replay: sending to %ls (%s mirror that answered the check)", pr[i]->host, tried++ ? "the next" : "the first");
        DWORD code = rp_post(pr[i]->host, buf, size, fname, out, sizeof out, RP_BUDGET_MS - used);
        logmsg("replay: %s, %lu bytes -> %ls: HTTP %lu in %lu ms %s", fname, size, pr[i]->host, code, GetTickCount() - u0, out);
        if (code >= 200 && code < 300) sent = 1;
        else if (code == 400 || code == 429) break;              // not a replay / the site's hourly limit: another mirror won't help
    }
    if (!sent) logmsg("replay: not sent");
    for (int i = 0; i < np; i++) rp_unref(pr[i]);
done:
    rp_marker_done(j->start); free(buf); free(j); return 0;
}
// -dev runs (our stands' AI series): only with [Replays] Upload=2, so test matches do not fill the site
static int rp_dev(void) { const wchar_t *c = GetCommandLineW(); return c && (wcsstr(c, L" -dev ") || (wcslen(c) > 5 && !wcscmp(c + wcslen(c) - 5, L" -dev"))); }
static int rp_upload_on(void) { int u = GetPrivateProfileIntW(L"Replays", L"Upload", 1, g_ini); return u == 2 || (u == 1 && !rp_dev()); }
static void rp_send(ULONGLONG start, const char *who, int old) {
    RpJob *j = calloc(1, sizeof *j); if (!j) return;
    j->start = start; j->old = old; snprintf(j->who, sizeof j->who, "%s", who);
    HANDLE h = CreateThread(NULL, 0, rp_upload, j, 0, NULL); if (h) CloseHandle(h); else free(j);
}
static void rp_match_over(void) {                           // back in the menu
    char who[160]; rp_who(who, sizeof who);
    ULONGLONG start = g_rpStart; int nw = g_rpNew, pb = g_rpPlayback;
    EnterCriticalSection(&g_lk); g_rpN = 0; g_rpNew = 0; g_rpMp = 0; g_rpStart = 0; g_rpPlayback = 0; LeaveCriticalSection(&g_lk);
    if (!nw) return;
    if (pb) { logmsg("replay: a replay taken over - not sent"); rp_marker_done(start); return; }
    if (!rp_upload_on()) { logmsg("replay: upload off (%s) - not sent", rp_dev() ? "-dev run: only with [Replays] Upload=2" : "[Replays] Upload=0"); rp_marker_done(start); return; }
    rp_send(start, who, 0);
}
static DWORD WINAPI rp_watch(LPVOID _) {                    // once a second: in a game or in the menus
    Sleep(2000);
    wchar_t docs[MAX_PATH], rec[MAX_PATH]; ULONGLONG mt; DWORD sz;
    int up = rp_upload_on();
    if (rp_docs(docs)) rp_rec_info(docs, rec, &mt, &sz, 1);
    logmsg("replay: upload after a match with the new AI %s", up ? "on" : rp_dev() ? "off (-dev run: only with [Replays] Upload=2)" : "off ([Replays] Upload=0)");
    ULONGLONG s0; char who[160];
    if (rp_marker_read(&s0, who, sizeof who)) {
        char t[32]; ft_str(s0, t, sizeof t);
        if (up) { logmsg("replay: the match of %s (%s) was not sent before the game closed - sending it now", t, who); rp_send(s0, who, 1); }
        else { wchar_t p[MAX_PATH]; rp_marker_path(p); DeleteFileW(p); }
    }
    int was = 0;
    for (;;) {
        Sleep(1000);
        DWORD *g = (DWORD *)G_GAME; int in = 0; DWORD t = 0;
        if (!IsBadReadPtr(g, 4) && *g && !IsBadReadPtr((void *)(*g + 0x114), 8)) { in = *(DWORD *)(*g + 0x114) != 0; t = *(DWORD *)(*g + 0x118); }
        if (in && t == T_PLAYBACK) g_rpPlayback = 1;
        if (in && !was) {                                        // set at the loading screen, before the AI scripts load (stand 11.11)
            EnterCriticalSection(&g_lk); g_rpN = 0; g_rpNew = 0; g_rpMp = 0; g_rpPlayback = t == T_PLAYBACK; g_rpStart = ft_now() - 300000000ULL; LeaveCriticalSection(&g_lk);
            if (g_verbose || g_rpNew) logmsg("replay: in a game (type %lX)", t);
        }
        if (!in && was) { if (g_verbose || g_rpNew) logmsg("replay: back in the menu"); rp_match_over(); }
        was = in;
    }
    return 0;
}

static const unsigned char LOAD_PRO[6] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x14};   // push ebp; mov ebp,esp; sub esp,14h
static int install(void) {
    HMODULE lc = GetModuleHandleW(L"LuaConfig.dll");
    unsigned char *t = lc ? (unsigned char *)GetProcAddress(lc, "_lua_load@16") : NULL;
    if (!t) { logmsg("LuaConfig.dll / lua_load not found"); return 0; }
    p_pushcclosure = (void *)GetProcAddress(lc, "_lua_pushcclosure@12"); p_setfield = (void *)GetProcAddress(lc, "_lua_setfield@12");
    p_tolstring = (void *)GetProcAddress(lc, "_lua_tolstring@12"); p_gettop = (void *)GetProcAddress(lc, "_lua_gettop@4");
    p_pushstring = (void *)GetProcAddress(lc, "_lua_pushstring@8"); p_pushnil = (void *)GetProcAddress(lc, "_lua_pushnil@4");
    p_getfield = (void *)GetProcAddress(lc, "_lua_getfield@12"); p_settop = (void *)GetProcAddress(lc, "_lua_settop@8");
    p_tonumber = (void *)GetProcAddress(lc, "_lua_tonumber@8");
    p_pushinteger = (void *)GetProcAddress(lc, "_lua_pushinteger@8"); p_tointeger = (void *)GetProcAddress(lc, "_lua_tointeger@8");
    if (!p_pushcclosure || !p_setfield || !p_tolstring || !p_gettop || !p_pushstring || !p_pushnil) { p_pushcclosure = NULL; logmsg("no Lua API for eliteai_log"); }
    if (memcmp(t, LOAD_PRO, 6)) { logmsg("lua_load prologue differs (%02X %02X %02X ...) - not hooking", t[0], t[1], t[2]); return 0; }
    unsigned char *tr = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(tr, t, 6); tr[6] = 0xE9; *(DWORD *)(tr + 7) = (DWORD)(t + 6) - (DWORD)(tr + 11);
    g_loadOrig = (LuaLoadFn)tr;
    DWORD o; VirtualProtect(t, 6, PAGE_EXECUTE_READWRITE, &o);
    t[0] = 0xE9; *(DWORD *)(t + 1) = (DWORD)load_hook - (DWORD)(t + 5); t[5] = 0x90;
    VirtualProtect(t, 6, o, &o); FlushInstructionCache(GetCurrentProcess(), t, 6);
    return 1;
}

// the move loop guard's fires into EliteAI.log, off the game's thread
static DWORD WINAPI mlg_watch(LPVOID _) { (void)_; LONG done = 0; for (;;) { Sleep(1000); mlg_poll(&done, logmsg); } return 0; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_lk);
    wchar_t mx[64]; _snwprintf(mx, 64, L"Local\\EliteAI_%lu", GetCurrentProcessId());   // one copy per game (Elite's injector + dow2-patches)
    CreateMutexW(NULL, FALSE, mx); if (GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
    g_active = 1;                                // (0.17: EliteAI_Mark of the idle copy answers 0)
    // data always in <game>\Elite_DLLs\ - also when loaded from dow2-patches (-dev runs: Elite's injector skips its DLLs there)
    GetModuleFileNameW(NULL, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) wcscpy(s + 1, L"Elite_DLLs\\");
    _snwprintf(g_ini, MAX_PATH, L"%lsEliteAI.ini", g_dir);
    g_verbose = GetPrivateProfileIntW(L"EliteAI", L"Verbose", 0, g_ini);
    logmsg("EliteAI 0.19 start (single player, and network matches for the computers this station computes - the lobby labels too; a log per match of the new AI: EliteAI.log, the match before in EliteAI.prev.log; the AI's lines around each mark of Marks.dll: EliteAI.marks.log), hook %s", install() ? "installed" : "NOT installed");
    install_labels();
    int mg = mlg_install();
    logmsg("move loop guard (DOW2.exe 009141A0 / 009144F3): %s", mg == 1 ? "installed" : mg == 0 ? "MoveLoopFix.dll is active, not needed"
           : mg == -1 ? "NOT installed (bytes differ: not DOW2.exe 3.19.10320 or already changed)" : "NOT installed (VirtualProtect failed)");
    if (mg == 1) { HANDLE g = CreateThread(NULL, 0, mlg_watch, NULL, 0, NULL); if (g) CloseHandle(g); }
    HANDLE w = CreateThread(NULL, 0, rp_watch, NULL, 0, NULL); if (w) CloseHandle(w);   // 0.11: the replay after a match with the new AI
    return TRUE;
}
