// ReplayTakeover.dll - continue a replay as a normal game from any moment, as any of its players (DoW2: Retribution 3.19,
// Elite; experiment).
//
// Owner 06.11.2026: "реплей запускаем и с любой минуты/тика можем его продолжить - за любого игрока".
// How the player uses it: while watching a replay, select any unit of a player (click it, or his hero in the player list
// at the bottom), open the menu (Menu / Esc) and press "Continue as <name>". The replay turns into an ordinary skirmish
// from that very tick: the same world, the selected player is yours, the others are played by the computer (or stand,
// ReplayTakeover.ini [ReplayTakeover] Others=idle).
//
// Why it works: a replay IS the live simulation of the match, fed with the recorded orders. DOW2.exe runs every game
// through a SimulationController (GameState+0x154) that takes each tick's orders from a SynchronizationInterface (+4):
// P2PNetworkSynchronization online, EchoSynchronization (vtable 0x01087A54) in a game against the computer - it gives back
// next tick exactly the local orders it got - and PlaybackSynchronization (0x010B9E2C), which reads them from the .rec.
// The takeover swaps the playback source for a new EchoSynchronization (ctor 0x404B74, the one 0x407947 makes for a
// skirmish) at a tick boundary, so from the next tick on the simulation runs on the orders of this machine, and makes the
// rest of the game see a skirmish with a local player:
//   - game type settings+0x118: 0x800 (playback) -> 4 (SP skirmish); the computer players think again (0x424496 skips
//     them only in a replay), Relic gets no stats (ReportMatchStats: "not a network game");
//   - the slots of GameInfo (settings+0x118+0x810, 0xA8 each; +0 type, +0x9C player id): the chosen one -> 0 (local),
//     the other players -> 1 (computer, local) with an AI made by the game's own Misc_AIControlLocalPlayer path (0x426875,
//     for the player in GameState+0x6C, which is set to each of them in turn), or 3 (human, remote: they stand);
//   - GameState+0x6C = the chosen player (the local player of GameObj, as the dev command switchplayer 0x426627 sets it);
//   - world+0x174 (the playback length the replay panels read) = 0, sim rate back to 10 ticks/s;
//   - the HUD is rebuilt (GameUI vtbl+0x68 / +0x64, the same calls switchplayer makes through 0x423E9B / 0x423B98):
//     a player's HUD - resources, build menus, abilities - for the new local player;
//   - the minimap: a new HUD gets the minimap texture only when MiniMapScene re-creates it, so the minimap manager's
//     "enabled" byte (*(0x013351A0)+0xAEC) is off for 3 frames and back on (otherwise the minimap stays black).
// The switch runs only between two ticks: with the threaded simulation (GameState+0x1AC) at the frame's call 0x4242AA
// when the sim job of the last tick is done, otherwise at the start of the frame (call 0x40E53C).
//
// The button: the pause menu (ui/movies/hud/menu_panel.gfx) has "Concede match" (BindingConcedeMatchButton, vtable
// 0x0110C16C), hidden outside online matches. In a replay its IsActive says yes, its caption becomes "Continue as
// <name>" and its click (the delegate 0xAD2EB0 set by its Init at 0xAD2E63) does the takeover; in every other game the
// button is the game's own, untouched. No overlay, no synthetic input, no files read or written during the game except
// this add-on's log.
//
// ReplayTakeover 2 (07.11.2026, players: "the computer walks single squads from point to point, boring"; owner: the computers
// put in for the others at the MAXIMUM difficulty): the computer of a former human took the difficulty of his Player
// (Player+0xBC; the AIPlayer's constructor 0x80E6A0 copies it to AIPlayer+0x900 = AIPlayer_GetDifficultyLevel, -1 -> Hard),
// which for a human of a replay is 0 = Easy - and Easy / Normal only go for capture points (Elite targets.ai). Now each of them
// gets Expert (AD_Hardest = 3, the game's highest: "Add Expert Computer", the same value the MP lobby's slot +0x2C holds) in
// the Player and in his GameInfo slot before his AI is made. ReplayTakeover.ini [ReplayTakeover] Difficulty=expert (default) |
// hard | normal | easy | keep (as before). Players who were computers in the match keep their own AI.
//
// ReplayTakeover 3 (10.11.2026, owner: test the new AI from any minute of a replay): Others = elite (old "ai") | newai_a |
// newai_b | newai_c | idle. The new AI (EliteAI.dll) picks its variant by the computer's difficulty (Easy = A, Normal = B,
// Hard = C, Expert = Elite's AI), so newai_* gives the former humans' computers that difficulty (Player+0xBC, slot +0x2C)
// instead of [Difficulty]. EliteAI.dll not loaded or EliteAI.ini [Lua] NewAI=none -> logged, Elite's AI (Difficulty) instead.
// EliteAI itself takes the game for a single-player one: the type is already 4 when the computers are made.
//
// ReplayTakeover 4 (15.11.2026, player noofancy, fb 1206 - he had reported both before): (1) after "Continue as" the player's
// squads had no squad tabs and no control-group numbers (Ctrl+N by hand worked) - a replay has no local player, so the skirmish
// SCAR never set them up; now the takeover runs the game's own Skirmish_SetupSquad for each of his squads (see setup_squads).
// (2) the next replay of the same run went out of sync from its first tick - the shutdown of the continued game skipped the
// end of the replay (its type was 4); now everything the takeover changed is put back at that shutdown (see restore).
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../common/gfxui.h"

#define VERSION "4"

#define G_SETTINGS   0x01335A78   // +0x110 GameObj, +0x118 game type (0x800 playback), +0x118+0x810 slots, +0x814 count
#define G_WORLD      0x013356A4   // +0xF8 entities (vtbl+0x10 = by id), +0x194 PlayerManager, +0x174 playback length, +0x178 tick
#define G_HUD        0x01335720   // +8: +0x20 selection, +0x68 sim rate (vtbl+0x28 set, +0x2C get)
#define G_UIMGR      0x013356A8   // +0x14 GameUI (vtbl+0x18 = unpause), +0x34 HUD (vtbl+0x34(1) after a rebuild)
#define G_MINIMAP    0x013351A0   // +0xAEC enabled
#define AT_FRAME     0x0040E53C   // call GameObj::Frame (stdcall GameObj, int, char, float)
#define FN_FRAME     0x004240F5
#define AT_SIMTHR    0x004242AA   // call 0x42DA47 (eax = threaded sim manager, +0x10 the sim job)
#define FN_SIMTHR    0x0042DA47
#define FN_AI_LOCAL  0x00426875   // stdcall(GameObj): an AI for the player in GameState+0x6C
#define FN_NEW       0x00DAA9B6   // operator new (cdecl)
#define FN_ECHO_CTOR 0x00404B74   // EchoSynchronization ctor, stdcall(this), 0x34 bytes
#define FN_SELECTION 0x00ADEE60   // thiscall(selection, vector<EntityID> *): the selected entities
#define VT_PLAYBACK  0x010B9E2C
#define VT_ECHO      0x01087A54   // EchoSynchronization (slot 0: scalar deleting destructor 0x404C85)
#define VT_CONCEDE   0x0110C16C   // BindingConcedeMatchButton
#define FN_CC_ACTIVE 0x00AD2D80   // slot 3: IsActive (thiscall, 1 arg)
#define FN_CC_CLICK  0x00AD2EB0   // the click delegate (thiscall widget, event)
#define AT_CC_CLICK  0x00AD2E63   // mov ecx, 0xAD2EB0 in its Init
#define FN_NOP4      0x009AB7D0   // slot 6 (Update): ret 4
#define T_PLAYBACK   0x800
#define T_SKIRMISH   4

typedef int (__stdcall *Frame_t)(void *, int, int, float);
typedef char (__stdcall *AiLocal_t)(void *);
typedef void *(__cdecl *New_t)(unsigned);
typedef void *(__stdcall *Ctor_t)(void *);
typedef void *(__fastcall *GetPl_t)(void *pm, void *edx, DWORD id);
typedef void *(__fastcall *Owner_t)(void *entity, void *edx);
typedef const wchar_t *(__fastcall *CStr_t)(void *ls, void *edx);
typedef char (__fastcall *IsDone_t)(void *job, void *edx);
typedef void (__fastcall *Fast1_t)(void *);
typedef void (__fastcall *SetF_t)(void *o, void *edx, float f);
typedef void (__fastcall *V1_t)(void *o, void *edx, int a);
typedef void *(__fastcall *ById_t)(void *em, void *edx, DWORD id);
typedef void (__fastcall *Sel_t)(void *sel, void *edx, void *vec);
typedef void (__stdcall *Dealloc_t)(void *, unsigned);
typedef char (__fastcall *Active_t)(void *self, void *edx, void *ctx);
typedef void (__fastcall *Click_t)(void *self, void *edx, void *widget, int ev);

static wchar_t g_log[MAX_PATH]; static CRITICAL_SECTION g_cs;
static void logmsg(const char *fmt, ...) {
    EnterCriticalSection(&g_cs);
    FILE *f = _wfopen(g_log, L"a");
    if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_cs);
}
static int ok(const void *p, int n) { return p && !IsBadReadPtr(p, n); }
#define U8(p, o)  (*(unsigned char *)((char *)(p) + (o)))
#define I32(p, o) (*(int *)((char *)(p) + (o)))
#define P32(p, o) (*(char **)((char *)(p) + (o)))

static GetPl_t g_getpl; static Owner_t g_owner; static CStr_t g_cstr; static IsDone_t g_isdone; static Dealloc_t g_dealloc;
static int g_others_ai = 1, g_ru;
static int g_newai = -1;                           // 3: Others=newai_a/b/c -> 0/1/2 (the EliteAI variant's difficulty), -1 = elite
static int g_diff = 3;                             // AD_Easy 0, AD_Standard 1, AD_Hard 2, AD_Hardest 3 ("Expert"); -1 = keep
static const char *DIFF_NAME[] = {"easy", "normal", "hard", "expert"};
#define PL_AIDIFF 0xBC                             // Player: the AI difficulty its AIPlayer is made with
#define SLOT_AIDIFF 0x2C                           // GameInfo slot: the lobby's AI difficulty (SetPlayerAIDifficulty 0x44EEEB)
static int g_testAt = -1, g_testSlot = -1;          // stands only (ReplayTakeover.ini TestTakeAt / TestSlot): take by itself

static char *settings(void) { return ok((void *)G_SETTINGS, 4) && ok(*(char **)G_SETTINGS, 0x1000) ? *(char **)G_SETTINGS : NULL; }
static char *world(void) { return ok((void *)G_WORLD, 4) && ok(*(char **)G_WORLD, 0x200) ? *(char **)G_WORLD : NULL; }
static char *gamestate(char *st) { char *go = st ? P32(st, 0x110) : NULL; return ok(go, 8) && ok(P32(go, 4), 0x200) ? P32(go, 4) : NULL; }
static wchar_t g_dirw[MAX_PATH];
// v3: the new AI is there - EliteAI.dll in this game and not switched off by EliteAI.ini [Lua] NewAI=none; NULL = yes
static const char *newai_missing(void) {
    if (!GetModuleHandleA("EliteAI.dll")) return "EliteAI.dll is not loaded (the New AI add-on is off)";
    wchar_t ini[MAX_PATH], v[32]; _snwprintf(ini, MAX_PATH, L"%lsEliteAI.ini", g_dirw); ini[MAX_PATH - 1] = 0;
    GetPrivateProfileStringW(L"Lua", L"NewAI", L"normal", v, 32, ini);
    if (!_wcsicmp(v, L"none")) return "EliteAI.ini [Lua] NewAI=none";
    if (_wcsicmp(v, L"normal") && _wcsicmp(v, L"all")) return "EliteAI.ini [Lua] NewAI is a test list of player ids";
    return NULL;
}
static int is_replay(void) { char *st = settings(); return st && I32(st, 0x118) == T_PLAYBACK; }
static void *player_by_id(DWORD id) { char *w = world(); return w && g_getpl ? g_getpl(w + 0x194, 0, id) : NULL; }
static DWORD player_id(void *p) { return ((DWORD (__fastcall *)(void *, void *))(*(void ***)p)[2])(p, 0); }
static const wchar_t *player_name(void *p) {
    const wchar_t *s = g_cstr && ok((char *)p + 0xC, 4) ? g_cstr((char *)p + 0xC, 0) : NULL;
    return s && ok(s, 2) ? s : L"?";
}
static const char *u8(const wchar_t *w) {           // for the log (main thread only)
    static char b[4][128]; static int k; k = (k + 1) & 3;
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, b[k], sizeof b[k], NULL, NULL)) strcpy(b[k], "?");
    return b[k];
}
static int slot_of(DWORD id) {                     // index of the GameInfo slot of this player id, -1 = none
    char *st = settings(); if (!st) return -1;
    char *arr = P32(st, 0x118 + 0x810); int n = I32(st, 0x118 + 0x814);
    for (int i = 0; i < n && i < 16; i++) if (ok(arr + i * 0xA8, 0xA8) && (DWORD)I32(arr + i * 0xA8, 0x9C) == id) return i;
    return -1;
}

// ---- the player of the selection: the owner of the first selected entity that has one (main thread) ----
static void *selected_player(void) {
    char *hud = ok((void *)G_HUD, 4) ? *(char **)G_HUD : NULL, *h = ok(hud, 12) ? P32(hud, 8) : NULL;
    char *sel = ok(h, 0x24) ? P32(h, 0x20) : NULL, *w = world();
    if (!ok(sel, 0x3A0) || !w || !g_owner) return NULL;
    DWORD *vec[3] = {0, 0, 0};                     // STLport vector<EntityID>: begin, end, end of storage
    ((Sel_t)FN_SELECTION)(sel, 0, vec);
    void *found = NULL, *em = P32(w, 0xF8);
    for (DWORD *p = vec[0]; p && p < vec[1] && !found && ok(em, 4); p++) {
        void *e = ((ById_t)(*(void ***)em)[0x10 / 4])(em, 0, *p);
        if (!ok(e, 0x54) || (I32(e, 0x50) & 0x18) != 0x18) continue;
        void *o = g_owner(e, 0);
        if (o && slot_of(player_id(o)) >= 0) found = o;
    }
    if (vec[0] && g_dealloc) g_dealloc(vec[0], (unsigned)((char *)vec[2] - (char *)vec[0]));
    return found;
}


// ---- PROBE (stand only, ReplayTakeover.ini Probe=1) ----
static int g_probe; static void *g_prGs; static int g_prState = -99, g_prType = -99, g_prTick = -1;
static void probe(const char *why) {
    char *st = settings(), *w = world(); char *go = st ? P32(st, 0x110) : NULL; char *gs = gamestate(st);
    char *g678 = ok((void *)0x1335678, 4) ? *(char **)0x1335678 : NULL; char *p4 = ok(g678, 8) ? P32(g678, 4) : NULL;
    char *sc = gs && ok(gs + 0x154, 4) ? P32(gs, 0x154) : NULL; char *sy = ok(sc, 0x78) ? P32(sc, 4) : NULL;
    logmsg("PROBE %s: st %p type %X | g678 %p +4 %p (=st+118? %d) *+4 %X | go %p gs %p state %d 6C %p 74 %d | sc %p sync %p vt %08lX sc74 %d | w %p 174 %d tick %d",
        why, st, st ? I32(st, 0x118) : -1, g678, p4, p4 == st + 0x118, ok(p4, 4) ? I32(p4, 0) : -1, go, gs, gs ? I32(gs, 0x34) : -1,
        gs ? P32(gs, 0x6C) : NULL, gs ? U8(gs, 0x74) : -1, sc, sy, ok(sy, 4) ? *(DWORD *)sy : 0, ok(sc, 0x78) ? I32(sc, 0x74) : -1,
        w, w ? I32(w, 0x174) : -1, w ? I32(w, 0x178) : -1);
    { char *rg = st ? P32(st, 0x10C) : NULL; char b[400]; int k = 0; b[0] = 0;
      if (ok(rg, 0x40)) for (int i = 0; i < 0x40; i += 4) k += snprintf(b + k, sizeof b - k, " %08X", I32(rg, i));
      logmsg("PROBE   st+10C %p vt %08lX vt8 %08lX:%s", rg, ok(rg, 4) ? *(DWORD *)rg : 0, ok(rg, 4) && ok(*(void **)rg, 12) ? (*(DWORD **)rg)[2] : 0, b); }
    if (st) { char *arr = P32(st, 0x118 + 0x810); int n = I32(st, 0x118 + 0x814); char b[512]; int k = 0; b[0] = 0;
        for (int i = 0; i < n && i < 16 && ok(arr + i * 0xA8, 0xA8); i++) k += snprintf(b + k, sizeof b - k, " [%d t%d id%d d%d]", i, I32(arr + i * 0xA8, 0), I32(arr + i * 0xA8, 0x9C), I32(arr + i * 0xA8, SLOT_AIDIFF));
        logmsg("PROBE   slots(%d) arr %p:%s", n, arr, b); }
    if (ok(p4, 0x820) && p4 != st + 0x118) { char *arr = P32(p4, 0x810); int n = I32(p4, 0x814); char b[512]; int k = 0; b[0] = 0;
        for (int i = 0; i < n && i < 16 && ok(arr + i * 0xA8, 0xA8); i++) k += snprintf(b + k, sizeof b - k, " [%d t%d id%d]", i, I32(arr + i * 0xA8, 0), I32(arr + i * 0xA8, 0x9C));
        logmsg("PROBE   +4 slots(%d) arr %p:%s", n, arr, b); }
}
static void probe_frame(char *gs) {
    if (!g_probe) return;
    char *st = settings(); char *w = world(); int stt = ok(gs, 0x40) ? I32(gs, 0x34) : -1, ty = st ? I32(st, 0x118) : -1, tk = w ? I32(w, 0x178) : -1;
    if (gs != g_prGs || stt != g_prState || ty != g_prType || (tk >= 0 && tk / 300 != g_prTick / 300 && tk < 1500)) {
        g_prGs = gs; g_prState = stt; g_prType = ty; g_prTick = tk; probe("frame");
    }
}


// ---- v4: the squad tabs and control groups of the new local player ----
// In a match the game's skirmish SCAR gives every squad of the LOCAL player a squad tab and the next free control group
// (Skirmish_Start for the starting squads, heroes first; Skirmish_GE_SquadCreated -> Skirmish_SetupSquad for each new one:
// UI_AddSquadTab + Misc_GetEmptyControlGroup / Misc_SetSquadControlGroup; Elite does the group in Elite.ui.on_squad_created).
// A replay has no local player, so none of his squads got a tab or a number, and after "Continue as" only squads built later
// got one (player noofancy, fb 1206: "no binds on the units that are yours"). The groups are UI state (the selection object,
// *(*(0x01335720)+8)+0x20: +0x344 groups, +0x380 pending), not the simulation; the replay does not record them.
// So right after the takeover the same SCAR function runs for each squad of the new local player that has no group yet:
// heroes first, then the others, each in the order the game made them (squad id) - as at a match start.
// Through the game's own SCAR state: World::GetSimManager -> SimManager::GetScar -> Scar::GetState (LuaConfig) ->
// LuaConfig::GetState (lua_State), luaL_loadbuffer + lua_pcall; the simulation is idle here (between two ticks).
typedef void *(__fastcall *This0_t)(void *o, void *edx);
typedef int (__stdcall *LLoadBuf_t)(void *L, const char *b, size_t n, const char *name);
typedef int (__stdcall *LPcall_t)(void *L, int nargs, int nres, int errf);
typedef int (__stdcall *LGetTop_t)(void *L);
typedef void (__stdcall *LSetTop_t)(void *L, int i);
typedef const char *(__stdcall *LToStr_t)(void *L, int i, size_t *n);
static This0_t g_getsm, g_getscar, g_scarstate, g_lcstate;
static LLoadBuf_t g_lload; static LPcall_t g_lpcall; static LGetTop_t g_lgettop; static LSetTop_t g_lsettop; static LToStr_t g_ltostr;
static const char SETUP_LUA[] =
    "local ok, r = pcall(function()\n"
    " if not (Game_HasLocalPlayer and Game_HasLocalPlayer()) then return 'no local player' end\n"
    " local setup = Skirmish_SetupSquad\n"
    " if setup == nil then return 'no Skirmish_SetupSquad' end\n"
    " local heroes, others, k = {}, {}, 0\n"
    " SGroup_ForEach(Player_GetSquads(Game_GetLocalPlayer()), function(g, i, s)\n"
    "  if Misc_GetSquadControlGroup(s) < 0 then\n"
    "   k = k + 1\n"
    "   local e = {s = s, id = (Squad_GetGameID and Squad_GetGameID(s)) or k}\n"
    "   if string.find(string.lower(Squad_GetBlueprintName(s)), '\\\\heroes\\\\', 1, true) then table.insert(heroes, e) else table.insert(others, e) end\n"
    "  end\n"
    "  return false\n"
    " end)\n"
    " local by = function(a, b) return a.id < b.id end\n"
    " table.sort(heroes, by); table.sort(others, by)\n"
    " local n = 0\n"
    " for _, e in ipairs(heroes) do setup(e.s); n = n + 1 end\n"
    " for _, e in ipairs(others) do setup(e.s); n = n + 1 end\n"
    " return n .. ' squad(s) set up (' .. #heroes .. ' hero), next free group ' .. Misc_GetEmptyControlGroup()\n"
    "end)\n"
    "return tostring(ok), tostring(r)\n";
static void setup_squads(void) {
    char *w = world();
    void *sm = w && g_getsm ? g_getsm(w, 0) : NULL, *sc = sm && g_getscar ? g_getscar(sm, 0) : NULL;
    void *lc = sc && g_scarstate ? g_scarstate(sc, 0) : NULL, *L = lc && g_lcstate ? g_lcstate(lc, 0) : NULL;
    if (!L || !g_lload || !g_lpcall || !g_lgettop || !g_lsettop || !g_ltostr) { logmsg("  squad tabs / control groups: no SCAR state (%p) - not set", L); return; }
    int top = g_lgettop(L);
    int r = g_lload(L, SETUP_LUA, sizeof SETUP_LUA - 1, "=ReplayTakeover");
    if (!r) r = g_lpcall(L, 0, 2, 0);
    const char *a = g_ltostr(L, r ? -1 : -2, NULL), *b = r ? NULL : g_ltostr(L, -1, NULL);
    if (r) logmsg("  squad tabs / control groups: Lua error %d: %s", r, a ? a : "?");
    else logmsg("  squad tabs / control groups: %s%s", a && !strcmp(a, "true") ? "" : "failed: ", b ? b : "?");
    g_lsettop(L, top);
}


// ---- v4: what the takeover changes, put back when that game shuts down ----
// Player noofancy (fb 1206): after "Continue as" in one replay, the NEXT replay of the same run went out of sync - from its
// first tick the game rejected the recorded orders ("Rejecting a command because it is coming from a player that doesn't
// own the squad", 390 times). GameObj::ShutdownGameObj (0x422104) ends a replay with a call of the recorded-game object
// (settings+0x10C, vtable 0x0108BA30, slot 2 = 0x42BE27): it empties the queue of recorded orders read ahead of the
// simulation (+4..+0x14) and zeroes its tick (+0x30) and flag (+0x2C) - but only when the game type (settings+0x118) is
// still 0x800 there. A continued game had type 4, so the orders the replay had read ahead stayed in that queue, and the
// next replay began by playing them: orders of the old match for squad ids that now belong to other players (stand 1,
// 3v3 rt_test321 taken at tick 200, then the same replay again: 34 rejected orders in 20 s). Now, in the shutdown
// of a continued game, at the call of "GAME -- Ending mission" (0x42224A -> 0x4202EC, before that type check), everything
// the takeover changed goes back to the replay's values: the type, the GameInfo slots (type, AI difficulty), the
// synchronization source of the SimulationController (the replay's PlaybackSynchronization back, our EchoSynchronization
// deleted) and its mode, GameState+0x6C/+0x74 (the shutdown zeroes those itself). The world, its players and the HUD die
// with the game anyway.
#define AT_ENDMISSION 0x0042224A   // call 0x4202EC in GameObj::ShutdownGameObj's body (0x422104), the only call of it
#define FN_ENDMISSION 0x004202EC
typedef void (__fastcall *Del_t)(void *o, void *edx, int flags);
static struct {
    int active; void *go, *gs, *sc, *playback, *echo; int type, scMode, n; void *local; unsigned char local74;
    int slotType[16], slotDiff[16];
} g_snap;
static int g_testNoRestore;                        // stands only (ReplayTakeover.ini TestNoRestore=1): the old behaviour
static void snapshot(char *st, char *gs, char *sc) {
    char *arr = P32(st, 0x118 + 0x810); int n = I32(st, 0x118 + 0x814); if (n > 16) n = 16;
    memset(&g_snap, 0, sizeof g_snap);
    g_snap.go = P32(st, 0x110); g_snap.gs = gs; g_snap.sc = sc; g_snap.playback = P32(sc, 4); g_snap.scMode = I32(sc, 0x74);
    g_snap.type = I32(st, 0x118); g_snap.local = P32(gs, 0x6C); g_snap.local74 = U8(gs, 0x74); g_snap.n = n;
    for (int i = 0; i < n; i++) { g_snap.slotType[i] = I32(arr + i * 0xA8, 0); g_snap.slotDiff[i] = I32(arr + i * 0xA8, SLOT_AIDIFF); }
    g_snap.active = 1;
}
static void restore(const char *why) {
    if (!g_snap.active) return;
    g_snap.active = 0;
    if (g_testNoRestore) { logmsg("restore (%s): TestNoRestore=1 - left as it is", why); return; }
    char *st = settings(); int k = 0;
    if (!st || P32(st, 0x110) != g_snap.go) { logmsg("restore (%s): not the continued game any more - nothing done", why); return; }
    I32(st, 0x118) = g_snap.type;
    char *arr = P32(st, 0x118 + 0x810); int n = I32(st, 0x118 + 0x814);
    if (n == g_snap.n || (n > 16 && g_snap.n == 16))
        for (int i = 0; i < g_snap.n; i++) if (ok(arr + i * 0xA8, 0xA8)) {
            if (I32(arr + i * 0xA8, 0) != g_snap.slotType[i]) { I32(arr + i * 0xA8, 0) = g_snap.slotType[i]; k++; }
            if (I32(arr + i * 0xA8, SLOT_AIDIFF) != g_snap.slotDiff[i]) { I32(arr + i * 0xA8, SLOT_AIDIFF) = g_snap.slotDiff[i]; k++; }
        }
    char *gs = g_snap.go && ok(g_snap.go, 8) ? P32(g_snap.go, 4) : NULL;
    const char *src = "gone";
    if (gs == g_snap.gs && ok(gs, 0x158) && P32(gs, 0x154) == g_snap.sc && ok(g_snap.sc, 0x78)) {
        if (P32(g_snap.sc, 4) == g_snap.echo && g_snap.playback) {
            P32(g_snap.sc, 4) = g_snap.playback; I32(g_snap.sc, 0x74) = g_snap.scMode; src = "playback back";
            if (ok(g_snap.echo, 4) && *(DWORD *)g_snap.echo == VT_ECHO) ((Del_t)(*(void ***)g_snap.echo)[0])(g_snap.echo, 0, 1);
        } else src = "not ours - left";
        P32(gs, 0x6C) = g_snap.local; U8(gs, 0x74) = g_snap.local74;
    }
    logmsg("restore (%s): game type %X, %d slot field(s), sync source: %s", why, g_snap.type, k, src);
}
static void my_endmission(void) {                  // GameObj::ShutdownGameObj -> "GAME -- Ending mission"
    if (g_probe) probe("shutdown");
    restore("the continued game shuts down");
    ((void (*)(void))FN_ENDMISSION)();
}

// ---- the takeover itself (main thread, between two ticks) ----
static void hud_down(char *gs) { void *gu = P32(gs, 4); ((Fast1_t)(*(void ***)gu)[0x68 / 4])(gu); }
static void hud_up(char *gs) { void *gu = P32(gs, 4); ((Fast1_t)(*(void ***)gu)[0x64 / 4])(gu); }
static int g_mm_frames; static unsigned char g_mm_saved;
static void minimap_kick(void) {
    char *m = ok((void *)G_MINIMAP, 4) ? *(char **)G_MINIMAP : NULL; if (!ok(m, 0xB10)) return;
    g_mm_saved = U8(m, 0xAEC); U8(m, 0xAEC) = 0; g_mm_frames = 3;
}
static void minimap_tick(void) {
    if (!g_mm_frames || --g_mm_frames) return;
    char *m = ok((void *)G_MINIMAP, 4) ? *(char **)G_MINIMAP : NULL;
    if (ok(m, 0xB10)) U8(m, 0xAEC) = g_mm_saved ? g_mm_saved : 1;
}


static volatile LONG g_pending;                    // player id to take, 0 = none
static void *g_takenGame;                          // GameObj of the game we turned into a skirmish
static int sim_busy(char *gs) {
    char *m = P32(gs, 0x1A8); if (!ok(m, 0x14)) return 0;
    void *job = P32(m, 0x10); if (!job) return 0;
    return g_isdone ? !g_isdone(job, 0) : 1;
}
static void take(DWORD id) {
    char *st = settings(), *gs = gamestate(st), *w = world();
    char *go = st ? P32(st, 0x110) : NULL;
    if (!gs || !w || I32(st, 0x118) != T_PLAYBACK || I32(gs, 0x34) != 5) { logmsg("not in a running replay - nothing done"); return; }
    char *sc = P32(gs, 0x154), *sync = ok(sc, 0x78) ? P32(sc, 4) : NULL;
    void *tp = player_by_id(id); int ts = slot_of(id);
    if (!ok(sync, 8) || *(DWORD *)sync != VT_PLAYBACK || !tp || ts < 0) { logmsg("player %lu: no player / not a playback (sync %p) - nothing done", id, sync); return; }
    char *arr = P32(st, 0x118 + 0x810); int n = I32(st, 0x118 + 0x814);
    logmsg("continue as %s (player %lu, slot %d) at tick %d, others: %s", u8(player_name(tp)), id, ts, I32(w, 0x178), g_others_ai ? "computer" : "stand");
    { char *hud = *(char **)G_HUD, *h = ok(hud, 12) ? P32(hud, 8) : NULL, *sr = ok(h, 0x70) ? P32(h, 0x68) : NULL;
      if (ok(sr, 4)) ((SetF_t)(*(void ***)sr)[0x28 / 4])(sr, 0, 10.0f); }          // normal speed (a replay may run at 4x)
    hud_down(gs);
    int diff = g_diff;
    if (g_others_ai && g_newai >= 0) {
        const char *why = newai_missing();
        if (why) logmsg("  new AI %c asked, but %s - Elite's AI instead (difficulty %s)", 'A' + g_newai, why, diff < 0 ? "kept" : DIFF_NAME[diff]);
        else { diff = g_newai; logmsg("  new AI %c (EliteAI.dll): the computers play at %s", 'A' + g_newai, DIFF_NAME[diff]); }
    }
    snapshot(st, gs, sc);
    I32(st, 0x118) = T_SKIRMISH;
    unsigned char wasAi[16] = {0};
    for (int i = 0; i < n; i++) {
        char *s = arr + i * 0xA8; int t = I32(s, 0), nt = t;
        if (i < 16) wasAi[i] = t == 1;
        if (i == ts) nt = 0;                               // local human
        else if (t == 0 || t == 1 || t == 3 || t == 4) nt = g_others_ai ? 1 : 3;   // computer here / a human who stands
        else if (t == 2) nt = 5;                           // the recording observer -> remote observer
        if (nt != t) I32(s, 0) = nt;
    }
    if (g_others_ai) for (int i = 0; i < n; i++) {
        char *s = arr + i * 0xA8; if (i == ts || I32(s, 0) != 1) continue;
        void *p = player_by_id(I32(s, 0x9C)); if (!p) continue;
        int was = ok((char *)p + PL_AIDIFF, 4) ? I32(p, PL_AIDIFF) : -99, now = was;
        if (diff >= 0 && !(i < 16 && wasAi[i]) && was != -99) {   // a former human: the chosen difficulty for his computer
            I32(p, PL_AIDIFF) = diff; I32(s, SLOT_AIDIFF) = diff; now = diff;
        }
        P32(gs, 0x6C) = p; char r = ((AiLocal_t)FN_AI_LOCAL)(go);
        logmsg("  computer for %s (player %d): %s, difficulty %s%s", u8(player_name(p)), I32(s, 0x9C), r ? "on" : "already / failed",
               now >= 0 && now <= 3 ? DIFF_NAME[now] : "?", now != was ? (was == 0 ? " (was easy)" : was == 1 ? " (was normal)" : was == 2 ? " (was hard)" : " (was other)")
               : (i < 16 && wasAi[i]) ? " (a computer of the match: its own)" : "");
    }
    P32(gs, 0x6C) = tp; U8(gs, 0x74) = 1;
    void *echo = ((New_t)FN_NEW)(0x34);
    if (echo) { ((Ctor_t)FN_ECHO_CTOR)(echo); P32(sc, 4) = echo; I32(sc, 0x74) = 3; g_snap.echo = echo; }   // the playback source: put back at shutdown
    I32(w, 0x174) = 0;
    hud_up(gs);
    setup_squads();
    char *um = *(char **)G_UIMGR; void *o = ok(um, 0x38) ? P32(um, 0x34) : NULL; if (ok(o, 4)) ((V1_t)(*(void ***)o)[0x34 / 4])(o, 0, 1);
    minimap_kick(); g_takenGame = go;
    logmsg("done: a skirmish now, %s is yours", u8(player_name(tp)));
    if (g_probe) probe("after take");
}
static void try_take(char *gs) {
    LONG id = g_pending; if (!id || sim_busy(gs)) return;
    InterlockedExchange(&g_pending, 0); take((DWORD)id);
}

// threaded simulation: before the frame schedules the next sim tick
void __stdcall pre_simthr(char *mgr) {
    (void)mgr; char *gs = gamestate(settings()); if (g_pending && gs) try_take(gs);
}
void *g_simthr = (void *)FN_SIMTHR;
__asm__(".globl _simthr_stub\n_simthr_stub:\n  pushal\n  pushl %eax\n  call _pre_simthr@4\n  popal\n  jmp *_g_simthr\n");
void simthr_stub(void);

static int __stdcall my_frame(void *go, int a, int b, float dt) {
    minimap_tick();
    char *gs = ok(go, 8) ? P32(go, 4) : NULL;
    probe_frame(gs);
    if (g_takenGame && (!ok(gs, 0x40) || I32(gs, 0x34) != 5 || is_replay())) g_takenGame = NULL;   // that game is over
    if (g_snap.active && go != g_snap.go) { g_snap.active = 0; logmsg("a new game, and the continued one was not restored at its shutdown (it is gone now)"); }
    if (g_testAt >= 0 && !g_pending && is_replay() && ok(gs, 0x40) && I32(gs, 0x34) == 5) {   // stands only: take at that tick
        char *w = world(), *st = settings();
        if (w && st && I32(w, 0x178) >= g_testAt && g_testSlot >= 0 && g_testSlot < I32(st, 0x118 + 0x814)) {
            DWORD id = (DWORD)I32(P32(st, 0x118 + 0x810) + g_testSlot * 0xA8, 0x9C);
            logmsg("test: take slot %d (player %lu) at tick %d", g_testSlot, id, I32(w, 0x178)); g_testAt = -1;
            InterlockedExchange(&g_pending, (LONG)id);
        }
    }
    if (g_pending && ok(gs, 0x1B0) && !U8(gs, 0x1AC)) try_take(gs);   // simulation in this thread: idle here
    return ((Frame_t)FN_FRAME)(go, a, b, dt);
}

// ---- the menu button ("Concede match" in a replay) ----
static Active_t g_ccActive = (Active_t)FN_CC_ACTIVE;
static char __fastcall my_cc_active(void *self, void *edx, void *ctx) {
    if (is_replay() && !g_pending) return 1;
    return g_ccActive(self, edx, ctx);
}
static void *g_lastShown; static DWORD g_lastTick; static wchar_t g_caption[160];
static void set_caption(void *self, const wchar_t *text) {
    char *wdg = ok((char *)self + 0x24, 4) ? P32(self, 0x24) : NULL; void *mv = gfx_movie_of_widget(wdg);
    if (!mv) return;                               // set every time: the menu movie may put its own text back
    GfxVal a[2]; a[0] = gv_str("btn_concede_match.text_resume"); memset(&a[1], 0, sizeof a[1]); a[1].type = GV_WSTRING; a[1].v.ws = text;
    if (gfx_invoke(mv, "Receive_SetText", NULL, a, 2)) wcsncpy(g_caption, text, 159);
}
static int taken_here(void) { char *st = settings(); return st && g_takenGame && P32(st, 0x110) == g_takenGame && !is_replay(); }
static void *g_hiddenWidget;                       // the button we hid in a continued game (the menu outlives the game)
static void widget_show(char *wdg, int on) {
    if (U8(wdg, 0x209) == on) return;
    U8(wdg, 0x209) = (unsigned char)on; ((Fast1_t)(*(void ***)wdg)[0x34 / 4])(wdg);   // as the game's own widget code does
}
static void __fastcall my_cc_update(void *self, void *edx, void *arg) {
    (void)edx; (void)arg;
    char *wdg = ok((char *)self + 0x24, 4) ? P32(self, 0x24) : NULL;
    if (!ok(wdg, 0x20C) || !ok(*(void **)wdg, 0x38)) return;
    if (taken_here()) {                            // a continued game: no Concede (it is a skirmish now)
        if (U8(wdg, 0x209)) { widget_show(wdg, 0); g_hiddenWidget = wdg; g_caption[0] = 0; }
        return;
    }
    if (!is_replay()) return;
    if (g_hiddenWidget == wdg) { widget_show(wdg, 1); g_hiddenWidget = NULL; }
    DWORD now = GetTickCount(); if (now - g_lastTick < 300) return;
    g_lastTick = now;
    void *p = selected_player(); wchar_t t[160];
    if (p) _snwprintf(t, 159, g_ru ? L"Продолжить за: %ls" : L"Continue as %ls", player_name(p));
    else wcscpy(t, g_ru ? L"Продолжить: выделите юнит" : L"Continue: select a unit");   // the button fits ~27 letters
    t[159] = 0;
    if (wdg != g_lastShown) { g_lastShown = wdg; g_caption[0] = 0; }   // a new menu movie: its own caption again
    set_caption(self, t);
}
static void __fastcall my_cc_click(void *self, void *edx, void *widget, int ev) {
    if (taken_here()) return;
    if (!is_replay()) { ((Click_t)FN_CC_CLICK)(self, edx, widget, ev); return; }
    if (ev != 0 || !ok((char *)self + 0x24, 4) || widget != P32(self, 0x24)) return;
    void *p = selected_player();
    if (!p) { g_lastTick = 0; logmsg("menu button: nothing of a player selected"); return; }
    InterlockedExchange(&g_pending, (LONG)player_id(p));
    logmsg("menu button: continue as %s", u8(player_name(p)));
    char *um = *(char **)G_UIMGR; void *gu = ok(um, 0x18) ? P32(um, 0x14) : NULL;      // close the menu as Resume does
    if (ok(gu, 4)) ((V1_t)(*(void ***)gu)[0x18 / 4])(gu, 0, 0);
}

// ---- install ----
static int patch_call(DWORD at, DWORD expect, void *fn) {      // E8 rel32 at `at` calling `expect` -> fn
    unsigned char *t = (unsigned char *)at; DWORD old;
    if (IsBadReadPtr(t, 5) || t[0] != 0xE8 || (DWORD)(t + 5) + *(int *)(t + 1) != expect) return 0;
    VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old); *(int *)(t + 1) = (DWORD)fn - (DWORD)(t + 5);
    VirtualProtect(t, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 5);
    return 1;
}
static int patch_imm(DWORD at, unsigned char op, DWORD expect, void *fn) {   // op imm32 -> op fn
    unsigned char *t = (unsigned char *)at; DWORD old;
    if (IsBadReadPtr(t, 5) || t[0] != op || *(DWORD *)(t + 1) != expect) return 0;
    VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old); *(DWORD *)(t + 1) = (DWORD)fn;
    VirtualProtect(t, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 5);
    return 1;
}
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_cs);
    wchar_t dir[MAX_PATH], ini[MAX_PATH];
    GetModuleFileNameW(h, dir, MAX_PATH); wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0; else dir[0] = 0;
    wcscpy(g_log, dir); wcscat(g_log, L"ReplayTakeover.log"); wcscpy(ini, dir); wcscat(ini, L"ReplayTakeover.ini");
    if (already_loaded("ReplayTakeover")) { logmsg("another ReplayTakeover copy is already active in this game, this one stays idle"); return TRUE; }
    wchar_t v[32]; GetPrivateProfileStringW(L"ReplayTakeover", L"Others", L"ai", v, 32, ini);
    g_others_ai = _wcsicmp(v, L"idle") != 0; wcscpy(g_dirw, dir);
    g_newai = !_wcsicmp(v, L"newai_a") ? 0 : !_wcsicmp(v, L"newai_b") ? 1 : !_wcsicmp(v, L"newai_c") ? 2 : -1;   // elite / ai = Elite's
    GetPrivateProfileStringW(L"ReplayTakeover", L"Difficulty", L"expert", v, 32, ini);
    g_diff = !_wcsicmp(v, L"keep") ? -1 : !_wcsicmp(v, L"easy") ? 0 : !_wcsicmp(v, L"normal") ? 1 : !_wcsicmp(v, L"hard") ? 2 : 3;
    g_probe = GetPrivateProfileIntW(L"ReplayTakeover", L"Probe", 0, ini);
    g_testNoRestore = GetPrivateProfileIntW(L"ReplayTakeover", L"TestNoRestore", 0, ini);
    g_testAt = GetPrivateProfileIntW(L"ReplayTakeover", L"TestTakeAt", -1, ini); g_testSlot = GetPrivateProfileIntW(L"ReplayTakeover", L"TestSlot", -1, ini);
    GetPrivateProfileStringW(L"ReplayTakeover", L"Language", L"auto", v, 32, ini);
    g_ru = !_wcsicmp(v, L"ru") || (!_wcsicmp(v, L"auto") && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
    HMODULE se = GetModuleHandleA("SimEngine.dll"), xt = GetModuleHandleA("XThread.dll"), lz = GetModuleHandleA("Localizer.dll"),
            sp = GetModuleHandleA("STLPort.dll");
    g_getpl = se ? (GetPl_t)GetProcAddress(se, "?GetPlayerFromID@PlayerManager@@QAEPAVPlayer@@K@Z") : NULL;
    g_owner = se ? (Owner_t)GetProcAddress(se, "?GetOwner@Entity@@QAEPAVPlayer@@XZ") : NULL;
    g_isdone = xt ? (IsDone_t)GetProcAddress(xt, "?IsDone@Job@XThread@@QBE_NXZ") : NULL;
    g_cstr = lz ? (CStr_t)GetProcAddress(lz, "?c_str@LocString@@QBEPB_WXZ") : NULL;
    g_dealloc = sp ? (Dealloc_t)GetProcAddress(sp, "?deallocate@__new_alloc@_STL@@SGXPAXI@Z") : NULL;

    if (se) { g_getsm = (This0_t)GetProcAddress(se, "?GetSimManager@World@@QAEPAVSimManager@@XZ");
              g_getscar = (This0_t)GetProcAddress(se, "?GetScar@SimManager@@QAEPAVScar@@XZ");
              g_scarstate = (This0_t)GetProcAddress(se, "?GetState@Scar@@QAEPAVLuaConfig@@XZ"); }
    HMODULE lcm = GetModuleHandleA("LuaConfig.dll");
    if (lcm) { g_lcstate = (This0_t)GetProcAddress(lcm, "?GetState@LuaConfig@@QAEPAUlua_State@@XZ");
               g_lload = (LLoadBuf_t)GetProcAddress(lcm, "_luaL_loadbuffer@16"); g_lpcall = (LPcall_t)GetProcAddress(lcm, "_lua_pcall@16");
               g_lgettop = (LGetTop_t)GetProcAddress(lcm, "_lua_gettop@4"); g_lsettop = (LSetTop_t)GetProcAddress(lcm, "_lua_settop@8");
               g_ltostr = (LToStr_t)GetProcAddress(lcm, "_lua_tolstring@12"); }
    // all or nothing: DOW2.exe 3.19 bytes where we patch, and what we call
    unsigned char *f = (unsigned char *)AT_FRAME, *s = (unsigned char *)AT_SIMTHR, *c = (unsigned char *)AT_CC_CLICK;
    DWORD *vt = (DWORD *)VT_CONCEDE;
    int good = g_getpl && g_owner && g_isdone && !IsBadReadPtr(f, 5) && f[0] == 0xE8 && !IsBadReadPtr(s, 5) && s[0] == 0xE8
        && !IsBadReadPtr(c, 5) && c[0] == 0xB9 && *(DWORD *)(c + 1) == FN_CC_CLICK && !IsBadReadPtr(vt, 0x1C)
        && vt[3] == FN_CC_ACTIVE && vt[6] == FN_NOP4 && *(DWORD *)VT_PLAYBACK == 0x0066AD20;
    if (!good) { logmsg("ReplayTakeover " VERSION ": not DOW2.exe 3.19 (or a module missing) - nothing changed"); return TRUE; }
    unsigned char *em = (unsigned char *)AT_ENDMISSION;
    if (IsBadReadPtr(em, 5) || em[0] != 0xE8 || (DWORD)(em + 5) + *(int *)(em + 1) != FN_ENDMISSION) {
        logmsg("ReplayTakeover " VERSION ": the game's shutdown call differs - nothing changed"); return TRUE; }
    int r = patch_call(AT_FRAME, FN_FRAME, (void *)my_frame) & patch_call(AT_SIMTHR, FN_SIMTHR, (void *)simthr_stub)
          & patch_call(AT_ENDMISSION, FN_ENDMISSION, (void *)my_endmission);
    if (!r) { logmsg("ReplayTakeover " VERSION ": the frame calls are taken by something else - nothing changed"); return TRUE; }
    gfx_patch_slot((DWORD)&vt[3], FN_CC_ACTIVE, (void *)my_cc_active);
    gfx_patch_slot((DWORD)&vt[6], FN_NOP4, (void *)my_cc_update);
    patch_imm(AT_CC_CLICK, 0xB9, FN_CC_CLICK, (void *)my_cc_click);
    logmsg("ReplayTakeover " VERSION ": in a replay - select a unit of a player, Menu -> \"Continue as ...\"; others: %s (difficulty %s), %s",
           g_others_ai ? (g_newai == 0 ? "new AI A" : g_newai == 1 ? "new AI B" : g_newai == 2 ? "new AI C" : "Elite's AI") : "stand", g_diff < 0 ? "kept" : DIFF_NAME[g_diff], g_ru ? "ru" : "en");
    if (g_testAt >= 0) logmsg("TEST: takes slot %d by itself at tick %d (ReplayTakeover.ini TestTakeAt - stands only)", g_testSlot, g_testAt);
    if (!g_cstr || !g_dealloc) logmsg("(names %s, selection buffer %s)", g_cstr ? "ok" : "missing", g_dealloc ? "freed" : "kept");
    return TRUE;
}
