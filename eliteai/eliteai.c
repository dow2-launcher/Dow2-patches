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
#include "moveloop_guard.h"     // 0.12 (14.11.2026): the engine's melee freeze (= MoveLoopFix 1; fastbench_1113 HANG_12da6e_RU.md, brain step 7 hangs)

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

static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsEliteAI.log", g_dir);
    EnterCriticalSection(&g_lk);
    FILE *f = _wfopen(p, L"a");
    if (f) {
        SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
    }
    LeaveCriticalSection(&g_lk);
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
static int __stdcall lua_cfg(void *L) {          // eliteai_cfg("Key") -> string from EliteAI.ini [Lua], or nil (quick tuning, A/B tests)
    const char *k = p_tolstring(L, 1, NULL); char v[256] = "";
    char ini[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, g_ini, -1, ini, sizeof ini, NULL, NULL);
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
static int __stdcall lua_take(void *L) {           // eliteai_take_orders() -> "id x y z mode;..." (empty string = none)
    static char buf[1024 * 64]; int n = 0; buf[0] = 0;
    EnterCriticalSection(&g_lk);
    for (int i = 0; i < g_nOrders && n < (int)sizeof buf - 128; i++)
        n += snprintf(buf + n, sizeof buf - n, "%.0f %.2f %.2f %.2f %.0f;", g_orders[i].id, g_orders[i].x, g_orders[i].y, g_orders[i].z, g_orders[i].mode);
    g_nOrders = 0; LeaveCriticalSection(&g_lk);
    p_pushstring(L, buf); return 1;
}
static void give_log(void *L) {
    if (!p_pushcclosure || !p_setfield) return;
    p_pushcclosure(L, lua_log, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_log");
    p_pushcclosure(L, lua_cfg, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_cfg");
    if (p_tonumber) {
        p_pushcclosure(L, lua_order, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_order");
        p_pushcclosure(L, lua_take, 0); p_setfield(L, LUA_GLOBALSINDEX, "eliteai_take_orders");
    }
    if (p_getfield && p_settop) {                 // AI scripts run in their own environment: also via the shared string table
        p_getfield(L, LUA_GLOBALSINDEX, "string");
        p_pushcclosure(L, lua_log, 0); p_setfield(L, -2, "eliteai_log");
        p_pushcclosure(L, lua_cfg, 0); p_setfield(L, -2, "eliteai_cfg");
        if (p_tonumber) { p_pushcclosure(L, lua_order, 0); p_setfield(L, -2, "eliteai_order");
                          p_pushcclosure(L, lua_take, 0); p_setfield(L, -2, "eliteai_take_orders"); }
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
// The new AI only in single-player games. The AI and SCAR scripts run on every machine of a match, so one player with it
// would desync an online game for everyone; a replay must replay with the scripts it was recorded with. The game type
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
    if (!single_player() && !(rt = replay_takeover_on())) {   // online, replay, menus: Elite's own scripts, untouched
        static DWORD said, lastType = 1;
        DWORD t = game_type();
        if (t && (t != lastType || GetTickCount() - said > 60000)) { said = GetTickCount(); lastType = t; logmsg("game type %lX (not single player): the new AI stays out", t); }
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
            BufState st = { buf, len, 0 }; give_log(L);
            int r = g_loadOrig(L, buf_reader, &st, chunkname);
            logmsg("ai script %s -> ours (%u bytes), load %d", chunkname, (unsigned)len, r);
            free(buf); return r;
        }
        if (!strcmp(rel, "simulation\\ai\\default.ai")) {           // Elite's default.ai + our module at its end
            _snwprintf(p, MAX_PATH, L"%lsEliteAI\\main.ai", g_dir);
            size_t mlen = 0; char *mod = read_file(p, &mlen);
            if (mod) {                                               // mechanics.ai (10.11): right after main.ai, same chunk
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
    if (!single_player()) return;                    // online lobbies: Elite's AI - no "new AI" label
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
    if (line[0] != 'p' || (!strstr(line, " init race ") && !strstr(line, " variant "))) return;   // every AI log line comes here
    if (sscanf(line, "p%d init race %23s", &pid, race) == 2) ;
    else if (sscanf(line, "p%d variant %15s", &pid, w) == 2 && w[0] >= 'A' && w[0] <= 'Z' && !w[1]) v = w[0];
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
    HINTERNET s = WinHttpOpen(L"EliteAI/0.12", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
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
    HINTERNET s = WinHttpOpen(L"EliteAI/0.12", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0); if (!s) return 0;
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
    EnterCriticalSection(&g_lk); g_rpN = 0; g_rpNew = 0; g_rpStart = 0; g_rpPlayback = 0; LeaveCriticalSection(&g_lk);
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
            EnterCriticalSection(&g_lk); g_rpN = 0; g_rpNew = 0; g_rpPlayback = t == T_PLAYBACK; g_rpStart = ft_now() - 300000000ULL; LeaveCriticalSection(&g_lk);
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
    // data always in <game>\Elite_DLLs\ - also when loaded from dow2-patches (-dev runs: Elite's injector skips its DLLs there)
    GetModuleFileNameW(NULL, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) wcscpy(s + 1, L"Elite_DLLs\\");
    _snwprintf(g_ini, MAX_PATH, L"%lsEliteAI.ini", g_dir);
    g_verbose = GetPrivateProfileIntW(L"EliteAI", L"Verbose", 0, g_ini);
    logmsg("EliteAI 0.12 start (single-player games only), hook %s", install() ? "installed" : "NOT installed");
    install_labels();
    int mg = mlg_install();
    logmsg("move loop guard (DOW2.exe 009141A0 / 009144F3): %s", mg == 1 ? "installed" : mg == 0 ? "MoveLoopFix.dll is active, not needed"
           : mg == -1 ? "NOT installed (bytes differ: not DOW2.exe 3.19.10320 or already changed)" : "NOT installed (VirtualProtect failed)");
    if (mg == 1) { HANDLE g = CreateThread(NULL, 0, mlg_watch, NULL, 0, NULL); if (g) CloseHandle(g); }
    HANDLE w = CreateThread(NULL, 0, rp_watch, NULL, 0, NULL); if (w) CloseHandle(w);   // 0.11: the replay after a match with the new AI
    return TRUE;
}
