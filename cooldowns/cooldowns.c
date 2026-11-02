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
        left = (1.0f - b->p) / rate * mt_ms_per_tick();
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
static void __fastcall hk_btnupd(char *btn, void *edx, char *ctx) {
    char *w0 = btn ? *(char **)(btn + 0x28) : NULL;
    g_capMax = 0; g_capLast = -1; g_capN = 0; g_capW = g_on ? w0 : NULL;
    o_btnupd(btn, edx, ctx);
    g_capW = NULL;
    if (!g_on || !btn) return;
    char *w = *(char **)(btn + 0x28); int ability = *(int *)(btn + 0x74);
    if (!w) return;
    if (w == w0 && g_capN) {
        // a recharge share in this update wins over the 0 of another reason after it (no energy...): the number stays, and
        // the game's own sweep too - its 0 is put back to the share (the ability stays unusable, the button dark as before)
        if (g_capMax > 0) { cd_share(w, g_capMax, 0); if (g_capLast != g_capMax) set_progress(w, g_capMax); }
        else cd_share(w, g_capLast, 0);
        if (g_trace) {
            static char last[SLOTS][64]; const char *path = w + 0xDD0; int s = !IsBadReadPtr(path, 16) && path[0] == '/' ? slot_of(path) : -1;
            char seq[64] = ""; int n = g_capN < 8 ? g_capN : 8;
            for (int i = 0; i < n; i++) snprintf(seq + strlen(seq), sizeof seq - strlen(seq), "%s%s", i ? "," : "", g_capSeq[i] <= 0 ? "0" : g_capSeq[i] >= 0.999f ? "1" : "s");
            if (s >= 0 && strcmp(last[s], seq)) { strcpy(last[s], seq); logmsg("trace %s ability %p: SetProgress %s (largest share %.3f)%s", path, (void *)(UINT_PTR)ability, seq, g_capMax, g_capMax > 0 && g_capLast != g_capMax ? " - share put back" : ""); }
        }
    }
    if (!ability || *(w + 0x209) != 1) cd_progress(w, 0.0f);         // no ability or the button is hidden: no number
    // after the match (no sim ticks) cd_show takes a number still there off
    const char *path = w + 0xDD0; int s;
    if (!IsBadReadPtr(path, 16) && path[0] == '/' && (s = slot_of(path)) >= 0) cd_show(s, w);
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
    unsigned char *tr = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
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
    logmsg("Cooldowns 4 (%s): the numbers in the game's own button movie, no Direct3D; a recharge share wins over the 0 of another reason%s",
           g_on ? "on" : "off, [Cooldowns] Enabled=0", g_trace ? "; Trace=1" : "");
    if (!g_on) return;
    InitializeCriticalSection(&g_cs);
    install(); if (g_on) install_btnupd();
}
