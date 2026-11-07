// unitstats.c - UnitStats.dll (asked by grantovich in Elite Mod - Dev, 04.11.2026): the selected squad's
// combat numbers in the empty space left of the Retreat button of the game's own selection panel, each behind a game icon:
//   row 1: ranged DPS, melee DPS, melee skill;  row 2: armour type, range, speed, sight.
// 0.5 (07.11.2026): an add-on of its own in the launcher (store id unitstats, experiment, off by default); the DPS shown is
// the attribute editor's (see weapon_dps). History of the experiment (0.1-0.4): docs/UNIT_STATS_HOWTO.md.
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
// Every engine read runs under a fault guard (vectored handler, this thread only): a fault skips that update (v3; v2 ended
// the numbers for the rest of the game at the first one), 20 of them end it, the log says where.
//
// v3 (05.11.2026, grantovich: "further left, active buffs, resists, auras and so on - like in DoW1"): a third block left
// of the two stat rows - the effects on the selected squad, each a game icon + the net change it makes:
//   * the numbers are the engine's own, as the game applies them: entity modifiers through 0x8A6AC0 (player x entity x
//     squad modifier extensions), weapon modifiers through 0x8A6790 per weapon (hardpoint-targeted ones included), squad
//     modifiers from the squad modifier extension; f(1) - f(0) = the multiplier, f(0) / multiplier = the addition, so
//     "+50%" is what the damage/speed/... code really gets (multipliers of one level add up in DoW2: x1.5 and x1.5 = x2.0);
//   * no seconds left: the engine keeps an applied modifier's timer (a record of 0x34 bytes: +0 the modifier, +4 its
//     handle, +8 the timer, vtable 0x010DDED8, +0x50 ticks left) in the "ModifierActionState" (vtable 0x010DBC28, list at
//     +0x3C) of the action that applied it, and that state lives in the action tree of the CASTER (its ability's
//     ActionUpdater, nested Timed/Delay/Repeat action states) - not on the squad it buffs, not in the world's ActionUpdater
//     (world +0x1E0: only IsAliveAction states) nor in the world's modifier applier manager (+0x1DC: empty in a skirmish),
//     checked on stand 1. What the squad itself holds (ModifierExt QI 2: per type {handle, modifier} pairs + the sums) has
//     no time. Finding it would mean walking every squad's ability updaters - left for later (docs/UNIT_STATS_HOWTO.md);
//   * which modifiers: the game's own tables of modifier names (0x8A7590 entity, 0x8A7400 weapon, 0x8A7910 squad), a
//     modifier object (0x40): +0x10 value, +0x14 type, +0x18 application, +0x1C class (0 entity, 2 squad, 3 weapon),
//     +0x20 usage (0 add, 1 multiply, 2 enable), +0x3D shown as a decorator over the unit;
//   * icons: the game's trait icons (ui/textures/space_marines/icon_traits_xp2: damage, damage_resistance, melee_def,
//     ranged_def, rate_of_fire, sprint, suppression_def ...) and the decorators it draws over a buffed unit (mod_melee,
//     mod_ranged); green = good for the squad, red = bad; [EffectIcons] in the ini can change any of them.
// v3 also: the selection is read with the selection mutex's TryEnter (never waits on it - one of four v2 runs froze the
// game; the panel update must not block on a lock the simulation holds), pointers are checked with VirtualQuery (an
// IsBadReadPtr fault inside the guard was caught by our own handler).
// v4 / 0.4 (06.11.2026, grantovich: "the armour type's name is so long that not all the effects fit"; "your DPS is not what I
// count by hand - the shotgun: 20 damage, cooldown 1.5, wind_down 0.5, 9 shots, reload 3 s - 8.6, not 10.3"):
//   * DPS by the weapon's REAL firing cycle (the engine's weapon state machine: windup, the firing ticks, winddown, reload
//     every F bursts, the randomised cooldown - see weapon_dps), not the game's tooltip formula Util_GetWeaponDPS (no
//     wind_down for bursts, no firing tick, the mean of reload count / cooldown as written); checked on stand 1 against the
//     shots the engine really fires (unitstats/stand/shotprobe.c);
//   * the armour type: its icon + a short name ("Heavy inf." / "Tyazh. pekh."), English or Russian as the game is.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wctype.h>
#include <math.h>
#include "../common/gfxui.h"

static wchar_t g_log[MAX_PATH], g_ini[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static volatile const char *g_where = "idle"; static volatile DWORD g_whereT, g_frames;
#define AT(s) (g_where = (s), g_whereT = GetTickCount())

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

// ---------- v3: the effect icons (game textures; [EffectIcons] in the ini) ----------
enum { EI_DAMAGE, EI_MELEE, EI_RANGED, EI_RESIST, EI_MELEEDEF, EI_RANGEDDEF, EI_EVADE, EI_ACCURACY, EI_RATE, EI_RANGE, EI_SPEED,
       EI_HEALTH, EI_REGEN, EI_SIGHT, EI_SKILL, EI_PEN, EI_SUPPDMG, EI_SUPPDEF, EI_KNOCKDEF, EI_ENERGY, EI_COOLDOWN, EI_SETUP,
       EI_INVIS, EI_NORETREAT, EI_NOFIRE, NEICON };
#define TRAIT "img://data:UI/textures/space_marines/icon_traits_xp2/"
static const wchar_t *EICON_KEY[NEICON] = { L"Damage", L"MeleeDamage", L"RangedDamage", L"DamageTaken", L"MeleeDamageTaken",
    L"RangedDamageTaken", L"Evasion", L"Accuracy", L"RateOfFire", L"Range", L"Speed", L"Health", L"Regeneration", L"Sight",
    L"MeleeSkill", L"Penetration", L"SuppressionDamage", L"SuppressionTaken", L"Knockback", L"Energy", L"Recharge", L"Setup",
    L"Hidden", L"NoRetreat", L"NoFire" };
static const wchar_t *EICON_DEF[NEICON] = {
    L"" TRAIT "damage.dds",                                         // crossed swords: damage dealt
    L"img://data:UI/textures/generic/decorators/mod_melee.dds|90",    // the decorator over a unit with a melee damage buff
    L"img://data:UI/textures/generic/decorators/mod_ranged.dds|90",   // ... a ranged damage buff
    L"" TRAIT "damage_resistance.dds",                              // a shield: damage taken
    L"" TRAIT "melee_def.dds",                                      // shield + fist: melee damage taken
    L"" TRAIT "ranged_def.dds",                                     // shield + bullet: ranged damage taken
    L"" TRAIT "force_shield.dds",                                   // accuracy of shots at it (evasion)
    L"img://data:UI/textures/generic/tooltips/ico_accuracy.dds|130",  // the weapon tooltip's accuracy icon
    L"" TRAIT "rate_of_fire.dds",
    L"img://data:UI/textures/generic/tooltips/ico_range.dds|130",
    L"" TRAIT "sprint.dds",
    L"" TRAIT "health_generic.dds",
    L"" TRAIT "health_regeneration.dds",
    L"img://data:UI/textures/generic/fe/loading/tips/up_detector.dds|150",
    L"" TRAIT "melee_paralyzes.dds",                                // a fist: melee skill
    L"" TRAIT "critical_hit.dds",                                   // armour penetration
    L"" TRAIT "suppression.dds",                                    // suppression it deals
    L"" TRAIT "suppression_def.dds",                                // suppression it takes
    L"" TRAIT "knockback_def.dds",                                  // knockback it takes
    L"" TRAIT "energy.dds",
    L"" TRAIT "cool_down.dds",                                      // ability recharge
    L"" TRAIT "setup.dds",
    L"img://data:UI/textures/imperial_guard/icon_traits_xp2/invisibility.dds",
    L"img://data:UI/textures/imperial_guard/icon_traits_xp2/fanaticism.dds",   // cannot retreat (the commissar's execution)
    L"" TRAIT "stun.dds",                                           // cannot fire
};
static Icon g_eicon[NEICON];
static int g_esizeC = 14;   // v4: [Effects] CompactFontSize
static int g_effOn = 1, g_effMax = 6, g_esize = 18, g_eisize = 0, g_effX = -470, g_effGap = 14;

// ---------- settings: UnitStats.ini [UnitStats], [Icons], [Effects], [EffectIcons] (re-read while the game runs) ----------
static int g_lang, g_ruSeen, g_langSeen;                  // v4: [UnitStats] Language: 0 auto (the game's own strings), 1 en, 2 ru
static int g_showEngine;                                  // v5: [UnitStats] ShowEngineCycle: 0 = the editor's DPS (default), 1 = the engine cycle (v4)
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
    { wchar_t l[16]; GetPrivateProfileStringW(L"UnitStats", L"Language", L"auto", l, 16, g_ini);
      g_lang = !_wcsicmp(l, L"en") ? 1 : !_wcsicmp(l, L"ru") ? 2 : 0; }
    g_useSkill = ini_int(L"GameSkill", 1, 0, 1); g_useEmod = ini_int(L"EntityMods", 1, 0, 1); g_useLvl = ini_int(L"LevelMul", 1, 0, 1);
    g_showEngine = ini_int(L"ShowEngineCycle", 0, 0, 1);
    for (int k = 0; k < NITEMS; k++) if (k != I_ARMOUR) ini_icon(ITEM_KEY[k], ITEM_DEF[k], &g_icon[k]);
    for (int k = 0; k < NARM; k++) ini_icon(ARM_KEY[k], ARM_DEF[k], &g_armIcon[k]);
    g_effOn = GetPrivateProfileIntW(L"Effects", L"Enabled", 1, g_ini) != 0;
    g_effMax = GetPrivateProfileIntW(L"Effects", L"Max", 6, g_ini); if (g_effMax < 0) g_effMax = 0; if (g_effMax > 10) g_effMax = 10;
    g_esize = GetPrivateProfileIntW(L"Effects", L"FontSize", 18, g_ini); if (g_esize < 8 || g_esize > 40) g_esize = 18;
    g_esizeC = GetPrivateProfileIntW(L"Effects", L"CompactFontSize", 14, g_ini); if (g_esizeC < 8 || g_esizeC > g_esize) g_esizeC = g_esize;
    g_eisize = GetPrivateProfileIntW(L"Effects", L"IconSize", 0, g_ini); if (g_eisize < 0 || g_eisize > 60) g_eisize = 0;
    g_effX = GetPrivateProfileIntW(L"Effects", L"Left", -470, g_ini);
    g_effGap = GetPrivateProfileIntW(L"Effects", L"Gap", 14, g_ini);
    for (int k = 0; k < NEICON; k++) {
        wchar_t w[256]; GetPrivateProfileStringW(L"EffectIcons", EICON_KEY[k], EICON_DEF[k], w, 256, g_ini);
        char u[256]; WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof u, NULL, NULL);
        memset(&g_eicon[k], 0, sizeof g_eicon[k]); g_eicon[k].scale = 100;
        char *bar = strchr(u, '|'); if (bar) { *bar = 0; sscanf(bar + 1, "%d", &g_eicon[k].scale); }
        if (g_eicon[k].scale < 20 || g_eicon[k].scale > 400) g_eicon[k].scale = 100;
        snprintf(g_eicon[k].path, sizeof g_eicon[k].path, "%s", u);
    }
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
typedef char (__thiscall *TE_t)(void *mutex);
static QI_t p_qi, p_sqi; static P0_t p_allEnts, p_begin, p_end, p_entBp; static S0_t p_sqName, p_entName, p_pbgFile;
static M0_t p_enter, p_leave; static TE_t p_tryEnter;
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
static volatile DWORD g_gThread; static DWORD g_gFault; static void *g_gAt; static int g_dead, g_faults;
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
    if (!ok) {            // v3: one fault skips this update (the reads are of a running simulation); 20 end it for this game
        if (++g_faults >= 20) g_dead = 1;
        logmsg("FAULT %08lX at %p in %s (%d)%s", g_gFault, g_gAt, what, g_faults, g_dead ? " - no numbers for the rest of this game" : "");
    }
    return ok;
}

// readable? VirtualQuery, not IsBadReadPtr: inside the guard our own vectored handler would take IsBadReadPtr's fault (v3)
static int rd(const void *p, unsigned n) {
    static const char *lo, *hi;                                  // the last readable region seen
    const char *a = p;
    if ((DWORD)a < 0x10000 || (DWORD)a + n < (DWORD)a) return 0;
    for (int k = 0; k < 4; k++) {
        if (a >= lo && a + n <= hi) return 1;
        if (a >= lo && a < hi) { n -= hi - a; a = hi; continue; }   // spans into the next region
        MEMORY_BASIC_INFORMATION mb;
        if (!VirtualQuery(a, &mb, sizeof mb) || mb.State != MEM_COMMIT || (mb.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            || !(mb.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return 0;
        lo = mb.BaseAddress; hi = lo + mb.RegionSize;
    }
    return a >= lo && a + n <= hi;
}
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
    float rtip, mtip;                                         // v4: the game's tooltip formula (v1-v3's number), for the log
    float reng, meng;                                         // v5: the engine's real cycle (v4's number), for the log
    int skill, skillOld, models, weapons, armour, sameR, sameM, nRanged, nMelee;
    float range0, speed0;                                     // blueprint values (the log)
    char armourName[96], armourShort[64];                     // v4: the game's name (log) and the short one shown
} Stats;
static float ticks(int t) { return (float)t; }
// v4: DPS of the weapon's REAL firing cycle - the weapon state machine of DOW2.exe 3.19, ticks of 0.1 s, as it fires in a
// fight against a target it keeps shooting (no target changes, no movement):
//   ranged (RangedWeapon / ProjectileWeapon, update 0xA4AA10; states 6 windup 0xA4B590, 7 firing 0xA4B810, 8 winddown
//   0xA4B9A0, 9 reload 0xA4BB60, 10 cooldown 0xA4BC30, 4 ready 0xA4AE30):
//     windup: wind_up ticks, the first shot one tick after it (the state that sets up the burst fires nothing);
//     firing: B ticks, B = trunc(random(burst min, max)) (>= 1; 1 without burst_info), per tick floor(acc + rate) shots
//       with acc = 1 - rate at the start when rate < 1 (so a burst starts with a shot); a ProjectileWeapon fires
//       projectiles_per_shot (bag +0x114) once and ends the burst after that one tick;
//     at the burst's end (0xA4CB90): winddown ends at now + wind_down, the reload (if due) after it, the cooldown after that:
//       cooldown = trunc(trunc(cooldown x modifiers) x random(weapon_cooldown_randomization_min, _max) / 100) (0x96E5A0,
//       tuning combat +0xB8/+0xBC: 80..120 % in vanilla and Elite), reload = trunc(min) + trunc(random x (max - min))
//       (0xA48F10, both through the reload modifier);
//     the next windup starts in the tick the cooldown ends; with a cooldown of 0 and no reload one tick later (ready
//       state first);
//     reload: after every F-th burst, F = min + random 0..max-min-1 (0x8221E0, Random::GetMax: max itself never comes) -
//       the counter (+0x148) is checked at the burst's end (<= 1: reload) and counted down when the winddown ends, so F
//       bursts per reload, not F + 1;
//     so per reload: F x (wind_up + B + wind_down + cooldown) + reload ticks, F x shots x damage.
//   melee (MeleeWeapon update 0x9AAC70: 6 windup, 7 strike 0x9AAE10, 8 winddown 0x9AAEA0, 10 cooldown 0x9AAF20, start
//   0x9AB020): max(wind_up, 1) + 1 + max(wind_down - 1, 0) + cooldown ticks per strike (cooldown randomised the same way).
// Expectations over the random parts are exact (the integer values the engine can draw, each equally likely).
// The game's own tooltip number (Util_GetWeaponDPS 0x81FC40, v1-v3 showed it) is still computed for the log ("tooltip").
#define VT_RANGEDW 0x010FC38Cu        // RangedWeapon (type 1)
#define VT_PROJW   0x010FC434u        // ProjectileWeapon (type 2)
static float g_cdLo = 0.8f, g_cdHi = 1.2f;  // tuning combat weapon_cooldown_randomization_min / _max (CombatInfo +0xB8/+0xBC) / 100
static double cd_expect(int c, double *p0) {  // E[trunc(c x U[lo, hi])] and the chance it is 0
    *p0 = 0;
    if (c <= 0) { *p0 = 1; return 0; }
    double lo = c * (double)g_cdLo, hi = c * (double)g_cdHi;
    if (hi - lo < 1e-9) { if (lo < 1) *p0 = 1; return floor(lo); }
    double s = 0;
    for (int k = (int)floor(lo); k <= (int)floor(hi); k++) {
        double a = k > lo ? k : lo, b = k + 1 < hi ? k + 1 : hi;
        if (b > a) { s += k * (b - a); if (k == 0) *p0 += b - a; }
    }
    *p0 /= hi - lo; return s / (hi - lo);
}
static int burst_shots(int nt, float rate) {   // 0xA4B810: per tick floor(acc + rate) shots, acc keeps the rest (x87 sum, float store)
    float acc = rate < 1.0f ? 1.0f - rate : 0.0f; int n = 0;
    for (int t = 0; t < nt && t < 2000; t++) {
        double s = (double)rate + (double)acc; int k = (int)floor(s); if (k < 0) k = 0;
        n += k; acc = (float)(s - k);
    }
    return n;
}
// v5: the weapon times as written in the blueprint. The engine keeps a weapon's times only as 0.1 s ticks, cut down at load
// (WeaponBag loader 0x820C..0x821A: GetFloatWithDefault, then SecondsToTicks::operator() - IAT 0x00F8850C - into the bag), so
// a wind_down of 0.49 is 4 ticks and an editor sheet's 0.49 is lost. UnitStats puts its own function in that one IAT slot
// of DOW2.exe (the result is the game's own, nothing in the simulation changes): when the call comes from one of the seven
// time fields of the weapon loader, it notes the seconds by bag and field. A bag loaded before the hook (or a slot that is
// not as in 3.19) keeps the ticks / 10; a noted value is used only while it still gives the bag's ticks.
enum { WT_CD, WT_WU, WT_WD, WT_RL0, WT_RL1, WT_B0, WT_B1, NWT };
static const struct { DWORD ret; int off; int edi; } S2T_SITE[NWT] = {      // the return address of each call, the bag field, bag in EDI (else EBX)
    { 0x00820D6F, 0xF0, 0 }, { 0x00820F2B, 0x10C, 0 }, { 0x00820F66, 0x110, 0 }, { 0x00821004, 0x11C, 0 }, { 0x0082103F, 0x120, 0 },
    { 0x008219EE, 0x174, 1 }, { 0x00821A29, 0x178, 1 } };
#define S2T_IAT 0x00F8850Cu
#define WT_N 16384
typedef struct { char *volatile bag; float sec[NWT]; unsigned char have; } WTimes;
static WTimes g_wt[WT_N];
static volatile LONG g_wtCount, g_wtCalls;
void *o_s2t __asm__("o_s2t");
void us_s2t_hook(void) __asm__("us_s2t_hook");
void __cdecl us_s2t_rec(DWORD ret, char *ebx, char *edi, DWORD fbits) __asm__("us_s2t_rec");
void __cdecl us_s2t_rec(DWORD ret, char *ebx, char *edi, DWORD fbits) {
    int k = 0; while (k < NWT && S2T_SITE[k].ret != ret) k++;
    if (k == NWT) return;
    char *bag = S2T_SITE[k].edi ? edi : ebx; float f; memcpy(&f, &fbits, 4);
    if (!bag || !(f >= 0 && f < 1e6f)) return;
    InterlockedIncrement(&g_wtCalls);
    unsigned h = ((DWORD)bag >> 3) * 2654435761u % WT_N;
    for (int i = 0; i < 64; i++, h = (h + 1) % WT_N) {
        WTimes *t = &g_wt[h];
        if (t->bag == bag) { t->sec[k] = f; t->have |= 1 << k; return; }
        if (!t->bag) { memset(t->sec, 0, sizeof t->sec); t->sec[k] = f; t->have = 1 << k; t->bag = bag; InterlockedIncrement(&g_wtCount); return; }
    }
}
__asm__(".text\n"
    "us_s2t_hook:\n\t"                               // thiscall SecondsToTicks::operator()(float): ecx, [esp+4] = seconds, ret 4
    "pushl %ecx\n\t"
    "pushl 8(%esp)\n\t"                              // the seconds
    "pushl %edi\n\t pushl %ebx\n\t"
    "pushl 16(%esp)\n\t"                             // the return address
    "call us_s2t_rec\n\t"
    "addl $16, %esp\n\t popl %ecx\n\t"
    "jmp *o_s2t\n");
static void s2t_install(void) {
    DWORD *slot = (DWORD *)S2T_IAT;
    for (int k = 0; k < NWT; k++) {                  // every site: "call [0x00F8850C]" right before its return address
        const unsigned char *c = (const unsigned char *)(S2T_SITE[k].ret - 6);
        if (!rd(c, 6) || c[0] != 0xFF || c[1] != 0x15 || *(DWORD *)(c + 2) != S2T_IAT) { logmsg("weapon times: the loader is not as in 3.19 (site %d) - ticks only", k); return; }
    }
    if (!rd(slot, 4) || !*slot) { logmsg("weapon times: no SecondsToTicks import - ticks only"); return; }
    DWORD old; if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) return;
    o_s2t = (void *)*slot; *slot = (DWORD)us_s2t_hook;
    VirtualProtect(slot, 4, old, &old);
    logmsg("weapon times: SecondsToTicks import hooked (the blueprint's seconds of the weapons loaded from now on)");
}
// the field's seconds as written (noted at load and still matching the bag's ticks), else ticks / 10
static double wsec(char *bag, int k, int tick) {
    unsigned h = ((DWORD)bag >> 3) * 2654435761u % WT_N;
    for (int i = 0; i < 64; i++, h = (h + 1) % WT_N) {
        WTimes *t = &g_wt[h];
        if (!t->bag) break;
        if (t->bag == bag) {
            if (t->have & (1 << k)) { double f = t->sec[k]; if (f * 10 + 0.01 >= tick && f * 10 < tick + 1.0) return f; }
            break;
        }
    }
    return tick / 10.0;
}
// v5 / 0.5 (07.11.2026, grantovich: "the shotgun - 20 damage, wind_up 0, cooldown 1.5, wind_down 0.5, 9 shots, reload 3 s -
// is 180 / (9 x (0 + 1.5 + 0.5) + 3) = 8.57 in the editor and in my sheet, yours says 8.3; there is a term too many"):
// the number SHOWN is the attribute editor's DPS, per weapon:
//   DPS = shots x damage per shot / (shots x (wind_up + firing + wind_down + cooldown) + reload)
//   shots   = reload frequency (min + max) / 2 - the shots between two reloads (the game's own tooltip takes the same mean);
//             no reload (frequency 0): one shot and no reload term;
//   reload  = reload duration (min + max) / 2;
//   damage per shot = damage_hp (a single shot; a "burst" with no rate of fire is one shot too), x projectiles_per_shot
//             for a projectile weapon, x rate_of_fire x burst duration for a real burst (damage_hp is per second of firing);
//   firing  = the burst duration (min + max) / 2 for a real burst (rate of fire > 0), else 0 (a shot takes no time);
//   melee   = damage / (wind_up + wind_down + cooldown), no reload (as the game's melee tooltip);
//   the times are the weapon's own (in the engine's 0.1 s ticks, as loaded), damage / cooldown / reload through the game's
//   weapon modifiers (upgrades, wargear, buffs count), times the level's damage multiplier.
// What v4 showed - the engine's real cycle (+1 tick before the first shot, the randomised cooldown cut to ticks, a reload
// after F = min..max-1 shots) - is kept in the log ("engine"), and shown instead with [UnitStats] ShowEngineCycle=1.
typedef struct { float tip, period, shots, F, R, cd, B, eng, edT, edN, edD, edR; int kind; } Cyc;   // for the log: tooltip DPS, the engine cycle, the editor terms
static float weapon_dps(char *w, char *bag, char *ent, int *melee, Cyc *cy) {
    float dmg = us_mod(w, *(float *)(bag + 0x64), ent, MOD_DAMAGE);
    float cdf = us_mod(w, ticks(*(int *)(bag + 0xF0)), ent, MOD_COOLDOWN);
    float rl0 = us_mod(w, ticks(*(int *)(bag + 0x11C)), ent, MOD_RELOAD), rl1 = us_mod(w, ticks(*(int *)(bag + 0x120)), ent, MOD_RELOAD);
    int wu = *(int *)(bag + 0x10C), wd = *(int *)(bag + 0x110), cdi = (int)cdf, fmin = *(int *)(bag + 0x124), fmax = *(int *)(bag + 0x128);
    float half = 0.5f, tps = 10.0f;                   // 0x0105BB74, 0x0107D78C
    DWORD vt = rd(w, 4) ? *(DWORD *)w : 0;
    memset(cy, 0, sizeof *cy);
    *melee = *(bag + 0xC8) != 0;
    // the game's tooltip formula (Util_GetWeaponDPS), for the log
    {
        float cd = cdf, wuf = ticks(wu), wdf = ticks(wd), freq = (fmin + fmax) * half, time, total, d = dmg;
        if (*melee) { time = wdf + wuf + cd; total = d; }
        else if (*(bag + 0x170)) {
            float burst = (*(int *)(bag + 0x174) + *(int *)(bag + 0x178)) * half, shots, rate = *(float *)(bag + 0x17C);
            if (freq > 0) { time = (rl0 + rl1) * half + (cd + burst) * freq; shots = freq; } else { time = cd + burst; shots = 1.0f; }
            if (rate > 0) d *= rate;
            total = shots * d * burst;
        } else {
            time = wdf + wuf + cd;
            if (freq > 0) { time = time * freq + (rl0 + rl1) * half; total = freq * d; } else total = d;
        }
        cy->tip = time > 0 && total > 0 ? total / time * tps : 0;
    }
    // v5: the attribute editor's DPS (shown)
    double ed;
    {   // in seconds: the blueprint's own where noted (wsec), the modifiers as the game applies them (to the ticks, here
        // the same factor on the seconds)
        double cd0 = *(int *)(bag + 0xF0), cdm = cd0 > 0 ? cdf / cd0 : 1;
        double r00 = *(int *)(bag + 0x11C), r10 = *(int *)(bag + 0x120), rlm = r00 + r10 > 0 ? (rl0 + rl1) / (r00 + r10) : 1;
        double cdS = cd0 > 0 ? wsec(bag, WT_CD, (int)cd0) * cdm : cdf / tps, wuS = wsec(bag, WT_WU, wu), wdS = wsec(bag, WT_WD, wd);
        double dF = dmg, fire = 0, per, n, rl, time;
        if (*melee) { per = wuS + wdS + cdS; n = 0; rl = 0; }
        else {
            if (vt == VT_PROJW) { int pps = *(int *)(bag + 0x114); if (pps > 1 && pps < 1000) dF = dmg * (double)pps; }
            else if (*(bag + 0x170)) {
                float rate = *(float *)(bag + 0x17C);          // shots per tick (rate_of_fire / 10)
                if (rate > 0) {
                    fire = (wsec(bag, WT_B0, *(int *)(bag + 0x174)) + wsec(bag, WT_B1, *(int *)(bag + 0x178))) * 0.5;
                    dF = dmg * (double)rate * tps * fire;      // shots of the burst x damage per shot
                }
            }
            per = wuS + fire + wdS + cdS;
            n = fmin + fmax > 0 ? (fmin + fmax) * 0.5 : 0;
            rl = (wsec(bag, WT_RL0, (int)r00) + wsec(bag, WT_RL1, (int)r10)) * 0.5 * rlm;
        }
        if (n > 0) { time = n * per + rl; } else { time = per; n = 1; rl = 0; }
        if (time < 0.1) time = 0.1;                      // all times 0: the engine still fires at most once a tick
        cy->edT = (float)per; cy->edN = (float)n; cy->edD = (float)dF; cy->edR = (float)rl;
        ed = dF > 0 ? n * dF / time : 0;
    }
    double p0, ecd = cd_expect(cdi, &p0), dps;
    cy->cd = (float)ecd;
    if (*melee) {
        double per = (wu > 1 ? wu : 1) + 1 + (wd > 1 ? wd - 1 : 0) + ecd;
        cy->period = (float)per; cy->shots = 1; cy->kind = 3;
        dps = dmg / per * tps;
    } else {
        double eB, eS;                                   // ticks of firing, shots per burst
        if (vt == VT_PROJW) { int pps = *(int *)(bag + 0x114); eB = 1; eS = pps > 0 && pps < 1000 ? pps : 1; cy->kind = 2; }
        else if (*(bag + 0x170)) {
            int b0 = *(int *)(bag + 0x174), b1 = *(int *)(bag + 0x178), n = 0; float rate = *(float *)(bag + 0x17C);
            if (b0 < 1) b0 = 1; if (b1 < b0) b1 = b0; if (b1 > b0 + 200) b1 = b0 + 200;
            eB = eS = 0;
            for (int b = b0; b < (b1 > b0 ? b1 : b0 + 1); b++) { eB += b; eS += burst_shots(b, rate); n++; }
            eB /= n; eS /= n; cy->kind = 1;
        } else { eB = 1; eS = 1; cy->kind = 0; }
        double per = wu + eB + wd + ecd, eF = 1, eR = 0, time;
        if (fmin > 0) {                                  // reload after F bursts
            int r0 = (int)rl0, r1 = (int)rl1; if (r0 < 0) r0 = 0; if (r1 < r0) r1 = r0;
            eF = fmax > fmin ? fmin + (fmax - fmin - 1) * 0.5 : fmin;
            eR = r0 + (r1 > r0 ? (r1 - r0 - 1) * 0.5 : 0);
            time = eF * per + (eF - 1) * p0 + eR + (r0 > 0 ? 0 : p0);
        } else time = per + p0;
        cy->period = (float)per; cy->shots = (float)eS; cy->F = (float)eF; cy->R = (float)eR; cy->B = (float)eB;
        dps = time > 0 ? eF * eS * dmg / time * tps : 0;
    }
    if (g_haveLvl && g_useLvl) { float lm = us_lvlmul(ent, *melee); dps *= lm; cy->tip *= lm; ed *= lm; }   // the level's damage multiplier (0xA27720)
    cy->eng = dps > 0 ? (float)dps : 0;
    if (g_showEngine) return cy->eng;
    return ed > 0 ? (float)ed : 0;
}
static char g_detail[8192]; static int g_detailN;
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
                    wchar_t t[64]; int k = 0; for (; k < 63 && ws[k]; k++) { t[k] = ws[k]; if (ws[k] >= 0x400 && ws[k] <= 0x4FF) g_ruSeen = 1; }
                    t[k] = 0; g_langSeen = 1;
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
// v4 (grantovich: "the armour type's name is so long that not all the effects fit"): a short name next to the icon, in
// the game's language - Russian when the game's own armour type names (the LocStrings above) are Cyrillic, else English;
// [UnitStats] Language=auto|en|ru, [ArmourNames] <internal name>=<text> in the ini override.
static const struct { const char *id, *en, *ru; } ARM_SHORT[] = {
    { "infantry",             "Infantry",     "\xD0\x9F\xD0\xB5\xD1\x85\xD0\xBE\xD1\x82\xD0\xB0" },
    { "infantry_fire_resist", "Inf. (fire)",  "\xD0\x9F\xD0\xB5\xD1\x85. (\xD0\xBE\xD0\xB3\xD0\xBD.)" },
    { "heavy_infantry",       "Heavy inf.",   "\xD0\xA2\xD1\x8F\xD0\xB6. \xD0\xBF\xD0\xB5\xD1\x85." },
    { "super_heavy_infantry", "S-heavy inf.", "\xD0\xA1\xD0\xB2\xD0\xB5\xD1\x80\xD1\x85\xD1\x82\xD1\x8F\xD0\xB6." },
    { "commander",            "Commander",    "\xD0\x9A\xD0\xBE\xD0\xBC\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB8\xD1\x80" },
    { "vehicle",              "Vehicle",      "\xD0\xA2\xD0\xB5\xD1\x85\xD0\xBD\xD0\xB8\xD0\xBA\xD0\xB0" },
    { "building",             "Building",     "\xD0\x97\xD0\xB4\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5" },
    { "building_light",       "Light bldg",   "\xD0\x9B\xD1\x91\xD0\xB3\xD0\xBA. \xD0\xB7\xD0\xB4." },
    { "building_defence",     "Defence",      "\xD0\x9E\xD0\xB1\xD0\xBE\xD1\x80\xD0\xBE\xD0\xBD\xD0\xB0" },
    { "building_ig_turret",   "Turret",       "\xD0\xA2\xD1\x83\xD1\x80\xD0\xB5\xD0\xBB\xD1\x8C" },
};
static int is_ru(void) { return g_lang == 2 || (g_lang == 0 && g_ruSeen); }
static void armour_short(const char *internal, const char *gameName, char *out, int n) {
    char ini[MAX_PATH * 2], u[128];                      // the A version: the ini's bytes as they are (UTF-8 text)
    WideCharToMultiByte(CP_ACP, 0, g_ini, -1, ini, sizeof ini, NULL, NULL);
    if (*internal && GetPrivateProfileStringA("ArmourNames", internal, "", u, sizeof u, ini) > 0) { snprintf(out, n, "%s", u); return; }
    for (unsigned i = 0; i < sizeof ARM_SHORT / sizeof ARM_SHORT[0]; i++)
        if (!strcmp(internal, ARM_SHORT[i].id)) { snprintf(out, n, "%s", is_ru() ? ARM_SHORT[i].ru : ARM_SHORT[i].en); return; }
    // not one of the ten types of vanilla / Elite: the game's own name if short, else the internal one cut to 12 letters
    if (gameName && *gameName && MultiByteToWideChar(CP_UTF8, 0, gameName, -1, NULL, 0) <= 13) { snprintf(out, n, "%s", gameName); return; }
    char t[64]; snprintf(t, sizeof t, "%s", internal); for (char *c = t; *c; c++) if (*c == '_') *c = ' ';
    if (t[0] >= 'a' && t[0] <= 'z') t[0] -= 32;
    if (strlen(t) > 12) { t[11] = '.'; t[12] = 0; }
    snprintf(out, n, "%s", t);
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
        if (rd(c + 0xB8, 8)) {                           // v4: weapon_cooldown_randomization_min / _max (percent; 0x96E5A0)
            float lo = *(float *)(c + 0xB8) * 0.01f, hi = *(float *)(c + 0xBC) * 0.01f;
            if (lo > 0 && hi >= lo && hi < 10) { g_cdLo = lo; g_cdHi = hi; }
        }
        logmsg("range allowance of the combat tuning (added to every weapon's max range at load): %.2f; cooldown randomisation %.2f..%.2f",
               g_rangeAllow, g_cdLo, g_cdHi);
    }
    char *grp = p_allEnts(j->squad); if (!grp) return;
    char **b = p_begin(grp), **e = p_end(grp);
    int armN[64] = {0}, armId[64], nArm = 0;
    for (char **it = b; it && it < e && st->models < 64; it++) {
        char *ent = *it; if (!rd(ent, 0x60)) continue;
        void *ctrl = *(void **)(ent + 0x3C); if (!ctrl) continue;
        st->models++;
        detail(" [%s:", p_entName ? p_entName(ent) : "?");
        float erd = 0, emd = 0, ert = 0, emt = 0, ere = 0, eme = 0;
        char *ce = p_qi(ctrl, EXT_COMBAT);
        if (vt_is(ce, VT_COMBATEXT)) {
            char **hp = *(char ***)(ce + 0x50); int n = *(int *)(ce + 0x54);
            for (int i = 0; i < n && i < 16 && rd(hp, (i + 1) * 4); i++) {
                char *h = hp[i]; if (!rd(h, 0x54)) continue;
                char *w = *(char **)(h + 0x50); if (!rd(w, 0xB0)) continue;
                char *bag = *(char **)(w + 0x54); if (!vt_is(bag, VT_WEAPONBAG)) continue;
                char *went = rd(*(char **)(w + 0xAC), 0x18) ? *(char **)(*(char **)(w + 0xAC) + 0x14) : ent;
                int melee; Cyc cy; float dps = weapon_dps(w, bag, went, &melee, &cy);
#ifdef US_DUMP
                {   // stand only: the weapon bag as loaded, once per bag (where the blueprint's own seconds are kept)
                    static char *seen[64]; static int ns; int k = 0; while (k < ns && seen[k] != bag) k++;
                    if (k == ns && ns < 64 && rd(bag, 0x200)) {
                        seen[ns++] = bag; char hx[0x200 * 9 / 4 + 8]; int o = 0;
                        for (int q = 0; q < 0x200; q += 4) o += sprintf(hx + o, "%08lx ", (unsigned long)*(DWORD *)(bag + q));
                        logmsg("BAG %s %s", p_pbgFile && rd(*(char **)(w + 0x4C), 4) ? p_pbgFile(*(char **)(w + 0x4C)) : "?", hx);
                    }
                }
#endif
                float rng = melee ? 0 : (*(Range_t *)(*(char **)w + 0x1C))(w) - g_rangeAllow;   // the weapon's range as written (and quoted)
                float rng0 = *(float *)(bag + 0xC4), rngW = melee ? 0 : us_mod(w, rng0, NULL, MOD_RANGE);
                st->weapons++; if (melee) st->nMelee++; else st->nRanged++;
                if (melee) { if (dps > emd) { emd = dps; emt = cy.tip; eme = cy.eng; } }      // one melee attack at a time
                else { erd += dps; ert += cy.tip; ere += cy.eng; if (rng > st->range) st->range = rng; if (rng0 - g_rangeAllow > st->range0) st->range0 = rng0 - g_rangeAllow; }   // every gun fires
                const char *wf = p_pbgFile && rd(*(char **)(w + 0x4C), 4) ? p_pbgFile(*(char **)(w + 0x4C)) : "?";
                const char *sl = wf ? strrchr(wf, '\\') : NULL; if (!sl && wf) sl = strrchr(wf, '/');
                detail(" %s%s %.2f dps (tooltip %.2f; cycle %s %.2f ticks, %.2f shots x%.2f, reload %.1f, cooldown %.2f; engine %.2f; editor %.2f shots x %.2f dmg, %.3f s each, reload %.3f s;"
                       " dmg %.2f->%.2f, ticks cd %d wu %d wd %d reload %d-%d x%d-%d burst %d %d-%d rof %.3f pps %d, lvl x%.2f, range bag %.1f weapon %.1f shown %.1f)",
                       melee ? "melee " : "", sl ? sl + 1 : wf, dps, cy.tip, cy.kind == 3 ? "melee" : cy.kind == 2 ? "projectile" : cy.kind == 1 ? "burst" : "single",
                       cy.period, cy.shots, cy.F, cy.R, cy.cd, cy.eng, cy.edN, cy.edD, cy.edT, cy.edR,
                       *(float *)(bag + 0x64), us_mod(w, *(float *)(bag + 0x64), went, MOD_DAMAGE), *(int *)(bag + 0xF0),
                       *(int *)(bag + 0x10C), *(int *)(bag + 0x110), *(int *)(bag + 0x11C), *(int *)(bag + 0x120), *(int *)(bag + 0x124),
                       *(int *)(bag + 0x128), *(bag + 0x170), *(int *)(bag + 0x174), *(int *)(bag + 0x178), *(float *)(bag + 0x17C), *(int *)(bag + 0x114),
                       g_haveLvl && g_useLvl ? us_lvlmul(went, melee) : 1.0f, rng0, rngW, rng);
            }
        }
        if (st->models == 1) { st->rdps1 = erd; st->mdps1 = emd; }
        else { if (erd < st->rdps1 - 0.05f || erd > st->rdps1 + 0.05f) st->sameR = 0; if (emd < st->mdps1 - 0.05f || emd > st->mdps1 + 0.05f) st->sameM = 0; }
        st->rdps += erd; st->mdps += emd; st->rtip += ert; st->mtip += emt; st->reng += ere; st->meng += eme;
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
    armour_short(armour_internal(st->armour), st->armourName, st->armourShort, sizeof st->armourShort);
}

// ---------- v3: the effects on the squad (the engine's modifiers, as the game applies them) ----------
#define EXT_MODIFIER   2                 // EntityController::QI: ModifierExt (vtable 0x011013B0)
#define SQEXT_MODIFIER 1                 // SquadController::QI: SquadModifierExt (vtable 0x01101470)
#define FN_MNAME_E     0x008A7590u       // modifier type -> name: EAX = type, the name in EAX (entity / weapon / squad)
#define FN_MNAME_W     0x008A7400u
#define FN_MNAME_S     0x008A7910u
enum { MC_ENTITY = 0, MC_SQUAD = 2, MC_WEAPON = 3 };
static const unsigned char MNAME_PRO[3] = {0xFF, 0x24, 0x85};          // jmp [eax*4 + table]
const char *us_mname(DWORD fn, int type) __asm__("us_mname");
__asm__(".text\n"
    "us_mname:\n\t"
    "movl 8(%esp), %eax\n\t movl 4(%esp), %ecx\n\t call *%ecx\n\t ret\n");
static int g_haveMName;
static const char *mod_name(int cls, int type) {                // "damage_weapon_modifier"
    DWORD fn = cls == MC_ENTITY ? FN_MNAME_E : cls == MC_WEAPON ? FN_MNAME_W : cls == MC_SQUAD ? FN_MNAME_S : 0;
    int n = cls == MC_ENTITY ? 0x39 : cls == MC_WEAPON ? 0x1F : 0x18;
    if (!g_haveMName || !fn || type < 0 || type >= n) return "?";
    const char *s = us_mname(fn, type); if (!rd(s, 4)) return "?";
    const char *b = strrchr(s, '\\'); return b ? b + 1 : s;
}

// what is shown for a modifier type: its icon, which way is good for the squad, how the number reads, a short tag
enum { EM_PCT, EM_RATE, EM_ADD, EM_ADDP, EM_ON, EM_OFF };   // PCT "+50%"; RATE: a time multiplier as a rate ("cooldown x0.5" = "+100%");
                                                           // ADD "+50"; ADDP "-100%" (an addition to a chance); ON / OFF: enabled / disabled
typedef struct { unsigned char cls, type; signed char good; unsigned char mode, icon; const char *tag; } EffDef;
static const EffDef EFFDEF[] = {
    { MC_ENTITY, 0x00, +1, EM_PCT,  EI_HEALTH,    "" },          // health_maximum
    { MC_ENTITY, 0x01, +1, EM_PCT,  EI_REGEN,     "" },          // health_regeneration
    { MC_ENTITY, 0x06, +1, EM_PCT,  EI_SPEED,     "" },          // speed_maximum
    { MC_ENTITY, 0x0A, +1, EM_PCT,  EI_SIGHT,     "" },          // sight_radius
    { MC_ENTITY, 0x0D, +1, EM_ON,   EI_INVIS,     "hidden" },    // camouflage_enable
    { MC_ENTITY, 0x0F, +1, EM_OFF,  EI_NOFIRE,    "no fire" },   // shoot_enable
    { MC_ENTITY, 0x11, -1, EM_PCT,  EI_EVADE,     "" },          // received_accuracy
    { MC_ENTITY, 0x12, -1, EM_PCT,  EI_RESIST,    "" },          // received_damage
    { MC_ENTITY, 0x13, -1, EM_PCT,  EI_MELEEDEF,  "" },          // received_damage_melee
    { MC_ENTITY, 0x16, -1, EM_PCT,  EI_MELEEDEF,  "light" },
    { MC_ENTITY, 0x17, -1, EM_PCT,  EI_MELEEDEF,  "heavy" },
    { MC_ENTITY, 0x18, -1, EM_PCT,  EI_MELEEDEF,  "power" },
    { MC_ENTITY, 0x14, -1, EM_PCT,  EI_RANGEDDEF, "" },          // received_damage_ranged
    { MC_ENTITY, 0x15, -1, EM_PCT,  EI_RANGEDDEF, "pierce" },
    { MC_ENTITY, 0x19, -1, EM_PCT,  EI_RANGEDDEF, "flame" },
    { MC_ENTITY, 0x1A, -1, EM_PCT,  EI_RANGEDDEF, "expl" },
    { MC_ENTITY, 0x1B, -1, EM_PCT,  EI_RANGEDDEF, "plasma" },
    { MC_ENTITY, 0x1C, -1, EM_PCT,  EI_RANGEDDEF, "det" },
    { MC_ENTITY, 0x1E, -1, EM_PCT,  EI_COOLDOWN,  "" },          // ability_recharge_time_entity
    { MC_ENTITY, 0x2F, +1, EM_ADD,  EI_SKILL,     "" },          // skill_melee
    { MC_ENTITY, 0x31, -1, EM_ADDP, EI_KNOCKDEF,  "" },          // knockback_chance_* (five kinds, one icon)
    { MC_ENTITY, 0x32, -1, EM_ADDP, EI_KNOCKDEF,  "" },
    { MC_ENTITY, 0x33, -1, EM_ADDP, EI_KNOCKDEF,  "" },
    { MC_ENTITY, 0x34, -1, EM_ADDP, EI_KNOCKDEF,  "" },
    { MC_ENTITY, 0x35, -1, EM_ADDP, EI_KNOCKDEF,  "" },
    { MC_WEAPON, 0x07, +1, EM_PCT,  EI_DAMAGE,    "" },          // damage_weapon
    { MC_WEAPON, 0x0E, +1, EM_PCT,  EI_MELEE,     "" },          // damage_melee_weapon
    { MC_WEAPON, 0x0F, +1, EM_PCT,  EI_RANGED,    "" },          // damage_ranged_weapon
    { MC_WEAPON, 0x10, +1, EM_PCT,  EI_RANGED,    "" },          // damage_projectile_weapon
    { MC_WEAPON, 0x11, +1, EM_PCT,  EI_DAMAGE,    "vs bld" },    // damage_armor_*
    { MC_WEAPON, 0x12, +1, EM_PCT,  EI_DAMAGE,    "vs bld" },
    { MC_WEAPON, 0x13, +1, EM_PCT,  EI_DAMAGE,    "vs hero" },
    { MC_WEAPON, 0x14, +1, EM_PCT,  EI_DAMAGE,    "vs heavy" },
    { MC_WEAPON, 0x15, +1, EM_PCT,  EI_DAMAGE,    "vs inf" },
    { MC_WEAPON, 0x16, +1, EM_PCT,  EI_DAMAGE,    "vs veh" },
    { MC_WEAPON, 0x08, +1, EM_PCT,  EI_PEN,       "" },          // armour_penetration
    { MC_WEAPON, 0x05, +1, EM_PCT,  EI_ACCURACY,  "" },          // accuracy
    { MC_WEAPON, 0x19, +1, EM_PCT,  EI_ACCURACY,  "" },          // accuracy_ranged
    { MC_WEAPON, 0x18, +1, EM_PCT,  EI_ACCURACY,  "melee" },
    { MC_WEAPON, 0x03, +1, EM_RATE, EI_RATE,      "" },          // cooldown
    { MC_WEAPON, 0x02, +1, EM_RATE, EI_RATE,      "reload" },
    { MC_WEAPON, 0x04, +1, EM_PCT,  EI_RATE,      "burst" },
    { MC_WEAPON, 0x17, +1, EM_PCT,  EI_RANGE,     "" },          // range_maximum
    { MC_WEAPON, 0x0B, +1, EM_PCT,  EI_SUPPDMG,   "" },          // courage_damage
    { MC_WEAPON, 0x1D, -1, EM_PCT,  EI_SETUP,     "" },          // setup
    { MC_SQUAD,  0x01, -1, EM_PCT,  EI_SUPPDEF,   "" },          // received_courage_damage
    { MC_SQUAD,  0x08, +1, EM_OFF,  EI_NORETREAT, "no retreat" },// retreat_enable
    { MC_SQUAD,  0x02, -1, EM_PCT,  EI_COOLDOWN,  "" },          // ability_recharge_time_squad
    { MC_SQUAD,  0x0C, +1, EM_PCT,  EI_ENERGY,    "" },          // energy_regen_rate
    { MC_SQUAD,  0x0E, +1, EM_PCT,  EI_ENERGY,    "max" },       // energy_maximum
};
#define NEFFDEF ((int)(sizeof EFFDEF / sizeof EFFDEF[0]))
#define MAXEFF 10
typedef struct { int def, nEnt; float mul, add, sum, score; } Eff;   // the engine's multiplier, addition, enable sum; models with it
typedef struct { Eff e[NEFFDEF]; char *squad; } Effs;
static int def_of(int cls, int type) { for (int i = 0; i < NEFFDEF; i++) if (EFFDEF[i].cls == cls && EFFDEF[i].type == type) return i; return -1; }
// the per-type entry of a modifier extension's vector (0x20 bytes: +0 {handle, modifier} pairs, +0xC mul, +0x10 add, +0x1C enable sum)
static char *mod_entry(char *ext, int vec, int type, int n) {
    if (!rd(ext + vec, 8)) return NULL;
    char *b = *(char **)(ext + vec), *e = *(char **)(ext + vec + 4);
    if (!b || e < b || (e - b) / 0x20 != n || type >= n || !rd(b + type * 0x20, 0x20)) return NULL;
    return b + type * 0x20;
}
static void *g_effEnts[64]; static int g_effNEnts;
typedef struct { char *squad; Effs *fx; } EJob;
// the engine's multiplier and addition of a modifier type for one entity / weapon: f(v) = (v + add) * mul
static void fit(float f1, float f0, float *mul, float *add) { *mul = f1 - f0; *add = *mul > 1e-6f || *mul < -1e-6f ? f0 / *mul : f0; }
static void eff_values(void *arg) {
    EJob *j = arg; Effs *fx = j->fx; char *squad = j->squad;
    char *grp = p_allEnts(squad); if (!grp) return;
    char **b = p_begin(grp), **e = p_end(grp);
    char *sqx = NULL;
    if (p_sqi && rd(squad + 0x4C, 4) && *(void **)(squad + 0x48)) sqx = p_sqi(*(void **)(squad + 0x48), SQEXT_MODIFIER);
    for (int i = 0; i < NEFFDEF; i++) { Eff *x = &fx->e[i]; x->def = i; x->mul = 1; x->add = 0; x->sum = 0; x->score = 0; }
    g_effNEnts = 0;
    for (char **it = b; it && it < e && g_effNEnts < 64; it++) {
        char *ent = *it; if (!rd(ent, 0x60)) continue;
        void *ctrl = *(void **)(ent + 0x3C); if (!ctrl) continue;
        g_effEnts[g_effNEnts++] = ent;
        char *mx = p_qi(ctrl, EXT_MODIFIER);
        // the entity's weapons (as squad_stats)
        char *ws[16], *wents[16]; int nw = 0;
        char *ce = p_qi(ctrl, EXT_COMBAT);
        if (vt_is(ce, VT_COMBATEXT)) {
            char **hp = *(char ***)(ce + 0x50); int n = *(int *)(ce + 0x54);
            for (int k = 0; k < n && k < 16 && rd(hp, (k + 1) * 4); k++) {
                char *h = hp[k]; if (!rd(h, 0x54)) continue;
                char *w = *(char **)(h + 0x50); if (!rd(w, 0xB0) || !vt_is(*(char **)(w + 0x54), VT_WEAPONBAG)) continue;
                ws[nw] = w; wents[nw] = rd(*(char **)(w + 0xAC), 0x18) ? *(char **)(*(char **)(w + 0xAC) + 0x14) : ent; nw++;
            }
        }
        for (int i = 0; i < NEFFDEF; i++) {
            const EffDef *d = &EFFDEF[i]; Eff *x = &fx->e[i];
            float mul = 1, add = 0, sum = 0;
            if (d->cls == MC_ENTITY) {
                if (d->mode == EM_ON || d->mode == EM_OFF) {        // enable: the sums of the entity's and the squad's entries
                    char *en = mx ? mod_entry(mx, 0x14, d->type, 0x39) : NULL, *es = sqx ? mod_entry(sqx, 0x20, d->type, 0x39) : NULL;
                    sum = (en ? *(float *)(en + 0x1C) : 0) + (es ? *(float *)(es + 0x1C) : 0);
                } else if (g_haveEmod && g_useEmod) fit(us_emod(1.0f, ent, d->type), us_emod(0.0f, ent, d->type), &mul, &add);
            } else if (d->cls == MC_WEAPON) {                       // the weapon with the biggest change
                float bs = 0;
                for (int k = 0; k < nw; k++) {
                    float m, a; fit(us_mod(ws[k], 1.0f, wents[k], d->type), us_mod(ws[k], 0.0f, wents[k], d->type), &m, &a);
                    float sc = (m > 1 ? m - 1 : 1 - m) + (a > 0 ? a : -a) * 0.01f;
                    if (sc > bs) { bs = sc; mul = m; add = a; }
                }
            } else continue;
            float sc = (mul > 1 ? mul - 1 : 1 - mul) + (add > 0 ? add : -add) * 0.01f + (sum > 0 ? sum : -sum);
            if (sc > 0.004f) x->nEnt++;
            if (sc > x->score) { x->score = sc; x->mul = mul; x->add = add; x->sum = sum; }
        }
    }
    for (int i = 0; i < NEFFDEF; i++) {                          // squad modifiers: the squad's own extension
        const EffDef *d = &EFFDEF[i]; Eff *x = &fx->e[i];
        if (d->cls != MC_SQUAD || !sqx) continue;
        char *en = mod_entry(sqx, 0x14, d->type, 0x18); if (!en) continue;
        x->mul = *(float *)(en + 0x0C); x->add = *(float *)(en + 0x10); x->sum = *(float *)(en + 0x1C);
        if (d->mode == EM_ON || d->mode == EM_OFF) x->score = x->sum > 0 ? x->sum : -x->sum;
        else x->score = (x->mul > 1 ? x->mul - 1 : 1 - x->mul) + (x->add > 0 ? x->add : -x->add) * 0.01f;
        if (x->score > 0.004f) x->nEnt = g_effNEnts;
    }
}

// ---------- the selection ----------
#define MAXSEL 64
typedef struct { char *squad; int n, own; } Sel;           // n: 0 none, 1 one squad, 2 several, -1 busy (keep the last)
static void get_selection(void *arg) {
    Sel *s = arg; s->squad = NULL; s->n = 0;
    char *r = *(char **)SEL_ROOT; if (!rd(r + 8, 4)) return;
    char *a = *(char **)(r + 8); if (!rd(a + 0x20, 4)) return;
    char *sel = *(char **)(a + 0x20); if (!rd(sel + 0x35C, 0x44)) return;
    DWORD ids[MAXSEL]; int n = 0;
    // v3: TryEnter, never Enter - this runs inside the panel's per-frame update on the UI thread; waiting there for a lock
    // the simulation holds (it selects/deselects under it) is how a v2 run may have frozen the game. Busy: keep the last.
    if (p_tryEnter) { if (!p_tryEnter(sel + 0x39D)) { s->n = -1; return; } }
    else p_enter(sel + 0x39D);
    DWORD *b = *(DWORD **)(sel + 0x35C), *e = *(DWORD **)(sel + 0x360);
    if (b && e >= b && e - b < 4096 && rd(b, (e - b) * 4)) for (DWORD *it = b; it < e && n < MAXSEL; it++) ids[n++] = *it;
    p_leave(sel + 0x39D);
    AT("selection: ids -> entities");
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
static double g_rowLeft[2] = { 0, 0 }; static int g_effDirty;      // v3: where each stat row starts (the effects end left of it)
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
// v5: DPS as the editor / a balance sheet has it (grantovich checks it against his numbers): two decimals under 100, one
// under 1000, trailing zeros dropped - 8.57, 14.58, 25, 216.4
static void fmtdps(char *o, size_t n, float v) {
    if (v < 0) { snprintf(o, n, "-"); return; }
    snprintf(o, n, v < 99.995f ? "%.2f" : v < 999.95f ? "%.1f" : "%.0f", v);
    char *d = strchr(o, '.'); if (!d) return;
    char *e = o + strlen(o) - 1; while (e > d && *e == '0') *e-- = 0;
    if (e == d) *e = 0;
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
        if (g_rowLeft[row] != x + gapItem) { g_rowLeft[row] = x + gapItem; g_effDirty = 1; }
    }
    g_layoutDirty = 0;
}
static void set_stats(void *mv, const Stats *s) {
    char v[32], sm[48];
    int pm = g_perModel && s->models > 1;
    if (s->nRanged) fmtdps(v, sizeof v, s->rdps); else snprintf(v, sizeof v, "-");
    sm[0] = 0;
    if (pm && s->sameR && s->rdps > 0) { char q[16]; fmtdps(q, sizeof q, s->rdps1); snprintf(sm, sizeof sm, "(%d\xC3\x97%s)", s->models, q); }
    set_item_text(mv, I_RANGED, v, sm);
    if (s->nMelee) fmtdps(v, sizeof v, s->mdps); else snprintf(v, sizeof v, "-");
    sm[0] = 0;
    if (pm && s->sameM && s->mdps > 0) { char q[16]; fmtdps(q, sizeof q, s->mdps1); snprintf(sm, sizeof sm, "(%d\xC3\x97%s)", s->models, q); }
    set_item_text(mv, I_MELEE, v, sm);
    if (s->skill >= 0) snprintf(v, sizeof v, "%d", s->skill); else snprintf(v, sizeof v, "-");
    set_item_text(mv, I_SKILL, v, NULL);
    set_item_text(mv, I_ARMOUR, s->armourShort[0] ? s->armourShort : "-", NULL);
    if (s->nRanged) fmtnum(v, sizeof v, s->range); else snprintf(v, sizeof v, "-");
    set_item_text(mv, I_RANGE, v, NULL);
    fmtnum(v, sizeof v, s->speed); set_item_text(mv, I_SPEED, v, NULL);
    fmtnum(v, sizeof v, s->sight); set_item_text(mv, I_SIGHT, v, NULL);
    for (int k = 0; k < NITEMS; k++)
        set_icon(mv, k, k == I_ARMOUR ? &g_armIcon[armour_class(armour_internal(s->armour))] : &g_icon[k]);
    if (g_layoutDirty) layout(mv);
}
// ---------- v3: the effects block, left of the stat rows: per effect an icon clip e<k> and a text f<k> (shadow g<k>) ----------
typedef struct { int def; float mul, add, sum, score; char html[240], plain[64]; } Cell;
static Cell g_cell[MAXEFF]; static int g_nCell = -1;
static char g_cellIcon[MAXEFF][200]; static int g_cellDone[MAXEFF], g_cellGen[MAXEFF], g_cellSc[MAXEFF];
static double g_eIconS, g_eLineH; static int g_efs = 18, g_eCompact;   // v4: the font size now, three compact rows
static void make_eff_fields(void *mv) {
    g_eCompact = 0; g_efs = g_esize;
    g_eIconS = g_eisize ? g_eisize : 1.3 * g_esize; g_eLineH = 1.3 * g_esize + 4;
    for (int k = 0; k < MAXEFF; k++) {
        char nm[16], p[96];
        for (int s = 0; s < 2; s++) {
            snprintf(nm, sizeof nm, s ? "f%d" : "g%d", k);
            GfxVal b[6] = { gv_str(nm), gv_num(200 + 2 * k + s), gv_num(0), gv_num(0), gv_num(10), gv_num(g_eLineH) };
            invoke_n(mv, BOX ".createTextField", b, 6);
            snprintf(p, sizeof p, BOX ".%s.html", nm); gfx_setvar(mv, p, gv_bool(1));
            snprintf(p, sizeof p, BOX ".%s.selectable", nm); gfx_setvar(mv, p, gv_bool(0));
            snprintf(p, sizeof p, BOX ".%s.autoSize", nm); gfx_setvar(mv, p, gv_str("left"));
            if (!s) { snprintf(p, sizeof p, BOX ".%s._alpha", nm); gfx_setvar(mv, p, gv_num(75)); }
        }
        snprintf(nm, sizeof nm, "e%d", k);
        GfxVal c[2] = { gv_str(nm), gv_num(300 + k) }; invoke_n(mv, BOX ".createEmptyMovieClip", c, 2);
        snprintf(p, sizeof p, BOX ".e%d.createEmptyMovieClip", k);
        GfxVal d[2] = { gv_str("h"), gv_num(1) }; invoke_n(mv, p, d, 2);
        g_cellIcon[k][0] = 0; g_cellDone[k] = 0; g_cellSc[k] = 100;
        g_cell[k].html[0] = 0; g_cell[k].plain[0] = 0;
        snprintf(p, sizeof p, BOX ".e%d._visible", k); gfx_setvar(mv, p, gv_bool(0));
    }
    g_nCell = -1; g_effDirty = 1;
}
// an effect icon: each new texture into a fresh child m<gen> (so the size read back is the new one's), scaled once loaded
static void eff_icon(void *mv, int k, const Icon *ic) {
    char p[128];
    if (strcmp(g_cellIcon[k], ic->path)) {
        if (g_cellGen[k]) { snprintf(p, sizeof p, BOX ".e%d.h.m%d.removeMovieClip", k, g_cellGen[k]); invoke_n(mv, p, NULL, 0); }
        g_cellGen[k]++;
        snprintf(p, sizeof p, BOX ".e%d.h.createEmptyMovieClip", k);
        char nm[16]; snprintf(nm, sizeof nm, "m%d", g_cellGen[k]);
        GfxVal a[2] = { gv_str(nm), gv_num(g_cellGen[k] % 1000 + 1) }; invoke_n(mv, p, a, 2);
        snprintf(p, sizeof p, BOX ".e%d.h.m%d", k, g_cellGen[k]);
        GfxVal l[2] = { gv_str(p), gv_str(ic->path) }; invoke_n(mv, "Receive_LoadMovie", l, 2);
        snprintf(g_cellIcon[k], sizeof g_cellIcon[k], "%s", ic->path); g_cellDone[k] = 0; g_cellSc[k] = ic->scale;
        snprintf(p, sizeof p, BOX ".e%d._visible", k); gfx_setvar(mv, p, gv_bool(0));
        return;
    }
    if (g_cellDone[k] > 0) return;
    snprintf(p, sizeof p, BOX ".e%d.h.m%d", k, g_cellGen[k]);
    double w = clip_w(mv, p), h = clip_h(mv, p);
    if (w <= 0 || h <= 0) {
        if (--g_cellDone[k] == -12 && g_verbose) logmsg("effect icon %d not there after 12 checks: %s", k, ic->path);
        return;
    }
    double sc = ic->scale * g_eIconS / (w > h ? w : h);
    snprintf(p, sizeof p, BOX ".e%d._xscale", k); gfx_setvar(mv, p, gv_num(sc));
    snprintf(p, sizeof p, BOX ".e%d._yscale", k); gfx_setvar(mv, p, gv_num(sc));
    g_cellDone[k] = 1; g_effDirty = 1;
}
static const char *C_GOOD = "#8FE36A", *C_BAD = "#FF6B5A", *C_NEUTRAL = "#FFFFFF", *C_SMALL = "#B8AE90";
static void pct(char *o, size_t n, float d) {                   // +50% / -30% / +2.5%
    float a = d < 0 ? -d : d;
    if (a < 0.095f && a >= 0.005f) snprintf(o, n, "%+.1f%%", d * 100); else snprintf(o, n, "%+.0f%%", d * 100);
}
static void num(char *o, size_t n, float v) { if (v - (int)v > 0.05f || (int)v - v > 0.05f) snprintf(o, n, "%+.1f", v); else snprintf(o, n, "%+.0f", v); }
// v4: the effect tags in Russian when the game is Russian (see is_ru)
static const char *TAG_RU[][2] = {
    { "hidden", "\xD1\x81\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82" },
    { "no fire", "\xD0\xBD\xD0\xB5 \xD1\x81\xD1\x82\xD1\x80\xD0\xB5\xD0\xBB\xD1\x8F\xD0\xB5\xD1\x82" },
    { "no retreat", "\xD0\xB1\xD0\xB5\xD0\xB7 \xD0\xBE\xD1\x82\xD1\x85\xD0\xBE\xD0\xB4\xD0\xB0" },
    { "light", "\xD0\xBB\xD1\x91\xD0\xB3\xD0\xBA." },
    { "heavy", "\xD1\x82\xD1\x8F\xD0\xB6." },
    { "power", "\xD1\x81\xD0\xB8\xD0\xBB." },
    { "pierce", "\xD0\xBF\xD1\x80\xD0\xBE\xD0\xB1\xD0\xB8\xD0\xB2." },
    { "flame", "\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xBD\xD1\x8C" },
    { "expl", "\xD0\xB2\xD0\xB7\xD1\x80\xD1\x8B\xD0\xB2" },
    { "plasma", "\xD0\xBF\xD0\xBB\xD0\xB0\xD0\xB7\xD0\xBC\xD0\xB0" },
    { "det", "\xD0\xB4\xD0\xB5\xD1\x82\xD0\xBE\xD0\xBD." },
    { "vs bld", "\xD0\xBF\xD0\xBE \xD0\xB7\xD0\xB4." },
    { "vs hero", "\xD0\xBF\xD0\xBE \xD0\xBA\xD0\xBE\xD0\xBC." },
    { "vs heavy", "\xD0\xBF\xD0\xBE \xD1\x82\xD1\x8F\xD0\xB6." },
    { "vs inf", "\xD0\xBF\xD0\xBE \xD0\xBF\xD0\xB5\xD1\x85." },
    { "vs veh", "\xD0\xBF\xD0\xBE \xD1\x82\xD0\xB5\xD1\x85\xD0\xBD." },
    { "melee", "\xD0\xB1\xD0\xBB\xD0\xB8\xD0\xB6\xD0\xBD." },
    { "reload", "\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB7\xD0\xB0\xD1\x80." },
    { "burst", "\xD0\xBE\xD1\x87\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB4\xD1\x8C" },
    { "max", "\xD0\xBC\xD0\xB0\xD0\xBA\xD1\x81." },
};
static const char *tag_tr(const char *t) {
    if (!is_ru() || !*t) return t;
    for (unsigned i = 0; i < sizeof TAG_RU / sizeof TAG_RU[0]; i++) if (!strcmp(t, TAG_RU[i][0])) return TAG_RU[i][1];
    return t;
}
// the cell's text: the value (coloured by what it means for the squad), the tag and the seconds left in small type
static void cell_text(Cell *c) {
    const EffDef *d = &EFFDEF[c->def]; char v[32] = ""; float delta = 0;
    float m = c->mul, a = c->add; int addOnly = (m - 1 < 0.005f && 1 - m < 0.005f);
    switch (d->mode) {
    case EM_RATE:
        if (m > 0.01f && !addOnly) { delta = 1 / m - 1; pct(v, sizeof v, delta); } else { delta = -a; num(v, sizeof v, a); } break;
    case EM_ADD:  if (!addOnly && (a < 0.005f && a > -0.005f)) { delta = m - 1; pct(v, sizeof v, delta); } else { delta = a; num(v, sizeof v, a); } break;
    case EM_ADDP: if (!addOnly && (a < 0.005f && a > -0.005f)) { delta = m - 1; pct(v, sizeof v, delta); } else { delta = a; snprintf(v, sizeof v, "%+.0f%%", a); } break;
    case EM_ON:   delta = 1; snprintf(v, sizeof v, "%s", tag_tr(d->tag)); break;
    case EM_OFF:  delta = -1; snprintf(v, sizeof v, "%s", tag_tr(d->tag)); break;
    default:      if (addOnly) { delta = a; num(v, sizeof v, a); } else { delta = m - 1; pct(v, sizeof v, delta); } break;
    }
    const char *col = delta * d->good > 0 ? C_GOOD : delta * d->good < 0 ? C_BAD : C_NEUTRAL;
    const char *tag = d->mode == EM_ON || d->mode == EM_OFF ? "" : d->tag;
    char sm[48]; snprintf(sm, sizeof sm, "%s", tag_tr(tag));
    if (*sm) snprintf(c->html, sizeof c->html, "<font face=\"$NormalFont\" size=\"%d\" color=\"%s\">%s<font size=\"%d\" color=\"%s\"> %s</font></font>",
                      g_efs, col, v, g_efs * 3 / 4, C_SMALL, sm);
    else snprintf(c->html, sizeof c->html, "<font face=\"$NormalFont\" size=\"%d\" color=\"%s\">%s</font>", g_efs, col, v);
    snprintf(c->plain, sizeof c->plain, "%s %s%s%s", mod_name(d->cls, d->type), v, *sm ? " " : "", sm);
}
static int cell_cmp(const void *x, const void *y) {           // the biggest change first
    const Cell *a = x, *b = y;
    return a->score < b->score ? 1 : a->score > b->score ? -1 : a->def - b->def;
}
// from the engine's numbers to the cells shown (merged by icon + tag: the five knockback chances are one "knockback")
static int compose(const Effs *fx, Cell *out) {
    Cell all[NEFFDEF]; int n = 0;
    for (int i = 0; i < NEFFDEF; i++) {
        const EffDef *d = &EFFDEF[i]; const Eff *x = &fx->e[i];
        if (d->mode == EM_ON ? x->sum <= 0 : d->mode == EM_OFF ? x->sum >= 0 : x->score <= 0.004f) continue;
        int f = -1;
        for (int q = 0; q < n; q++) if (EFFDEF[all[q].def].icon == d->icon && !strcmp(EFFDEF[all[q].def].tag, d->tag)) f = q;
        Cell c = { i, x->mul, x->add, x->sum, x->score, "", "" };
        if (f < 0) all[n++] = c;
        else if (c.score > all[f].score) all[f] = c;
    }
    qsort(all, n, sizeof all[0], cell_cmp);
    if (n > g_effMax) n = g_effMax;
    for (int k = 0; k < n; k++) { out[k] = all[k]; cell_text(&out[k]); }
    return n;
}
static int g_effShown;                                         // cells laid out (the rest did not fit)
// v4: two rows of cells at [Effects] FontSize beside the two stat rows; when they do not hold all the cells, three rows at
// CompactFontSize in the same height (grantovich: "not all the modifiers fit"). A dry run with the widths at the normal
// size decides; switching changes the font, so the cells are re-made and placed on the next update.
static int eff_rows(int compact) { return compact ? 3 : 2; }
static double eff_row_y(int compact, int r) { return 134.7 + 2 + g_offY + r * (compact ? 2 * g_lineH / 3 : g_lineH); }
static double eff_row_h(int compact) { return compact ? 2 * g_lineH / 3 : g_lineH; }
static double eff_row_right(int compact, int r) {      // the stats of the row(s) the cell row lies beside
    if (!compact) return g_rowLeft[r];
    if (r == 0) return g_rowLeft[0];
    if (r == 2) return g_rowLeft[1];
    return g_rowLeft[0] < g_rowLeft[1] ? g_rowLeft[0] : g_rowLeft[1];
}
// place the cells with text widths w[] at font fs: x / row of each, how many fit
static int eff_place(int compact, int fs, const double *w, int n, double *xs, int *rs) {
    double iconS = g_eisize ? g_eisize * fs / (double)g_esize : 1.3 * fs, gapI = 0.2 * fs, gapItem = 0.6 * fs;
    double x0 = g_effX + g_offX, x = x0; int row = 0, fit = 0;
    for (int k = 0; k < n; k++) {
        double cw = iconS + gapI + w[k];
        for (; row < eff_rows(compact); row++, x = x0) {
            double right = eff_row_right(compact, row);
            if (x + cw <= right - g_effGap || (x == x0 && right == 0)) break;
        }
        if (row >= eff_rows(compact)) break;
        xs[k] = x; rs[k] = row; x += cw + gapItem; fit++;
    }
    return fit;
}
static void layout_eff(void *mv) {
    int fs = g_efs; double iconS = g_eisize ? g_eisize * fs / (double)g_esize : 1.3 * fs, gapI = 0.2 * fs;
    double w[MAXEFF], wn[MAXEFF], xs[MAXEFF]; int rs[MAXEFF], n = g_nCell < MAXEFF ? g_nCell : MAXEFF;
    for (int k = 0; k < n; k++) {
        char p[96]; snprintf(p, sizeof p, BOX ".f%d", k);
        w[k] = clip_w(mv, p); if (w[k] < 0) w[k] = 0.55 * fs * 5;
        wn[k] = w[k] * g_esize / (double)fs * (g_eCompact ? 1.05 : 1.0);   // at the normal size (a margin before going back)
    }
    int want = g_esizeC < g_esize && n > 0 && eff_place(0, g_esize, wn, n, xs, rs) < n;
    if (want != g_eCompact) {                                 // re-made at the other size on the next update
        g_eCompact = want; g_efs = want ? g_esizeC : g_esize;
        g_eIconS = g_eisize ? g_eisize * g_efs / (double)g_esize : 1.3 * g_efs; g_eLineH = 1.3 * g_efs + 4;
        for (int k = 0; k < MAXEFF; k++) { g_cell[k].html[0] = 0; if (g_cellDone[k] > 0) g_cellDone[k] = 0; }
        g_effDirty = 1;
        if (g_verbose) logmsg("effects: %d cells - %s", n, want ? "three compact rows" : "two rows");
        return;
    }
    int fit = eff_place(g_eCompact, fs, w, n, xs, rs); g_effShown = 0;
    double lh = 1.3 * fs + 4, rh = eff_row_h(g_eCompact);
    for (int k = 0; k < MAXEFF; k++) {
        char p[96]; int vis = k < fit;
        if (vis) {
            double x = xs[k], yr = eff_row_y(g_eCompact, rs[k]), yt = yr + (rh - lh) / 2;
            double ov = iconS * (g_cellSc[k] - 100) / 200.0;
            setnum(mv, BOX ".e%d", k, "._x", x - ov); setnum(mv, BOX ".e%d", k, "._y", yr + (rh - iconS) / 2 + 1 - ov);
            setnum(mv, BOX ".f%d", k, "._x", x + iconS + gapI); setnum(mv, BOX ".f%d", k, "._y", yt);
            setnum(mv, BOX ".g%d", k, "._x", x + iconS + gapI + 1.1); setnum(mv, BOX ".g%d", k, "._y", yt + 1.3);
            g_effShown++;
        }
        snprintf(p, sizeof p, BOX ".e%d._visible", k); gfx_setvar(mv, p, gv_bool(vis && g_cellDone[k] > 0));
        snprintf(p, sizeof p, BOX ".f%d._visible", k); gfx_setvar(mv, p, gv_bool(vis));
        snprintf(p, sizeof p, BOX ".g%d._visible", k); gfx_setvar(mv, p, gv_bool(vis));
    }
    g_effDirty = 0;
}
static void set_effects(void *mv, const Cell *c, int n) {
    for (int k = 0; k < n; k++) {
        if (strcmp(c[k].html, g_cell[k].html)) {
            char p[96], sh[260];
            snprintf(p, sizeof p, BOX ".f%d.htmlText", k); gfx_setvar(mv, p, gv_str(c[k].html));
            char *o = sh; const char *q = c[k].html;                 // the shadow: the same, black
            while (*q && o < sh + sizeof sh - 8) {
                if (*q == '#' && strlen(q) >= 7 && (!strncmp(q, C_GOOD, 7) || !strncmp(q, C_BAD, 7) || !strncmp(q, C_NEUTRAL, 7) || !strncmp(q, C_SMALL, 7))) { memcpy(o, "#000000", 7); o += 7; q += 7; }
                else *o++ = *q++;
            }
            *o = 0;
            snprintf(p, sizeof p, BOX ".g%d.htmlText", k); gfx_setvar(mv, p, gv_str(sh));
            g_effDirty = 1;
        }
        eff_icon(mv, k, &g_eicon[EFFDEF[c[k].def].icon]);
        g_cell[k] = c[k];
    }
    if (n != g_nCell) g_effDirty = 1;
    for (int k = n; k < MAXEFF; k++) g_cell[k].html[0] = 0;
    g_nCell = n;
    if (g_effDirty) layout_eff(mv);
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
// v3: a watchdog (its own thread, reads two globals, writes the log only): where the panel update is if it does not return
static DWORD WINAPI watchdog(LPVOID _) {
    DWORD lastFrames = 0, quiet = 0; int told = 0;
    for (;;) {
        Sleep(1000);
        DWORD f = g_frames, now = GetTickCount();
        if (f != lastFrames) { lastFrames = f; quiet = 0; told = 0; continue; }
        if (++quiet == 5 && f) { logmsg("watchdog: the selection panel's update has not run for 5 s (frames %lu), last in \"%s\" %lu ms ago", f, (const char *)g_where, now - g_whereT); told = 1; }
        if (told && quiet % 60 == 0) logmsg("watchdog: still no panel update (%lu s), last in \"%s\"", quiet, (const char *)g_where);
    }
    return 0;
}
static void frame(char *form) {
    void *mv = gfx_movie_of_form(form);
    if (!mv) return;
    DWORD now = GetTickCount();
    reload_ini(now);
    if (mv != g_mv || (now - g_check > 1000 && (g_check = now, !gfx_exists(mv, "core_actions.us_box.t0")))) {
        if (!gfx_exists(mv, "core_actions")) return;
        g_mv = mv; g_check = now; g_shown = -1;
        make_fields(mv); make_eff_fields(mv);
    }
    if (now - g_lastCalc < 250) return;                  // four times a second is plenty
    g_lastCalc = now;
    Sel s; memset(&s, 0, sizeof s);
    AT("selection");
    if (g_enabled && !g_dead && run_guarded(get_selection, &s, "the selection") && s.n == -1) return;   // the selection is busy: as it was
    if (!g_enabled || g_dead || s.n != 1) { show(mv, 0); g_lastSq = NULL; return; }
    AT("stats"); Stats st; Job j = { s.squad, &st };
    if (!run_guarded(squad_stats, &j, "the squad's numbers") || !st.models) { show(mv, 0); return; }
    AT("stats panel"); set_stats(mv, &st);
    show(mv, 1);
    if (g_verbose && s.squad != g_lastSq) {
        logmsg("sel %s: ranged %.2f (tooltip %.2f, engine %.2f), melee %.2f (tooltip %.2f, engine %.2f), skill %d (v1 %d), %s, range %.1f, speed %.2f, sight %.1f",
               p_sqName ? p_sqName(s.squad) : "?", st.rdps, st.rtip, st.reng, st.mdps, st.mtip, st.meng, st.skill, st.skillOld, st.armourName, st.range, st.speed, st.sight);
    }
    g_lastSq = s.squad;
    int seen = 0; for (int i = 0; i < g_nLogged; i++) if (g_loggedSq[i] == s.squad) seen = 1;
    if (g_verbose && !seen) {
        g_loggedSq[g_nLogged++ % 32] = s.squad; if (g_nLogged > 32) g_nLogged = 32;
        logmsg("squad %s: %d models, %d weapons - ranged DPS %.2f (per model %.2f%s; tooltip %.2f; engine %.2f), melee DPS %.2f (per model %.2f%s; tooltip %.2f; engine %.2f), melee skill %d (melee_ext %d), "
               "armour %d %s / %s, range %.1f (blueprint %.1f), speed %.2f (blueprint %.2f), sight %.1f; weapon times noted %ld calls %ld bags |%s",
               p_sqName ? p_sqName(s.squad) : "?", st.models, st.weapons, st.rdps, st.rdps1, st.sameR ? "" : " differ", st.rtip, st.reng, st.mdps, st.mdps1,
               st.sameM ? "" : " differ", st.mtip, st.meng, st.skill, st.skillOld, st.armour, armour_internal(st.armour), st.armourName, st.range, st.range0,
               st.speed, st.speed0, st.sight, g_wtCalls, g_wtCount, g_detail);
    }
    // v3: the effects
    if (g_effOn && g_effMax > 0 && !g_dead) {
        static Effs fx; memset(&fx, 0, sizeof fx); fx.squad = s.squad;
        EJob ej = { s.squad, &fx };
        g_detailN = 0; g_detail[0] = 0;
        AT("effect values");
        if (run_guarded(eff_values, &ej, "the squad's effects")) {
            AT("effects panel"); Cell cells[MAXEFF]; int n = compose(&fx, cells);
            set_effects(mv, cells, n);
            static char last[1024]; static char *lastSq; char now[1024]; int o = 0; now[0] = 0;
            for (int k = 0; k < n && o < (int)sizeof now - 80; k++) o += snprintf(now + o, sizeof now - o, "%s%s", k ? "; " : "", cells[k].plain);
            if (g_verbose && (strcmp(now, last) || s.squad != lastSq)) {
                char per[400]; int q = 0; per[0] = 0;                 // the engine's numbers, and how many of the models have each
                for (int k = 0; k < n && q < (int)sizeof per - 60; k++) q += snprintf(per + q, sizeof per - q, " %s x%.3f %+.2f sum %g on %d/%d;", mod_name(EFFDEF[cells[k].def].cls, EFFDEF[cells[k].def].type),
                                                                   cells[k].mul, cells[k].add, cells[k].sum, fx.e[cells[k].def].nEnt, g_effNEnts);
                logmsg("effects %s: %s%s (%d shown) |%s", p_sqName ? p_sqName(s.squad) : "?", n ? now : "none", n > g_effShown ? " - not all fit" : "", g_effShown, per);
                strcpy(last, now); lastSq = s.squad;
            }
        }
    } else if (g_nCell) set_effects(mv, NULL, 0);
}
static void __thiscall hk_sp_upd(char *form, float dt) { g_frames++; AT("the game's panel update"); o_spUpd(form, dt); AT("ours"); frame(form); AT("idle"); }

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
    p_tryEnter = (TE_t)plat("?TryEnter@RecursiveMutex@Plat@@QAE_NXZ");
    p_sqi     = (QI_t)sim("?QI@SquadController@@QAEPAVSquadExtension@@I@Z");
    g_haveMName = code_is(FN_MNAME_E, MNAME_PRO, 3) && code_is(FN_MNAME_W, MNAME_PRO, 3) && code_is(FN_MNAME_S, MNAME_PRO, 3);
    g_haveSkill = code_is(FN_SKILL, SKILL_PRO, 4);
    g_haveLvl = code_is(FN_LVLMUL, LVL_PRO, 4);
    g_haveTun = code_is(FN_TUNING, TUN_PRO, 4);
    g_haveEmod = code_is(FN_EMOD, EMOD_PRO, 4);
    g_haveComb = g_haveTun && code_is(0x007E2F50, COMB_PRO, 4) && rd((void *)0x00F88130, 8);
    logmsg("engine: melee skill %s, level multiplier %s, world tuning %s, entity modifiers %s, combat tuning %s; v3: squad modifiers %s, "
           "modifier names %s, selection TryEnter %s", g_haveSkill ? "ok" : "NOT FOUND",
           g_haveLvl ? "ok" : "NOT FOUND", g_haveTun ? "ok" : "NOT FOUND", g_haveEmod ? "ok" : "NOT FOUND", g_haveComb ? "ok" : "NOT FOUND",
           p_sqi ? "ok" : "NOT FOUND", g_haveMName ? "ok" : "NOT FOUND", p_tryEnter ? "ok" : "NOT FOUND");
    return p_qi && p_allEnts && p_begin && p_end && p_enter && p_leave && code_is(FN_MOD, MOD_PRO, 4);
}

static DWORD WINAPI worker(LPVOID _) {
    for (int i = 0; i < 120 && !(GetModuleHandleA("SimEngine.dll") && GetModuleHandleA("Platform.dll")); i++) Sleep(500);
    if (!resolve()) { logmsg("SimEngine.dll / Platform.dll functions or DOW2.exe 3.19 code not found - no numbers"); return 0; }
    AddVectoredExceptionHandler(1, us_veh);
    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
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
    {   // 0.5 (an add-on for players now): the log starts over once it is past 2 MB
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (GetFileAttributesExW(g_log, GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > 2u << 20)) DeleteFileW(g_log);
    }
    load_ini();
    logmsg("UnitStats 0.5 (DPS as the attribute editor counts it%s; selected squad's DPS / melee skill / armour / range / speed / sight and its active effects with the game's icons left of Retreat, in the game's own selection panel movie)", g_showEngine ? "; ShowEngineCycle=1: the engine's real cycle shown instead" : "");
    if (!g_enabled) { logmsg("disabled in UnitStats.ini"); return TRUE; }
    s2t_install();
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
