// RetreatNow.dylib - "Instant retreat" for the native Mac game (Feral 1.0.3, x86_64). The same simulation change as the Windows
// RetreatNow.dll (retreatnow/retreatnow.c, version 4) - the same decisions on the same ticks, so a Mac player and a Windows
// player who both have it stay in sync (crossplay_fix makes the rest of the Mac simulation compute like Windows).
//
// THIS CHANGES THE SIMULATION: every player and observer of a match needs it (Windows: RetreatNow.dll 4), otherwise the match
// desyncs at the first retreat it changes. Replays recorded with it play correctly only with it.
//
// Five changes, each at the engine's own decision point; Mac addresses (1.0.3 symbol map) next to the Windows ones:
//  0. Leave the squad state on the retreat order's tick: SquadStateProcessor::Update (Mac 0x102179800, its only call
//     0x10217755f; Windows SimEngine.dll export) RequestExit()s the active squad state on a normal order and waits for it to end.
//     A retreat order (WorldCommand +0 = 0x3D, +4 processing = 0) at the front of the squad's queue (SquadController +8 ->
//     libc++ list, front = sentinel +8, command at node +0x10), accepted by the squad's Retreat state (states +0x310, id 10;
//     its AcceptCommand = vtable +0x38), while the active state (+0x3D0, id = vtable +0x10) is one of 0-5, 12-14, gets the
//     engine's BaseState::RequestExit (0x102156000; exit-requested flag +8) and processing type 2: the engine ForceExit()s it
//     and enters Retreat on that tick.
//  1. Abilities: SquadStateAbility::UpdateAbility (Mac 0x101bfd310 / Windows 0x009705A0): both can_retreat_interrupt tests
//     (bag +0x11F8: je at 0x101bfd3d4 and 0x101bfd52b) always pass (Windows: 0x00970654, 0x009707F5).
//  2. Start hold: SquadMovement::SetTarget tail-jumps (0x101d73746) to AssignRandomStartDelays (0x101d73bb0 / Windows
//     0x00A44F30, called at 0x00A480EE); for a retreating squad (SquadMovement +0x758) the 4 holds (+0x778..+0x784) are zeroed
//     after the draw.
//  3. Melee: StateMelee::AttackState (0x101bc7f80 / Windows 0x0097C890): the "finish the swing after a stop" test (+0x11C at
//     0x101bc821e / Windows +0x80 at 0x0097CA06) takes the leave-now branch (0x101bc8238) for an entity (+0x70) of a retreating
//     squad (entity +0x70).
//  4. Straight home: SquadMovement::IssueOrders (0x101d75730, its only call 0x101d744fe / Windows 0x00A449C0 at 0x00A4236C):
//     for a retreating squad every model gets the order the engine's own direct branch gives the leader - the squad's target
//     (SquadMovement +0x700), tolerance 1.0, FormationUtil::CalculateHighwaySize, the squad's move type, through the engine's
//     per-entity move (0x101d79380 / Windows 0x00A41130) - unless the engine's check says it already moves there (its StateMove:
//     +0x178 == 0, not stale +0x1D7, +0x1C4 == 1.0, target type +0x88 >= 2, same target +0x78, active state Move) or cannot path
//     there (exit state 2..4: the leader -> IssueOrders returns false as the engine's; another model -> no order).
// Code hooks go through a page allocated next to the game image (rel32 reach) holding jmp [rip] thunks into this dylib.
// RetreatNow.ini [RetreatNow] Enabled=0 turns it off - but then the game desyncs against players who have it: leave it at 1.
#include "../common/machook.h"

#define RN_VERSION 4
#define CALL_PROC  0x10217755fUL   // call SquadStateProcessor::Update
#define FN_PROC    0x102179800UL
#define FN_REQEXIT 0x102156000UL   // BaseState::RequestExit
#define AB_JE1     0x101bfd3d4UL   // je rel8 after cmp byte [r14+0x11f8],0 (event update)
#define AB_JE2     0x101bfd52bUL   // je rel32 after cmp byte [r14+0x11f8],0 (event request exit)
#define JMP_HOLD   0x101d73746UL   // jmp AssignRandomStartDelays (end of SquadMovement::SetTarget)
#define FN_HOLD    0x101d73bb0UL
#define ML_TEST    0x101bc821eUL   // cmp byte [r15+0x11c],0 / je 0x101bc8238 (10 bytes)
#define ML_STAY    0x101bc8228UL
#define ML_LEAVE   0x101bc8238UL
#define CALL_ISSUE 0x101d744feUL   // call SquadMovement::IssueOrders
#define FN_ISSUE   0x101d75730UL
#define FN_STALE   0x101bcd400UL   // StateMove::IsMoveOrderStale
#define FN_EXITST  0x101bccc40UL   // StateMove::GetExitState
#define FN_CANT    0x101bcaf70UL   // StateMove::IsCantPathToTargetError
#define FN_TGTEQ   0x10217bf10UL   // operator==(Target const&, Target const&)
#define FN_HWAY    0x101d190d0UL   // FormationUtil::CalculateHighwaySize(Entity const&)
#define FN_MOVE    0x101d79380UL   // SquadMovement per-entity move (entity, Target*, highway, movetype, xmm0 tolerance)
#define FN_MOVETP  0x101befdb0UL   // SquadMoveTypeExt::GetCurrentMoveType
#define ST_RETREAT 10
#define IMM_STATES ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 12) | (1u << 13) | (1u << 14))

typedef unsigned char u8;
static volatile long n_imm, n_hold, n_melee, n_direct;
static int state_id(void *st) { if (!st) return -1; int (*f)(void *) = *(int (**)(void *))(*(uintptr_t *)st + 0x10); return f(st); }
static int squad_retreating(uintptr_t sq) {
    if (!sq) return 0;
    uintptr_t ctl = *(uintptr_t *)(sq + 0x78);
    return ctl && state_id(*(void **)(ctl + 0x3D0)) == ST_RETREAT;
}

// 0. retreat order -> leave the squad state now
static void (*g_proc_next)(void *, void *);
static void (*g_reqexit)(void *);
static void rn_sp_update(void *self, void *ctlp) {
    uintptr_t ctl = (uintptr_t)ctlp;
    if (ctl) {
        uintptr_t q = *(uintptr_t *)(ctl + 8), node = q ? *(uintptr_t *)(q + 8) : 0;
        uintptr_t cmd = (node && node != q) ? *(uintptr_t *)(node + 0x10) : 0;
        if (cmd && *(int *)cmd == 0x3D && *(int *)(cmd + 4) == 0) {
            void *st = *(void **)(ctl + 0x3D0); uintptr_t states = *(uintptr_t *)(ctl + 0x310);
            void *rs = states ? *(void **)(states + 8 * ST_RETREAT) : 0;
            int id = state_id(st);
            if (st && rs && id >= 0 && id < 32 && (IMM_STATES >> id & 1)) {
                _Bool (*accept)(void *, void *) = *(_Bool (**)(void *, void *))(*(uintptr_t *)rs + 0x38);
                if (accept(rs, (void *)cmd)) {
                    if (!*(u8 *)((uintptr_t)st + 8)) g_reqexit(st);
                    *(int *)(cmd + 4) = 2;
                    __sync_fetch_and_add(&n_imm, 1);
                }
            }
        }
    }
    g_proc_next(self, ctlp);
}
// 2. start hold
static void (*g_hold_next)(void *);
static void rn_hold(void *smp) {
    g_hold_next(smp);
    uintptr_t sm = (uintptr_t)smp;
    if (!squad_retreating(*(uintptr_t *)(sm + 0x758))) return;
    int *d = (int *)(sm + 0x778);
    if (d[0] | d[1] | d[2] | d[3]) __sync_fetch_and_add(&n_hold, 1);
    d[0] = d[1] = d[2] = d[3] = 0;
}
// 3. melee: C side of the cave
__attribute__((used)) int rn_melee_leave(uintptr_t state) {
    uintptr_t ent = *(uintptr_t *)(state + 0x70); if (!ent) return 0;
    if (!squad_retreating(*(uintptr_t *)(ent + 0x70))) return 0;
    __sync_fetch_and_add(&n_melee, 1); return 1;
}
__attribute__((used)) uint64_t rn_ml_stay, rn_ml_leave;
__asm__(".text\n.globl _rn_cave_ml\n_rn_cave_ml:\n"
        "cmpb $0, 0x11c(%r15)\nje 1f\n"
        "pushq %rax\npushq %rcx\npushq %rdx\npushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\n"
        "subq $8, %rsp\nmovq %r15, %rdi\ncall _rn_melee_leave\nleaq 8(%rsp), %rsp\ntestl %eax, %eax\n"   // lea: flags kept
        "popq %r11\npopq %r10\npopq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rdx\npopq %rcx\npopq %rax\n"     // pop: flags kept
        "jnz 1f\njmpq *_rn_ml_stay(%rip)\n"
        "1:\njmpq *_rn_ml_leave(%rip)\n");
void rn_cave_ml(void);
// 4. straight home
static int (*g_issue_next)(void *);
static _Bool (*f_stale)(void *); static int (*f_exitst)(void *); static _Bool (*f_cant)(int); static _Bool (*f_tgteq)(void *, void *);
static int (*f_hway)(void *); static void (*f_move)(void *, void *, int, int, float); static int (*f_movetp)(void *);
static int rn_chk(uintptr_t ent, void *target) {             // the engine's own check (Windows 0x00A409C0, Mac inline)
    uintptr_t ctl = *(uintptr_t *)(ent + 0x78);
    uintptr_t s = *(uintptr_t *)(*(uintptr_t *)(ctl + 0x578) + 0x18);
    if (s && !*(u8 *)(s + 0x31)) s = 0;
    if (!s) return 1;
    void *active = *(void **)(ctl + 0x620);
    _Bool stale = f_stale((void *)s); float f178 = *(float *)(s + 0x178), f1c4 = *(float *)(s + 0x1C4); int ex = f_exitst((void *)s);
    if (f178 == 0.0f && !stale && f1c4 == 1.0f && *(uint32_t *)(s + 0x88) >= 2 && f_tgteq((void *)(s + 0x78), target)) {
        if (f_cant(ex)) return 0;
        return state_id(active) == 3 ? 2 : 1;
    }
    return 1;
}
static int rn_issue_direct(uintptr_t sm) {
    uintptr_t sq = *(uintptr_t *)(sm + 0x758), leader = *(uintptr_t *)(sm + 0x768); void *target = (void *)(sm + 0x700);
    uintptr_t x = *(uintptr_t *)(*(uintptr_t *)(sq + 0x78) + 0x30), ext = *(uintptr_t *)(x + 0xA8);
    if (!*(u8 *)(x + 0xA0)) ext = 0;
    int mt = f_movetp((void *)ext);
    int r = rn_chk(leader, target);
    if (r == 0) return 0;
    if (r == 1) f_move((void *)leader, target, f_hway((void *)leader), mt, 1.0f);
    int64_t vb = *(int64_t *)(*(uintptr_t *)(sm + 0x498) - 0x18);
    uintptr_t *b = *(uintptr_t **)(sm + vb + 0x4F0); uint64_t n = *(uint64_t *)(sm + vb + 0x4F8);
    for (uint64_t i = 0; i < n; i++) {
        uintptr_t ent = b[i];
        if (*(int *)(ent + 0x58) == *(int *)(leader + 0x58)) continue;
        if (rn_chk(ent, target) == 1) { f_move((void *)ent, target, f_hway((void *)ent), mt, 1.0f); __sync_fetch_and_add(&n_direct, 1); }
    }
    return 1;
}
static int rn_issue(void *smp) {
    uintptr_t sm = (uintptr_t)smp, sq = *(uintptr_t *)(sm + 0x758);
    if (sq && squad_retreating(sq) && *(uintptr_t *)(sm + 0x768)) return rn_issue_direct(sm);
    return g_issue_next(smp) & 0xFF;
}

// a page within rel32 reach of the game image: jmp [rip] thunks into this dylib
static uint8_t *near_page(void) {
    vm_address_t base = (MH_IMAGE_HI + mh_slide + 0xFFFFF) & ~(vm_address_t)0xFFFFF;
    for (int i = 0; i < 1024; i++) {
        vm_address_t a = base + (vm_address_t)i * 0x100000;
        if (vm_allocate(mach_task_self(), &a, vm_page_size, VM_FLAGS_FIXED) == KERN_SUCCESS) return (uint8_t *)a;
    }
    return 0;
}
static uintptr_t thunk(uint8_t *page, int k, void *to) {
    uint8_t *t = page + 16 * k; t[0] = 0xFF; t[1] = 0x25; memset(t + 2, 0, 4); uint64_t a = (uint64_t)(uintptr_t)to; memcpy(t + 6, &a, 8);
    return (uintptr_t)t;
}
static int rel_to(uintptr_t at_unslid, int len, uintptr_t dst, uint8_t *out) {   // e8/e9 at at: its rel32 to dst
    int64_t d = (int64_t)dst - (int64_t)(at_unslid + mh_slide + len);
    if (d > 0x7FFFFFFFLL || d < -0x80000000LL) return 0;
    int32_t r = (int32_t)d; memcpy(out, &r, 4); return 1;
}
static uintptr_t rel_target(uintptr_t at_unslid) { int32_t r; memcpy(&r, mh_at(at_unslid + 1), 4); return at_unslid + mh_slide + 5 + r; }

__attribute__((constructor)) static void retreatnow_init(void) {
    if (!mh_start("RetreatNow", RN_VERSION)) return;
    char h[64];
    static const uint8_t W_AB1[10] = { 0x41, 0x80, 0xBE, 0xF8, 0x11, 0x00, 0x00, 0x00, 0x74, 0x26 };
    static const uint8_t W_AB2[14] = { 0x41, 0x80, 0xBE, 0xF8, 0x11, 0x00, 0x00, 0x00, 0x0F, 0x84, 0xF8, 0x00, 0x00, 0x00 };
    static const uint8_t W_ML[10]  = { 0x41, 0x80, 0xBF, 0x1C, 0x01, 0x00, 0x00, 0x00, 0x74, 0x10 };
    static const uint8_t W_STALE[8] = { 0x55, 0x48, 0x89, 0xE5, 0x8A, 0x87, 0xD7, 0x01 };
    static const uint8_t W_CANT[8] = { 0x55, 0x48, 0x89, 0xE5, 0x83, 0xC7, 0xFE, 0x83 };
    uintptr_t g = mh_slide;
    // all or nothing: every place checked first. The calls / jmp may already go to another dylib (chained), not elsewhere in the game.
    int ok = mh_same(AB_JE1 - 8, W_AB1, 10) && mh_same(AB_JE2 - 8, W_AB2, 14) && mh_same(ML_TEST, W_ML, 10) &&
             mh_same(FN_STALE, W_STALE, 8) && mh_same(FN_CANT, W_CANT, 8) &&
             *(u8 *)mh_at(CALL_PROC) == 0xE8 && *(u8 *)mh_at(JMP_HOLD) == 0xE9 && *(u8 *)mh_at(CALL_ISSUE) == 0xE8;
    if (!ok) { mh_hex(h, mh_at(ML_TEST), 10); mh_log("NOT installed: the game code is not where 1.0.3 has it or is already changed (melee site %s)", h); return; }
    uintptr_t tp = rel_target(CALL_PROC), th = rel_target(JMP_HOLD), ti = rel_target(CALL_ISSUE);
    int in_game_p = tp >= MH_IMAGE_LO + g && tp < MH_IMAGE_HI + g, in_game_h = th >= MH_IMAGE_LO + g && th < MH_IMAGE_HI + g,
        in_game_i = ti >= MH_IMAGE_LO + g && ti < MH_IMAGE_HI + g;
    if ((in_game_p && tp != FN_PROC + g) || (in_game_h && th != FN_HOLD + g) || (in_game_i && ti != FN_ISSUE + g)) {
        mh_log("NOT installed: a call/jmp to patch goes somewhere else in the game (%#lx %#lx %#lx)", (unsigned long)tp, (unsigned long)th, (unsigned long)ti);
        return;
    }
    uint8_t *pg = near_page();
    if (!pg) { mh_log("NOT installed: no page next to the game image for the jump thunks"); return; }
    g_proc_next = (void (*)(void *, void *))tp; g_hold_next = (void (*)(void *))th; g_issue_next = (int (*)(void *))ti;
    g_reqexit = (void (*)(void *))(FN_REQEXIT + g);
    f_stale = (void *)(FN_STALE + g); f_exitst = (void *)(FN_EXITST + g); f_cant = (void *)(FN_CANT + g); f_tgteq = (void *)(FN_TGTEQ + g);
    f_hway = (void *)(FN_HWAY + g); f_move = (void *)(FN_MOVE + g); f_movetp = (void *)(FN_MOVETP + g);
    rn_ml_stay = ML_STAY + g; rn_ml_leave = ML_LEAVE + g;
    uintptr_t t0 = thunk(pg, 0, (void *)rn_sp_update), t2 = thunk(pg, 1, (void *)rn_hold), t3 = thunk(pg, 2, (void *)rn_cave_ml),
              t4 = thunk(pg, 3, (void *)rn_issue);
    vm_protect(mach_task_self(), (vm_address_t)pg, vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    uint8_t r0[4], r2[4], r3[4], r4[4];
    if (!rel_to(CALL_PROC, 5, t0, r0) || !rel_to(JMP_HOLD, 5, t2, r2) || !rel_to(ML_TEST, 5, t3, r3) || !rel_to(CALL_ISSUE, 5, t4, r4)) {
        mh_log("NOT installed: the thunk page is out of rel32 reach"); return; }
    static const uint8_t NOP2[2] = { 0x90, 0x90 }, NOP6[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    uint8_t ml[10] = { 0xE9 }; memcpy(ml + 1, r3, 4); memset(ml + 5, 0x90, 5);
    int w = mh_write(mh_at(CALL_PROC + 1), r0, 4, MH_CODE) && mh_write(mh_at(AB_JE1), NOP2, 2, MH_CODE) &&
            mh_write(mh_at(AB_JE2), NOP6, 6, MH_CODE) && mh_write(mh_at(JMP_HOLD + 1), r2, 4, MH_CODE) &&
            mh_write(mh_at(ML_TEST), ml, 10, MH_CODE) && mh_write(mh_at(CALL_ISSUE + 1), r4, 4, MH_CODE);
    if (!w) { mh_log("vm_protect failed while patching - the game may be half patched: quit it and untick RetreatNow"); return; }
    mh_log("RetreatNow %d (Mac): installed - a retreat order is carried out at once (squad state left on the order's tick, abilities "
           "interrupted, no start hold, melee left without finishing the swing, every model straight home). Changes the simulation: "
           "every player of the match needs it (Windows: RetreatNow.dll 4), or the match desyncs. Thunks at %p", RN_VERSION, (void *)pg);
}
