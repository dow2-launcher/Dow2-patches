// unitstats.c - UnitStats.dll, EXPERIMENT (asked by grantovich in Elite Mod - Dev, 04.11.2026): the selected squad's
// combat numbers in the empty space left of the Retreat button of the game's own selection panel, each behind a game icon:
//   row 1: ranged DPS, melee DPS, melee skill;  row 2: armour type, range, speed, sight.
// Archive branch only (archive/unitstats-experiment) - not for players, not in the store. How it works and what to do
// before it could ship: docs/UNIT_STATS_HOWTO.md.
//
// v2 (05.11.2026, after the Discord review: "15 melee skill banshees", "tacs are also wrong", nootersnooter: the real
// melee skill is in leveling_ext; grantovich: icons instead of words, and the armour type):
//   * melee skill = the game's own getter 0x862CB0 (what melee combat uses): leveling_ext attrib_levels[squad level]
//     melee_skill, else melee_ext weapon_skill, then the entity modifiers of type 0x2F (melee skill buffs);
//     v1 read only melee_ext weapon_skill - a number melee combat never uses when leveling_ext is there (banshee 15 vs 70);
//   * damage x the level's melee / ranged damage multiplier (0x9688E0, what the damage code 0xA27720 multiplies by);
//   * speed through the game's entity modifiers (0x8A6AC0, type 6 = speed_maximum_modifier, as 0x928050);
//   * armour type from type_ext (TypeExtInfo +0x24), named the way the game names it: the world tuning's
//     armour_type_names (the LocStrings the weapon tooltips print, 0x836F60), else the internal type name;
//   * squad total and, when every model is the same, the per-model number the unit tooltips quote ("44 (3x14.6)");
//   * icons: the game's own textures loaded into clips of the panel movie by its own Receive_LoadMovie
//     (MovieClipLoader.loadClip, "DATA:UI/..." paths as the engine passes them) - no new textures, no Direct3D.
//
// Shown only through the game's own interface (docs/UI_RULES.md, common/gfxui.h): a clip of ours ("us_box") inside the
// selection panel movie (ui\movies\hud\selection_panel), in its clip "core_actions" (the order buttons and Retreat,
// core_btn03) - it moves and scales with the panel (vanilla and Elite movies have the same core_actions inside).
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
//     4 = SightExt (vtable 0x010FA130): +0x28 the sight radius now (with modifiers; the game's Entity_GetSightRadius);
//     0x20 = MeleeExt (no melee extension: no melee skill shown).
//   the entity blueprint's bags (PropertyBagGroup +0x0C pairs {key, bag}, +0x10 count; keys are .data slots):
//     moving_ext 0x01301B60 -> MovingExtInfo +0x5C speed_max; melee_ext 0x01301B94 -> MeleeExtInfo +0x04 weapon_skill
//     (logged as v1's number); type_ext 0x01301A84 -> TypeExtInfo (vtable 0x010CFF8C) +0x24 armour type id.
//   DPS = the game's own formula (Util_GetWeaponDPS 0x934D70: ranged 0x81FC40, melee inline), with the weapon's current
//     damage, cooldown and reload through the game's own modifier function 0x8A6790 (weapon, value, entity, type:
//     7 damage, 3 cooldown, 2 reload, 0x17 max range) - so upgrades, wargear and buffs on the weapon count - times the
//     level multiplier. Range = the weapon's own GetMaxRange (vtable slot 7, modifiers included).
// Every engine read runs under a fault guard (vectored handler, this thread only): a fault ends the numbers for the
// rest of the game, the log says where.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wctype.h>
#include "../common/gfxui.h"

static wchar_t g_log[MAX_PATH], g_ini[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---------- the items and their icons (the game's own textures) ----------
enum { I_RANGED, I_MELEE, I_SKILL, I_ARMOUR, I_RANGE, I_SPEED, I_SIGHT, NITEMS };
enum { A_LIGHT, A_HEAVY, A_VEHICLE, A_BUILDING, NARM };
static const int ITEM_ROW[NITEMS] = { 0, 0, 0, 1, 1, 1, 1 };
static const wchar_t *ITEM_KEY[NITEMS] = { L"Ranged", L"Melee", L"Skill", L"Armour", L"Range", L"Speed", L"Sight" };
static const wchar_t *ARM_KEY[NARM] = { L"ArmourInfantry", L"ArmourHeavy", L"ArmourVehicle", L"ArmourBuilding" };
// "path|scale%": a texture of the game; scale% enlarges an icon with much empty border (default 100)
static const wchar_t *ITEM_DEF[NITEMS] = {
    L"img://data:UI/textures/space_marines/icon_movement/stance_ranged.dds",   // the panel's own stance button, ranged: a bolter
    L"img://data:UI/textures/space_marines/icon_movement/stance_melee.dds",    // the stance button, melee: a fist
    L"img://data:UI/textures/generic/decorators/mod_melee.dds",                // the melee modifier decorator: a red fist
    L"",                                                                       // armour: by type, below
    L"img://data:UI/textures/generic/tooltips/ico_range.dds|130",          // the weapon tooltip's range icon
    L"img://data:UI/textures/eldar/icon_traits/eld_swift.dds",                 // the Swift trait: a boot
    L"img://data:UI/textures/generic/fe/loading/tips/up_detector.dds|150",     // the detector loading tip: an eye
};
static const wchar_t *ARM_DEF[NARM] = {                              // the weapon tooltip's "effective against" icons
    L"img://data:UI/textures/generic/tooltips/ico_l_infantry.dds|130", L"img://data:UI/textures/generic/tooltips/ico_h_infantry.dds|130",
    L"img://data:UI/textures/generic/tooltips/ico_vehicles.dds|130", L"img://data:UI/textures/generic/tooltips/ico_buildings.dds|130" };
// paths as the GFx loader of this game takes them: "img://data:UI/..." (plain "DATA:..." / "data:...", the way the movies
// import their own bitmaps, are not loaded by MovieClipLoader here - stand 1, 05.11.2026); .dds only (.tga comes out garbled)
// An atlas cell cannot be cut out: setMask (by name or "_parent.m") is ignored by this GFx, the drawn mask shows as a white
// square and the whole atlas is drawn (stand 1, 05.11.2026, attrib_icons.dds) - so whole textures only.
typedef struct { char path[200]; int scale; } Icon;
static Icon g_icon[NITEMS], g_armIcon[NARM];

// ---------- settings: UnitStats.ini [UnitStats], [Icons] (re-read while the game runs) ----------
static int g_useSkill = 1, g_useEmod = 1, g_useLvl = 1;   // [UnitStats] GameSkill / EntityMods / LevelMul: the engine calls, each can be switched off
static int g_enabled = 1, g_size = 22, g_isize = 0, g_offX = 0, g_offY = 0, g_perModel = 1, g_verbose = 1;
static FILETIME g_iniTime;
static int ini_int(const wchar_t *k, int def, int lo, int hi) { int v = GetPrivateProfileIntW(L"UnitStats", k, def, g_ini); return v < lo ? lo : v > hi ? hi : v; }
static void ini_icon(const wchar_t *key, const wchar_t *def, Icon *ic) {
    wchar_t w[256]; char s[256];
    GetPrivateProfileStringW(L"Icons", key, def, w, 256, g_ini);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, sizeof s, NULL, NULL);
    memset(ic, 0, sizeof *ic); ic->scale = 100;
    char *bar = strchr(s, '|'); if (bar) { *bar = 0; sscanf(bar + 1, "%d", &ic->scale); }
    if (ic->scale < 20 || ic->scale > 400) ic->scale = 100;
    snprintf(ic->path, sizeof ic->path, "%s", s);
}
static void load_ini(void) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a)) g_iniTime = a.ftLastWriteTime;
    g_enabled = ini_int(L"Enabled", 1, 0, 1);
    g_size = ini_int(L"FontSize", 22, 8, 40);
    g_isize = ini_int(L"IconSize", 0, 0, 60);
    g_offX = ini_int(L"OffsetX", 0, -800, 800);
    g_offY = ini_int(L"OffsetY", 0, -800, 800);
    g_perModel = ini_int(L"PerModel", 1, 0, 1);
    g_verbose = ini_int(L"Verbose", 1, 0, 1);
    g_useSkill = ini_int(L"GameSkill", 1, 0, 1); g_useEmod = ini_int(L"EntityMods", 1, 0, 1); g_useLvl = ini_int(L"LevelMul", 1, 0, 1);
    for (int k = 0; k < NITEMS; k++) if (k != I_ARMOUR) ini_icon(ITEM_KEY[k], ITEM_DEF[k], &g_icon[k]);
    for (int k = 0; k < NARM; k++) ini_icon(ARM_KEY[k], ARM_DEF[k], &g_armIcon[k]);
}

// ---------- the engine (DOW2.exe 3.19, SimEngine.dll, Platform.dll) ----------
#define SEL_ROOT      0x01335720u
#define G_WORLD       0x013356A4u
#define G_FAMILIES    0x013356B0u        // **: +0xC83C armour type names (0x100 each), +0xC840 their count (TypeExtInfo 0x7E3120)
#define IAT_LOC_CSTR  0x00F874C8u        // DOW2.exe's import of LocString::c_str
#define VT_COMBATEXT  0x010DE6F0u
#define VT_SIGHTEXT   0x010FA130u
#define VT_WEAPONBAG  0x010D68B4u
#define VT_MOVINGINFO 0x010F1404u
#define VT_MELEEINFO  0x010F8F7Cu
#define VT_TYPEINFO   0x010CFF8Cu
#define KEY_MOVING    0x01301B60u
#define KEY_MELEE     0x01301B94u
#define KEY_TYPE      0x01301A84u
#define FN_MOD        0x008A6790u
#define FN_EMOD       0x008A6AC0u
#define FN_SKILL      0x00862CB0u
#define FN_LVLMUL     0x009688E0u
#define FN_TUNING     0x00445F50u
#define EXT_COMBAT    6
#define EXT_SIGHT     4
#define EXT_MELEE     0x20
#define MOD_RELOAD    2
#define MOD_COOLDOWN  3
#define MOD_DAMAGE    7
#define MOD_RANGE     0x17
#define EMOD_SPEED    6
static const unsigned char MOD_PRO[4] = {0x51, 0x0F, 0x57, 0xC0};      // 0x8A6790: push ecx; xorps xmm0,xmm0
static const unsigned char SKILL_PRO[4] = {0x85, 0xF6, 0x74, 0x31};    // 0x862CB0: test esi,esi; jz
static const unsigned char LVL_PRO[4] = {0x85, 0xFF, 0x53, 0x8A};      // 0x9688E0: test edi,edi; push ebx; mov bl,..
static const unsigned char TUN_PRO[4] = {0x53, 0x56, 0x57, 0x8B};      // 0x445F50: push ebx/esi/edi; mov edi,..
static const unsigned char COMB_PRO[4] = {0x83, 0x7E, 0x4C, 0x07};    // 0x7E2F50: cmp [esi+0x4c],7
static const unsigned char EMOD_PRO[4] = {0x0F, 0x57, 0xC0, 0xF3};     // 0x8A6AC0: xorps xmm0,xmm0; movss xmm1,..

typedef void *(__thiscall *QI_t)(void *ctrl, unsigned id);
typedef void *(__thiscall *P0_t)(void *obj);
typedef const char *(__thiscall *S0_t)(const void *obj);
typedef const wchar_t *(__thiscall *W0_t)(const void *obj);
typedef void (__thiscall *M0_t)(void *mutex);
typedef void *(__thiscall *Lookup_t)(void *obj, DWORD id);
typedef float (__thiscall *Range_t)(void *weapon);
static QI_t p_qi; static P0_t p_allEnts, p_begin, p_end, p_entBp; static S0_t p_sqName, p_entName, p_pbgFile; static M0_t p_enter, p_leave;
static int g_haveSkill, g_haveLvl, g_haveTun, g_haveEmod, g_haveComb; static float g_rangeAllow;

// the game's weapon modifier function: thiscall (weapon, float value, Entity *, int type), the result in xmm0
float us_mod(void *weapon, float v, void *ent, int type) __asm__("us_mod");
// the entity modifier function: stdcall (float value, Entity *, int type, bool disabled), ret 0x10, the result in xmm0
float us_emod(float v, void *ent, int type) __asm__("us_emod");
// the melee skill melee combat uses: ESI = entity, the result in EAX
int us_skill(void *ent) __asm__("us_skill");
// the level's damage multiplier: EDI = entity, [esp+4] = melee (byte), ret 4, the result in xmm0
float us_lvlmul(void *ent, int melee) __asm__("us_lvlmul");
// the world tuning (property bag group type 7): EAX
void *us_tuning(void) __asm__("us_tuning");
// ModTuning::CombatInfo of the same group (0x7E2F50, ESI = group): EAX; its +0x2C is added to every weapon's max range at load (0x821E60)
void *us_combat(void) __asm__("us_combat");
__asm__(".text\n"
    "us_mod:\n\t"
    "movl 4(%esp), %ecx\n\t"
    "pushl 16(%esp)\n\t"                             // type
    "pushl 16(%esp)\n\t"                             // entity
    "pushl 16(%esp)\n\t"                             // value
    "movl $0x008A6790, %eax\n\t"
    "call *%eax\n\t"                                 // ret 0xC
    "subl $4, %esp\n\t movss %xmm0, (%esp)\n\t flds (%esp)\n\t addl $4, %esp\n\t ret\n"
    "us_emod:\n\t"
    "pushl $0\n\t"                                   // not the disabled ones
    "pushl 16(%esp)\n\t"                             // type
    "pushl 16(%esp)\n\t"                             // entity
    "pushl 16(%esp)\n\t"                             // value
    "movl $0x008A6AC0, %eax\n\t"
    "call *%eax\n\t"                                 // ret 0x10
    "subl $4, %esp\n\t movss %xmm0, (%esp)\n\t flds (%esp)\n\t addl $4, %esp\n\t ret\n"
    "us_skill:\n\t"
    "pushl %esi\n\t movl 8(%esp), %esi\n\t"
    "movl $0x00862CB0, %eax\n\t call *%eax\n\t"
    "popl %esi\n\t ret\n"
    "us_lvlmul:\n\t"
    "pushl %edi\n\t movl 8(%esp), %edi\n\t"
    "pushl 12(%esp)\n\t"                             // melee
    "movl $0x009688E0, %eax\n\t call *%eax\n\t"      // ret 4
    "popl %edi\n\t"
    "subl $4, %esp\n\t movss %xmm0, (%esp)\n\t flds (%esp)\n\t addl $4, %esp\n\t ret\n"
    "us_tuning:\n\t"
    "pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t"
    "movl $0x00445F50, %eax\n\t call *%eax\n\t"
    "popl %edi\n\t popl %esi\n\t popl %ebx\n\t ret\n"
    "us_combat:\n\t"
    "pushl %ebx\n\t pushl %esi\n\t pushl %edi\n\t"
    "pushl $0\n\t pushl $7\n\t call *0x00F88130\n\t"          // as 0x445F50: PropertyBagGroupManager::Instance, GetGroup(7, 0)
    "movl %eax, %ecx\n\t call *0x00F88134\n\t"
    "testl %eax, %eax\n\t jz 1f\n\t"
    "movl %eax, %esi\n\t movl $0x007E2F50, %eax\n\t call *%eax\n"
    "1:\n\t popl %edi\n\t popl %esi\n\t popl %ebx\n\t ret\n");

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
typedef struct {
    float rdps, mdps, range, speed, sight, rdps1, mdps1;      // squad totals; *1 = per model when every model is the same
    int skill, skillOld, models, weapons, armour, sameR, sameM, nRanged, nMelee;
    float range0, speed0;                                     // blueprint values (the log)
    char armourName[96];
} Stats;
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
    if (g_haveLvl && g_useLvl) dps *= us_lvlmul(ent, *melee);                         // the level's damage multiplier (0xA27720)
    return dps > 0 ? dps : 0;
}
static char g_detail[3072]; static int g_detailN;
static void detail(const char *fmt, ...) {
    if (g_detailN >= (int)sizeof g_detail - 2) return;
    va_list a; va_start(a, fmt); int n = vsnprintf(g_detail + g_detailN, sizeof g_detail - g_detailN, fmt, a); va_end(a);
    if (n > 0) g_detailN += n; if (g_detailN > (int)sizeof g_detail - 1) g_detailN = sizeof g_detail - 1;
}
// the armour type's name as the game prints it: the world tuning's armour_type_names (LocStrings, 8 bytes each, at
// tuning +0x414..+0x418 - what 0x836F60 returns), else the internal family name ("super_heavy_infantry" -> "super heavy infantry")
static void armour_name(int id, char *out, int n) {
    out[0] = 0;
    if (id < 0) return;
    if (g_haveTun && rd((void *)IAT_LOC_CSTR, 4)) {
        char *tun = us_tuning();
        if (rd(tun + 0x414, 8)) {
            char *b = *(char **)(tun + 0x414), *e = *(char **)(tun + 0x418);
            if (b && e > b && (e - b) / 8 > id && rd(b + id * 8, 8)) {
                const wchar_t *ws = (*(W0_t *)IAT_LOC_CSTR)(b + id * 8);
                if (ws && rd(ws, 2) && ws[0]) {
                    wchar_t t[64]; int k = 0; for (; k < 63 && ws[k]; k++) t[k] = ws[k]; t[k] = 0;
                    t[0] = towupper(t[0]);
                    WideCharToMultiByte(CP_UTF8, 0, t, -1, out, n, NULL, NULL);
                    return;
                }
            }
        }
    }
    char **pp = (char **)G_FAMILIES; if (!rd(pp, 4) || !rd(*pp, 4)) return;
    char *fam = *(char **)*pp; if (!rd(fam + 0xC83C, 8)) return;
    char *names = *(char **)(fam + 0xC83C); unsigned cnt = *(unsigned *)(fam + 0xC840);
    if ((unsigned)id >= cnt || cnt > 64 || !rd(names + id * 0x100, 64)) return;
    snprintf(out, n, "%.60s", names + id * 0x100);
    for (char *c = out; *c; c++) if (*c == '_') *c = ' ';
    if (out[0] >= 'a' && out[0] <= 'z') out[0] -= 32;
}
static int armour_class(const char *internal) {
    if (strstr(internal, "vehicle")) return A_VEHICLE;
    if (strstr(internal, "building")) return A_BUILDING;
    if (strstr(internal, "heavy") || strstr(internal, "commander")) return A_HEAVY;
    return A_LIGHT;
}
static const char *armour_internal(int id) {
    char **pp = (char **)G_FAMILIES; if (id < 0 || !rd(pp, 4) || !rd(*pp, 4)) return "";
    char *fam = *(char **)*pp; if (!rd(fam + 0xC83C, 8)) return "";
    char *names = *(char **)(fam + 0xC83C); unsigned cnt = *(unsigned *)(fam + 0xC840);
    return (unsigned)id < cnt && cnt <= 64 && rd(names + id * 0x100, 64) ? names + id * 0x100 : "";
}
typedef struct { char *squad; Stats *st; } Job;
static void squad_stats(void *arg) {
    Job *j = arg; Stats *st = j->st; memset(st, 0, sizeof *st); st->speed = st->speed0 = -1; st->skill = st->skillOld = -1; st->armour = -1;
    st->sameR = st->sameM = 1;
    g_detailN = 0; g_detail[0] = 0;
    static int allowDone;
    if (!allowDone && g_haveComb) {                      // the engine's range allowance, once (a tuning value, 1.0 in vanilla and Elite)
        allowDone = 1; char *c = us_combat();
        if (rd(c + 0x2C, 4)) { float a = *(float *)(c + 0x2C); if (a >= 0 && a <= 10) g_rangeAllow = a; }
        logmsg("range allowance of the combat tuning (added to every weapon's max range at load): %.2f", g_rangeAllow);
    }
    char *grp = p_allEnts(j->squad); if (!grp) return;
    char **b = p_begin(grp), **e = p_end(grp);
    int armN[64] = {0}, armId[64], nArm = 0;
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
                float rng = melee ? 0 : (*(Range_t *)(*(char **)w + 0x1C))(w) - g_rangeAllow;   // the weapon's range as written (and quoted)
                float rng0 = *(float *)(bag + 0xC4), rngW = melee ? 0 : us_mod(w, rng0, NULL, MOD_RANGE);
                st->weapons++; if (melee) st->nMelee++; else st->nRanged++;
                if (melee) { if (dps > emd) emd = dps; }                  // one melee attack at a time
                else { erd += dps; if (rng > st->range) st->range = rng; if (rng0 - g_rangeAllow > st->range0) st->range0 = rng0 - g_rangeAllow; }   // every gun fires
                const char *wf = p_pbgFile && rd(*(char **)(w + 0x4C), 4) ? p_pbgFile(*(char **)(w + 0x4C)) : "?";
                const char *sl = wf ? strrchr(wf, '\\') : NULL; if (!sl && wf) sl = strrchr(wf, '/');
                detail(" %s%s %.2f dps (dmg %.2f->%.2f, ticks cd %d wu %d wd %d reload %d-%d x%d-%d burst %d %d-%d rof %.3f, lvl x%.2f, range bag %.1f weapon %.1f shown %.1f)",
                       melee ? "melee " : "", sl ? sl + 1 : wf, dps,
                       *(float *)(bag + 0x64), us_mod(w, *(float *)(bag + 0x64), went, MOD_DAMAGE), *(int *)(bag + 0xF0),
                       *(int *)(bag + 0x10C), *(int *)(bag + 0x110), *(int *)(bag + 0x11C), *(int *)(bag + 0x120), *(int *)(bag + 0x124),
                       *(int *)(bag + 0x128), *(bag + 0x170), *(int *)(bag + 0x174), *(int *)(bag + 0x178), *(float *)(bag + 0x17C),
                       g_haveLvl && g_useLvl ? us_lvlmul(went, melee) : 1.0f, rng0, rngW, rng);
            }
        }
        if (st->models == 1) { st->rdps1 = erd; st->mdps1 = emd; }
        else { if (erd < st->rdps1 - 0.05f || erd > st->rdps1 + 0.05f) st->sameR = 0; if (emd < st->mdps1 - 0.05f || emd > st->mdps1 + 0.05f) st->sameM = 0; }
        st->rdps += erd; st->mdps += emd;
        char *se = p_qi(ctrl, EXT_SIGHT);
        if (vt_is(se, VT_SIGHTEXT)) { float s = *(float *)(se + 0x28); if (s > st->sight) st->sight = s; }
        int sk = -1;
        if (g_haveSkill && g_useSkill && p_qi(ctrl, EXT_MELEE)) { sk = us_skill(ent); if (sk > st->skill) st->skill = sk; }
        const char *pbg = p_entBp ? (const char *)p_entBp(ent) : NULL;
        if (pbg) {
            char *mv = pbg_bag(pbg, KEY_MOVING), *ml = pbg_bag(pbg, KEY_MELEE), *ty = pbg_bag(pbg, KEY_TYPE);
            if (vt_is(mv, VT_MOVINGINFO)) {
                float sp0 = *(float *)(mv + 0x5C), sp = g_haveEmod && g_useEmod && sp0 > 0 ? us_emod(sp0, ent, EMOD_SPEED) : sp0;
                if (sp0 > 0 && (st->speed < 0 || sp < st->speed)) { st->speed = sp; st->speed0 = sp0; }
                detail(" speed %.2f->%.2f", sp0, sp);
            }
            if (vt_is(ml, VT_MELEEINFO)) { int k = *(int *)(ml + 4); if (k > st->skillOld) st->skillOld = k; detail(" skill melee_ext %d", k); }
            if (vt_is(ty, VT_TYPEINFO)) {
                int a = *(int *)(ty + 0x24), f = 0;
                for (int q = 0; q < nArm; q++) if (armId[q] == a) { armN[q]++; f = 1; }
                if (!f && nArm < 64) { armId[nArm] = a; armN[nArm++] = 1; }
                detail(" armour %d %s", a, armour_internal(a));
            }
        }
        if (sk >= 0) detail(" skill now %d", sk);
        detail("]");
    }
    for (int q = 0, best = 0; q < nArm; q++) if (armN[q] > best) { best = armN[q]; st->armour = armId[q]; }   // most of the models
    armour_name(st->armour, st->armourName, sizeof st->armourName);
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

// ---------- the panel: a clip of ours in core_actions, an icon and a text (with a black copy as its shadow) per item ----------
#define SP_VT_UPD 0x011154A0u        // selection panel form (vtable 0x01115490), slot 4
#define SP_UPD_FN 0x00B3DA00u
#define BOX "_root.core_actions.us_box"
typedef void (__thiscall *Upd_t)(char *form, float dt);
static Upd_t o_spUpd;
static void *g_mv; static int g_shown = -1; static DWORD g_check, g_lastCalc;
static char g_txt[NITEMS][200]; static int g_iconDone[NITEMS]; static char g_iconPath[NITEMS][200]; static int g_layoutDirty;
static double g_lineH, g_iconS; static int g_iconSc[NITEMS];
static void setnum(void *mv, const char *fmt, int k, const char *prop, double v) {
    char p[96]; snprintf(p, sizeof p, fmt, k); strcat(p, prop); gfx_setvar(mv, p, gv_num(v));
}
static int invoke_n(void *mv, const char *m, GfxVal *a, int n) { return gfx_invoke(mv, m, NULL, a, n); }
static double clip_w(void *mv, const char *path) {
    GfxVal a = gv_str(path), r; memset(&r, 0, sizeof r);
    if (!gfx_invoke(mv, "Receive_GetMovieClipWidth", &r, &a, 1) || r.type != GV_NUMBER) return -1;
    return r.v.num;
}
static double clip_h(void *mv, const char *path) {
    GfxVal a = gv_str(path), r; memset(&r, 0, sizeof r);
    if (!gfx_invoke(mv, "Receive_GetMovieClipHeight", &r, &a, 1) || r.type != GV_NUMBER) return -1;
    return r.v.num;
}
static void make_fields(void *mv) {
    // core_actions: Retreat (core_btn03) at 70.9,146.9 in it, 61 x 61; the order row (core_btn05..02) at y 81.6..134.7 from
    // x -135; the space left of Retreat under the row is empty down to the panel's bottom edge (~208 in core_actions): two
    // rows of icon + number right under the row, right aligned 6 left of Retreat (vanilla and Elite movies alike).
    g_lineH = 1.3 * g_size + 4; g_iconS = g_isize ? g_isize : 1.2 * g_size;
    GfxVal a[6] = { gv_str("us_box"), gv_num(16110) };
    if (!invoke_n(mv, "_root.core_actions.createEmptyMovieClip", a, 2)) logmsg("movie %p: createEmptyMovieClip us_box failed", mv);
    for (int k = 0; k < NITEMS; k++) {
        char nm[16], p[96];
        for (int s = 0; s < 2; s++) {                    // s = 0 the shadow (below), 1 the text
            snprintf(nm, sizeof nm, s ? "t%d" : "s%d", k);
            GfxVal b[6] = { gv_str(nm), gv_num(10 + 2 * k + s), gv_num(0), gv_num(0), gv_num(10), gv_num(g_lineH) };
            invoke_n(mv, BOX ".createTextField", b, 6);
            snprintf(p, sizeof p, BOX ".%s.html", nm); gfx_setvar(mv, p, gv_bool(1));
            snprintf(p, sizeof p, BOX ".%s.selectable", nm); gfx_setvar(mv, p, gv_bool(0));
            snprintf(p, sizeof p, BOX ".%s.autoSize", nm); gfx_setvar(mv, p, gv_str("left"));
            if (!s) { snprintf(p, sizeof p, BOX ".%s._alpha", nm); gfx_setvar(mv, p, gv_num(75)); }
        }
        snprintf(nm, sizeof nm, "i%d", k);
        GfxVal c[2] = { gv_str(nm), gv_num(100 + k) };
        invoke_n(mv, BOX ".createEmptyMovieClip", c, 2);
        snprintf(p, sizeof p, BOX ".i%d.createEmptyMovieClip", k);
        GfxVal d[2] = { gv_str("h"), gv_num(1) }; invoke_n(mv, p, d, 2);
        snprintf(p, sizeof p, BOX ".i%d.h.createEmptyMovieClip", k);
        GfxVal im[2] = { gv_str("img"), gv_num(1) }; invoke_n(mv, p, im, 2);
        g_txt[k][0] = 0; g_iconDone[k] = 0; g_iconPath[k][0] = 0;
    }
    g_layoutDirty = 1;
    logmsg("clip us_box in the selection panel movie %p, core_actions: %s (font %d, icons %g, line %g)", mv,
           gfx_exists(mv, "core_actions.us_box.t0") ? "ok" : "NOT THERE", g_size, g_iconS, g_lineH);
}
// an icon: the texture into i<k>.h.img by the movie's own Receive_LoadMovie; once it is there (its size known), i<k> is
// scaled to the icon size
static void set_icon(void *mv, int k, const Icon *ic) {
    char p[128];
    if (strcmp(g_iconPath[k], ic->path)) {
        snprintf(g_iconPath[k], sizeof g_iconPath[k], "%s", ic->path); g_iconDone[k] = 0;
        snprintf(p, sizeof p, BOX ".i%d.h.img", k);
        GfxVal a[2] = { gv_str(p), gv_str(ic->path) };
        invoke_n(mv, "Receive_LoadMovie", a, 2);
        snprintf(p, sizeof p, BOX ".i%d._visible", k); gfx_setvar(mv, p, gv_bool(0));
        return;
    }
    if (g_iconDone[k] > 0) return;
    snprintf(p, sizeof p, BOX ".i%d.h.img", k);
    double w = clip_w(mv, p), h = clip_h(mv, p);
    if (w <= 0 || h <= 0) {                            // not loaded yet
        int t = -g_iconDone[k]; g_iconDone[k]--;
        if (g_verbose && (t == 4 || t == 12 || t == 40)) logmsg("icon %d not there after %d checks: %s (width %g, height %g, clip %s)", k, t, ic->path, w, h,
                                              gfx_exists(mv, p + 6) ? "exists" : "MISSING");
        if (t == 8) {                                  // the movie's loader did not take it: the clip's own loadMovie
            char q[128]; snprintf(q, sizeof q, "%s.loadMovie", p); GfxVal a = gv_str(ic->path); invoke_n(mv, q, &a, 1);
            if (g_verbose) logmsg("icon %d: %s.loadMovie(%s)", k, p, ic->path);
        }
        return;
    }
    double sc = ic->scale * g_iconS / (w > h ? w : h);
    snprintf(p, sizeof p, BOX ".i%d._xscale", k); gfx_setvar(mv, p, gv_num(sc));
    snprintf(p, sizeof p, BOX ".i%d._yscale", k); gfx_setvar(mv, p, gv_num(sc));
    snprintf(p, sizeof p, BOX ".i%d._visible", k); gfx_setvar(mv, p, gv_bool(1));
    g_iconSc[k] = ic->scale;
    if (g_verbose) logmsg("icon %d loaded: %s %gx%g -> scale %.1f%%", k, ic->path, w, h, sc);
    g_iconDone[k] = 1; g_layoutDirty = 1;
}
static void fmtnum(char *o, size_t n, float v) { if (v < 0) snprintf(o, n, "-"); else if (v < 10) snprintf(o, n, "%.1f", v); else snprintf(o, n, "%.0f", v); }
static void fmtper(char *o, size_t n, float v) {             // the per-model number as the unit tooltips quote it: 14.58 -> 14.6
    if (v < 100 && (v - (int)v > 0.05f && v - (int)v < 0.95f)) snprintf(o, n, "%.1f", v); else snprintf(o, n, "%.0f", v);
}
static void set_item_text(void *mv, int k, const char *val, const char *small) {
    char h[200], sh[200];
    snprintf(h, sizeof h, "<font face=\"$NormalFont\" size=\"%d\" color=\"#FFFFFF\">%s</font>", g_size, val);
    if (small && *small) snprintf(h, sizeof h, "<font face=\"$NormalFont\" size=\"%d\" color=\"#FFFFFF\">%s<font size=\"%d\" color=\"#B8AE90\"> %s</font></font>",
                                  g_size, val, g_size * 3 / 4, small);
    if (!strcmp(h, g_txt[k])) return;
    strcpy(g_txt[k], h); g_layoutDirty = 1;
    char p[96]; snprintf(p, sizeof p, BOX ".t%d.htmlText", k); gfx_setvar(mv, p, gv_str(h));
    char *o = sh; const char *q = h;                 // the shadow: the same, black
    while (*q && o < sh + sizeof sh - 8) { if (!strncmp(q, "#B8AE90", 7) || !strncmp(q, "#FFFFFF", 7)) { memcpy(o, "#000000", 7); o += 7; q += 7; } else *o++ = *q++; }
    *o = 0;
    snprintf(p, sizeof p, BOX ".s%d.htmlText", k); gfx_setvar(mv, p, gv_str(sh));
}
static void layout(void *mv) {
    double right = 70.9 - 6 + g_offX, y0 = 134.7 + 2 + g_offY, gapI = 0.2 * g_size, gapItem = 0.75 * g_size;
    for (int row = 1; row >= 0; row--) {
        double x = right, y = y0 + row * g_lineH;
        for (int k = NITEMS - 1; k >= 0; k--) {
            if (ITEM_ROW[k] != row) continue;
            char p[96]; snprintf(p, sizeof p, BOX ".t%d", k);
            double w = clip_w(mv, p); if (w < 0) w = 0.55 * g_size * (strlen(g_txt[k]) > 90 ? 6 : 3);
            x -= w;
            setnum(mv, BOX ".t%d", k, "._x", x); setnum(mv, BOX ".t%d", k, "._y", y);
            setnum(mv, BOX ".s%d", k, "._x", x + 1.2); setnum(mv, BOX ".s%d", k, "._y", y + 1.5);
            x -= gapI + g_iconS;
            double ov = g_iconS * (g_iconSc[k] - 100) / 200.0;          // a scaled-up icon overflows its box evenly
            setnum(mv, BOX ".i%d", k, "._x", x - ov); setnum(mv, BOX ".i%d", k, "._y", y + (g_lineH - g_iconS) / 2 + 1 - ov);
            x -= gapItem;
        }
    }
    g_layoutDirty = 0;
}
static void set_stats(void *mv, const Stats *s) {
    char v[32], sm[48];
    int pm = g_perModel && s->models > 1;
    if (s->nRanged) fmtnum(v, sizeof v, s->rdps); else snprintf(v, sizeof v, "-");
    sm[0] = 0;
    if (pm && s->sameR && s->rdps > 0) { char q[16]; fmtper(q, sizeof q, s->rdps1); snprintf(sm, sizeof sm, "(%d\xC3\x97%s)", s->models, q); }
    set_item_text(mv, I_RANGED, v, sm);
    if (s->nMelee) fmtnum(v, sizeof v, s->mdps); else snprintf(v, sizeof v, "-");
    sm[0] = 0;
    if (pm && s->sameM && s->mdps > 0) { char q[16]; fmtper(q, sizeof q, s->mdps1); snprintf(sm, sizeof sm, "(%d\xC3\x97%s)", s->models, q); }
    set_item_text(mv, I_MELEE, v, sm);
    if (s->skill >= 0) snprintf(v, sizeof v, "%d", s->skill); else snprintf(v, sizeof v, "-");
    set_item_text(mv, I_SKILL, v, NULL);
    set_item_text(mv, I_ARMOUR, s->armourName[0] ? s->armourName : "-", NULL);
    if (s->nRanged) fmtnum(v, sizeof v, s->range); else snprintf(v, sizeof v, "-");
    set_item_text(mv, I_RANGE, v, NULL);
    fmtnum(v, sizeof v, s->speed); set_item_text(mv, I_SPEED, v, NULL);
    fmtnum(v, sizeof v, s->sight); set_item_text(mv, I_SIGHT, v, NULL);
    for (int k = 0; k < NITEMS; k++)
        set_icon(mv, k, k == I_ARMOUR ? &g_armIcon[armour_class(armour_internal(s->armour))] : &g_icon[k]);
    if (g_layoutDirty) layout(mv);
}
static void show(void *mv, int vis) {
    if (vis == g_shown) return;
    g_shown = vis;
    gfx_setvar(mv, BOX "._visible", gv_bool(vis));
}
static void reload_ini(DWORD now) {
    static DWORD last; if (now - last < 1000) return; last = now;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a) || !CompareFileTime(&a.ftLastWriteTime, &g_iniTime)) return;
    load_ini(); g_mv = NULL;
    logmsg("UnitStats.ini re-read: Enabled=%d FontSize=%d IconSize=%d OffsetX=%d OffsetY=%d PerModel=%d", g_enabled, g_size, g_isize, g_offX, g_offY, g_perModel);
}
static char *g_loggedSq[32]; static int g_nLogged; static char *g_lastSq;
static void frame(char *form) {
    void *mv = gfx_movie_of_form(form);
    if (!mv) return;
    DWORD now = GetTickCount();
    reload_ini(now);
    if (mv != g_mv || (now - g_check > 1000 && (g_check = now, !gfx_exists(mv, "core_actions.us_box.t0")))) {
        if (!gfx_exists(mv, "core_actions")) return;
        g_mv = mv; g_check = now; g_shown = -1;
        make_fields(mv);
    }
    if (now - g_lastCalc < 250) return;                  // four times a second is plenty
    g_lastCalc = now;
    Sel s; memset(&s, 0, sizeof s);
    if (!g_enabled || g_dead || !run_guarded(get_selection, &s, "the selection") || s.n != 1) { show(mv, 0); g_lastSq = NULL; return; }
    Stats st; Job j = { s.squad, &st };
    if (!run_guarded(squad_stats, &j, "the squad's numbers") || !st.models) { show(mv, 0); return; }
    set_stats(mv, &st);
    show(mv, 1);
    if (g_verbose && s.squad != g_lastSq) {
        logmsg("sel %s: ranged %.2f, melee %.2f, skill %d (v1 %d), %s, range %.1f, speed %.2f, sight %.1f",
               p_sqName ? p_sqName(s.squad) : "?", st.rdps, st.mdps, st.skill, st.skillOld, st.armourName, st.range, st.speed, st.sight);
    }
    g_lastSq = s.squad;
    int seen = 0; for (int i = 0; i < g_nLogged; i++) if (g_loggedSq[i] == s.squad) seen = 1;
    if (g_verbose && !seen) {
        g_loggedSq[g_nLogged++ % 32] = s.squad; if (g_nLogged > 32) g_nLogged = 32;
        logmsg("squad %s: %d models, %d weapons - ranged DPS %.2f (per model %.2f%s), melee DPS %.2f (per model %.2f%s), melee skill %d (melee_ext %d), "
               "armour %d %s / %s, range %.1f (blueprint %.1f), speed %.2f (blueprint %.2f), sight %.1f |%s",
               p_sqName ? p_sqName(s.squad) : "?", st.models, st.weapons, st.rdps, st.rdps1, st.sameR ? "" : " differ", st.mdps, st.mdps1,
               st.sameM ? "" : " differ", st.skill, st.skillOld, st.armour, armour_internal(st.armour), st.armourName, st.range, st.range0,
               st.speed, st.speed0, st.sight, g_detail);
    }
}
static void __thiscall hk_sp_upd(char *form, float dt) { o_spUpd(form, dt); frame(form); }

static void *sim(const char *name) { HMODULE m = GetModuleHandleA("SimEngine.dll"); return m ? (void *)GetProcAddress(m, name) : NULL; }
static void *plat(const char *name) { HMODULE m = GetModuleHandleA("Platform.dll"); return m ? (void *)GetProcAddress(m, name) : NULL; }
static int code_is(DWORD at, const unsigned char *pro, int n) { return rd((void *)at, n) && !memcmp((void *)at, pro, n); }
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
    g_haveSkill = code_is(FN_SKILL, SKILL_PRO, 4);
    g_haveLvl = code_is(FN_LVLMUL, LVL_PRO, 4);
    g_haveTun = code_is(FN_TUNING, TUN_PRO, 4);
    g_haveEmod = code_is(FN_EMOD, EMOD_PRO, 4);
    g_haveComb = g_haveTun && code_is(0x007E2F50, COMB_PRO, 4) && rd((void *)0x00F88130, 8);
    logmsg("engine: melee skill %s, level multiplier %s, world tuning %s, entity modifiers %s, combat tuning %s", g_haveSkill ? "ok" : "NOT FOUND",
           g_haveLvl ? "ok" : "NOT FOUND", g_haveTun ? "ok" : "NOT FOUND", g_haveEmod ? "ok" : "NOT FOUND", g_haveComb ? "ok" : "NOT FOUND");
    return p_qi && p_allEnts && p_begin && p_end && p_enter && p_leave && code_is(FN_MOD, MOD_PRO, 4);
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
    logmsg("UnitStats 0.2 EXPERIMENT (selected squad's DPS / melee skill / armour / range / speed / sight with the game's icons left of Retreat, in the game's own selection panel movie)");
    if (!g_enabled) { logmsg("disabled in UnitStats.ini"); return TRUE; }
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
