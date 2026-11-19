// RetreatNow.dll - "Instant retreat" (experiment, 19.11.2026). DoW2: Retribution 3.19.10320 (the same DOW2.exe for Elite and
// vanilla). A retreat order is carried out at once: the squad drops what it is doing and every model runs, no start hold.
//
// THIS CHANGES THE SIMULATION. It is deterministic (only simulation state is read: no clock, no local player, no UI), so two
// players who both have it compute the same game; a player (or observer) without it desyncs at the first retreat it changes.
// Replays recorded with it play correctly only with it, and replays recorded without it only without it.
//
// Four changes, each at the engine's own decision point (found 19.11.2026, Mac Feral build symbols + DOW2.exe 3.19):
//  0. Leave the current squad state now: the squad state processor (SimEngine.dll SquadStateProcessor::Update, export) handles
//     a normal order by RequestExit() on the active squad state and then waits - for many ticks - until that state ends by
//     itself (an ability until its action is over, any state while one of its models lies knocked back...). Its "immediate"
//     orders (processing type 2) ForceExit() the state on the same tick. For a retreat order (WorldCommand 0x3D, type 0) at the
//     front of a squad's queue, when the squad's Retreat state accepts it (SquadStateRetreat::AcceptCommand: can retreat, has a
//     retreat point) and the active state is idle / move / stop / combat / capture / ability / attack-move / patrol / melee,
//     the state first gets the engine's own RequestExit() (so it runs its exit request handling) and the order is marked
//     immediate (type 2): the processor ForceExit()s the state and enters Retreat on that tick. Any other state (load, unload,
//     hold-unload, weapon set-up, jump, construction, a downed hero, worship, an ongoing retreat) keeps the engine's way.
//  1. Ability: SquadStateAbility::UpdateAbility (0x009705A0) lets a squad leave an ability whose action already started only when
//     the ability bag says can_interrupt, or can_retreat_interrupt and a retreat (WorldCommand 0x3D) is queued. Many abilities have
//     both false (e.g. the ork Kommandos' burna bomb): the retreat order waited in the queue until the throw ended - 4.3 s in a
//     real game (fastbench 1aba1106..., tick 21208), the thrower even handed over on death. Both can_retreat_interrupt tests
//     (0x00970654 event "update", 0x009707F5 event "request exit": the bag+0x111C test) are made to pass, i.e. every ability
//     behaves as if it had can_retreat_interrupt = true - Relic's own path for the abilities that do (62 in the data).
//  2. Start hold: squad movement holds each of the 4 formation elements at the start of a move for secs_to_hold_at_move_start
//     (move type bag, converted to ticks: 1-4 ticks = 0.1-0.4 s in the games measured; times hold_at_move_start_modifier_when_
//     threatened, 0 for a squad under threat): SquadMovement::AssignRandomStartDelays (0x00A44F30, called once at 0x00A480EE
//     from SquadMovement::SetTarget, i.e. whenever the retreat (re)starts its move) writes 4 tick counts to SquadMovement
//     +0x434..+0x440, CalculateOrders (0x00A43C80) issues no orders to a holding element (all but the lead element). For a squad
//     whose active state is Retreat (id 10) the 4 holds are zeroed right after they are drawn (the random numbers are still
//     drawn: the random stream stays the same).
//  3. Melee: an entity in melee told to stop (StateMelee+0x80 = the stop order, type 1) finishes its swing first
//     (StateMelee attack state 0x0097C890: PauseMeleeWeaponsAfterSwing, then "+0x80 set and not all weapons paused -> stay").
//     For an entity whose squad is retreating that test (0x0097CA06) takes the engine's own "leave now" branch. Sync kills and
//     special attacks are other states of StateMelee and are not touched (RetreatNow 2 also ended a special attack early: the
//     model left melee but still stood until the attack's time was over - no gain, so version 3 is version 1's logic again).
//  4. Straight home: squad movement (SquadMovement::IssueOrders 0x00A449C0, called once per update at 0x00A4236C) sends the
//     squad leader to the target but every other model to its formation spot around the leader, so a scattered squad first
//     gathers on the leader and then moves as a block ("runs back and forth anywhere except to the base"). For a retreating
//     squad the call goes to our version: every model gets the order the leader gets in the engine's own direct branch -
//     the squad's target (SquadMovement+0x404), tolerance 1.0, the engine's highway size (0x009B3560) and the squad's move
//     type, issued through the engine's own per-entity move (0x00A41130 = WorldDoCommandEntity move) only when the engine's
//     own check (0x00A409C0) says the model is not already moving there (2) and can path there (not 0). Each model takes its
//     own path; a model the engine keeps from new orders (CombatExt lock, knocked down...) keeps it as before.
// What the engine does not let a unit drop (knockback, sync kill, jump in flight, a weapon team packing up, garrison/transport,
// a downed hero) stays as it is. Every patched place is checked byte for byte first; any other exe leaves the game untouched.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define VERSION   "4"
#define G_WORLD   0x013356A4u      // World*, +0x178 sim tick
#define CALL_HOLD 0x00A480EEu      // call 0x00A44F30
#define FN_HOLD   0x00A44F30u      // SquadMovement::AssignRandomStartDelays(SquadMovement*) stdcall
#define AB_UPD    0x00970654u      // cmp byte [edi+111Ch],0 / je 0097067A
#define AB_EXIT   0x009707F5u      // cmp byte [esi+111Ch],al / je 0097085C
#define ML_TEST   0x0097CA06u      // cmp byte [ebx+80h],0 / je 0097CA1A
#define ST_RETREAT 10
#define CALL_ISSUE 0x00A4236Cu     // call 0x00A449C0 (SquadMovement::IssueOrders, stdcall 1 arg) in SquadMovement::Update
#define FN_ISSUE   0x00A449C0u
#define FN_CHK     0x00A409C0u     // eax = entity, [esp+4] = Target*: 0 cannot path, 1 order needed, 2 already moving there; ret 4
#define FN_HWAY    0x009B3560u     // esi = entity: highway size; ret
#define FN_MOVE    0x00A41130u     // thiscall entity, (Target*, float tol, int highway, int movetype); ret 10h
// squad states that may be left at once for a retreat: 0 idle 1 move 2 stop 3 combat 4 capture 5 ability 12 attack-move 13 patrol 14 melee
#define IMM_STATES ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 12) | (1u << 13) | (1u << 14))

static char g_log[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = fopen(g_log, "a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
#ifdef RN_RESEARCH
int rn_from = 0;                   // research build: changes only from this sim tick on (RetreatNow.ini FromTick)
#else
int rn_from = -0x7FFFFFFF;
#endif
DWORD rn_world = G_WORLD;
volatile LONG rn_n_hold, rn_n_melee, rn_n_imm, rn_n_direct;

static int state_id(DWORD st) {    // BaseState vtable slot 1 = the state's id (mov eax,N / ret)
    if (!st) return -1;
    int (__attribute__((thiscall)) *f)(DWORD) = (void *)*(DWORD *)(*(DWORD *)st + 4);
    return f(st);
}
static int squad_retreating(DWORD sq) {
    if (!sq) return 0;
    DWORD ctl = *(DWORD *)(sq + 0x48);  // Squad::GetController
    return ctl && state_id(*(DWORD *)(ctl + 0x1E8)) == ST_RETREAT;   // SquadController::QIStateActive
}
static int now_on(void) { DWORD w = *(DWORD *)G_WORLD; return !w || *(int *)(w + 0x178) >= rn_from; }

// 0. retreat order -> leave the current squad state now. Hooked: SimEngine.dll SquadStateProcessor::Update(SquadController*)
//    (thiscall, ret 4; first 6 bytes moved into a trampoline). Read: SquadController +4 its WorldCommandQueue (+8 list head,
//    node +0 next / +8 WorldCommand*), +0x188 its states by id, +0x1E8 the active state; WorldCommand +0 type, +4 processing type.
static unsigned char g_tramp[16];
static void (__attribute__((thiscall)) *g_request_exit)(DWORD);           // BaseState::RequestExit (export)
static __attribute__((thiscall)) void sp_update(DWORD self, DWORD ctl) {
    if (ctl && now_on()) {
        DWORD q = *(DWORD *)(ctl + 4), head = q ? *(DWORD *)(q + 8) : 0, n = head ? *(DWORD *)head : 0;
        DWORD cmd = (n && n != head) ? *(DWORD *)(n + 8) : 0;
        if (cmd && *(int *)cmd == 0x3D && *(int *)(cmd + 4) == 0) {
            DWORD st = *(DWORD *)(ctl + 0x1E8), states = *(DWORD *)(ctl + 0x188);
            DWORD rs = states ? *(DWORD *)(states + 4 * ST_RETREAT) : 0;
            int id = state_id(st);
            if (st && rs && id >= 0 && id < 32 && (IMM_STATES >> id & 1)) {
                // the Retreat state's own AcceptCommand (vtable slot 6, thiscall, 1 arg): can this squad retreat with this order?
                char (__attribute__((thiscall)) *accept)(DWORD, DWORD) = (void *)*(DWORD *)(*(DWORD *)rs + 6 * 4);
                if (accept(rs, cmd)) {
                    if (!*(unsigned char *)(st + 4)) g_request_exit(st);  // what the processor does for a normal order
                    *(int *)(cmd + 4) = 2;                                 // ... and then the engine's immediate path: ForceExit
                    InterlockedIncrement(&rn_n_imm);
                }
            }
        }
    }
    ((void (__attribute__((thiscall)) *)(DWORD, DWORD))(void *)g_tramp)(self, ctl);
}
static DWORD hook_processor(void) {
    static const unsigned char O[6] = {0x53, 0x57, 0x8B, 0x7C, 0x24, 0x0C};   // push ebx / push edi / mov edi,[esp+0Ch]
    HMODULE se = GetModuleHandleA("SimEngine.dll"); if (!se) se = LoadLibraryA("SimEngine.dll");
    unsigned char *f = se ? (unsigned char *)GetProcAddress(se, "?Update@SquadStateProcessor@@QAEXPAVSquadController@@@Z") : 0;
    g_request_exit = se ? (void *)GetProcAddress(se, "?RequestExit@BaseState@@QAEXXZ") : 0;
    if (!f || !g_request_exit) { logmsg("NOT installed: SimEngine.dll exports not found"); return 0; }
    DWORD old;
    VirtualProtect(g_tramp, sizeof g_tramp, PAGE_EXECUTE_READWRITE, &old);
    if (f[0] == 0xE9) {                     // already hooked by another DLL: we run first, then its hook (then the game's code)
        DWORD tgt = (DWORD)f + 5 + *(LONG *)(f + 1);
        g_tramp[0] = 0xE9; *(LONG *)(g_tramp + 1) = (LONG)(tgt - ((DWORD)g_tramp + 5));
    } else if (!memcmp(f, O, 6)) {
        memcpy(g_tramp, f, 6); g_tramp[6] = 0xE9; *(LONG *)(g_tramp + 7) = (LONG)((DWORD)f + 6 - ((DWORD)g_tramp + 11));
    } else {
        char h[3 * 6 + 1]; for (int i = 0; i < 6; i++) sprintf(h + 3 * i, "%02X ", f[i]);
        logmsg("NOT installed: SquadStateProcessor::Update is not the expected code (%s)", h); return 0;
    }
    FlushInstructionCache(GetCurrentProcess(), g_tramp, sizeof g_tramp);
    return (DWORD)f;
}
static void put_processor_jmp(unsigned char *f) {
    DWORD old; VirtualProtect(f, 6, PAGE_EXECUTE_READWRITE, &old);
    f[0] = 0xE9; *(LONG *)(f + 1) = (LONG)((DWORD)sp_update - ((DWORD)f + 5)); f[5] = 0x90;   // 6th byte: moved, or another hook's nop
    VirtualProtect(f, 6, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 6);
}

// 4. straight home: our IssueOrders for a retreating squad
DWORD rn_fn_chk = FN_CHK, rn_fn_hway = FN_HWAY, rn_fn_move = FN_MOVE;
__asm__(".intel_syntax noprefix\n"
        // int __stdcall rn_chk(DWORD ent, DWORD target)
        ".globl _rn_chk@8\n_rn_chk@8:\n"
        "push ebx\npush esi\npush edi\npush ebp\n"
        "mov eax, dword ptr [esp + 20]\npush dword ptr [esp + 24]\ncall dword ptr [_rn_fn_chk]\n"
        "pop ebp\npop edi\npop esi\npop ebx\nret 8\n"
        // void __stdcall rn_move(DWORD ent, DWORD target, DWORD movetype): highway = 0x9B3560(esi = ent), then
        // 0xA41130(ecx = ent; target, 1.0f, highway, movetype) - the engine's own direct-branch call
        ".globl _rn_move@12\n_rn_move@12:\n"
        "push ebx\npush esi\npush edi\npush ebp\n"
        "mov esi, dword ptr [esp + 20]\n"
        "call dword ptr [_rn_fn_hway]\n"
        "push dword ptr [esp + 28]\n"          // movetype
        "push eax\n"                            // highway
        "push 0x3f800000\n"                     // 1.0f
        "push dword ptr [esp + 36]\n"          // target
        "mov ecx, esi\ncall dword ptr [_rn_fn_move]\n"
        "pop ebp\npop edi\npop esi\npop ebx\nret 12\n"
        ".att_syntax prefix\n");
int __stdcall rn_chk(DWORD ent, DWORD target);
void __stdcall rn_move(DWORD ent, DWORD target, DWORD movetype);
static DWORD (__attribute__((thiscall)) *g_squad_qi)(DWORD, unsigned);       // SquadController::QI (export)
static DWORD *(__attribute__((thiscall)) *g_grp_begin)(DWORD), *(__attribute__((thiscall)) *g_grp_end)(DWORD);
static DWORD g_issue_next = FN_ISSUE;
static int rn_issue_direct(DWORD sm) {
    DWORD leader = *(DWORD *)(sm + 0x42C), target = sm + 0x404, sq = *(DWORD *)(sm + 0x424);
    DWORD ext = g_squad_qi(*(DWORD *)(sq + 0x48), 10);                        // SquadMoveTypeExt: the current move type
    int mt = (*(DWORD *)(ext + 0x1C) == *(DWORD *)(ext + 0x20)) ? *(int *)(ext + 0x18) : **(int **)(ext + 0x1C);
    int r = rn_chk(leader, target);
    if (r == 0) return 0;                                                       // as the engine's direct branch: cannot path
    if (r == 1) rn_move(leader, target, mt);
    DWORD grp = sm + 0x28C; DWORD *b = g_grp_begin(grp), *e = g_grp_end(grp);
    for (; b != e; b++) {
        DWORD ent = *b;
        if (*(DWORD *)(ent + 0x2C) == *(DWORD *)(leader + 0x2C)) continue;
        if (rn_chk(ent, target) == 1) { rn_move(ent, target, mt); InterlockedIncrement(&rn_n_direct); }
    }
    return 1;
}
static int __stdcall rn_issue_wrap(DWORD sm) {
    DWORD sq = *(DWORD *)(sm + 0x424);
    if (now_on() && sq && squad_retreating(sq) && *(DWORD *)(sm + 0x42C)) return rn_issue_direct(sm);
    return ((int (__stdcall *)(DWORD))g_issue_next)(sm) & 0xFF;
}

// 2. start hold
static DWORD g_hold_next = FN_HOLD;   // the call's target before us: the game's function, or another DLL's wrapper of it
static void __stdcall hold_wrap(DWORD sm) {
    ((void (__stdcall *)(DWORD))g_hold_next)(sm);
    if (!now_on() || !squad_retreating(*(DWORD *)(sm + 0x424))) return;   // SquadMovement+0x424 = its Squad
    int *d = (int *)(sm + 0x434);
    if (d[0] | d[1] | d[2] | d[3]) InterlockedIncrement(&rn_n_hold);
    d[0] = d[1] = d[2] = d[3] = 0;
}
// 3. melee: C side of the cave (all registers and flags saved by the cave)
int __stdcall rn_melee_leave(DWORD state) {
    if (!now_on()) return 0;
    DWORD ent = *(DWORD *)(state + 0x34); if (!ent) return 0;        // ModEntityState+0x34 = its Entity
    if (!squad_retreating(*(DWORD *)(ent + 0x38))) return 0;         // Entity+0x38 = its Squad
    InterlockedIncrement(&rn_n_melee); return 1;
}
DWORD rn_ab1_ok = 0x0097065D, rn_ab1_no = 0x0097067A, rn_ab2_ok = 0x009707FD, rn_ab2_no = 0x0097085C;
DWORD rn_ml_leave = 0x0097CA1A, rn_ml_stay = 0x0097CA0F;
__asm__(".intel_syntax noprefix\n"
        // 1. ability, event "update": can_retreat_interrupt counts as set
        ".globl _rn_cave_ab1\n_rn_cave_ab1:\n"
        "push eax\nmov eax, dword ptr [_rn_world]\nmov eax, dword ptr [eax]\ntest eax, eax\njz 1f\n"
        "mov eax, dword ptr [eax + 0x178]\ncmp eax, dword ptr [_rn_from]\njl 2f\n"
        "1:\npop eax\njmp dword ptr [_rn_ab1_ok]\n"
        "2:\npop eax\ncmp byte ptr [edi + 0x111c], 0\nje 3f\njmp dword ptr [_rn_ab1_ok]\n3:\njmp dword ptr [_rn_ab1_no]\n"
        // 1. ability, event "request exit"
        ".globl _rn_cave_ab2\n_rn_cave_ab2:\n"
        "push eax\nmov eax, dword ptr [_rn_world]\nmov eax, dword ptr [eax]\ntest eax, eax\njz 1f\n"
        "mov eax, dword ptr [eax + 0x178]\ncmp eax, dword ptr [_rn_from]\njl 2f\n"
        "1:\npop eax\njmp dword ptr [_rn_ab2_ok]\n"
        "2:\npop eax\ncmp byte ptr [esi + 0x111c], al\nje 3f\njmp dword ptr [_rn_ab2_ok]\n3:\njmp dword ptr [_rn_ab2_no]\n"
        // 3. melee: the stop-order test of the attack state
        ".globl _rn_cave_ml\n_rn_cave_ml:\n"
        "cmp byte ptr [ebx + 0x80], 0\nje 1f\n"
        "pushad\npushfd\npush ebx\ncall _rn_melee_leave@4\ntest eax, eax\njnz 2f\npopfd\npopad\n"
        "jmp dword ptr [_rn_ml_stay]\n"
        "2:\npopfd\npopad\n"
        "1:\njmp dword ptr [_rn_ml_leave]\n"
        ".att_syntax prefix\n");
void rn_cave_ab1(void); void rn_cave_ab2(void); void rn_cave_ml(void);

static int same(DWORD at, const unsigned char *want, int n, const char *what) {
    if (IsBadReadPtr((void *)at, n)) { logmsg("NOT installed: no code at %08lX (%s) - not DOW2.exe 3.19?", at, what); return 0; }
    if (memcmp((void *)at, want, n)) {
        char h[3 * 16 + 1]; for (int i = 0; i < n && i < 16; i++) sprintf(h + 3 * i, "%02X ", ((unsigned char *)at)[i]);
        logmsg("NOT installed: DOW2.exe is not 3.19.10320 or this code is already changed (%s at %08lX: %s)", what, at, h);
        return 0;
    }
    return 1;
}
static void put_jmp(DWORD at, int n, void *to) {
    DWORD old; unsigned char *t = (unsigned char *)at; VirtualProtect(t, n, PAGE_EXECUTE_READWRITE, &old);
    t[0] = 0xE9; *(LONG *)(t + 1) = (LONG)((DWORD)to - (at + 5)); for (int i = 5; i < n; i++) t[i] = 0x90;
    VirtualProtect(t, n, old, &old); FlushInstructionCache(GetCurrentProcess(), t, n);
}
static int install(void) {
    static const unsigned char W_AB1[9] = {0x80, 0xBF, 0x1C, 0x11, 0x00, 0x00, 0x00, 0x74, 0x1D};
    static const unsigned char W_AB2[8] = {0x38, 0x86, 0x1C, 0x11, 0x00, 0x00, 0x74, 0x5F};
    static const unsigned char W_ML[9] = {0x80, 0xBB, 0x80, 0x00, 0x00, 0x00, 0x00, 0x74, 0x0B};
    unsigned char *c = (unsigned char *)CALL_HOLD;
    // all or nothing: every place is checked before anything is written
    if (!same(AB_UPD, W_AB1, 9, "ability update test") || !same(AB_EXIT, W_AB2, 8, "ability exit test") ||
        !same(ML_TEST, W_ML, 9, "melee stop test")) return 0;
    if (IsBadReadPtr(c, 5) || c[0] != 0xE8) { logmsg("NOT installed: no call at %08X (start hold)", CALL_HOLD); return 0; }
    DWORD tgt = CALL_HOLD + 5 + *(LONG *)(c + 1);
    if (tgt != FN_HOLD && tgt >= 0x00400000u && tgt < 0x01400000u) { logmsg("NOT installed: the start hold call goes to %08lX", tgt); return 0; }
    unsigned char *ci = (unsigned char *)CALL_ISSUE;
    if (IsBadReadPtr(ci, 5) || ci[0] != 0xE8) { logmsg("NOT installed: no call at %08X (issue orders)", CALL_ISSUE); return 0; }
    DWORD itgt = CALL_ISSUE + 5 + *(LONG *)(ci + 1);
    if (itgt != FN_ISSUE && itgt >= 0x00400000u && itgt < 0x01400000u) { logmsg("NOT installed: the issue-orders call goes to %08lX", itgt); return 0; }
    HMODULE se0 = GetModuleHandleA("SimEngine.dll"); if (!se0) se0 = LoadLibraryA("SimEngine.dll");
    g_squad_qi = se0 ? (void *)GetProcAddress(se0, "?QI@SquadController@@QAEPAVSquadExtension@@I@Z") : 0;
    g_grp_begin = se0 ? (void *)GetProcAddress(se0, "?begin@EntityGroupTemp@@QAEPAPAVEntity@@XZ") : 0;
    g_grp_end = se0 ? (void *)GetProcAddress(se0, "?end@EntityGroupTemp@@QAEPAPAVEntity@@XZ") : 0;
    if (!g_squad_qi || !g_grp_begin || !g_grp_end) { logmsg("NOT installed: SimEngine.dll exports (QI, begin, end) not found"); return 0; }
    static const unsigned char W_CHK[6] = {0x83, 0xEC, 0x08, 0x8B, 0x48, 0x3C}, W_HW[4] = {0x8B, 0x46, 0x38, 0x83}, W_MV[6] = {0x64, 0xA1, 0x00, 0x00, 0x00, 0x00};
    if (!same(FN_CHK, W_CHK, 6, "move check") || !same(FN_HWAY, W_HW, 4, "highway size") || !same(FN_MOVE, W_MV, 6, "entity move")) return 0;
    DWORD proc = hook_processor(); if (!proc) return 0;
    g_issue_next = itgt;
    g_hold_next = tgt;     // the game's function, or (outside DOW2.exe) another DLL's wrapper of it: we call that one
    put_processor_jmp((unsigned char *)proc);
    { DWORD o2; VirtualProtect(ci, 5, PAGE_EXECUTE_READWRITE, &o2); *(LONG *)(ci + 1) = (LONG)((DWORD)rn_issue_wrap - (CALL_ISSUE + 5));
      VirtualProtect(ci, 5, o2, &o2); FlushInstructionCache(GetCurrentProcess(), ci, 5); }
    put_jmp(AB_UPD, 9, (void *)rn_cave_ab1);
    put_jmp(AB_EXIT, 8, (void *)rn_cave_ab2);
    put_jmp(ML_TEST, 9, (void *)rn_cave_ml);
    DWORD old; VirtualProtect(c, 5, PAGE_EXECUTE_READWRITE, &old);
    *(LONG *)(c + 1) = (LONG)((DWORD)hold_wrap - (CALL_HOLD + 5));
    VirtualProtect(c, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), c, 5);
    return 1;
}
// counts go to the log from a side thread, never from the game's thread
static DWORD WINAPI watch(LPVOID _) {
    (void)_; LONG h = 0, m = 0, i = 0, d = 0;
    for (;;) {
        Sleep(30000);
        if (rn_n_hold != h || rn_n_melee != m || rn_n_imm != i || rn_n_direct != d) {
            h = rn_n_hold; m = rn_n_melee; i = rn_n_imm; d = rn_n_direct;
            logmsg("so far in this game process: squad states left at once %ld, start holds dropped %ld, melee left at once %ld, "
                   "models sent straight home %ld", i, h, m, d);
        }
    }
    return 0;
}
static int already_loaded(const char *name) {     // one copy per game process (Elite_DLLs and dow2-patches\ both possible)
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameA(h, g_log, MAX_PATH); char *s = strrchr(g_log, '\\'); if (s) s[1] = 0; else g_log[0] = 0;
#ifdef RN_RESEARCH
    char ini[MAX_PATH]; strcpy(ini, g_log); strcat(ini, "RetreatNow.ini");
    rn_from = GetPrivateProfileIntA("RetreatNow", "FromTick", 0, ini);
#endif
    strcat(g_log, "RetreatNow.log");
    if (already_loaded("RetreatNow")) { logmsg("another RetreatNow copy is already active in this game, this one stays idle"); return TRUE; }
    if (!install()) return TRUE;
#ifdef RN_RESEARCH
    logmsg("RetreatNow " VERSION " RESEARCH build: installed, active from sim tick %d", rn_from);
#else
    logmsg("RetreatNow " VERSION ": installed - a retreat order is carried out at once (the squad state is left on the order's tick, "
           "abilities interrupted, no start hold, melee left without finishing the swing, every model straight home). Changes the simulation: every player of the match needs it, or the match desyncs");
#endif
    HANDLE w = CreateThread(NULL, 0, watch, NULL, 0, NULL); if (w) CloseHandle(w);
    return TRUE;
}
