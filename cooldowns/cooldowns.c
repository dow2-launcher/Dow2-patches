// cooldowns.c - seconds left on recharging abilities and global abilities, in the middle of the button over the game's
// own dark recharge sweep. Cooldowns.dll (cooldowns_main.c; the match state from matchtimer.c with MT_EMBED).
// Cooldowns 9 (05.11.2026): the number is taken off per HUD movie and button place, not per widget (see cd_show).
// cooldowns-3 (02.11.2026): the number is the button's own text in the game's own movie, no Direct3D (see "the number").
//
// How the game shows a recharge (found with a probe on 2026-10-29, DOW2.exe 3.19 + Elite 3.2):
//   the HUD buttons (ability_btn00..06 of the selection panel 0xB9AF80, global_abilities/global_btn00.. of 0xB5B850) are
//   button widgets (class size 0xED4); every frame the panel calls the button's SetProgress 0x688620 (usercall:
//   EAX = button, [esp+4] = float done 0..1), which turns it into frame 1..101 of "<button>/disabled/progress" (path at
//   button+0xDD0). Abilities: 0 = ready, otherwise the share of the recharge done (each frame 0 first, then the share);
//   global abilities: 1 - ticks left / recharge ticks, 1 = ready.
// We only read: a detour on SetProgress keeps the latest share of each button and the game time it came at (game ms of
// matchtimer.c, pauses not counted); the share grows evenly with game time, so two samples give the rate and the rate
// the seconds left.
// Nothing in the simulation is touched: no desync, the other players don't need it.
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define SETPROGRESS_ADDR 0x00688620
static const unsigned char SETPROGRESS_PRO[9] = {0x51, 0xF3, 0x0F, 0x10, 0x05, 0x98, 0xDF, 0x07, 0x01};   // push ecx; movss xmm0,[0x0107DF98]

DWORD mt_game_ms(void); int mt_ticking(void); float mt_ms_per_tick(void); long mt_ticks(void);
#include "../common/gfxui.h"

static wchar_t g_log[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---------- settings: Cooldowns.ini, [Cooldowns] ----------
static int g_on = 1, g_size = 26;                   // DigitSize: height of the digits in the button's own units (it is 64; the game's count is 16)
static int g_trace;                                  // (Trace=1, read in cd_start)
static DWORD g_color = 0xFFFFFF;                     // RRGGBB; white = the game's own colour of the count, left as it is

// ---------- per button: the latest share and the rate ----------
#define SLOTS 24                                     // 0..15 ability_btnNN, 16..23 global_btnNN
typedef struct {
    void *w;                                         // the button widget
    float p, p0; DWORD t, t0;                        // latest share at sim tick t; first share of this recharge at tick t0
    DWORD seen;                                      // GetTickCount() of the latest share in (0,1)
    int shown;                                       // seconds on the screen: within one recharge they only go down
    volatile int ready;                              // the latest value said ready (0 or 1), or the button is hidden
    int exact; DWORD exactLeft, exactAt;             // global abilities: game ms left read from the engine at game ms exactAt
    struct { void *mv; int shown; } ui[4];           // UI thread: per HUD movie, the seconds our count clip of this button shows there
    int ab; unsigned key;                            // the ability (the button update's +0x74) and the recharge (squad + ability) shown
} Slot;
static Slot g_slot[SLOTS];
static CRITICAL_SECTION g_cs;
static unsigned g_named;                             // slots already written to the log (bit per slot)
#define GONE_MS 100                                  // a button the game stopped updating (deselected, recharged) for this long is gone

static int slot_of(const char *path) {
    const char *a = strstr(path, "ability_btn"), *g = strstr(path, "global_btn");
    if (a && a[11] >= '0' && a[11] <= '9' && a[12] >= '0' && a[12] <= '9') { int n = (a[11] - '0') * 10 + a[12] - '0'; return n < 16 ? n : -1; }
    if (g && g[10] >= '0' && g[10] <= '9' && g[11] >= '0' && g[11] <= '9') { int n = (g[10] - '0') * 10 + g[11] - '0'; return n < 8 ? 16 + n : -1; }
    return -1;
}

static void cd_share(char *w, float p);
// One ability button update (0xB9AF80) calls SetProgress once per reason the ability can't be used: the share for a
// recharge (reasons 0xD / 0xE) and 0 for every other reason - so an ability recharging while its squad lacks the energy
// got its share and then a 0 in the same frame, and the number (and the game's own dark sweep) went (siritos 02.11).
// While that update runs the values are only collected (g_cap*); after it the largest share wins (see hk_btnupd).
static char *volatile g_capW;                        // the button whose update runs now (UI thread), NULL = none
static float g_capMax, g_capLast; static int g_capN;
void cd_progress(char *w, float p) __asm__("cd_progress");
void cd_progress(char *w, float p) {
    if (w && w == g_capW) {
        if (p > 0.0f && p < 0.999f && p > g_capMax) g_capMax = p;
        g_capLast = p;
        g_capN++;
        return;
    }
    cd_share(w, p);
}
static void cd_share(char *w, float p) {
    if (!g_on || !w || !mt_ticking()) return;
    const char *path = w + 0xDD0;
    if (IsBadReadPtr(path, 16) || path[0] != '/') return;
    int s = slot_of(path); if (s < 0) return;
    // ready (0 or 1): the number goes at once - after a selection change the button may hold another, ready ability.
    // The game sends 0 and then the share every frame of a recharge, so this only marks; the share clears it again.
    if (!(p > 0.0f) || p >= 0.999f) { g_slot[s].ready = 1; return; }
    g_slot[s].ready = 0;
    if (!(g_named & 1u << s)) { g_named |= 1u << s; logmsg("button %.80s -> slot %d (share %.3f)", path, s, p); }
    // sim ticks, not game ms: the share grows by the same step every tick, so one tick gives the exact rate (game ms are
    // the wall-clock gaps between ticks and made the first seconds jump by one)
    DWORD now = GetTickCount(), gt = (DWORD)mt_ticks();
    EnterCriticalSection(&g_cs);
    Slot *b = &g_slot[s];
    // a new recharge, another ability on this button (the selection changed) or a gap: start the rate again
    float expect = b->p + 0.05f;
    if (b->w != w || now - b->seen > 500 || p < b->p - 0.002f || (b->t != gt && p > expect && gt - b->t < 10)) {
        b->w = w; b->p0 = p; b->t0 = gt; b->shown = 0;
    }
    b->p = p; b->t = gt; b->seen = now;
    LeaveCriticalSection(&g_cs);
}

void *g_spTramp __asm__("cd_sp_tramp");
__attribute__((naked)) static void hk_setprogress(void) {
    __asm__ volatile(
        "pushal\n\t"
        "pushl 36(%esp)\n\t"                         // the share (float, [esp+4] at the call)
        "pushl %eax\n\t"                             // the button
        "call cd_progress\n\t"
        "addl $8, %esp\n\t"
        "popal\n\t"
        "jmp *cd_sp_tramp\n\t");
}

// ---------- the number: the button's own "count" clip, copied (cooldowns-3, 02.11.2026: no Direct3D) ----------
// Every ability and global ability button of selection_panel.gfx has a "count" clip with a text field "count_text" (the
// game's 16 px digits with their shadow, bottom right; the game uses it for charges). On the first number of a button we
// ask the movie itself (Receive_DuplicateMovieClip) for a copy of it, "mt_cd", inside the same button and over the rest
// of it, then move it to the middle and scale it (Receive_SetMovieClipPosition/Scale); its text field is centred
// (autoSize). The seconds go in with Receive_SetText, away with Receive_SetVisible - the same calls the game makes.
// A clip without mouse handlers takes no clicks: the button and its tooltip work as before. All of it from the button
// updates below, on the game's UI thread; widget + 8 is the button's path ("/ability_btn01",
// "/global_abilities/global_btn00"), the movie comes from its form (common/gfxui.h).
static int cd_seconds(Slot *b, DWORD now, DWORD gt) {       // seconds left to show, 0 = none (under g_cs)
    if (!b->w || b->ready || now - b->seen > GONE_MS) return 0;
    if (IsBadReadPtr((char *)b->w + 0x209, 1) || *((char *)b->w + 0x209) != 1) return 0;   // the button is not on the screen
    if (!b->exact && (b->t - b->t0 < 1 || b->p <= b->p0)) return 0;                        // the rate isn't known yet (one sim tick)
    float left;
    if (b->exact) left = (float)b->exactLeft - (float)(gt - b->exactAt);   // global ability: read, not guessed
    else {
        float rate = (b->p - b->p0) / (float)(b->t - b->t0);                 // share per sim tick
        // from the latest share on: ticks without one (the squad busy) count down too
        left = ((1.0f - b->p) / rate - (float)((DWORD)mt_ticks() - b->t)) * mt_ms_per_tick();
    }
    if (left < 0) left = 0;
    int sec = (int)((left + 999.0f) / 1000.0f);
    if (sec <= 0 || sec > 3600) return 0;
    if (!b->shown || sec < b->shown || sec > b->shown + 2) b->shown = sec;    // no jumping back and forth by a second
    return b->shown;
}
static void cd_show(int s, char *w) {
    Slot *b = &g_slot[s];
    DWORD now = GetTickCount();
    EnterCriticalSection(&g_cs);
    int sec = b->w == w && mt_ticking() ? cd_seconds(b, now, mt_game_ms()) : 0;
    LeaveCriticalSection(&g_cs);
    // Cooldowns 9 (siritos 05.11: a generator's number under construction, then the Techmarine selected - the frozen number on
    // his first ability; the same with the scouts' grenade and any unit with an ability there): our clip lives in the HUD movie
    // under the button's PATH, but it was taken off only by the button widget it was put on. After a selection change the
    // panel updates that place through another widget - "not ours", nothing taken off - and the number stayed. Now what is
    // shown is kept per movie and place (the slot), whatever widget updates it.
    int any = 0; for (int i = 0; i < 4; i++) if (b->ui[i].mv && b->ui[i].shown) any = 1;
    if (!sec && !any) return;                                  // nothing to show and nothing shown anywhere
    void *mv = gfx_movie_of_widget(w);
    const char *wp = w + 8;
    if (!mv || IsBadReadPtr(wp, 48) || wp[0] != '/' || !memchr(wp, 0, 48)) return;
    char dot[64], p[96];                                       // "/global_abilities/global_btn00" -> "global_abilities.global_btn00"
    snprintf(dot, sizeof dot, "%s", wp + 1);
    for (char *c = dot; *c; c++) if (*c == '/') *c = '.';
    int ui = -1;
    for (int i = 0; i < 4; i++) if (b->ui[i].mv == mv) ui = i;
    if (ui < 0) {                                              // nothing of ours in this movie yet
        if (!sec) return;
        for (int i = 0; i < 4 && ui < 0; i++) if (!b->ui[i].mv || !b->ui[i].shown) ui = i;
        if (ui < 0) ui = 0;                                    // (4 HUD movies with a number on this place: hardly - reuse the first)
        b->ui[ui].mv = mv; b->ui[ui].shown = 0;
    }
    int *shown = &b->ui[ui].shown;
    if (sec && *shown <= 0) {                                  // a number comes: the copy, if this movie has none (a new movie at an old address too)
        snprintf(p, sizeof p, "%s.mt_cd", dot);
        if (!gfx_exists(mv, p)) {
            char src[96]; snprintf(src, sizeof src, "%s/count", wp);
            GfxVal a[2] = { gv_str(src), gv_str("mt_cd") }, r; memset(&r, 0, sizeof r);
            gfx_invoke(mv, "Receive_DuplicateMovieClip", &r, a, 2);
            if (r.type != GV_BOOL || !r.v.b) { logmsg("%s: no count clip to copy - no number on this button", wp); b->ui[ui].mv = NULL; return; }
            // count_text sits in its clip at -24.5..0 x -17.5..7.9: its centre, scaled, onto the button's (64 x 64)
            double k = g_size / 16.0;
            GfxVal pa[3] = { gv_str(p), gv_num(32 + 12.2 * k), gv_num(32 + 4.8 * k) }, sa[3] = { gv_str(p), gv_num(k), gv_num(k) };
            gfx_invoke(mv, "Receive_SetMovieClipPosition", NULL, pa, 3);
            gfx_invoke(mv, "Receive_SetMovieClipScale", NULL, sa, 3);
            snprintf(p, sizeof p, "_root.%s.mt_cd.count_text.autoSize", dot); gfx_setvar(mv, p, gv_str("center"));
            if (g_color != 0xFFFFFF) {
                snprintf(p, sizeof p, "%s.mt_cd.count_text", dot);
                GfxVal ca[2] = { gv_str(p), gv_num(g_color) }; gfx_invoke(mv, "Receive_SetTextColor", NULL, ca, 2);
            }
            logmsg("%s: number in the button's own count clip (copied as mt_cd)", wp);
        }
    }
    if (sec == *shown) return;
    char t[16];
    if (sec < 100) snprintf(t, sizeof t, "%d", sec); else snprintf(t, sizeof t, "%d:%02d", sec / 60, sec % 60);
    if (sec) { snprintf(p, sizeof p, "%s.mt_cd.count_text", dot); gfx_set_text(mv, p, t); }
    // shown -1: from a match before (see cd_match_check) - not known whether the clip is visible: set it either way
    if (sec ? *shown <= 0 : *shown != 0) { snprintf(p, sizeof p, "%s.mt_cd", dot); gfx_set_visible(mv, p, sec != 0); }
    if (g_trace && (!sec || *shown <= 0)) logmsg("trace %.40s: number %s (%d -> %d) widget %p movie %p", wp, sec ? "ON" : "off", *shown, sec, (void *)w, mv);
    *shown = sec;
}

// The selection panel's ability button update (virtual, slot 0x0111AFBC = 0xB9AF80; this = the button's slot struct,
// +0x28 the button widget, +0x74 the ability). The panel works out the owner and the reasons the ability can't be used
// itself (0xB9DA80) and asks for the share done only for a recharge (reasons 0xD/0xE); that share reaches us through
// SetProgress above. We ask the engine nothing here: 0.9.0 asked vt+0xB4 for the share of every button with the owner
// cached in the button (+0x2A8) and crashed the game on abilities without a recharge (the cultists' prayer on W); asking
// vt+0xB0 with that owner crashed it too. After the update we take the number off a hidden or empty button and put the
// seconds into the button (cd_show).
#define BTNUPD_SLOT 0x0111AFBC
#define BTNUPD_FN   0x00B9AF80
typedef void (__fastcall *BtnUpd_t)(char *btn, void *edx, char *ctx);
static BtnUpd_t o_btnupd;
// The game's own SetProgress (usercall: EAX = the button, the share on the stack, ret 4 - as the game calls it at
// 0x6886E0), through our trampoline: the original code, not our hook
static void set_progress(char *w, float p) {
    void *fn = g_spTramp; if (!fn || !w) return;
    DWORD bits; memcpy(&bits, &p, 4);                 // the float as it lies on the stack
    __asm__ volatile("pushl %[p]\n\t call *%[f]\n\t"  // the callee takes it off (ret 4)
                     : "+a"(w) : [p] "g"(bits), [f] "c"(fn) : "edx", "memory", "cc");
}
// ---------- Cooldowns 6 (03.11.2026): every recharge kept per squad AND ability, asked from the engine ----------
// Cooldowns 4-5 kept the recharge per button number (ability_btn00..) and remembered its end per ability blueprint (+0x74,
// the same for every squad of a kind). siritos 03.11, Catachans (several abilities): one ability's recharge reset another's
// number, and with no energy the number went. Why: a button number holds another ability (or another squad's) after a
// selection change, so its shares mixed into the rate of the one before; and right after a cast the squad often lacks the
// energy - the game then sends only 0s (the reasons have no recharge, 0xD/0xE), so no share came at all.
// Now (as in LobbyStats 0.9.0, the version that counted right): after the panel's own update we ask the engine the same
// read-only question the panel asks for a recharge - iface vt+0xB4(owner, ability) = the share done - with the owner the
// panel has just worked out for this button (+0x2A8, ConstOwnerPointer) and its ability (+0x74), whatever the reasons.
// 0.9.0's crash was not this call (it was GetBounds declared __cdecl, fixed in 0.9.2); still, the call is guarded: a fault
// inside it is caught (vectored handler, this thread only), queries stop for the rest of the game, the log says where.
// The recharge is kept per (owner, ability) - its share, the tick it came, the start of its rate and the whole length -
// so other squads, other buttons and the selection going back and forth don't touch it.
typedef struct { unsigned key; int ab; float p, p0; DWORD t, t0; float total; DWORD used;
                 float pchg; DWORD tchg; int grown; } Rec;   // ticks; pchg/tchg: the share when it last moved, grown: seen moving
#define STALL_TICKS 15                               // Cooldowns 8: a share that stays put this long (1.5 s) is no running recharge
#define NREC 96
static Rec g_rec[NREC];
typedef struct { int ab; float total; } AbLen;      // the length of an ability's recharge in ticks, any owner: the
static AbLen g_abLen[48]; static int g_nabLen;       // first number of another squad of that kind comes at once
static float ab_len(int ab) { for (int i = 0; i < g_nabLen; i++) if (g_abLen[i].ab == ab) return g_abLen[i].total; return 0; }
static void ab_len_set(int ab, float total) {
    for (int i = 0; i < g_nabLen; i++) if (g_abLen[i].ab == ab) { g_abLen[i].total = total; return; }
    AbLen *a = &g_abLen[g_nabLen < 48 ? g_nabLen++ : (unsigned)ab % 48]; a->ab = ab; a->total = total;
}
// the ability and its owner. ConstOwnerPointer (SimEngine.dll) is { type, Entity *, Squad *, Player * }: its constructors set
// the type and the one pointer of that type only, the other words are whatever was on the stack (seen with Trace=1: the same
// squad came with other words in every second update), and its operator== compares the type's pointer only - so does the key.
// Cooldowns 1-7 hashed all 16 bytes: one recharge got a new key now and then, so a remembered recharge was lost or a fresh one
// started on the number of another (the number stuck or did not follow, siritos 04.11: "a generator under construction").
static unsigned rec_key(const char *btn, int ability) {
    const unsigned *o = (const unsigned *)(btn + 0x2A8);
    unsigned type = o[0], ptr = type == 0 ? o[1] : type == 1 ? o[2] : type == 2 ? o[3] : 0;
    unsigned key = (unsigned)ability * 2654435761u;
    key = (key ^ type) * 16777619u; key = (key ^ ptr) * 16777619u;
    return key | 1;
}
static Rec *rec_of(unsigned key, int ab, int make) {
    Rec *old = &g_rec[0];
    for (int i = 0; i < NREC; i++) {
        if (g_rec[i].key == key) { g_rec[i].used = GetTickCount(); return &g_rec[i]; }
        if (g_rec[i].used < old->used) old = &g_rec[i];
    }
    if (!make) return NULL;
    memset(old, 0, sizeof *old); old->key = key; old->ab = ab; old->used = GetTickCount();
    return old;
}
// a share p (0..1) of this recharge at sim tick gt; game = it is the game's own share of this update (reason 0xD/0xE), not
// only the engine's answer
static void rec_share(Rec *r, float p, DWORD gt, int game) {
    float tot = r->total > 0 ? r->total : ab_len(r->ab);
    int fresh = !r->t0 || !(r->p > 0) || p < r->p - 0.002f;
    // not where this recharge would be by now (it ended and was cast again while not selected, or got shorter): a new one
    if (!fresh && tot > 0 && gt > r->t) { float expect = r->p + (float)(gt - r->t) / tot; if (p > expect + 0.03f) fresh = 1; }
    if (fresh) { r->p0 = p; r->t0 = gt ? gt : 1; r->pchg = p; r->tchg = gt; r->grown = 0; }
    else if (p > r->pchg + 0.0001f) { r->pchg = p; r->tchg = gt; r->grown = 1; }
    if (game) r->grown = 1;
    if (gt > r->t0 && p > r->p0 + 0.0001f) { r->total = (float)(gt - r->t0) / (p - r->p0); ab_len_set(r->ab, r->total); }
    r->p = p; r->t = gt;
}
// Cooldowns 8 (siritos 04.11, the Techmarine's mines: "11" instead of a count down): a running recharge grows every sim tick.
// An answer of the engine that the game itself has not shown as a recharge and that has not moved yet, or a share that stays
// put for STALL_TICKS of samples, is something else - no number for it (before, the time left was worked out again from the same
// share at every sample, so the number stood still).
static int rec_running(const Rec *r) { return r->grown && r->t - r->tchg <= STALL_TICKS; }
static float rec_left(const Rec *r, DWORD gt) {        // ticks left, <= 0 = over or not known
    float tot = r->total > 0 ? r->total : ab_len(r->ab);
    if (!(r->p > 0) || tot <= 0 || !rec_running(r)) return 0;
    return (1.0f - r->p) * tot - (float)(gt - r->t);
}

// the guarded engine query: iface vt+0xB4 (__thiscall, float in st0)
static volatile DWORD g_qThread; static int g_qOff; static DWORD g_qFault; static void *g_qFaultAt;
DWORD g_qEsp __asm__("cd_q_esp"); DWORD g_qSeh __asm__("cd_q_seh"); unsigned short g_qCw __asm__("cd_q_cw");
float cd_q_call(void *fn, void *iface, void *owner, int ability) __asm__("cd_q_call");
void cd_q_fail(void) __asm__("cd_q_fail");
__asm__(".text\n"
    "cd_q_call:\n\t"
    "pushl %ebp\n\t pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t"
    "movl %fs:0, %eax\n\t movl %eax, cd_q_seh\n\t fnstcw cd_q_cw\n\t"
    "movl %esp, cd_q_esp\n\t"
    "pushl 32(%esp)\n\t"                             // ability (arg 4: esp+16+16)
    "pushl 32(%esp)\n\t"                             // owner   (arg 3, one push later)
    "movl 32(%esp), %ecx\n\t"                        // iface   (arg 2) = this
    "movl 28(%esp), %eax\n\t"                        // fn      (arg 1)
    "call *%eax\n\t"                                 // callee pops its 2 arguments; the share in st0
    "movl cd_q_esp, %esp\n\t"
    "popl %edi\n\t popl %esi\n\t popl %ebx\n\t popl %ebp\n\t ret\n"
    "cd_q_fail:\n\t"                                 // a fault inside: back here from the handler
    "movl cd_q_esp, %esp\n\t movl cd_q_seh, %eax\n\t movl %eax, %fs:0\n\t"
    "fninit\n\t fldcw cd_q_cw\n\t fld1\n\t fchs\n\t" // -1 = no answer (the game's FPU precision kept)
    "popl %edi\n\t popl %esi\n\t popl %ebx\n\t popl %ebp\n\t ret\n");
static LONG CALLBACK cd_veh(EXCEPTION_POINTERS *e) {
    if (!g_qThread || GetCurrentThreadId() != g_qThread) return EXCEPTION_CONTINUE_SEARCH;
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if (c != EXCEPTION_ACCESS_VIOLATION && c != EXCEPTION_ILLEGAL_INSTRUCTION && c != EXCEPTION_PRIV_INSTRUCTION
        && c != EXCEPTION_INT_DIVIDE_BY_ZERO && c != EXCEPTION_ARRAY_BOUNDS_EXCEEDED) return EXCEPTION_CONTINUE_SEARCH;
    g_qFault = c; g_qFaultAt = e->ExceptionRecord->ExceptionAddress; g_qThread = 0;
    e->ContextRecord->Eip = (DWORD)cd_q_fail;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static int g_query = 1;                              // Cooldowns.ini [Cooldowns] Ask=0: no engine query (shares only)
static int g_trace;                                  // Cooldowns.ini [Cooldowns] Trace=1: every change of a button's state in the log
typedef struct { char *w; unsigned sig; DWORD at; } TraceSeen;
static TraceSeen g_tr[SLOTS][4];                     // per place and button widget (05.11: two buttons update one place every frame)
static void trace_btn(int s, char *btn, char *w, int ability, float q, int en, int vis, float left, float sweep, const char *what) {
    if (!g_trace || s < 0 || s >= SLOTS) return;
    const unsigned *o = (const unsigned *)(btn + 0x2A8);
    unsigned own = o[0] == 0 ? o[1] : o[0] == 1 ? o[2] : o[0] == 2 ? o[3] : 0;   // the real owner (rec_key): the other words are stack left-overs
    unsigned sig = (unsigned)ability * 31u + o[0] * 7u + own * 13u + (unsigned)g_capN * 101u + (unsigned)(g_capMax * 100) * 1009u
        + (unsigned)((q + 2) * 100) * 10007u + (unsigned)en * 3u + (unsigned)vis * 5u + (unsigned)(left > 0.5f) * 17u + (unsigned)what[0];
    DWORD now = GetTickCount();
    TraceSeen *ts = NULL;
    for (int i = 0; i < 4; i++) if (g_tr[s][i].w == w) ts = &g_tr[s][i];
    if (!ts) { ts = &g_tr[s][0]; for (int i = 0; i < 4; i++) if (g_tr[s][i].at < ts->at) ts = &g_tr[s][i]; ts->w = w; ts->sig = 0;
               logmsg("trace slot %d: button widget %p (path %.40s) in movie %p - new on this place", s, (void *)w, w + 8, gfx_movie_of_widget(w)); }
    if (ts->sig == sig && now - ts->at < 2000) return;
    ts->sig = sig; ts->at = now;
    logmsg("trace %.40s w %p ab %08X owner %u:%08X en %d vis %d calls %d gameshare %.3f last %.3f engine %.3f left %.0f ticks sweep %.3f tick %ld: %s",
           w + 0xDD0, (void *)w, ability, o[0], own, en, vis, g_capN, g_capMax, g_capLast, q, left, sweep, mt_ticks(), what);
}
static float ask_vt(char *ctx, char *btn, int ability, int off) {   // the engine's answer of iface vt+off, -1 = none
    if (!g_query || g_qOff || !ctx || IsBadReadPtr(ctx + 0xC, 4)) return -1;
    void **iface = *(void ***)(ctx + 0xC);
    if (!iface || IsBadReadPtr(iface, 4) || IsBadReadPtr(*iface, 0xD0)) return -1;
    void *fn = (*(void ***)iface)[off / 4];
    static HMODULE exe; static MODULEINFO mi;
    if (!exe) { exe = GetModuleHandleA(NULL); GetModuleInformation(GetCurrentProcess(), exe, &mi, sizeof mi); }
    if ((DWORD)fn < (DWORD)mi.lpBaseOfDll || (DWORD)fn >= (DWORD)mi.lpBaseOfDll + mi.SizeOfImage) {
        g_qOff = 1; logmsg("engine query: vt+0x%X = %p is not in DOW2.exe - shares only", off, fn); return -1;
    }
    static unsigned said;
    if (!(said & 1u << (off / 4 % 32))) { said |= 1u << (off / 4 % 32); logmsg("engine query: iface %p, vtable %p, vt+0x%X = %p (%s)", (void *)iface, *iface, off, fn, "the share of a recharge"); }
    g_qThread = GetCurrentThreadId();
    float p = cd_q_call(fn, iface, btn + 0x2A8, ability);
    if (!g_qThread) {                                  // the handler took us out
        g_qOff = 1;
        logmsg("engine query FAULTED (%08lX at %p, ability %p): no more queries this game, shares only", g_qFault, g_qFaultAt, (void *)(UINT_PTR)ability);
        return -1;
    }
    g_qThread = 0;
    return p != p ? -1 : p;                            // NaN: none
}

// A new match (siritos 05.11: "after the game nothing of it must stay or matter"): everything of the match before goes. The keys
// of the recharges are squad / entity addresses and the HUD movies are addresses too - the next match gets the same addresses
// again, so a squad there took a recharge of the match before (ticks of that match) and a new movie at an old address was taken
// as one with our clip in it already (no number on that button for the whole match). The match core counts its sim ticks from
// 0 in every match: the ticks going back = a new match. What our clips show is not forgotten but marked unknown (-1): a HUD
// movie kept for the next match gets its clip set visible or not at its first update. Called on the UI thread (button updates).
static long g_lastTicks;
static void cd_match_check(void) {
    long t = mt_ticks();
    if (t + 10 < g_lastTicks) {
        EnterCriticalSection(&g_cs);
        int n = 0;
        for (int s = 0; s < SLOTS; s++) {
            Slot *b = &g_slot[s];
            for (int i = 0; i < 4; i++) { if (b->ui[i].mv && b->ui[i].shown) { b->ui[i].shown = -1; n++; } else b->ui[i].mv = NULL; }
            b->w = NULL; b->p = b->p0 = 0; b->t = b->t0 = b->seen = 0; b->shown = 0; b->ready = 1; b->exact = 0; b->ab = 0; b->key = 0;
        }
        memset(g_rec, 0, sizeof g_rec);
        memset(g_abLen, 0, sizeof g_abLen); g_nabLen = 0; memset(g_tr, 0, sizeof g_tr);
        LeaveCriticalSection(&g_cs);
        logmsg("a new match: the recharges of the match before forgotten (%d numbers to check in a kept HUD movie)", n);
    }
    g_lastTicks = t;
}
static void cd_trace_dump(void) {                    // Trace=1: every 2 s, where a number is up (by our bookkeeping)
    static DWORD last; DWORD now = GetTickCount();
    if (!g_trace || now - last < 2000) return;
    last = now;
    for (int s = 0; s < SLOTS; s++) for (int i = 0; i < 4; i++)
        if (g_slot[s].ui[i].mv && g_slot[s].ui[i].shown) logmsg("trace up: slot %d shows %d in movie %p (recharge widget %p, ready %d)", s, g_slot[s].ui[i].shown, g_slot[s].ui[i].mv, g_slot[s].w, g_slot[s].ready);
}
static void __fastcall hk_btnupd(char *btn, void *edx, char *ctx) {
    cd_match_check(); cd_trace_dump();
    char *w0 = btn ? *(char **)(btn + 0x28) : NULL;
    g_capMax = 0; g_capLast = -1; g_capN = 0; g_capW = g_on ? w0 : NULL;
    o_btnupd(btn, edx, ctx);
    g_capW = NULL;
    if (!g_on || !btn) return;
    char *w = *(char **)(btn + 0x28); int ability = *(int *)(btn + 0x74);
    if (!w) return;
    const char *path = w + 0xDD0;
    int s = !IsBadReadPtr(path, 16) && path[0] == '/' ? slot_of(path) : -1;
    int vis = *(w + 0x209) == 1;
    if (s >= 0 && w == w0 && ability && vis && mt_ticking()) {
        // reasons came (SetProgress was called): ask the engine; none: the ability can be used - no recharge
        // stratagems: asked even with no reasons - the bunker (siritos 03.11) has none 10 s after the cast (the drop is down) while
        // the engine's 120 s recharge still runs; the number ended there
        float q = g_capN || s >= 16 ? ask_vt(ctx, btn, ability, 0xB4) : -1;
        float p = q >= 0 ? q : g_capMax > 0 ? g_capMax : -1;
        int rech = p > 0.0f && p < 0.999f;
        // Cooldowns 8: the game's own verdict of this update - the button is usable (+0x208, set by the update above from the
        // reasons: none, or only the ones 4-8 that don't block). A usable ability gets no number and never the dark sweep: the
        // sweep put back on a usable button made it look unavailable (Cooldowns 7, berserk 04.11: "the cursor says it can't be
        // used"). Stratagems keep their number (the bunker: usable buttons while the engine's 120 s recharge runs, siritos 03.11),
        // without the sweep.
        int en = *(w + 0x208) != 0;
        unsigned key = rec_key(btn, ability);
        DWORD gt = (DWORD)mt_ticks(), now = GetTickCount();
        float sweep = -1, left = 0;
        EnterCriticalSection(&g_cs);
        Rec *r = rec_of(key, ability, rech);
        if (r) {
            if ((!g_capN && !rech) || (q >= 0 && !rech)) r->p = 0;   // usable, or the engine says no recharge: over
            else if (rech) rec_share(r, p, gt, g_capMax > 0);
            left = rec_left(r, gt);                          // only 0s and no answer: the remembered recharge goes on
            if (en && s < 16) left = 0;
        }
        Slot *b = &g_slot[s];
        if (left > 0.5f) {
            float tot = r->total > 0 ? r->total : ab_len(ability);
            if (b->w != w || b->key != key) b->shown = 0;
            b->w = w; b->key = key; b->ab = ability; b->ready = 0; b->exact = 1; b->seen = now;
            b->exactLeft = (DWORD)(left * mt_ms_per_tick()); b->exactAt = mt_game_ms();
            sweep = tot > 0 ? 1.0f - left / tot : -1;
        } else { b->ready = 1; b->exact = 0; b->key = key; }
        LeaveCriticalSection(&g_cs);
        // the game's own sweep: its last value of this update is a 0 of another reason (no energy, busy) - put the share back
        int push = sweep > 0 && !en && (g_capN || s >= 16) && (g_capLast < sweep - 0.002f || g_capLast > sweep + 0.002f);
        trace_btn(s, btn, w, ability, q, en, vis, left, sweep, push ? "sweep pushed" : left > 0.5f ? "number" : r && r->p > 0 && !rec_running(r) ? "share not moving: none" : "none");
        if (push) set_progress(w, sweep < 0.001f ? 0.001f : sweep > 0.998f ? 0.998f : sweep);
    } else if (!ability || !vis) cd_progress(w, 0.0f);         // no ability or the button is hidden: no number
    // after the match (no sim ticks) cd_show takes a number still there off
    if (s >= 0) cd_show(s, w);
}
// Global abilities: their panel's button update (virtual, slot 0x01118B4C = 0xB5B850; this = the button's slot struct,
// +0x28 the widget, arg +0x10 the ability's entity) takes the entity's extension 1 (EntityController::QI through the import
// 0x00F88338) whose +0x14 is the sim ticks left; it shows 1 - left / total. We read the ticks left after it: exact
// seconds from the first frame, no rate to guess (the guess made the number come late).
#define GLOBUPD_SLOT 0x01118B4C
#define GLOBUPD_FN   0x00B5B850
#define IAT_QI       0x00F88338
typedef void *(__thiscall *QI_t)(void *controller, unsigned id);
static BtnUpd_t o_globupd;
static void __fastcall hk_globupd(char *btn, void *edx, char *ctx) {
    cd_match_check();
    o_globupd(btn, edx, ctx);
    if (!g_on || !btn) return;
    char *w = *(char **)(btn + 0x28);
    if (!w) return;
    const char *path = w + 0xDD0;
    if (IsBadReadPtr(path, 16) || path[0] != '/') return;
    int s = slot_of(path); if (s < 0) return;
    char *ent = ctx && mt_ticking() ? *(char **)(ctx + 0x10) : NULL;
    void *ec = ent ? *(void **)(ent + 0x3C) : NULL; QI_t qi = *(QI_t *)IAT_QI;
    char *ext = ec && qi ? qi(ec, 1) : NULL;
    if (ext) {
        int left = *(int *)(ext + 0x14);
        EnterCriticalSection(&g_cs);
        Slot *b = &g_slot[s];
        if (left > 0) { b->exact = 1; b->exactLeft = (DWORD)(left * mt_ms_per_tick()); b->exactAt = mt_game_ms(); }
        else b->exact = 0;
        LeaveCriticalSection(&g_cs);
    }
    cd_show(s, w);
}
static void install_btnupd(void) {
    DWORD *gs = (DWORD *)GLOBUPD_SLOT, go;
    if (!IsBadReadPtr(gs, 4) && *gs == GLOBUPD_FN && !IsBadReadPtr((void *)IAT_QI, 4) && VirtualProtect(gs, 4, PAGE_READWRITE, &go)) {
        o_globupd = (BtnUpd_t)GLOBUPD_FN; *gs = (DWORD)hk_globupd; VirtualProtect(gs, 4, go, &go);
        logmsg("hooked global ability button update (vtable %08X): exact seconds", GLOBUPD_SLOT);
    } else logmsg("global ability button update not at %08X: guessed seconds", GLOBUPD_SLOT);
    DWORD *slot = (DWORD *)BTNUPD_SLOT; DWORD old;
    if (IsBadReadPtr(slot, 4) || *slot != BTNUPD_FN) { logmsg("ability button update not at %08X: numbers only with the game's own sweep", BTNUPD_SLOT); return; }
    o_btnupd = (BtnUpd_t)BTNUPD_FN;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) return;
    *slot = (DWORD)hk_btnupd;
    VirtualProtect(slot, 4, old, &old);
    logmsg("hooked ability button update (vtable %08X)", BTNUPD_SLOT);
}

static void install(void) {
    unsigned char *t = (unsigned char *)SETPROGRESS_ADDR;
    if (IsBadReadPtr(t, 9) || memcmp(t, SETPROGRESS_PRO, 9)) {
        logmsg("DOW2.exe differs at %08X: no cooldown numbers", SETPROGRESS_ADDR); g_on = 0; return;
    }
    unsigned char *tr = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(tr, SETPROGRESS_PRO, 9); tr[9] = 0xE9; *(DWORD *)(tr + 10) = (SETPROGRESS_ADDR + 9) - (DWORD)(tr + 14);
    g_spTramp = tr;
    DWORD old; VirtualProtect(t, 9, PAGE_EXECUTE_READWRITE, &old);
    t[0] = 0xE9; *(DWORD *)(t + 1) = (DWORD)hk_setprogress - (DWORD)(t + 5); t[5] = t[6] = t[7] = t[8] = 0x90;
    VirtualProtect(t, 9, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 9);
    logmsg("hooked button SetProgress at %08X", SETPROGRESS_ADDR);
}

// called by LobbyStats once: dir = its folder (with a trailing backslash), ini = its ini file
void cd_start(const wchar_t *dir, const wchar_t *ini) {
    _snwprintf(g_log, MAX_PATH, L"%lsCooldowns.log", dir);
    {   // Cooldowns 9: the log does not grow for ever (05.11: 1 MB) - over 1 MB at the game's start it becomes Cooldowns.log.old
        WIN32_FILE_ATTRIBUTE_DATA a; wchar_t old[MAX_PATH]; _snwprintf(old, MAX_PATH, L"%lsCooldowns.log.old", dir);
        if (GetFileAttributesExW(g_log, GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > 1024 * 1024)) MoveFileExW(g_log, old, MOVEFILE_REPLACE_EXISTING);
    }
    #ifdef CD_STANDALONE
    g_on = GetPrivateProfileIntW(L"Cooldowns", L"Enabled", 1, ini);   // Cooldowns.dll: its launcher tick is the switch
#else
    g_on = GetPrivateProfileIntW(L"Cooldowns", L"Enabled", 0, ini);
#endif
    g_size = GetPrivateProfileIntW(L"Cooldowns", L"DigitSize", g_size, ini);   // not "Size": that was pixels of the Direct3D digits
    if (g_size < 12) g_size = 12; if (g_size > 40) g_size = 40;
    wchar_t c[16]; GetPrivateProfileStringW(L"Cooldowns", L"Color", L"FFFFFF", c, 16, ini);
    g_color = wcstoul(c, NULL, 16) & 0xFFFFFF;
    g_query = GetPrivateProfileIntW(L"Cooldowns", L"Ask", 1, ini);
    g_trace = GetPrivateProfileIntW(L"Cooldowns", L"Trace", 0, ini);
    logmsg("Cooldowns 9 (%s): the numbers in the game's own button movie; every recharge per squad and ability%s; no number, no sweep on a usable button",
           g_on ? "on" : "off, [Cooldowns] Enabled=0", g_query ? ", asked from the engine (guarded)" : ", Ask=0: shares only");
    if (!g_on) return;
    InitializeCriticalSection(&g_cs);
    if (g_query) AddVectoredExceptionHandler(1, cd_veh);
    install(); if (g_on) install_btnupd();
}
