// JumpRetry.dll - a jump order isn't lost on an obstacle hop (Dawn of War II: Retribution 3.19, Elite and vanilla).
//
// The Autarch (and any squad with the always-on "auto jump" passive, enable_obstacle_jumping_action) hops over low
// walls by itself while walking. Ordered to jump to a point beyond the ability's range (50 m), he walks toward it
// inside the ability order (SquadStateAbility, 0x0096F460, sub-state STATE_MOVEMENT 0x00970120). An obstacle hop on
// the way is a jump sub-state of the entity's own movement (StateMove STATE_JUMP 0x0097FB30 -> StateJump); the squad
// movement ends while he is in the air, the ability re-checks (STATE_DEFAULT 0x00970890: not in range yet, the squad
// can't move right now) and ends the order instead of walking on: no energy spent, no recharge, the squad idles
// (tested on stand 1, 03.11.2026: 5 Ability -> 0 Idle ~0.25 s after the hop, the jump never happens).
//
// Fix, client side only: remember the local player's own "ability at a point" order for the listed abilities; if the
// squad falls out of that ability back to idle without the jump starting, and the player gave it no other order, send
// the very same order again as soon as the squad can walk again (the ability's own check, 0x008645A0/0x00864670; at
// most LandTimeoutMs after falling out), plus RetryDelayMs (0). An order the game still drops (sent while in the air)
// shows as "no Ability state within TakeMs (250)" and is sent again; at most MaxRetries (3) within WindowSec (10).
// Stand 1, 03.11.2026: v1 (a fixed 600 ms) jumped ~1000 ms after falling out; v2 ~400 ms (retry taken ~100 ms after
// sending, the jump ~300 ms later), 1 retry in 3 of 4 trials, 3 in one (two sent too early, caught by TakeMs).
// The order is
// the game's own (CommandProxy, the same call the HUD makes): it goes through the normal command queue and is simulated
// the same on every machine of the match - no desync, the other players don't need the DLL. Replays and observing
// issue no orders of the local player, so nothing happens there.
//
// Hooks: the vftables of CommandProxy (0x0110E514) and EventGeneratorCommandProxy (0x01117704, the HUD's) - the ability
// orders: +0x40 (owner, ability id, Target[5], queued)
// 0x00AF4D50, +0x3C (the same with a direction) 0x00AF4ED0, +0x38 (one Target) 0x00AF51A0, +0x34 0x00AF53C0; a retry goes
// through the same method; the DOW2.exe import slots of WorldDoCommandSquad /
// WorldDoCommandSquads / WorldDoCommandEntity, only to see any other order the HUD (its code range) gives the squad,
// which cancels the retry. A WM_TIMER on the game window (main thread, as Reverse.dll) watches the squads.
// JumpRetry.ini [JumpRetry]: Enabled=1, MaxRetries=3 (1..5), WindowSec=10 (3..30), RetryDelayMs=0 (0..3000), LandTimeoutMs=600,
// TakeMs=250,
// Abilities=ability\pvp\race_eldar\autarch\eld_autarch_jump;... (';'-separated attribute names), Verbose=0; re-read on
// every order (Enabled=0 then stops remembering new orders).
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define VT_PROXY      0x0110E514u                    // CommandProxy vftable
#define VT_EVPROXY    0x01117704u                    // EventGeneratorCommandProxy (derived; the one the HUD uses)
#define SLOT_DIR      0x34                           // SquadAbility with a target and a direction point
#define SLOT_TGT      0x38                           // SquadAbility with a target (the HUD's ability click)
#define SLOT_MDIR     0x3C                           // with up to 5 targets and a direction
#define SLOT_MULTI    0x40                           // with up to 5 targets
#define ORIG_DIR      0x00AF53C0u
#define ORIG_TGT      0x00AF51A0u
#define ORIG_MDIR     0x00AF4ED0u
#define ORIG_MULTI    0x00AF4D50u
#define TGT_SIZE      0x1C                           // sizeof(Target): the multi-target orders take Target[5]
#define IAT_SQUAD     0x00F883D4u                    // ?WorldDoCommandSquad@@YGPAVWorldCommand@@K_NPBVPlayer@@PBVSquad@@@Z
#define IAT_SQUADS    0x00F88BECu                    // ?WorldDoCommandSquads@@...ABVConstSquadGroupTemp@@@Z
#define IAT_ENTITY    0x00F88858u                    // ?WorldDoCommandEntity@@...PBVEntity@@@Z
#define HUD_LO        0x00AD0000u                    // the HUD's code (bindings, CommandProxy, hotkeys): orders the player
#define HUD_HI        0x00BB0000u                    // gives (the AI's commands come from 0x00A6.., the simulation's below)
#define G_WORLD       0x013356A4u                    // the world; +0x154 squad lookup by id (vtbl+4), as auto-reinforce
#define CMD_REINFORCE 0x31                           // the HUD's auto-reinforce is not an order to the squad
#define ST_IDLE       0
#define ST_ABILITY    5                              // SquadStateAbility
#define ST_JUMP       15                             // SquadStateJump: the jump went off

typedef struct { float x, y, z; } Vec3;
typedef void  (__fastcall *AbTgt_t)(void *self, void *edx, void *owner, DWORD abil, void *target, int queued);
typedef void  (__fastcall *AbDir_t)(void *self, void *edx, void *owner, DWORD abil, void *target, Vec3 *dir, int queued);
typedef void  (__fastcall *AbMul_t)(void *self, void *edx, void *owner, DWORD abil, void *targets, int queued);
typedef void  (__fastcall *AbMDir_t)(void *self, void *edx, void *owner, DWORD abil, void *targets, void *dir, int queued);
typedef void *(__stdcall *CmdSq_t)(DWORD type, int queued, void *player, void *squad);
typedef void *(__stdcall *CmdGrp_t)(DWORD type, int queued, void *player, void *group);
typedef int   (__fastcall *I0_t)(void *obj, void *edx);
typedef void *(__fastcall *P0_t)(void *obj, void *edx);
typedef void  (__fastcall *V0_t)(void *obj, void *edx);
typedef void  (__fastcall *V1_t)(void *obj, void *edx, const void *a);
typedef Vec3 *(__fastcall *Pos_t)(void *target, void *edx, Vec3 *out);
typedef void *(__fastcall *Find_t)(void *cont, void *edx, void *const *item);
typedef void *(__fastcall *Name_t)(void *mgr, void *edx, const char *name);
typedef void *(__fastcall *Grp2_t)(void *mgr, void *edx, DWORD type, DWORD id);
typedef void *(__stdcall *Inst_t)(void);
typedef void *(__fastcall *Lookup_t)(void *obj, void *edx, DWORD id);

static AbTgt_t o_tgt; static AbDir_t o_dir; static AbMul_t o_mul; static AbMDir_t o_mdir; static V0_t p_tdef; static CmdSq_t o_sq, o_ent; static CmdGrp_t o_grp;
static I0_t p_otype, p_ttype; static P0_t p_osq; static V1_t p_octor, p_tctor; static V0_t p_tdtor; static Pos_t p_tpos;
static I0_t p_sqid; static Find_t p_find; static P0_t p_end; static Name_t p_byname; static Grp2_t p_byid; static Inst_t p_inst;

static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH];
static int g_max = 3, g_window = 10, g_delay = 0, g_land = 600, g_take = 250, g_verbose = 0, g_resend = 0;
#define MAXAB 16
static char g_names[MAXAB][160]; static int g_nnames; static void *g_pbg[MAXAB]; static DWORD g_abid[MAXAB]; static int g_resolved;

// one remembered order per squad (the player's own squads only)
#define MAXP 8
typedef struct { int used, slot, armed, tries, abSeen; DWORD sq, abil, t0, ts, idleAt, landAt, sentAt; void *proxy, *world; Vec3 pos, dir; } Pend;
static Pend g_p[MAXP];

static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsJumpRetry.log", g_dir);
    FILE *f = _wfopen(p, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

static void load_ini(void) {
    g_max = GetPrivateProfileIntW(L"JumpRetry", L"MaxRetries", 3, g_ini); if (g_max < 1) g_max = 1; if (g_max > 5) g_max = 5;
    g_window = GetPrivateProfileIntW(L"JumpRetry", L"WindowSec", 10, g_ini); if (g_window < 3) g_window = 3; if (g_window > 30) g_window = 30;
    g_delay = GetPrivateProfileIntW(L"JumpRetry", L"RetryDelayMs", 0, g_ini); if (g_delay < 0) g_delay = 0; if (g_delay > 3000) g_delay = 3000;
    g_land = GetPrivateProfileIntW(L"JumpRetry", L"LandTimeoutMs", 600, g_ini); if (g_land < 100) g_land = 100; if (g_land > 3000) g_land = 3000;
    g_take = GetPrivateProfileIntW(L"JumpRetry", L"TakeMs", 250, g_ini); if (g_take < 100) g_take = 100; if (g_take > 2000) g_take = 2000;
    g_verbose = GetPrivateProfileIntW(L"JumpRetry", L"Verbose", 0, g_ini);
    char buf[2048];
    { char inia[MAX_PATH]; WideCharToMultiByte(CP_ACP, 0, g_ini, -1, inia, MAX_PATH, NULL, NULL);
      GetPrivateProfileStringA("JumpRetry", "Abilities", "ability\\pvp\\race_eldar\\autarch\\eld_autarch_jump", buf, sizeof buf, inia); }
    g_nnames = 0;
    for (char *s = strtok(buf, ";,"); s && g_nnames < MAXAB; s = strtok(NULL, ";,")) {
        while (*s == ' ') s++;
        size_t n = strlen(s); while (n && (s[n - 1] == ' ' || s[n - 1] == '\r')) s[--n] = 0;
        if (n > 4 && !_stricmp(s + n - 4, ".rbf")) s[n -= 4] = 0;
        for (char *c = s; *c; c++) if (*c == '/') *c = '\\';
        if (n) lstrcpynA(g_names[g_nnames++], s, sizeof g_names[0]);
    }
}

static void resolve(void) {                          // attribute names -> the ability groups (lazily: the game has loaded them)
    if (g_resolved) return;
    void *mgr = p_inst(); if (!mgr) return;
    g_resolved = 1;
    for (int i = 0; i < g_nnames; i++) {
        void *g = p_byname(mgr, NULL, g_names[i]);
        g_pbg[i] = g; g_abid[i] = (g && !IsBadReadPtr((char *)g + 0x50, 4)) ? *(DWORD *)((char *)g + 0x50) : 0xFFFFFFFF;
        logmsg("ability %s -> %s (id %ld)", g_names[i], g ? "found" : "NOT FOUND", g ? (long)g_abid[i] : -1L);
    }
}

static int listed(DWORD abil) {
    resolve();
    void *mgr = p_inst(); void *g = mgr ? p_byid(mgr, NULL, 6, abil) : NULL;   // 6 = ability groups (as the game's own lookup)
    for (int i = 0; i < g_nnames; i++) if (g_pbg[i] && (g_abid[i] == abil || (g && g == g_pbg[i]))) return 1;
    return 0;
}

static void *squad_by_id(DWORD id) {
    void *w = *(void **)G_WORLD; if (!w) return NULL;
    void *obj = *(void **)((char *)w + 0x154); if (!obj) return NULL;
    Lookup_t f = (Lookup_t)(*(void ***)obj)[1];
    return f(obj, NULL, id);
}
static int squad_state(void *sq) {                   // the squad's active state id (as Squad_GetActiveCommand), -1 = none
    void *ctl = *(void **)((char *)sq + 0x48); if (!ctl) return -1;
    void *st = *(void **)((char *)ctl + 0x1E8); if (!st) return -1;
    I0_t f = (I0_t)(*(void ***)st)[1];
    return f(st, NULL);
}

static void drop(int i, const char *why) {
    if (!g_p[i].used) return;
    logmsg("squad %lu: %s", (unsigned long)g_p[i].sq, why);
    g_p[i].used = 0;
}
static void cancel_squad(void *sq, DWORD type) {
    if (g_resend || !sq) return;
    DWORD id = (DWORD)p_sqid(sq, NULL);
    for (int i = 0; i < MAXP; i++) if (g_p[i].used && g_p[i].sq == id) {
        char why[64]; wsprintfA(why, "another order from the player (command %lu) - no retry", (unsigned long)type); drop(i, why);
    }
}
static int any_pending(void) { for (int i = 0; i < MAXP; i++) if (g_p[i].used) return 1; return 0; }

// ---- the HUD's other orders: they cancel a retry (only calls from the HUD's code = the local player's own clicks and
// hotkeys: CommandProxy 0x00AF51A0.., the Stop hotkey BindingHalt 0x00B75E5C - tested on stand 1)
static int from_hud(void *ret) { return (DWORD)ret >= HUD_LO && (DWORD)ret < HUD_HI; }
static void *__stdcall hk_sq(DWORD type, int queued, void *player, void *squad) {
    if (g_verbose >= 2) logmsg("WorldDoCommandSquad %lu from %p", (unsigned long)type, __builtin_return_address(0));
    if (any_pending() && type != CMD_REINFORCE && from_hud(__builtin_return_address(0))) cancel_squad(squad, type);
    return o_sq(type, queued, player, squad);
}
static void *__stdcall hk_ent(DWORD type, int queued, void *player, void *ent) {
    if (any_pending() && type != CMD_REINFORCE && ent && from_hud(__builtin_return_address(0)) && !IsBadReadPtr((char *)ent + 0x38, 4))
        cancel_squad(*(void **)((char *)ent + 0x38), type);
    return o_ent(type, queued, player, ent);
}
static void *__stdcall hk_grp(DWORD type, int queued, void *player, void *group) {
    if (g_verbose >= 2) logmsg("WorldDoCommandSquads %lu from %p", (unsigned long)type, __builtin_return_address(0));
    if (any_pending() && type != CMD_REINFORCE && group && !g_resend && from_hud(__builtin_return_address(0))) {
        for (int i = 0; i < MAXP; i++) {
            if (!g_p[i].used) continue;
            void *sq = squad_by_id(g_p[i].sq); if (!sq) continue;
            void *const k = sq;
            if (p_find(group, NULL, &k) != p_end(group, NULL)) {
                char why[64]; wsprintfA(why, "another order from the player (command %lu) - no retry", (unsigned long)type); drop(i, why);
            }
        }
    }
    return o_grp(type, queued, player, group);
}

// ---- watching the remembered orders (WM_TIMER on the game window = the game's main thread)
static void resend(Pend *p, void *sq) {
    unsigned char owner[64], tgt[TGT_SIZE * 5 + 16]; memset(owner, 0, sizeof owner); memset(tgt, 0, sizeof tgt);
    if (IsBadReadPtr(p->proxy, 4) || (*(DWORD *)p->proxy != VT_PROXY && *(DWORD *)p->proxy != VT_EVPROXY)) { logmsg("squad %lu: the HUD is gone - no retry", (unsigned long)p->sq); p->used = 0; return; }
    p_octor(owner, NULL, sq);
    // the same order as the player's click, through the same CommandProxy method: one Target (+0x38; +0x34 with its
    // direction point), or Target[5] = { the point, 4 empty } (+0x40 / +0x3C: the jump has no direction)
    p_tctor(tgt, NULL, &p->pos);
    for (int k = 1; k < 5; k++) p_tdef(tgt + k * TGT_SIZE, NULL);
    g_resend = 1;
    if (p->slot == SLOT_TGT) o_tgt(p->proxy, NULL, owner, p->abil, tgt, 0);
    else if (p->slot == SLOT_DIR) o_dir(p->proxy, NULL, owner, p->abil, tgt, &p->dir, 0);
    else o_mul(p->proxy, NULL, owner, p->abil, tgt, 0);
    g_resend = 0;
    for (int k = 0; k < 5; k++) p_tdtor(tgt + k * TGT_SIZE, NULL);
}
// "the squad can walk again" - the very check the ability state makes before walking on (SquadStateAbility STATE_DEFAULT
// 0x00970890 -> 0x008645A0(1) && !0x00864670, both with the squad in EAX): false while a model is in the air of an obstacle
// hop, so an order sent then is dropped the same way. Read-only queries, the HUD's thread as the HUD's own reads.
static int squad_ready(void *sq) {
    int a, b;
    __asm__ volatile ("pushl $1\n\tcall *%2" : "=a"(a) : "a"(sq), "r"((DWORD)0x008645A0u) : "ecx", "edx", "memory", "cc");
    if (!(a & 0xFF)) return 0;
    __asm__ volatile ("call *%2" : "=a"(b) : "a"(sq), "r"((DWORD)0x00864670u) : "ecx", "edx", "memory", "cc");
    return !(b & 0xFF);
}
static void CALLBACK tick(HWND w, UINT m, UINT_PTR id, DWORD now) {
    (void)w; (void)m; (void)id;
    now = GetTickCount();
    for (int i = 0; i < MAXP; i++) {
        Pend *p = &g_p[i]; if (!p->used) continue;
        if (*(void **)G_WORLD != p->world) { drop(i, "the match is over"); continue; }
        if (now - p->t0 > (DWORD)g_window * 1000) { drop(i, "window over"); continue; }
        void *sq = squad_by_id(p->sq); if (!sq) { drop(i, "squad gone"); continue; }
        int st = squad_state(sq);
        if (st == ST_JUMP) {
            if (p->tries) logmsg("squad %lu: the jump went off %lu ms after retry %d", (unsigned long)p->sq, (unsigned long)(now - p->sentAt), p->tries);
            drop(i, p->tries ? "done (jumped after the retry)" : "the jump went off"); continue;
        }
        if (st == ST_ABILITY) {
            if (p->tries && !p->abSeen) { p->abSeen = 1; logmsg("squad %lu: retry %d taken (ability again) %lu ms after it was sent", (unsigned long)p->sq, p->tries, (unsigned long)(now - p->sentAt)); }
            p->armed = 1; p->idleAt = 0; p->landAt = 0; continue;
        }
        if (st != ST_IDLE) { char b[48]; wsprintfA(b, "busy with something else (state %d) - no retry", st); drop(i, b); continue; }
        if (!p->armed) {
            if (!p->tries) { if (now - p->ts > 3000) drop(i, "the order never started - no retry"); continue; }
            if (now - p->sentAt < (DWORD)g_take) continue;
            logmsg("squad %lu: retry %d not taken within %d ms (sent too early?) - again", (unsigned long)p->sq, p->tries, g_take);
            p->armed = 1; p->idleAt = now; p->landAt = 0;                  // falls through to "send again when it can walk"
        }
        if (!p->idleAt) { p->idleAt = now; p->landAt = 0; logmsg("squad %lu: fell out of the ability without jumping", (unsigned long)p->sq); }
        int rd = squad_ready(sq);
        if (rd && !p->landAt) p->landAt = now;
        if (!rd && now - p->idleAt < (DWORD)g_land) continue;              // still in the air after the hop
        if (now - p->idleAt < (DWORD)g_delay) continue;
        if (p->tries >= g_max) { drop(i, "no retries left"); continue; }
        p->tries++; p->armed = 0; p->abSeen = 0; p->ts = p->sentAt = now;
        logmsg("squad %lu: retry %d/%d to (%.1f, %.1f): %s %lu ms after falling out", (unsigned long)p->sq, p->tries, g_max, p->pos.x, p->pos.z,
               rd ? "landed" : "landing not seen, timeout", (unsigned long)(now - p->idleAt));
        p->idleAt = 0; p->landAt = 0;
        resend(p, sq);
    }
}

// ---- the HUD's ability orders
static void remember(void *self, void *owner, DWORD abil, void *target, const Vec3 *dir, int slot, int queued) {
    load_ini();                                                       // re-read: switching it off / tuning while the game runs
    if (!GetPrivateProfileIntW(L"JumpRetry", L"Enabled", 1, g_ini)) return;
    if (g_verbose) logmsg("ability order slot %02X ability %lu queued %d", slot, (unsigned long)abil, queued);
    if (queued || !owner || !target || p_otype(owner, NULL) != 1) return;            // 1 = a squad
    int tt = p_ttype(target, NULL); if (tt != 2) { if (g_verbose) logmsg("  target type %d (not a ground point)", tt); return; }
    if (!listed(abil)) return;
    void *sq = p_osq(owner, NULL); if (!sq) return;
    void *me = *(void **)((char *)self + 4), *own = *(void **)((char *)sq + 0x4C);
    if (!me || own != me) { if (g_verbose) logmsg("  not the local player's squad"); return; }
    static UINT_PTR timer;
    if (!timer) {
        HWND w = GetActiveWindow(); if (!w) w = GetForegroundWindow();
        timer = SetTimer(w, 0x4A52, 30, tick);
        logmsg("timer %s (window %p)", timer ? "on" : "FAILED", (void *)w);
        if (!timer) return;
    }
    DWORD id = (DWORD)p_sqid(sq, NULL); int k = -1;
    for (int i = 0; i < MAXP; i++) if (g_p[i].used && g_p[i].sq == id) k = i;
    for (int i = 0; i < MAXP && k < 0; i++) if (!g_p[i].used) k = i;
    if (k < 0) k = 0;
    Pend *p = &g_p[k]; memset(p, 0, sizeof *p);
    p->used = 1; p->slot = slot; p->sq = id; p->abil = abil; p->t0 = p->ts = GetTickCount(); p->proxy = self; p->world = *(void **)G_WORLD;
    p_tpos(target, NULL, &p->pos); if (dir) p->dir = *dir;
    logmsg("squad %lu: jump order to (%.1f, %.1f) remembered", (unsigned long)id, p->pos.x, p->pos.z);
}
static void __fastcall hk_tgt(void *self, void *edx, void *owner, DWORD abil, void *target, int queued) {
    o_tgt(self, edx, owner, abil, target, queued);                 // first: its WorldDoCommandSquad cancels an older retry
    if (!g_resend) remember(self, owner, abil, target, NULL, 0x38, queued & 0xFF);
}
static void __fastcall hk_mul(void *self, void *edx, void *owner, DWORD abil, void *targets, int queued) {
    o_mul(self, edx, owner, abil, targets, queued);
    if (!g_resend) remember(self, owner, abil, targets, NULL, 0x40, queued & 0xFF);   // the first of the targets
}
static void __fastcall hk_mdir(void *self, void *edx, void *owner, DWORD abil, void *targets, void *dir, int queued) {
    o_mdir(self, edx, owner, abil, targets, dir, queued);
    if (!g_resend) remember(self, owner, abil, targets, NULL, 0x3C, queued & 0xFF);
}
static void __fastcall hk_dir(void *self, void *edx, void *owner, DWORD abil, void *target, Vec3 *dir, int queued) {
    o_dir(self, edx, owner, abil, target, dir, queued);
    if (!g_resend) remember(self, owner, abil, target, dir, 0x34, queued & 0xFF);
}

// ---- install
static void *sim(const char *name) { HMODULE m = GetModuleHandleA("SimEngine.dll"); return m ? (void *)GetProcAddress(m, name) : NULL; }
static int put(DWORD at, DWORD expect, void *to, void **orig, const char *what) {
    DWORD *slot = (DWORD *)at;
    if (IsBadReadPtr(slot, 4)) { logmsg("%s: %08lX unreadable - not hooking", what, (unsigned long)at); return 0; }
    if (expect && *slot != expect) { logmsg("%s: %08lX holds %08lX, not %08lX (not 3.19?) - not hooking", what, (unsigned long)at, (unsigned long)*slot, (unsigned long)expect); return 0; }
    *orig = (void *)*slot;
    DWORD o; VirtualProtect(slot, 4, PAGE_READWRITE, &o); *slot = (DWORD)to; VirtualProtect(slot, 4, o, &o);
    return 1;
}
static int install(void) {
    p_otype = (I0_t) sim("?GetType@ConstOwnerPointer@@QBE?AW4OwnerType@1@XZ");
    p_osq   = (P0_t) sim("?GetSquad@ConstOwnerPointer@@QBEPBVSquad@@XZ");
    p_octor = (V1_t) sim("??0ConstOwnerPointer@@QAE@PBVSquad@@@Z");
    p_ttype = (I0_t) sim("?GetType@ConstTarget@@QBE?AW4TargetType@1@XZ");
    p_tpos  = (Pos_t)sim("?GetPosition@ConstTarget@@QBE?BVVector3f@Math@@XZ");
    p_tctor = (V1_t) sim("??0Target@@QAE@ABVVector3f@Math@@@Z");
    p_tdtor = (V0_t) sim("??1Target@@UAE@XZ");
    p_tdef  = (V0_t) sim("??0Target@@QAE@XZ");
    p_sqid  = (I0_t) sim("?GetID@Squad@@QBEKXZ");
    p_find  = (Find_t)sim("?find@?$ObservableContainer@PBVSquad@@$00@ast@@QBEPBQBVSquad@@ABQBV3@@Z");
    p_end   = (P0_t) sim("?end@?$ObservableContainer@PBVSquad@@$00@ast@@QBEPBQBVSquad@@XZ");
    p_inst  = (Inst_t)sim("?Instance@PropertyBagGroupManager@@SGAAV1@XZ");
    p_byname= (Name_t)sim("?GetGroup@PropertyBagGroupManager@@QAEPBVPropertyBagGroup@@PBD@Z");
    p_byid  = (Grp2_t)sim("?GetGroup@PropertyBagGroupManager@@QBEPBVPropertyBagGroup@@II@Z");
    if (!p_otype || !p_osq || !p_octor || !p_ttype || !p_tpos || !p_tctor || !p_tdtor || !p_tdef || !p_sqid || !p_find || !p_end || !p_inst || !p_byname || !p_byid) {
        logmsg("SimEngine.dll functions not found - not hooking"); return 0;
    }
    HMODULE sm = GetModuleHandleA("SimEngine.dll");
    void *wsq = sim("?WorldDoCommandSquad@@YGPAVWorldCommand@@K_NPBVPlayer@@PBVSquad@@@Z");
    void *wgr = sim("?WorldDoCommandSquads@@YGPAVWorldCommand@@K_NPBVPlayer@@ABVConstSquadGroupTemp@@@Z");
    void *wen = sim("?WorldDoCommandEntity@@YGPAVWorldCommand@@K_NPBVPlayer@@PBVEntity@@@Z");
    (void)sm;
    // the import slots first (they only watch): another add-on may have hooked them already - then chain to it
    if (!put(IAT_SQUAD, 0, hk_sq, (void **)&o_sq, "WorldDoCommandSquad import") ||
        !put(IAT_SQUADS, 0, hk_grp, (void **)&o_grp, "WorldDoCommandSquads import") ||
        !put(IAT_ENTITY, 0, hk_ent, (void **)&o_ent, "WorldDoCommandEntity import")) return 0;
    if ((void *)o_sq != wsq || (void *)o_grp != wgr || (void *)o_ent != wen)
        logmsg("note: an import slot was already hooked by another add-on (chained): %d%d%d", (void *)o_sq != wsq, (void *)o_grp != wgr, (void *)o_ent != wen);
    // both vftables that carry these methods: CommandProxy and EventGeneratorCommandProxy (the HUD's)
    static const DWORD vts[2] = { VT_PROXY, VT_EVPROXY };
    for (int i = 0; i < 2; i++) {
        DWORD v = vts[i]; void *x;
        if (!put(v + SLOT_TGT, ORIG_TGT, hk_tgt, i ? &x : (void **)&o_tgt, "ability order +0x38") ||
            !put(v + SLOT_DIR, ORIG_DIR, hk_dir, i ? &x : (void **)&o_dir, "ability order +0x34") ||
            !put(v + SLOT_MDIR, ORIG_MDIR, hk_mdir, i ? &x : (void **)&o_mdir, "ability order +0x3C") ||
            !put(v + SLOT_MULTI, ORIG_MULTI, hk_mul, i ? &x : (void **)&o_mul, "ability order +0x40")) return 0;
    }
    return 1;
}

static DWORD WINAPI worker(LPVOID _) {
    (void)_;
    // wait for the import table to be filled in (an early injection runs before the loader binds DOW2.exe's imports)
    for (int i = 0; i < 120; i++) {
        HMODULE m = GetModuleHandleA("SimEngine.dll");
        void *wsq = m ? (void *)GetProcAddress(m, "?WorldDoCommandSquad@@YGPAVWorldCommand@@K_NPBVPlayer@@PBVSquad@@@Z") : NULL;
        if (wsq && !IsBadReadPtr((void *)IAT_SQUAD, 4) && *(void **)IAT_SQUAD == wsq) break;
        Sleep(500);
    }
    logmsg("JumpRetry 2 start: retry a jump order lost on an obstacle hop, up to %d time(s) within %d s, on landing (+%d ms, timeout %d ms, re-sent if not taken in %d ms), %d abilit%s - %s",
           g_max, g_window, g_delay, g_land, g_take, g_nnames, g_nnames == 1 ? "y" : "ies", install() ? "installed" : "NOT installed");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0; else g_dir[0] = 0;
    _snwprintf(g_ini, MAX_PATH, L"%lsJumpRetry.ini", g_dir);
    // one copy per game process: it can come both from Elite_DLLs (launcher) and from dow2-patches\ (winmm loader)
    char mx[64]; wsprintfA(mx, "Local\\dow2patch_JumpRetry_%lu", GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, mx);
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("another JumpRetry copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"JumpRetry", L"Enabled", 1, g_ini)) { logmsg("disabled in JumpRetry.ini"); return TRUE; }
    load_ini();
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
