// unitstats.c - UnitStats.dll, EXPERIMENT (05.11.2026, asked by grantovich in Elite Mod - Dev, 04.11): the selected
// squad's combat numbers in the empty space left of the Retreat button of the game's own selection panel:
//   ranged DPS, melee DPS, melee skill / range, speed, sight.
// Archive branch only (archive/unitstats-experiment) - not for players, not in the store. How it works and what to do
// before it could ship: docs/UNIT_STATS_HOWTO.md.
//
// Shown only through the game's own interface (docs/UI_RULES.md, common/gfxui.h): two text fields of ours inside the
// selection panel movie (ui\movies\hud\selection_panel), in its clip "core_actions" (the five order buttons and Retreat,
// core_btn03) - they move and scale with it. No Direct3D.
// Driven from the selection panel form's own per-frame update (form vtable 0x01115490, made in 0xB39850; slot 4 =
// 0xB3DA00, chained: the game's first, then ours), on the game's UI thread.
//
// Read-only: nothing is written into the simulation and no order is given - no desync, only this player sees it.
//   selection: the selection object *(*(*(0x01335720)+8)+0x20), entity ids at +0x35C..+0x360 under its mutex +0x39D (as
//     the game's own Misc_GetSelectedSquads 0xAFAD10 / Misc_IsSquadSelected 0xAFAE80); id -> entity through the world
//     (*(0x013356A4)+0xF8, vtable +0x10); entity +0x38 = its squad. Exactly one squad selected: numbers, else nothing.
//   per entity of the squad (Squad::GetAllEntities): +0x3C its controller, EntityController::QI(id) its extensions:
//     6 = CombatExt (vtable 0x010DE6F0): +0x50 hardpoints[], +0x54 count; hardpoint +0x50 = its current Weapon;
//       weapon +0x54 = WeaponBag (the weapon blueprint as loaded, vtable 0x010D68B4), +0x4C its PropertyBagGroup,
//       +0xAC the hardpoint again (+0x14 the entity);
//     4 = SightExt (vtable 0x010FA130): +0x28 the sight radius now (with modifiers; the game's Entity_GetSightRadius).
//   the entity blueprint's bags (PropertyBagGroup +0x0C pairs {key, bag}, +0x10 count; keys are .data slots):
//     moving_ext 0x01301B60 -> MovingExtInfo +0x5C speed_max; melee_ext 0x01301B94 -> MeleeExtInfo +0x04 weapon_skill.
//   DPS = the game's own formula (Util_GetWeaponDPS 0x934D70: ranged 0x81FC40, melee inline), with the weapon's current
//     damage, cooldown and reload through the game's own modifier function 0x8A6790 (weapon, value, entity, type:
//     7 damage, 3 cooldown, 2 reload, 0x17 max range) - so upgrades, wargear and buffs on the weapon count.
//     Range = the weapon's own GetMaxRange (vtable slot 7, modifiers included).
// Every engine read runs under a fault guard (vectored handler, this thread only): a fault ends the numbers for the
// rest of the game, the log says where.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../common/gfxui.h"

static wchar_t g_log[MAX_PATH], g_ini[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---------- settings: UnitStats.ini [UnitStats] (re-read while the game runs) ----------
static int g_enabled = 1, g_size = 17, g_offX = 0, g_offY = 0, g_verbose = 1;
static FILETIME g_iniTime;
static int ini_int(const wchar_t *k, int def, int lo, int hi) { int v = GetPrivateProfileIntW(L"UnitStats", k, def, g_ini); return v < lo ? lo : v > hi ? hi : v; }
static void load_ini(void) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a)) g_iniTime = a.ftLastWriteTime;
    g_enabled = ini_int(L"Enabled", 1, 0, 1);
    g_size = ini_int(L"FontSize", 17, 8, 40);
    g_offX = ini_int(L"OffsetX", 0, -800, 800);
    g_offY = ini_int(L"OffsetY", 0, -800, 800);
    g_verbose = ini_int(L"Verbose", 1, 0, 1);
}

// ---------- the engine (DOW2.exe 3.19, SimEngine.dll, Platform.dll) ----------
#define SEL_ROOT      0x01335720u
#define G_WORLD       0x013356A4u
#define VT_COMBATEXT  0x010DE6F0u
#define VT_SIGHTEXT   0x010FA130u
#define VT_WEAPONBAG  0x010D68B4u
#define VT_MOVINGINFO 0x010F1404u
#define VT_MELEEINFO  0x010F8F7Cu
#define KEY_MOVING    0x01301B60u
#define KEY_MELEE     0x01301B94u
#define FN_MOD        0x008A6790u
#define EXT_COMBAT    6
#define EXT_SIGHT     4
#define MOD_RELOAD    2
#define MOD_COOLDOWN  3
#define MOD_DAMAGE    7
static const unsigned char MOD_PRO[4] = {0x51, 0x0F, 0x57, 0xC0};      // push ecx; xorps xmm0,xmm0

typedef void *(__thiscall *QI_t)(void *ctrl, unsigned id);
typedef void *(__thiscall *P0_t)(void *obj);
typedef const char *(__thiscall *S0_t)(const void *obj);
typedef void (__thiscall *M0_t)(void *mutex);
typedef void *(__thiscall *Lookup_t)(void *obj, DWORD id);
typedef float (__thiscall *Range_t)(void *weapon);
static QI_t p_qi; static P0_t p_allEnts, p_begin, p_end, p_entBp; static S0_t p_sqName, p_entName, p_pbgFile; static M0_t p_enter, p_leave;

// the game's modifier function: thiscall (weapon, float value, Entity *, int type), the result in xmm0
float us_mod(void *weapon, float v, void *ent, int type) __asm__("us_mod");
__asm__(".text\n"
    "us_mod:\n\t"
    "movl 4(%esp), %ecx\n\t"
    "pushl 16(%esp)\n\t"                             // type
    "pushl 16(%esp)\n\t"                             // entity
    "pushl 16(%esp)\n\t"                             // value
    "movl $0x008A6790, %eax\n\t"
    "call *%eax\n\t"                                 // ret 0xC
    "subl $4, %esp\n\t movss %xmm0, (%esp)\n\t flds (%esp)\n\t addl $4, %esp\n\t ret\n");

// ---------- the fault guard (as cooldowns.c): fn(arg) on this thread, 0 = it faulted ----------
static volatile DWORD g_gThread; static DWORD g_gFault; static void *g_gAt; static int g_dead;
DWORD g_gEsp __asm__("us_g_esp"); DWORD g_gSeh __asm__("us_g_seh"); unsigned short g_gCw __asm__("us_g_cw");
int us_guarded(void (*fn)(void *), void *arg) __asm__("us_guarded");
void us_fail(void) __asm__("us_fail");
__asm__(".text\n"
    "us_guarded:\n\t"
    "pushl %ebp\n\t pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t"
    "movl %fs:0, %eax\n\t movl %eax, us_g_seh\n\t fnstcw us_g_cw\n\t"
    "movl %esp, us_g_esp\n\t"
    "pushl 24(%esp)\n\t"                             // arg
    "call *24(%esp)\n\t"                             // fn
    "movl us_g_esp, %esp\n\t movl $1, %eax\n\t"
    "popl %edi\n\t popl %esi\n\t popl %ebx\n\t popl %ebp\n\t ret\n"
    "us_fail:\n\t"
    "movl us_g_esp, %esp\n\t movl us_g_seh, %eax\n\t movl %eax, %fs:0\n\t"
    "fninit\n\t fldcw us_g_cw\n\t xorl %eax, %eax\n\t"
    "popl %edi\n\t popl %esi\n\t popl %ebx\n\t popl %ebp\n\t ret\n");
static LONG CALLBACK us_veh(EXCEPTION_POINTERS *e) {
    if (!g_gThread || GetCurrentThreadId() != g_gThread) return EXCEPTION_CONTINUE_SEARCH;
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if (c != EXCEPTION_ACCESS_VIOLATION && c != EXCEPTION_ILLEGAL_INSTRUCTION && c != EXCEPTION_PRIV_INSTRUCTION
        && c != EXCEPTION_INT_DIVIDE_BY_ZERO && c != EXCEPTION_ARRAY_BOUNDS_EXCEEDED) return EXCEPTION_CONTINUE_SEARCH;
    g_gFault = c; g_gAt = e->ExceptionRecord->ExceptionAddress; g_gThread = 0;
    e->ContextRecord->Eip = (DWORD)us_fail;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static int run_guarded(void (*fn)(void *), void *arg, const char *what) {
    if (g_dead) return 0;
    g_gThread = GetCurrentThreadId();
    int ok = us_guarded(fn, arg);
    g_gThread = 0;
    if (!ok) { g_dead = 1; logmsg("FAULT %08lX at %p in %s - no numbers for the rest of this game", g_gFault, g_gAt, what); }
    return ok;
}

static int rd(const void *p, unsigned n) { return p && !IsBadReadPtr(p, n); }
static int vt_is(const void *obj, DWORD vt) { return rd(obj, 4) && *(const DWORD *)obj == vt; }
// a bag of a blueprint by its key (PropertyBagGroup +0x0C {key, bag}[], +0x10 count; as 0x436CDE does for weapon_bag)
static void *pbg_bag(const char *pbg, DWORD key) {
    if (!rd(pbg + 0x0C, 8)) return NULL;
    DWORD *pr = *(DWORD **)(pbg + 0x0C); int n = *(int *)(pbg + 0x10);
    if (n <= 0 || n > 200 || !rd(pr, n * 8)) return NULL;
    for (int i = 0; i < n; i++) if (pr[i * 2] == key) return (void *)pr[i * 2 + 1];
    return NULL;
}

// ---------- the numbers ----------
typedef struct { float rdps, mdps, range, speed, sight; int skill, models, weapons; } Stats;
static float ticks(int t) { return (float)t; }
// one weapon: the game's DPS formula on the weapon's current values
static float weapon_dps(char *w, char *bag, char *ent, int *melee) {
    float dmg = us_mod(w, *(float *)(bag + 0x64), ent, MOD_DAMAGE);
    float cd = us_mod(w, ticks(*(int *)(bag + 0xF0)), ent, MOD_COOLDOWN);
    float rl0 = us_mod(w, ticks(*(int *)(bag + 0x11C)), ent, MOD_RELOAD), rl1 = us_mod(w, ticks(*(int *)(bag + 0x120)), ent, MOD_RELOAD);
    float wu = ticks(*(int *)(bag + 0x10C)), wd = ticks(*(int *)(bag + 0x110));
    float half = 0.5f, tps = 10.0f;                   // 0x0105BB74, 0x0107D78C
    float freq = (*(int *)(bag + 0x124) + *(int *)(bag + 0x128)) * half, time, total;
    *melee = *(bag + 0xC8) != 0;
    if (*melee) { time = wd + wu + cd; total = dmg; }                     // Util_GetWeaponDPS, is_melee
    else if (*(bag + 0x170)) {                                            // bursts (0x81FC40, the first branch)
        float burst = (*(int *)(bag + 0x174) + *(int *)(bag + 0x178)) * half, shots, rate = *(float *)(bag + 0x17C);
        if (freq > 0) { time = (rl0 + rl1) * half + (cd + burst) * freq; shots = freq; }
        else { time = cd + burst; shots = 1.0f; }
        if (rate > 0) dmg *= rate;
        total = shots * dmg * burst;
    } else {                                                              // single shots
        time = wd + wu + cd;
        if (freq > 0) { time = time * freq + (rl0 + rl1) * half; total = freq * dmg; } else total = dmg;
    }
    float dps = time > 0 ? total / time * tps : 0;
    return dps > 0 ? dps : 0;
}
static char g_detail[2048]; static int g_detailN;
static void detail(const char *fmt, ...) {
    if (g_detailN >= (int)sizeof g_detail - 2) return;
    va_list a; va_start(a, fmt); int n = vsnprintf(g_detail + g_detailN, sizeof g_detail - g_detailN, fmt, a); va_end(a);
    if (n > 0) g_detailN += n; if (g_detailN > (int)sizeof g_detail - 1) g_detailN = sizeof g_detail - 1;
}
typedef struct { char *squad; Stats *st; } Job;
static void squad_stats(void *arg) {
    Job *j = arg; Stats *st = j->st; memset(st, 0, sizeof *st); st->speed = -1;
    g_detailN = 0; g_detail[0] = 0;
    char *grp = p_allEnts(j->squad); if (!grp) return;
    char **b = p_begin(grp), **e = p_end(grp);
    for (char **it = b; it && it < e && st->models < 64; it++) {
        char *ent = *it; if (!rd(ent, 0x60)) continue;
        void *ctrl = *(void **)(ent + 0x3C); if (!ctrl) continue;
        st->models++;
        detail(" [%s:", p_entName ? p_entName(ent) : "?");
        float erd = 0, emd = 0;
        char *ce = p_qi(ctrl, EXT_COMBAT);
        if (vt_is(ce, VT_COMBATEXT)) {
            char **hp = *(char ***)(ce + 0x50); int n = *(int *)(ce + 0x54);
            for (int i = 0; i < n && i < 16 && rd(hp, (i + 1) * 4); i++) {
                char *h = hp[i]; if (!rd(h, 0x54)) continue;
                char *w = *(char **)(h + 0x50); if (!rd(w, 0xB0)) continue;
                char *bag = *(char **)(w + 0x54); if (!vt_is(bag, VT_WEAPONBAG)) continue;
                char *went = rd(*(char **)(w + 0xAC), 0x18) ? *(char **)(*(char **)(w + 0xAC) + 0x14) : ent;
                int melee; float dps = weapon_dps(w, bag, went, &melee);
                float rng = melee ? 0 : (*(Range_t *)(*(char **)w + 0x1C))(w);
                st->weapons++;
                if (melee) { if (dps > emd) emd = dps; }                  // one melee attack at a time
                else { erd += dps; if (rng > st->range) st->range = rng; }   // every gun fires
                const char *wf = p_pbgFile && rd(*(char **)(w + 0x4C), 4) ? p_pbgFile(*(char **)(w + 0x4C)) : "?";
                const char *sl = wf ? strrchr(wf, '\\') : NULL; if (!sl && wf) sl = strrchr(wf, '/');
                detail(" %s%s %.1f dps (dmg %.1f->%.1f, cd %d, range %.1f)", melee ? "melee " : "", sl ? sl + 1 : wf, dps,
                       *(float *)(bag + 0x64), us_mod(w, *(float *)(bag + 0x64), went, MOD_DAMAGE), *(int *)(bag + 0xF0), rng);
            }
        }
        st->rdps += erd; st->mdps += emd;
        char *se = p_qi(ctrl, EXT_SIGHT);
        if (vt_is(se, VT_SIGHTEXT)) { float s = *(float *)(se + 0x28); if (s > st->sight) st->sight = s; }
        const char *pbg = p_entBp ? (const char *)p_entBp(ent) : NULL;
        if (pbg) {
            char *mv = pbg_bag(pbg, KEY_MOVING), *ml = pbg_bag(pbg, KEY_MELEE);
            if (vt_is(mv, VT_MOVINGINFO)) { float sp = *(float *)(mv + 0x5C); if (sp > 0 && (st->speed < 0 || sp < st->speed)) st->speed = sp; }
            if (vt_is(ml, VT_MELEEINFO)) { int k = *(int *)(ml + 4); if (k > st->skill) st->skill = k; }
        }
        detail("]");
    }
}

// ---------- the selection ----------
#define MAXSEL 64
typedef struct { char *squad; int n, own; } Sel;
static void get_selection(void *arg) {
    Sel *s = arg; s->squad = NULL; s->n = 0;
    char *r = *(char **)SEL_ROOT; if (!rd(r + 8, 4)) return;
    char *a = *(char **)(r + 8); if (!rd(a + 0x20, 4)) return;
    char *sel = *(char **)(a + 0x20); if (!rd(sel + 0x35C, 0x44)) return;
    DWORD ids[MAXSEL]; int n = 0;
    p_enter(sel + 0x39D);
    DWORD *b = *(DWORD **)(sel + 0x35C), *e = *(DWORD **)(sel + 0x360);
    if (b && e >= b && e - b < 4096 && rd(b, (e - b) * 4)) for (DWORD *it = b; it < e && n < MAXSEL; it++) ids[n++] = *it;
    p_leave(sel + 0x39D);
    char *w = *(char **)G_WORLD; if (!rd(w + 0xF8, 4)) return;
    void *em = *(void **)(w + 0xF8); if (!rd(em, 4)) return;
    Lookup_t look = (Lookup_t)(*(void ***)em)[4];
    for (int i = 0; i < n; i++) {
        char *ent = look(em, ids[i]); if (!rd(ent, 0x60)) continue;
        if ((*(DWORD *)(ent + 0x50) & 0x18) != 0x18) continue;
        char *sq = *(char **)(ent + 0x38); if (!sq) continue;
        if (sq != s->squad) { if (s->squad) { s->n = 2; return; } s->squad = sq; s->n = 1; }
    }
}

// ---------- the text in the selection panel movie ----------
#define SP_VT_UPD 0x011154A0u        // selection panel form (vtable 0x01115490), slot 4
#define SP_UPD_FN 0x00B3DA00u
typedef void (__thiscall *Upd_t)(char *form, float dt);
static Upd_t o_spUpd;
static void *g_mv; static int g_shown = -1; static DWORD g_check, g_lastCalc; static char g_last[600];
static void make_fields(void *mv) {
    // core_actions: Retreat (core_btn03) at 70.9,146.9 in it, 61 x 61; the order row (core_btn05..02) at y 81.6..134.7 from
    // x -135; the space left of Retreat under the row is empty down to the panel's (and the screen's) bottom edge - two
    // lines fit only right under the row (stand 1, 05.11: at Retreat's middle the second line fell off the screen).
    // The text ends 6 left of Retreat, right aligned.
    double h = 2.5 * g_size + 6, w = 470, x = 70.9 - 6 - w + g_offX, y = 134.7 + 1 + g_offY;
    for (int k = 0; k < 2; k++) {
        const char *nm = k ? "us_stats" : "us_stats_sh"; char p[96];
        GfxVal a[6] = { gv_str(nm), gv_num(16110 + k), gv_num(x + (k ? 0 : 1.2)), gv_num(y + (k ? 0 : 1.5)), gv_num(w), gv_num(h) };
        int ok = gfx_invoke(mv, "_root.core_actions.createTextField", NULL, a, 6);
        snprintf(p, sizeof p, "_root.core_actions.%s.html", nm); gfx_setvar(mv, p, gv_bool(1));
        snprintf(p, sizeof p, "_root.core_actions.%s.multiline", nm); gfx_setvar(mv, p, gv_bool(1));
        snprintf(p, sizeof p, "_root.core_actions.%s.selectable", nm); gfx_setvar(mv, p, gv_bool(0));
        if (!k) { snprintf(p, sizeof p, "_root.core_actions.%s._alpha", nm); gfx_setvar(mv, p, gv_num(75)); }
        if (!ok) logmsg("movie %p: createTextField %s failed", mv, nm);
    }
    logmsg("text fields in the selection panel movie %p, core_actions at %.1f,%.1f %gx%g: %s", mv, x, y, w, h,
           gfx_exists(mv, "core_actions.us_stats") ? "ok" : "NOT THERE");
}
static void fmtnum(char *o, size_t n, float v) { if (v < 0) snprintf(o, n, "-"); else if (v < 10) snprintf(o, n, "%.1f", v); else snprintf(o, n, "%.0f", v); }
static void set_text(void *mv, const Stats *s) {
    char a[6][16], t[480], h[600];
    fmtnum(a[0], 16, s->rdps); fmtnum(a[1], 16, s->mdps); snprintf(a[2], 16, "%d", s->skill);
    fmtnum(a[3], 16, s->range); fmtnum(a[4], 16, s->speed); fmtnum(a[5], 16, s->sight);
#define L(x) "<font color=\"#B8AE90\">" x "</font>"
    snprintf(t, sizeof t, L("Ranged DPS") " %s   " L("Melee DPS") " %s   " L("Melee skill") " %s<br>" L("Range") " %s   " L("Speed") " %s   " L("Sight") " %s",
             a[0], a[1], a[2], a[3], a[4], a[5]);
    snprintf(h, sizeof h, "<textformat leading=\"-2\"><p align=\"right\"><font face=\"$NormalFont\" size=\"%d\" color=\"#FFFFFF\">%s</font></p></textformat>", g_size, t);
    if (!strcmp(h, g_last)) return;
    strcpy(g_last, h);
    gfx_setvar(mv, "_root.core_actions.us_stats.htmlText", gv_str(h));
    char sh[600], *o = sh; const char *p = h;                 // the shadow: the same, black
    while (*p && o < sh + sizeof sh - 8) { if (!strncmp(p, "#B8AE90", 7) || !strncmp(p, "#FFFFFF", 7)) { memcpy(o, "#000000", 7); o += 7; p += 7; } else *o++ = *p++; }
    *o = 0;
    gfx_setvar(mv, "_root.core_actions.us_stats_sh.htmlText", gv_str(sh));
}
static void show(void *mv, int vis) {
    if (vis == g_shown) return;
    g_shown = vis;
    gfx_setvar(mv, "_root.core_actions.us_stats._visible", gv_bool(vis));
    gfx_setvar(mv, "_root.core_actions.us_stats_sh._visible", gv_bool(vis));
}
static void reload_ini(DWORD now) {
    static DWORD last; if (now - last < 1000) return; last = now;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a) || !CompareFileTime(&a.ftLastWriteTime, &g_iniTime)) return;
    load_ini(); g_mv = NULL; g_last[0] = 0;
    logmsg("UnitStats.ini re-read: Enabled=%d FontSize=%d OffsetX=%d OffsetY=%d", g_enabled, g_size, g_offX, g_offY);
}
static char *g_loggedSq[32]; static int g_nLogged;
static void frame(char *form) {
    void *mv = gfx_movie_of_form(form);
    if (!mv) return;
    DWORD now = GetTickCount();
    reload_ini(now);
    if (mv != g_mv || (now - g_check > 1000 && (g_check = now, !gfx_exists(mv, "core_actions.us_stats")))) {
        if (!gfx_exists(mv, "core_actions")) return;
        g_mv = mv; g_check = now; g_shown = -1; g_last[0] = 0;
        make_fields(mv);
    }
    if (now - g_lastCalc < 250) return;                  // four times a second is plenty
    g_lastCalc = now;
    Sel s; memset(&s, 0, sizeof s);
    if (!g_enabled || g_dead || !run_guarded(get_selection, &s, "the selection") || s.n != 1) { show(mv, 0); return; }
    Stats st; Job j = { s.squad, &st };
    if (!run_guarded(squad_stats, &j, "the squad's numbers") || !st.models) { show(mv, 0); return; }
    set_text(mv, &st);
    show(mv, 1);
    int seen = 0; for (int i = 0; i < g_nLogged; i++) if (g_loggedSq[i] == s.squad) seen = 1;
    if (g_verbose && !seen) {
        g_loggedSq[g_nLogged++ % 32] = s.squad; if (g_nLogged > 32) g_nLogged = 32;
        logmsg("squad %s: %d models, %d weapons - ranged DPS %.2f, melee DPS %.2f, melee skill %d, range %.1f, speed %.2f, sight %.1f |%s",
               p_sqName ? p_sqName(s.squad) : "?", st.models, st.weapons, st.rdps, st.mdps, st.skill, st.range, st.speed, st.sight, g_detail);
    }
}
static void __thiscall hk_sp_upd(char *form, float dt) { o_spUpd(form, dt); frame(form); }

static void *sim(const char *name) { HMODULE m = GetModuleHandleA("SimEngine.dll"); return m ? (void *)GetProcAddress(m, name) : NULL; }
static void *plat(const char *name) { HMODULE m = GetModuleHandleA("Platform.dll"); return m ? (void *)GetProcAddress(m, name) : NULL; }
static int resolve(void) {
    p_qi      = (QI_t)sim("?QI@EntityController@@QAEPAVEntityExtension@@I@Z");
    p_allEnts = (P0_t)sim("?GetAllEntities@Squad@@QAEABVEntityGroupTemp@@XZ");
    p_begin   = (P0_t)sim("?begin@EntityGroupTemp@@QAEPAPAVEntity@@XZ");
    p_end     = (P0_t)sim("?end@EntityGroupTemp@@QAEPAPAVEntity@@XZ");
    p_entBp   = (P0_t)sim("?GetBlueprint@Entity@@QBEPBVPropertyBagGroup@@XZ");
    p_sqName  = (S0_t)sim("?GetBlueprintName@Squad@@QBEPBDXZ");
    p_entName = (S0_t)sim("?GetBlueprintName@Entity@@QBEPBDXZ");
    p_pbgFile = (S0_t)sim("?GetFilename@PropertyBagGroup@@QBEPBDXZ");
    p_enter   = (M0_t)plat("?Enter@RecursiveMutex@Plat@@QAEXXZ");
    p_leave   = (M0_t)plat("?Leave@RecursiveMutex@Plat@@QAEXXZ");
    return p_qi && p_allEnts && p_begin && p_end && p_enter && p_leave && rd((void *)FN_MOD, 4) && !memcmp((void *)FN_MOD, MOD_PRO, 4);
}

static DWORD WINAPI worker(LPVOID _) {
    for (int i = 0; i < 120 && !(GetModuleHandleA("SimEngine.dll") && GetModuleHandleA("Platform.dll")); i++) Sleep(500);
    if (!resolve()) { logmsg("SimEngine.dll / Platform.dll functions or DOW2.exe 3.19 code not found - no numbers"); return 0; }
    AddVectoredExceptionHandler(1, us_veh);
    if (gfx_hook_slot(SP_VT_UPD, SP_UPD_FN, (void *)hk_sp_upd, (void **)&o_spUpd))
        logmsg("hooked the selection panel's per-frame update%s", (DWORD)o_spUpd != SP_UPD_FN ? " (after another add-on)" : "");
    else logmsg("selection panel form not as in DOW2.exe 3.19 - no numbers");
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    char mx[64]; wsprintfA(mx, "Local\\dow2patch_UnitStats_%lu", GetCurrentProcessId());
    CreateMutexA(NULL, FALSE, mx); if (GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
    static wchar_t dir[MAX_PATH];
    GetModuleFileNameW(h, dir, MAX_PATH); wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0; else dir[0] = 0;
    _snwprintf(g_ini, MAX_PATH, L"%lsUnitStats.ini", dir); _snwprintf(g_log, MAX_PATH, L"%lsUnitStats.log", dir);
    load_ini();
    logmsg("UnitStats 0.1 EXPERIMENT (selected squad's DPS / melee skill / range / speed / sight left of Retreat, in the game's own selection panel movie)");
    if (!g_enabled) { logmsg("disabled in UnitStats.ini"); return TRUE; }
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
