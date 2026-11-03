// cooldowns.c - seconds left on recharging abilities and global abilities, in the middle of the button over the game's
// own dark recharge sweep. Cooldowns.dll (cooldowns_main.c; the match state from matchtimer.c with MT_EMBED).
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
    void *uiMovie; char *uiW; int uiShown;           // UI thread: our copy of the count clip made in this movie on this button, seconds in it
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

// the start of each recharge per ability of each owner (squad): switching the selection back and forth keeps the rate,
// so the number comes at once instead of after two sim ticks
typedef struct { unsigned key; float p0, p; DWORD t0; } Seen;
static Seen g_seen[64]; static int g_nseen;
static void cd_share(char *w, float p, unsigned key);
// One ability button update (0xB9AF80) calls SetProgress once per reason the ability can't be used: the share for a
// recharge (reasons 0xD / 0xE) and 0 for every other reason - so an ability recharging while its squad lacks the energy
// got its share and then a 0 in the same frame, and the number (and the game's own dark sweep) went (siritos 02.11).
// While that update runs the values are only collected (g_cap*); after it the largest share wins (see hk_btnupd).
static char *volatile g_capW;                        // the button whose update runs now (UI thread), NULL = none
static float g_capMax, g_capLast; static int g_capN; static float g_capSeq[8];
static int g_trace;                                  // Cooldowns.ini [Cooldowns] Trace=1: what each update did, in the log
// Built-like global abilities (the Imperial Guard bunker, siritos 02.11): no recharge share through SetProgress - the
// update's reason 10 asks the engine (vt+0xCC) and gives the value to another widget of the button (+0x40) through
// 0x688CD0 (usercall: ESI = that widget, the value on the stack; it fills the widget 0..1). Caught for that widget while
// the update runs (g_cap10*), turned into the share after it (see hk_btnupd).
static char *volatile g_capW10; static float g_cap10 = -1;
void cd_value(char *w, float v, DWORD ret) __asm__("cd_value");
void cd_value(char *w, float v, DWORD ret) {
    if (w && w == g_capW10) g_cap10 = v;
    if (g_trace && w && v > 0.0f && v < 0.999f) {      // where the other fills come from (the bunker's sweep, siritos 02.11)
        static char *seen[32]; static int n;
        for (int i = 0; i < n; i++) if (seen[i] == w) return;
        if (n < 32) seen[n++] = w;
        const char *p = w + 8;
        logmsg("trace value %.3f into widget %p \"%.60s\" from %08lX%s", v, (void *)w, !IsBadReadPtr(p, 64) && p[0] == '/' ? p : "?", (unsigned long)ret, w == g_capW10 ? " (reason 10)" : "");
    }
}
void cd_progress(char *w, float p) __asm__("cd_progress");
void cd_progress(char *w, float p) {
    if (w && w == g_capW) {
        if (p > 0.0f && p < 0.999f && p > g_capMax) g_capMax = p;
        g_capLast = p;
        if (g_capN < 8) g_capSeq[g_capN] = p;
        g_capN++;
        return;
    }
    cd_share(w, p, 0);
}
static void cd_share(char *w, float p, unsigned key) {
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
    if (key) {                                       // this ability of this owner seen before in this recharge: its start
        Seen *k = NULL;
        for (int i = 0; i < g_nseen; i++) if (g_seen[i].key == key) { k = &g_seen[i]; break; }
        if (!k) { k = &g_seen[g_nseen < 64 ? g_nseen++ : (int)(key % 64)]; k->key = key; k->p0 = p; k->t0 = gt; }
        else if (p < k->p - 0.002f) { k->p0 = p; k->t0 = gt; }                 // a new recharge
        k->p = p;
        if (k->t0 < b->t0 && k->p0 < p) { b->p0 = k->p0; b->t0 = k->t0; }       // an older start: the rate at once
    }
    b->p = p; b->t = gt; b->seen = now;
    LeaveCriticalSection(&g_cs);
}

void *g_spTramp __asm__("cd_sp_tramp");
void *g_vTramp __asm__("cd_v_tramp");
// Trace=1 only: the engine's "go to frame" of a widget clip (0x686890: widget, clip path, frame, flag - all on the stack).
// Who moves a global ability's sweep that comes through neither SetProgress nor 0x688CD0 (the bunker, siritos 02.11).
void *g_fTramp __asm__("cd_f_tramp");
#define SETFRAME_ADDR 0x00686890
void cd_frame(char *w, const char *path, int frame, DWORD ret) __asm__("cd_frame");
void cd_frame(char *w, const char *path, int frame, DWORD ret) {
    (void)w;
    if (!path || IsBadReadPtr(path, 48) || !memchr(path, 0, 48) || !strstr(path, "global_btn")) return;
    static struct { DWORD ret; char path[48]; int frame; DWORD t; } seen[24]; static int n;
    DWORD now = GetTickCount();
    for (int i = 0; i < n; i++) if (seen[i].ret == ret && !strcmp(seen[i].path, path)) {
        if (frame != seen[i].frame && now - seen[i].t > 2000) { seen[i].frame = frame; seen[i].t = now; logmsg("trace frame %d of %s from %08lX", frame, path, (unsigned long)ret); }
        return;
    }
    if (n < 24) { seen[n].ret = ret; snprintf(seen[n].path, 48, "%s", path); seen[n].frame = frame; seen[n].t = now; n++; }
    logmsg("trace frame %d of %s from %08lX (first)", frame, path, (unsigned long)ret);
}
// Trace=1 only: every call the engine makes into a HUD movie (GFxMovieView::Invoke, movie vtable +0x48) with a global
// ability button in its first argument - method, path, the number after it (who draws the bunker's sweep)
typedef char (__fastcall *Inv_t)(void *mv, void *edx, const char *method, GfxVal *res, const GfxVal *args, unsigned n);
static Inv_t o_inv; static DWORD *g_invSlot;
static char __fastcall hk_inv(void *mv, void *edx, const char *method, GfxVal *res, const GfxVal *args, unsigned n) {
    if (method && args && n >= 1 && args[0].type == GV_STRING && args[0].v.s && !IsBadReadPtr(args[0].v.s, 8) && strstr(args[0].v.s, "global_btn")
        && strncmp(args[0].v.s, "global_abilities.global_btn0", 28) /* ours: dotted */) {
        static struct { char m[40], p[48]; DWORD t; double num; } seen[32]; static int cnt;
        char m[40], p[48]; snprintf(m, sizeof m, "%s", method); snprintf(p, sizeof p, "%s", args[0].v.s);
        double num = n >= 2 && args[1].type == GV_NUMBER ? args[1].v.num : -1;
        int i; for (i = 0; i < cnt; i++) if (!strcmp(seen[i].m, m) && !strcmp(seen[i].p, p)) break;
        DWORD now = GetTickCount();
        if (i == cnt && cnt < 32) { cnt++; snprintf(seen[i].m, 40, "%s", m); snprintf(seen[i].p, 48, "%s", p); seen[i].t = 0; seen[i].num = -2; }
        if (i < 32 && (num != seen[i].num) && now - seen[i].t > 2000) {
            seen[i].t = now; seen[i].num = num;
            logmsg("trace invoke %s(%s, %s%g) n=%u", m, p, n >= 2 ? (args[1].type == GV_NUMBER ? "" : "type ") : "", n >= 2 ? (args[1].type == GV_NUMBER ? num : (double)args[1].type) : 0.0, n);
        }
    }
    return o_inv(mv, edx, method, res, args, n);
}
static void trace_invoke(void *mv) {                 // once, from the first movie we see
    if (!g_trace || g_invSlot || !mv) return;
    DWORD *slot = (DWORD *)(*(char **)mv + 0x48), old;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) return;
    o_inv = (Inv_t)*slot; *slot = (DWORD)hk_inv; g_invSlot = slot;
    VirtualProtect(slot, 4, old, &old);
    logmsg("trace: hooked movie Invoke (vtable slot %p)", (void *)slot);
}
__attribute__((naked)) static void hk_setframe(void) {
    __asm__ volatile(
        "pushal\n\t"
        "pushl 32(%esp)\n\t"                         // the caller
        "pushl 48(%esp)\n\t"                         // the frame  ([esp+12] at the call)
        "pushl 48(%esp)\n\t"                         // the path   ([esp+8])
        "pushl 48(%esp)\n\t"                         // the widget ([esp+4])
        "call cd_frame\n\t"
        "addl $16, %esp\n\t"
        "popal\n\t"
        "jmp *cd_f_tramp\n\t");
}
#define SETVALUE_ADDR 0x00688CD0
static const unsigned char SETVALUE_PRO[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};   // push ebp; mov ebp,esp; and esp,-8
__attribute__((naked)) static void hk_setvalue(void) {
    __asm__ volatile(
        "pushal\n\t"
        "pushl 32(%esp)\n\t"                         // the caller
        "pushl 40(%esp)\n\t"                         // the value (float, [esp+4] at the call)
        "pushl %esi\n\t"                             // the widget
        "call cd_value\n\t"
        "addl $12, %esp\n\t"
        "popal\n\t"
        "jmp *cd_v_tramp\n\t");
}
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
    if (!sec && (b->uiW != w || !b->uiShown)) return;          // nothing on this button and nothing to take off
    void *mv = gfx_movie_of_widget(w);
    const char *wp = w + 8;
    if (!mv || IsBadReadPtr(wp, 48) || wp[0] != '/' || !memchr(wp, 0, 48)) return;
    char dot[64], p[96];                                       // "/global_abilities/global_btn00" -> "global_abilities.global_btn00"
    snprintf(dot, sizeof dot, "%s", wp + 1);
    for (char *c = dot; *c; c++) if (*c == '/') *c = '.';
    if (b->uiMovie != mv || b->uiW != w) {                     // first number on this button (in this HUD): the copy
        if (!sec) return;
        b->uiMovie = mv; b->uiW = w; b->uiShown = 0;
        snprintf(p, sizeof p, "%s.mt_cd", dot);
        if (!gfx_exists(mv, p)) {
            char src[96]; snprintf(src, sizeof src, "%s/count", wp);
            GfxVal a[2] = { gv_str(src), gv_str("mt_cd") }, r; memset(&r, 0, sizeof r);
            gfx_invoke(mv, "Receive_DuplicateMovieClip", &r, a, 2);
            if (r.type != GV_BOOL || !r.v.b) { logmsg("%s: no count clip to copy - no number on this button", wp); b->uiMovie = NULL; return; }
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
    if (sec == b->uiShown) return;
    char t[16];
    if (sec < 100) snprintf(t, sizeof t, "%d", sec); else snprintf(t, sizeof t, "%d:%02d", sec / 60, sec % 60);
    if (sec) { snprintf(p, sizeof p, "%s.mt_cd.count_text", dot); gfx_set_text(mv, p, t); }
    if (!sec != !b->uiShown) { snprintf(p, sizeof p, "%s.mt_cd", dot); gfx_set_visible(mv, p, sec != 0); }
    b->uiShown = sec;
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
typedef struct { unsigned key; int ab; float p, p0; DWORD t, t0; float total; DWORD used; } Rec;   // ticks
#define NREC 96
static Rec g_rec[NREC];
typedef struct { int ab; float total; } AbLen;      // the length of an ability's recharge in ticks, any owner: the
static AbLen g_abLen[48]; static int g_nabLen;       // first number of another squad of that kind comes at once
static float ab_len(int ab) { for (int i = 0; i < g_nabLen; i++) if (g_abLen[i].ab == ab) return g_abLen[i].total; return 0; }
static void ab_len_set(int ab, float total) {
    for (int i = 0; i < g_nabLen; i++) if (g_abLen[i].ab == ab) { g_abLen[i].total = total; return; }
    AbLen *a = &g_abLen[g_nabLen < 48 ? g_nabLen++ : (unsigned)ab % 48]; a->ab = ab; a->total = total;
}
static unsigned rec_key(const char *btn, int ability) {   // the ability and its owner (ConstOwnerPointer, 16 bytes)
    unsigned key = (unsigned)ability * 2654435761u;
    for (int i = 0; i < 4; i++) key = (key ^ ((const unsigned *)(btn + 0x2A8))[i]) * 16777619u;
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
// a share p (0..1) of this recharge at sim tick gt
static void rec_share(Rec *r, float p, DWORD gt) {
    float tot = r->total > 0 ? r->total : ab_len(r->ab);
    int fresh = !r->t0 || !(r->p > 0) || p < r->p - 0.002f;
    // not where this recharge would be by now (it ended and was cast again while not selected, or got shorter): a new one
    if (!fresh && tot > 0 && gt > r->t) { float expect = r->p + (float)(gt - r->t) / tot; if (p > expect + 0.03f) fresh = 1; }
    if (fresh) { r->p0 = p; r->t0 = gt ? gt : 1; }
    if (gt > r->t0 && p > r->p0 + 0.0001f) { r->total = (float)(gt - r->t0) / (p - r->p0); ab_len_set(r->ab, r->total); }
    r->p = p; r->t = gt;
}
static float rec_left(const Rec *r, DWORD gt) {        // ticks left, <= 0 = over or not known
    float tot = r->total > 0 ? r->total : ab_len(r->ab);
    if (!(r->p > 0) || tot <= 0) return 0;
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
static float ask_share(char *ctx, char *btn, int ability) {   // the engine's share, -1 = none
    if (!g_query || g_qOff || !ctx || IsBadReadPtr(ctx + 0xC, 4)) return -1;
    void **iface = *(void ***)(ctx + 0xC);
    if (!iface || IsBadReadPtr(iface, 4) || IsBadReadPtr(*iface, 0xB8)) return -1;
    void *fn = (*(void ***)iface)[0xB4 / 4];
    static HMODULE exe; static MODULEINFO mi;
    if (!exe) { exe = GetModuleHandleA(NULL); GetModuleInformation(GetCurrentProcess(), exe, &mi, sizeof mi); }
    if ((DWORD)fn < (DWORD)mi.lpBaseOfDll || (DWORD)fn >= (DWORD)mi.lpBaseOfDll + mi.SizeOfImage) {
        g_qOff = 1; logmsg("engine query: vt+0xB4 = %p is not in DOW2.exe - shares only", fn); return -1;
    }
    static int said;
    if (!said) { said = 1; logmsg("engine query: iface %p, vtable %p, vt+0xB4 = %p (the share of a recharge, asked for every ability button)", (void *)iface, *iface, fn); }
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

static void __fastcall hk_btnupd(char *btn, void *edx, char *ctx) {
    char *w0 = btn ? *(char **)(btn + 0x28) : NULL;
    g_capMax = 0; g_capLast = -1; g_capN = 0; g_capW = g_on ? w0 : NULL;
    g_cap10 = -1; g_capW10 = g_on && btn ? *(char **)(btn + 0x40) : NULL;
    o_btnupd(btn, edx, ctx);
    g_capW = NULL; g_capW10 = NULL;
    if (!g_on || !btn) return;
    char *w = *(char **)(btn + 0x28); int ability = *(int *)(btn + 0x74);
    if (!w) return;
    if (g_trace && !g_invSlot) trace_invoke(gfx_movie_of_widget(w));
    const char *path = w + 0xDD0;
    int s = !IsBadReadPtr(path, 16) && path[0] == '/' ? slot_of(path) : -1;
    int vis = *(w + 0x209) == 1;
    if (s >= 0 && s < 16 && w == w0 && ability && vis && mt_ticking()) {
        // reasons came (SetProgress was called): ask the engine; none: the ability can be used - no recharge
        float q = g_capN ? ask_share(ctx, btn, ability) : -1;
        float p = q >= 0 ? q : g_capMax > 0 ? g_capMax : g_cap10 > 0.0f && g_cap10 < 0.999f ? g_cap10 : -1;
        int rech = p > 0.0f && p < 0.999f;
        unsigned key = rec_key(btn, ability);
        DWORD gt = (DWORD)mt_ticks(), now = GetTickCount();
        float sweep = -1, left = 0;
        EnterCriticalSection(&g_cs);
        Rec *r = rec_of(key, ability, rech);
        if (r) {
            if (!g_capN || (q >= 0 && !rech)) r->p = 0;     // usable, or the engine says no recharge: over
            else if (rech) rec_share(r, p, gt);
            left = rec_left(r, gt);                          // only 0s and no answer: the remembered recharge goes on
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
        if (sweep > 0 && g_capN && (g_capLast < sweep - 0.002f || g_capLast > sweep + 0.002f)) set_progress(w, sweep < 0.001f ? 0.001f : sweep > 0.998f ? 0.998f : sweep);
        if (g_trace) {
            static unsigned last[16]; static int lastSec[16];
            int sec = left > 0 ? (int)((left * mt_ms_per_tick() + 999) / 1000) : 0;
            if (last[s] != key || (lastSec[s] > 0) != (sec > 0) || (sec && sec % 10 == 0 && sec != lastSec[s])) {
                last[s] = key; lastSec[s] = sec;
                logmsg("trace %.40s ability %p key %08X: reasons %d, engine %.3f, captured %.3f/%.3f -> %s %d s (length %.0f ticks)", path, (void *)(UINT_PTR)ability, key,
                       g_capN, q, g_capMax, g_capLast, sec ? "recharging" : "ready", sec, r ? (r->total > 0 ? r->total : ab_len(ability)) : 0.0f);
            }
        }
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
    if (g_trace && ent) {                            // the drop bunker (siritos 02.11): where its recharge is, if anywhere
        static void *lastEnt[SLOTS]; static int lastLeft[SLOTS];
        int left = ext ? *(int *)(ext + 0x14) : -1;
        if (lastEnt[s] != ent || (left > 0) != (lastLeft[s] > 0)) {
            lastEnt[s] = ent; lastLeft[s] = left;
            logmsg("trace %s: entity %p controller %p extension 1 %p, ticks left %d", path, (void *)ent, ec, (void *)ext, left);
        }
    }
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
    logmsg("hooked ability button update (vtable %08X): hidden buttons cleared, no engine queries", BTNUPD_SLOT);
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
    unsigned char *v = (unsigned char *)SETVALUE_ADDR;
    if (!g_trace) return;                                         // the bunker: not solved, its probes only with Trace=1
    if (IsBadReadPtr(v, 6) || memcmp(v, SETVALUE_PRO, 6)) { logmsg("DOW2.exe differs at %08X: no numbers for reason-10 buttons (the bunker)", SETVALUE_ADDR); return; }
    unsigned char *tv = tr + 16;
    memcpy(tv, SETVALUE_PRO, 6); tv[6] = 0xE9; *(DWORD *)(tv + 7) = (SETVALUE_ADDR + 6) - (DWORD)(tv + 11);
    g_vTramp = tv;
    VirtualProtect(v, 6, PAGE_EXECUTE_READWRITE, &old);
    v[0] = 0xE9; *(DWORD *)(v + 1) = (DWORD)hk_setvalue - (DWORD)(v + 5); v[5] = 0x90;
    VirtualProtect(v, 6, old, &old); FlushInstructionCache(GetCurrentProcess(), v, 6);
    logmsg("hooked widget value at %08X (reason 10)", SETVALUE_ADDR);
    unsigned char *f = (unsigned char *)SETFRAME_ADDR;            // same prologue as 0x688CD0
    if (!g_trace || IsBadReadPtr(f, 6) || memcmp(f, SETVALUE_PRO, 6)) return;
    unsigned char *tf = tr + 32;
    memcpy(tf, SETVALUE_PRO, 6); tf[6] = 0xE9; *(DWORD *)(tf + 7) = (SETFRAME_ADDR + 6) - (DWORD)(tf + 11);
    g_fTramp = tf;
    VirtualProtect(f, 6, PAGE_EXECUTE_READWRITE, &old);
    f[0] = 0xE9; *(DWORD *)(f + 1) = (DWORD)hk_setframe - (DWORD)(f + 5); f[5] = 0x90;
    VirtualProtect(f, 6, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 6);
    logmsg("trace: hooked clip frames at %08X", SETFRAME_ADDR);
}

// called by LobbyStats once: dir = its folder (with a trailing backslash), ini = its ini file
void cd_start(const wchar_t *dir, const wchar_t *ini) {
    _snwprintf(g_log, MAX_PATH, L"%lsCooldowns.log", dir);
    #ifdef CD_STANDALONE
    g_on = GetPrivateProfileIntW(L"Cooldowns", L"Enabled", 1, ini);   // Cooldowns.dll: its launcher tick is the switch
#else
    g_on = GetPrivateProfileIntW(L"Cooldowns", L"Enabled", 0, ini);
#endif
    g_size = GetPrivateProfileIntW(L"Cooldowns", L"DigitSize", g_size, ini);   // not "Size": that was pixels of the Direct3D digits
    if (g_size < 12) g_size = 12; if (g_size > 40) g_size = 40;
    wchar_t c[16]; GetPrivateProfileStringW(L"Cooldowns", L"Color", L"FFFFFF", c, 16, ini);
    g_color = wcstoul(c, NULL, 16) & 0xFFFFFF;
    g_trace = GetPrivateProfileIntW(L"Cooldowns", L"Trace", 0, ini);
    g_query = GetPrivateProfileIntW(L"Cooldowns", L"Ask", 1, ini);
    logmsg("Cooldowns 6 (%s): the numbers in the game's own button movie; every recharge per squad and ability%s%s",
           g_on ? "on" : "off, [Cooldowns] Enabled=0", g_query ? ", asked from the engine (guarded)" : ", Ask=0: shares only", g_trace ? "; Trace=1" : "");
    if (!g_on) return;
    InitializeCriticalSection(&g_cs);
    if (g_query) AddVectoredExceptionHandler(1, cd_veh);
    install(); if (g_on) install_btnupd();
}
