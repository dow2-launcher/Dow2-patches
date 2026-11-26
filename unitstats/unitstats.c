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
// v7 / 0.7 (26.11.2026, grantovich: "on a transport the buffs/debuffs are drawn over the icons of the units inside"; "buffs to
// speed, range, sight, damage, melee skill - change the number itself, green when buffed, red when debuffed, no separate
// lines"; "energy and HP buffs - into the bar under the portrait"):
//   * the stat numbers were already the engine's modified values (DPS through the weapon modifier function, speed / melee
//     skill through the entity modifier function and the game's getter, range = the weapon's GetMaxRange, sight = SightExt);
//     now each is compared with the same number without any modifier (DPS: the same formula with the weapon's raw damage,
//     cooldown and reload; range / speed: the blueprint; sight / melee skill: the engine's own value with the entity
//     modifier function's multiplier and addition taken out) and drawn green when the effects raise it, red when they lower
//     it - and those effects (damage, melee / ranged / projectile damage, cooldown, reload, range, speed, sight, melee skill)
//     get no icon of their own any more;
//   * max HP / max energy: the native "500 / 500" text of the bar under the portrait (status_bars.health / .energy
//     .amount_txt) turns green / red; HP / energy regeneration: a small icon + "+50%" at the right end of that bar;
//   * the effects that map to no number (damage taken, suppression taken, accuracy, armour penetration, ...) stay icons, but
//     start right of every native widget visible in that strip (the loaded / garrisoned squads' portraits of
//     main_panel.multi_select), measured every update - nothing of ours covers them.
//   [UnitStats] BuffColors=0 = as 0.6 (white numbers, every effect an icon).
// v8 / 0.8 (26.11.2026, noofancy 24.11: "unit stats does not show the dogs' regeneration - an integer value there, it shows only
// per cent"; Hormagaunts at a Brood Nest, tyr_heal_aoe_broodnest = health_regeneration addition 0.1): HP / energy regeneration
// additions are per sim tick (0.05..0.35 in Elite's data, the tooltip's "1 HP/s" = 0.1) and an addition was scored at 1/100 of
// its size - under the 0.004 cut, never shown. Any real addition is shown now (eff_score), regeneration as "+1/s" (per second),
// a multiplier and an addition together "+100% +1/s". Other additions (max HP +200, range +5, melee skill +10, knockback -100)
// already scored above the cut; small speed additions (+0.15) only colour the speed number, as before.
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
static int g_buffColors = 1, g_barFx = 1;                  // v7: [UnitStats] BuffColors (effects colour the numbers), BarEffects (HP / energy into the bars)
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
static void load_ini_replay(void);
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
    g_buffColors = ini_int(L"BuffColors", 1, 0, 1); g_barFx = ini_int(L"BarEffects", 1, 0, 1);
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
    load_ini_replay();
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
    float range0, speed0;                                     // blueprint values (the log; v7: the base of range / speed)
    char armourName[96], armourShort[64];                     // v4: the game's name (log) and the short one shown
    float rdpsB, mdpsB, sightB; int skillB;                   // v7: the same numbers without any modifier (sightB < 0: zeroed by one)
    signed char dir[NITEMS];                                  // v7: +1 the effects raise it (green), -1 they lower it (red), 0 white
} Stats;
static int g_noMods;   // v7: weapon_dps without the weapon modifiers (the base the shown DPS is compared with)
static float wmod(void *w, float v, void *ent, int type) { return g_noMods ? v : us_mod(w, v, ent, type); }
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
    float dmg = wmod(w, *(float *)(bag + 0x64), ent, MOD_DAMAGE);   // type 7 takes in the melee / ranged / projectile damage types too (0x8A65C0)
    float cdf = wmod(w, ticks(*(int *)(bag + 0xF0)), ent, MOD_COOLDOWN);
    float rl0 = wmod(w, ticks(*(int *)(bag + 0x11C)), ent, MOD_RELOAD), rl1 = wmod(w, ticks(*(int *)(bag + 0x120)), ent, MOD_RELOAD);
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
static void fit(float f1, float f0, float *mul, float *add);
#define EMOD_SIGHT 0x0A
#define EMOD_SKILL 0x2F
// v7: the value without the entity modifiers of a type: the engine's value v = (base + add) x mul (0x8A6AC0, f(1) - f(0) = mul,
// f(0) / mul = add) -> base = v / mul - add; -1 when a multiplier of ~0 took it all (no base to tell)
static float unmod(float v, char *ent, int type) {
    if (!g_haveEmod || !g_useEmod) return v;
    float m, a; fit(us_emod(1.0f, ent, type), us_emod(0.0f, ent, type), &m, &a);
    return m > 0.01f ? v / m - a : -1;
}
static void squad_stats(void *arg) {
    Job *j = arg; Stats *st = j->st; memset(st, 0, sizeof *st); st->speed = st->speed0 = -1; st->skill = st->skillOld = st->skillB = -1; st->armour = -1;
    st->sameR = st->sameM = 1; g_noMods = 0;
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
        float erd = 0, emd = 0, ert = 0, emt = 0, ere = 0, eme = 0, erB = 0, emB = 0;
        char *ce = p_qi(ctrl, EXT_COMBAT);
        if (vt_is(ce, VT_COMBATEXT)) {
            char **hp = *(char ***)(ce + 0x50); int n = *(int *)(ce + 0x54);
            for (int i = 0; i < n && i < 16 && rd(hp, (i + 1) * 4); i++) {
                char *h = hp[i]; if (!rd(h, 0x54)) continue;
                char *w = *(char **)(h + 0x50); if (!rd(w, 0xB0)) continue;
                char *bag = *(char **)(w + 0x54); if (!vt_is(bag, VT_WEAPONBAG)) continue;
                char *went = rd(*(char **)(w + 0xAC), 0x18) ? *(char **)(*(char **)(w + 0xAC) + 0x14) : ent;
                int melee; Cyc cy; float dps = weapon_dps(w, bag, went, &melee, &cy), dpsB = dps;
                if (g_buffColors) { int m2; Cyc c2; g_noMods = 1; dpsB = weapon_dps(w, bag, went, &m2, &c2); g_noMods = 0; }   // v7: the base
                if (melee) { if (dpsB > emB) emB = dpsB; } else erB += dpsB;
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
        st->rdps += erd; st->mdps += emd; st->rtip += ert; st->mtip += emt; st->reng += ere; st->meng += eme; st->rdpsB += erB; st->mdpsB += emB;
        char *se = p_qi(ctrl, EXT_SIGHT);
        if (vt_is(se, VT_SIGHTEXT)) { float s = *(float *)(se + 0x28); if (s > st->sight) { st->sight = s; st->sightB = g_buffColors ? unmod(s, ent, EMOD_SIGHT) : s; } }
        int sk = -1;
        if (g_haveSkill && g_useSkill && p_qi(ctrl, EXT_MELEE)) {
            sk = us_skill(ent);
            if (sk > st->skill) { st->skill = sk; float b = g_buffColors ? unmod((float)sk, ent, EMOD_SKILL) : sk; st->skillB = b < 0 ? -2 : (int)floorf(b + 0.5f); }
        }
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
    // v7: which way the effects move each number (half a percent of slack for the float noise of the modifier functions)
    if (g_buffColors) {
        #define DIR(c, b) ((c) > (b) + 0.005f * fabsf(b) + 0.001f ? 1 : (c) < (b) - 0.005f * fabsf(b) - 0.001f ? -1 : 0)
        if (st->nRanged) { st->dir[I_RANGED] = DIR(st->rdps, st->rdpsB); st->dir[I_RANGE] = DIR(st->range, st->range0); }
        if (st->nMelee) st->dir[I_MELEE] = DIR(st->mdps, st->mdpsB);
        if (st->speed >= 0 && st->speed0 > 0) st->dir[I_SPEED] = DIR(st->speed, st->speed0);
        if (st->sight > 0 || st->sightB != 0) st->dir[I_SIGHT] = st->sightB < 0 ? -1 : DIR(st->sight, st->sightB);
        if (st->skill >= 0) st->dir[I_SKILL] = st->skillB == -2 ? -1 : st->skill > st->skillB ? 1 : st->skill < st->skillB ? -1 : 0;
        #undef DIR
    }
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
enum { EM_PCT, EM_RATE, EM_ADD, EM_ADDP, EM_ON, EM_OFF, EM_REGEN };   // PCT "+50%"; RATE: a time multiplier as a rate ("cooldown x0.5" = "+100%");
                                                           // ADD "+50"; ADDP "-100%" (an addition to a chance); ON / OFF: enabled / disabled;
                                                           // v8 REGEN: HP / energy regeneration - a multiplier "+100%", an addition per sim tick
                                                           // shown per second "+1/s" (Brood Nest: addition 0.1 = the tooltip's "1 HP/s")
// v7: fold - FOLD_STAT: the effect is in one of the stat numbers (shown by its colour, no icon of its own); FOLD_BAR: in the HP /
// energy bar under the portrait (the selection panel only; the replay boxes have no such bar and keep its icon)
enum { FOLD_NONE = 0, FOLD_STAT = 1, FOLD_BAR = 2 };
typedef struct { unsigned char cls, type; signed char good; unsigned char mode, icon; const char *tag; unsigned char fold; } EffDef;
static const EffDef EFFDEF[] = {
    { MC_ENTITY, 0x00, +1, EM_PCT,  EI_HEALTH,    "", FOLD_BAR }, // health_maximum
    { MC_ENTITY, 0x01, +1, EM_REGEN, EI_REGEN,    "", FOLD_BAR }, // health_regeneration
    { MC_ENTITY, 0x06, +1, EM_PCT,  EI_SPEED,     "", FOLD_STAT }, // speed_maximum
    { MC_ENTITY, 0x0A, +1, EM_PCT,  EI_SIGHT,     "", FOLD_STAT }, // sight_radius
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
    { MC_ENTITY, 0x2F, +1, EM_ADD,  EI_SKILL,     "", FOLD_STAT }, // skill_melee
    { MC_ENTITY, 0x31, -1, EM_ADDP, EI_KNOCKDEF,  "" },          // knockback_chance_* (five kinds, one icon)
    { MC_ENTITY, 0x32, -1, EM_ADDP, EI_KNOCKDEF,  "" },
    { MC_ENTITY, 0x33, -1, EM_ADDP, EI_KNOCKDEF,  "" },
    { MC_ENTITY, 0x34, -1, EM_ADDP, EI_KNOCKDEF,  "" },
    { MC_ENTITY, 0x35, -1, EM_ADDP, EI_KNOCKDEF,  "" },
    { MC_WEAPON, 0x07, +1, EM_PCT,  EI_DAMAGE,    "", FOLD_STAT }, // damage_weapon
    { MC_WEAPON, 0x0E, +1, EM_PCT,  EI_MELEE,     "", FOLD_STAT }, // damage_melee_weapon
    { MC_WEAPON, 0x0F, +1, EM_PCT,  EI_RANGED,    "", FOLD_STAT }, // damage_ranged_weapon
    { MC_WEAPON, 0x10, +1, EM_PCT,  EI_RANGED,    "", FOLD_STAT }, // damage_projectile_weapon
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
    { MC_WEAPON, 0x03, +1, EM_RATE, EI_RATE,      "", FOLD_STAT }, // cooldown
    { MC_WEAPON, 0x02, +1, EM_RATE, EI_RATE,      "reload", FOLD_STAT },
    { MC_WEAPON, 0x04, +1, EM_PCT,  EI_RATE,      "burst" },
    { MC_WEAPON, 0x17, +1, EM_PCT,  EI_RANGE,     "", FOLD_STAT }, // range_maximum
    { MC_WEAPON, 0x0B, +1, EM_PCT,  EI_SUPPDMG,   "" },          // courage_damage
    { MC_WEAPON, 0x1D, -1, EM_PCT,  EI_SETUP,     "" },          // setup
    { MC_SQUAD,  0x01, -1, EM_PCT,  EI_SUPPDEF,   "" },          // received_courage_damage
    { MC_SQUAD,  0x08, +1, EM_OFF,  EI_NORETREAT, "no retreat" },// retreat_enable
    { MC_SQUAD,  0x02, -1, EM_PCT,  EI_COOLDOWN,  "" },          // ability_recharge_time_squad
    { MC_SQUAD,  0x0C, +1, EM_REGEN, EI_ENERGY,   "", FOLD_BAR }, // energy_regen_rate
    { MC_SQUAD,  0x0E, +1, EM_PCT,  EI_ENERGY,    "max", FOLD_BAR }, // energy_maximum
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
// how big a change is (the biggest is shown first; > 0.004 = shown at all). v8: an addition counts by its size / 100 (+50 HP =
// a +50% multiplier) but any real one is shown: the regeneration additions are per sim tick (Brood Nest +0.1 = 1 HP/s, Warlock
// +0.07, energy +0.05) and scored 0.001 they were never shown (noofancy 24.11: "the dogs' regeneration is not shown").
// No addition gives an exact 0 here ((0 + 0) x mul), float dust of a +x / -x pair stays under 1e-4.
static float eff_score(float mul, float add) {
    float m = mul > 1 ? mul - 1 : 1 - mul, a = add > 0 ? add : -add;
    return m + (a > 1e-4f ? (a * 0.01f > 0.005f ? a * 0.01f : 0.005f) : 0);
}
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
                    float sc = eff_score(m, a);
                    if (sc > bs) { bs = sc; mul = m; add = a; }
                }
            } else continue;
            float sc = eff_score(mul, add) + (sum > 0 ? sum : -sum);
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
        else x->score = eff_score(x->mul, x->add);
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
static const char *C_GOOD = "#8FE36A", *C_BAD = "#FF6B5A", *C_NEUTRAL = "#FFFFFF", *C_SMALL = "#B8AE90";
// the black shadow copy of a text: every color="#xxxxxx" -> #000000
static void html_shadow(const char *h, char *o, size_t n) {
    char *e = o + n - 1;
    while (*h && o < e) {
        if (!strncmp(h, "color=\"#", 8) && strlen(h) >= 15 && o + 15 <= e) { memcpy(o, "color=\"#000000", 14); o += 14; h += 14; }
        else *o++ = *h++;
    }
    *o = 0;
}
// v7: the colour of a stat number - green when the effects on the squad raise it, red when they lower it
static const char *dir_col(int d) { return d > 0 ? C_GOOD : d < 0 ? C_BAD : C_NEUTRAL; }
static void item_html(char *h, size_t n, int fs, const char *val, const char *small, const char *col) {
    if (small && *small) snprintf(h, n, "<font face=\"$NormalFont\" size=\"%d\" color=\"%s\">%s<font size=\"%d\" color=\"%s\"> %s</font></font>", fs, col, val, fs * 3 / 4, C_SMALL, small);
    else snprintf(h, n, "<font face=\"$NormalFont\" size=\"%d\" color=\"%s\">%s</font>", fs, col, val);
}
static void set_item_text(void *mv, int k, const char *val, const char *small, const char *col) {
    char h[200], sh[200];
    item_html(h, sizeof h, g_size, val, small, col);
    if (!strcmp(h, g_txt[k])) return;
    strcpy(g_txt[k], h); g_layoutDirty = 1;
    char p[96]; snprintf(p, sizeof p, BOX ".t%d.htmlText", k); gfx_setvar(mv, p, gv_str(h));
    html_shadow(h, sh, sizeof sh);
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
// the shown text of each item: the value and the small "(3x14.6)" after it (v6: shared by the panel and the replay boxes)
static void stat_strings(const Stats *s, char v[NITEMS][64], char sm[NITEMS][48]) {
    int pm = g_perModel && s->models > 1;
    for (int k = 0; k < NITEMS; k++) { v[k][0] = 0; sm[k][0] = 0; }
    if (s->nRanged) fmtdps(v[I_RANGED], 64, s->rdps); else snprintf(v[I_RANGED], 64, "-");
    if (pm && s->sameR && s->rdps > 0) { char q[16]; fmtdps(q, sizeof q, s->rdps1); snprintf(sm[I_RANGED], 48, "(%d\xC3\x97%s)", s->models, q); }
    if (s->nMelee) fmtdps(v[I_MELEE], 64, s->mdps); else snprintf(v[I_MELEE], 64, "-");
    if (pm && s->sameM && s->mdps > 0) { char q[16]; fmtdps(q, sizeof q, s->mdps1); snprintf(sm[I_MELEE], 48, "(%d\xC3\x97%s)", s->models, q); }
    if (s->skill >= 0) snprintf(v[I_SKILL], 64, "%d", s->skill); else snprintf(v[I_SKILL], 64, "-");
    snprintf(v[I_ARMOUR], 64, "%s", s->armourShort[0] ? s->armourShort : "-");
    if (s->nRanged) fmtnum(v[I_RANGE], 64, s->range); else snprintf(v[I_RANGE], 64, "-");
    fmtnum(v[I_SPEED], 64, s->speed);
    fmtnum(v[I_SIGHT], 64, s->sight);
}
static const Icon *item_icon(const Stats *s, int k) { return k == I_ARMOUR ? &g_armIcon[armour_class(armour_internal(s->armour))] : &g_icon[k]; }
static void set_stats(void *mv, const Stats *s) {
    char v[NITEMS][64], sm[NITEMS][48];
    stat_strings(s, v, sm);
    for (int k = 0; k < NITEMS; k++) set_item_text(mv, k, v[k], sm[k], dir_col(s->dir[k]));
    for (int k = 0; k < NITEMS; k++) set_icon(mv, k, item_icon(s, k));
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
static void pct(char *o, size_t n, float d) {                   // +50% / -30% / +2.5%
    float a = d < 0 ? -d : d;
    if (a < 0.095f && a >= 0.005f) snprintf(o, n, "%+.1f%%", d * 100); else snprintf(o, n, "%+.0f%%", d * 100);
}
static void num(char *o, size_t n, float v) { if (v - (int)v > 0.05f || (int)v - v > 0.05f) snprintf(o, n, "%+.1f", v); else snprintf(o, n, "%+.0f", v); }
// v8: a regeneration addition per second ("+1/s", "+0.5/s", "+0.25/s", "+12/s")
#define TICKS_PER_SEC 10
static void persec(char *o, size_t n, float a) {
    float v = a * TICKS_PER_SEC, r = v < 0 ? -v : v, r10 = r * 10;
    if (r >= 10 || fabsf(r - roundf(r)) < 0.005f) snprintf(o, n, "%+.0f/s", v);
    else if (r >= 1 || fabsf(r10 - roundf(r10)) < 0.05f) snprintf(o, n, "%+.1f/s", v);
    else snprintf(o, n, "%+.2f/s", v);
}
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
// an effect's number as shown ("+50%", "-0.5", "x0.5 cooldown" as "+100%") and its change (> 0 more of it)
static float eff_text(int def, float m, float a, char *v, size_t n) {
    const EffDef *d = &EFFDEF[def]; float delta = 0; int addOnly = (m - 1 < 0.005f && 1 - m < 0.005f);
    v[0] = 0;
    switch (d->mode) {
    case EM_RATE:
        if (m > 0.01f && !addOnly) { delta = 1 / m - 1; pct(v, n, delta); } else { delta = -a; num(v, n, a); } break;
    case EM_ADD:  if (!addOnly && (a < 0.005f && a > -0.005f)) { delta = m - 1; pct(v, n, delta); } else { delta = a; num(v, n, a); } break;
    case EM_ADDP: if (!addOnly && (a < 0.005f && a > -0.005f)) { delta = m - 1; pct(v, n, delta); } else { delta = a; snprintf(v, n, "%+.0f%%", a); } break;
    case EM_REGEN:                                             // v8: "+1/s", "+100%", both: "+100% +1/s"
        if (addOnly) { delta = a; persec(v, n, a); }
        else if (a < 1e-4f && a > -1e-4f) { delta = m - 1; pct(v, n, delta); }
        else { char p1[16], p2[16]; pct(p1, sizeof p1, m - 1); persec(p2, sizeof p2, a); snprintf(v, n, "%s %s", p1, p2);
               delta = (m - 1) + a * TICKS_PER_SEC * 0.01f; }
        break;
    case EM_ON:   delta = 1; snprintf(v, n, "%s", tag_tr(d->tag)); break;
    case EM_OFF:  delta = -1; snprintf(v, n, "%s", tag_tr(d->tag)); break;
    default:      if (addOnly) { delta = a; num(v, n, a); } else { delta = m - 1; pct(v, n, delta); } break;
    }
    return delta;
}
// the cell's text: the value (coloured by what it means for the squad), the tag in small type
static void cell_text_fs(Cell *c, int efs) {   // v6: the font size given (the replay boxes have their own)
    const EffDef *d = &EFFDEF[c->def]; char v[32];
    float delta = eff_text(c->def, c->mul, c->add, v, sizeof v);
    const char *col = delta * d->good > 0 ? C_GOOD : delta * d->good < 0 ? C_BAD : C_NEUTRAL;
    const char *tag = d->mode == EM_ON || d->mode == EM_OFF ? "" : d->tag;
    char sm[48]; snprintf(sm, sizeof sm, "%s", tag_tr(tag));
    if (*sm) snprintf(c->html, sizeof c->html, "<font face=\"$NormalFont\" size=\"%d\" color=\"%s\">%s<font size=\"%d\" color=\"%s\"> %s</font></font>",
                      efs, col, v, efs * 3 / 4, C_SMALL, sm);
    else snprintf(c->html, sizeof c->html, "<font face=\"$NormalFont\" size=\"%d\" color=\"%s\">%s</font>", efs, col, v);
    snprintf(c->plain, sizeof c->plain, "%s %s%s%s", mod_name(d->cls, d->type), v, *sm ? " " : "", sm);
}
static void cell_text(Cell *c) { cell_text_fs(c, g_efs); }
static int cell_cmp(const void *x, const void *y) {           // the biggest change first
    const Cell *a = x, *b = y;
    return a->score < b->score ? 1 : a->score > b->score ? -1 : a->def - b->def;
}
static int eff_on(const EffDef *d, const Eff *x) { return d->mode == EM_ON ? x->sum > 0 : d->mode == EM_OFF ? x->sum < 0 : x->score > 0.004f; }
// from the engine's numbers to the cells shown (merged by icon + tag: the five knockback chances are one "knockback");
// v7: the effects of the fold kinds in `fold` are in a number / a bar instead (no cell)
static int compose(const Effs *fx, Cell *out, int fold) {
    Cell all[NEFFDEF]; int n = 0;
    for (int i = 0; i < NEFFDEF; i++) {
        const EffDef *d = &EFFDEF[i]; const Eff *x = &fx->e[i];
        if (!eff_on(d, x) || (d->fold & fold)) continue;
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
// ---------- v7: native widgets in the strip of the effects (grantovich 26.11.2026: "on a transport the buffs / debuffs are drawn
// over the icons of the units inside it") ----------
// The selection panel shows the squads inside a transport / a garrison as small portraits main_panel.multi_select.mini_portrait00..07
// (the same row the effects use: in core_actions' units x -490 + 66 per portrait, y 146..213). Every update their boxes are read back
// from the movie (each clip's _x/_y/_width/_height in its parent, then each parent's _x/_y/_xscale/_yscale up to _root, then into
// core_actions) and an effect row starts right of every visible one it really overlaps (more than 30 % of the height). The
// reinforce button / queue and the squad count above the strip are not taken: their clips' boxes hold hidden parts (the queue's
// progress dial, rollover frames) reaching far into the strip, while what they draw stays above it (stand 1, 26.11.2026).
static int gfx_getnum(void *mv, const char *path, double *out);
typedef struct { double x0, y0, x1, y1; } Rect;
static const char *NATIVE[] = {
    "main_panel.multi_select.mini_portrait00", "main_panel.multi_select.mini_portrait01", "main_panel.multi_select.mini_portrait02",
    "main_panel.multi_select.mini_portrait03", "main_panel.multi_select.mini_portrait04", "main_panel.multi_select.mini_portrait05",
    "main_panel.multi_select.mini_portrait06", "main_panel.multi_select.mini_portrait07" };
#define NNATIVE ((int)(sizeof NATIVE / sizeof NATIVE[0]))
static Rect g_obs[NNATIVE]; static int g_nObs; static int g_obsWho[NNATIVE];
static int native_rect(void *mv, const char *rel, const double *ca, Rect *r) {
    char p[160]; double v, x, y, w, h; int n = (int)strlen(rel);
    for (int i = 1; i <= n; i++) if (i == n || rel[i] == '.') {            // the clip and every parent visible
        snprintf(p, sizeof p, "_root.%.*s._visible", i, rel);
        if (!gfx_getnum(mv, p, &v) || !v) return 0;
    }
    snprintf(p, sizeof p, "_root.%s._x", rel); if (!gfx_getnum(mv, p, &x)) return 0;
    snprintf(p, sizeof p, "_root.%s._y", rel); if (!gfx_getnum(mv, p, &y)) return 0;
    snprintf(p, sizeof p, "_root.%s._width", rel); if (!gfx_getnum(mv, p, &w)) return 0;
    snprintf(p, sizeof p, "_root.%s._height", rel); if (!gfx_getnum(mv, p, &h)) return 0;
    if (w <= 0.5 || h <= 0.5) return 0;
    for (int i = n - 1; i > 0; i--) if (rel[i] == '.') {                    // the parents, innermost first (no rotation in this movie)
        double px, py, sx, sy;
        snprintf(p, sizeof p, "_root.%.*s._x", i, rel); if (!gfx_getnum(mv, p, &px)) return 0;
        snprintf(p, sizeof p, "_root.%.*s._y", i, rel); if (!gfx_getnum(mv, p, &py)) return 0;
        snprintf(p, sizeof p, "_root.%.*s._xscale", i, rel); if (!gfx_getnum(mv, p, &sx)) return 0;
        snprintf(p, sizeof p, "_root.%.*s._yscale", i, rel); if (!gfx_getnum(mv, p, &sy)) return 0;
        x = px + x * sx / 100; y = py + y * sy / 100; w *= sx / 100; h *= sy / 100;
    }
    r->x0 = (x - ca[0]) / ca[2]; r->x1 = (x + w - ca[0]) / ca[2]; r->y0 = (y - ca[1]) / ca[3]; r->y1 = (y + h - ca[1]) / ca[3];
    return 1;
}
static void scan_native(void *mv) {
    double ca[4]; Rect r[NNATIVE]; int who[NNATIVE], n = 0;
    if (gfx_getnum(mv, "_root.core_actions._x", &ca[0]) && gfx_getnum(mv, "_root.core_actions._y", &ca[1])
        && gfx_getnum(mv, "_root.core_actions._xscale", &ca[2]) && gfx_getnum(mv, "_root.core_actions._yscale", &ca[3])
        && (ca[2] /= 100) > 0.01 && (ca[3] /= 100) > 0.01)
        for (int i = 0; i < NNATIVE; i++) if (native_rect(mv, NATIVE[i], ca, &r[n])) who[n++] = i;
    for (int i = 0; i < n; i++) { r[i].x0 = floor(r[i].x0); r[i].y0 = floor(r[i].y0); r[i].x1 = ceil(r[i].x1); r[i].y1 = ceil(r[i].y1); }
    if (n == g_nObs && !memcmp(r, g_obs, n * sizeof r[0]) && !memcmp(who, g_obsWho, n * sizeof who[0])) return;
    memcpy(g_obs, r, n * sizeof r[0]); memcpy(g_obsWho, who, n * sizeof who[0]); g_nObs = n; g_effDirty = 1;
    if (g_verbose) {
        char t[1024]; int o = 0; t[0] = 0;
        for (int i = 0; i < n && o < (int)sizeof t - 100; i++) { const char *nm = strrchr(NATIVE[who[i]], '.');
            o += snprintf(t + o, sizeof t - o, " %s x %.0f..%.0f y %.0f..%.0f;", nm ? nm + 1 : NATIVE[who[i]], r[i].x0, r[i].x1, r[i].y0, r[i].y1); }
        logmsg("v7: native widgets in the panel's bottom strip (core_actions units):%s", n ? t : " none");
    }
}
// where a cell row may start: [Effects] Left, or right of every native widget it overlaps by more than 30 % of the smaller height
static double eff_row_left(int compact, int r) {
    double x0 = g_effX + g_offX, y = eff_row_y(compact, r), h = eff_row_h(compact), right = eff_row_right(compact, r);
    for (int i = 0; i < g_nObs; i++) {
        const Rect *o = &g_obs[i];
        double ov = (y + h < o->y1 ? y + h : o->y1) - (y > o->y0 ? y : o->y0), oh = o->y1 - o->y0;
        if (ov <= 0.3 * (h < oh ? h : oh) || (right != 0 && o->x0 >= right)) continue;
        if (o->x1 + g_effGap > x0) x0 = o->x1 + g_effGap;
    }
    return x0;
}
// place the cells with text widths w[] at font fs: x / row of each, how many fit
static int eff_place(int compact, int fs, const double *w, int n, double *xs, int *rs) {
    double iconS = g_eisize ? g_eisize * fs / (double)g_esize : 1.3 * fs, gapI = 0.2 * fs, gapItem = 0.6 * fs;
    int row = 0, fit = 0; double x0 = eff_row_left(compact, 0), x = x0;
    for (int k = 0; k < n; k++) {
        double cw = iconS + gapI + w[k];
        while (row < eff_rows(compact)) {
            double right = eff_row_right(compact, row);
            if (x + cw <= right - g_effGap || (x == x0 && right == 0)) break;
            if (++row < eff_rows(compact)) x = x0 = eff_row_left(compact, row);
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
        for (int k = 0; k < MAXEFF; k++) { char p[96];                 // nothing shown at the old size meanwhile
            snprintf(p, sizeof p, BOX ".e%d._visible", k); gfx_setvar(mv, p, gv_bool(0));
            snprintf(p, sizeof p, BOX ".f%d._visible", k); gfx_setvar(mv, p, gv_bool(0));
            snprintf(p, sizeof p, BOX ".g%d._visible", k); gfx_setvar(mv, p, gv_bool(0)); }
        g_effShown = 0;
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
            html_shadow(c[k].html, sh, sizeof sh);
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

// ---------- v7: HP / energy effects in the bars under the portrait (grantovich: "buffs to energy and HP - into the bar itself") ----------
// status_bars.health / .energy each have amount_txt ("500 / 500", the engine's own current / max, centred over the bar - the field
// spans -2..143.8 of the bar, white, set by the game with .text through Receive_SetText). Max HP / max energy changed by an effect:
// that native text turns green / red through the movie's own Receive_SetTextColor (put back to white when the effect is gone or
// nothing / another squad is shown; re-applied every update in case the game sets it). HP / energy regeneration changed: our small
// clip us_n in the bar (the effect's icon + "+50%"), right aligned at the bar's right end; when the centred number would reach it,
// the native field is moved left just enough (back to its own place when the note goes).
#define RGB_GOOD 0x8FE36A
#define RGB_BAD  0xFF6B5A
#define RGB_NATIVE 0xFFFFFF            // amount_txt's own colour (DefineEditText 151 / 160: white, vanilla and Elite)
#define BAR_FS 10
typedef struct { const char *bar; int maxCls, maxType, regCls, regType, icon; int maxDef, regDef; double ty, tx0, txNow;
                 int tinted, made, done, gen, vis, dirty, logged; char txt[240], ipath[200]; double noteW, pill[2]; } Bar;
static Bar g_bar[2] = { { "health", MC_ENTITY, 0x00, MC_ENTITY, 0x01, EI_REGEN }, { "energy", MC_SQUAD, 0x0E, MC_SQUAD, 0x0C, EI_ENERGY } };
static int g_barsOk;
static void bar_path(const Bar *b, char *o, size_t n, const char *rest) { snprintf(o, n, "_root.status_bars.%s.us_n%s%s", b->bar, *rest ? "." : "", rest); }
static void bar_make(void *mv, Bar *b) {
    char p[160], q[96]; snprintf(q, sizeof q, "_root.status_bars.%s.createEmptyMovieClip", b->bar);
    GfxVal a[2] = { gv_str("us_n"), gv_num(16110) }; invoke_n(mv, q, a, 2);
    for (int sh = 0; sh < 2; sh++) {
        const char *nm = sh ? "t" : "s";
        GfxVal c[6] = { gv_str(nm), gv_num(10 + sh), gv_num(0), gv_num(0), gv_num(10), gv_num(BAR_FS * 1.3 + 4) };
        bar_path(b, p, sizeof p, "createTextField"); invoke_n(mv, p, c, 6);
        char f[32]; snprintf(f, sizeof f, "%s.html", nm); bar_path(b, p, sizeof p, f); gfx_setvar(mv, p, gv_bool(1));
        snprintf(f, sizeof f, "%s.selectable", nm); bar_path(b, p, sizeof p, f); gfx_setvar(mv, p, gv_bool(0));
        snprintf(f, sizeof f, "%s.autoSize", nm); bar_path(b, p, sizeof p, f); gfx_setvar(mv, p, gv_str("left"));
        if (!sh) { snprintf(f, sizeof f, "%s._alpha", nm); bar_path(b, p, sizeof p, f); gfx_setvar(mv, p, gv_num(75)); }
    }
    GfxVal c[2] = { gv_str("i"), gv_num(20) }; bar_path(b, p, sizeof p, "createEmptyMovieClip"); invoke_n(mv, p, c, 2);
    GfxVal d[2] = { gv_str("h"), gv_num(1) }; bar_path(b, p, sizeof p, "i.createEmptyMovieClip"); invoke_n(mv, p, d, 2);
    bar_path(b, p, sizeof p, "_visible"); gfx_setvar(mv, p, gv_bool(0));
    snprintf(q, sizeof q, "_root.status_bars.%s.createEmptyMovieClip", b->bar);              // the dark backing of a coloured number:
    GfxVal bg[2] = { gv_str("us_bg"), gv_num(9 - 16384) }; invoke_n(mv, q, bg, 2);            // timeline depth 9 (AS: -16375) = over the bar's fill (4), under amount_txt (10)
    snprintf(q, sizeof q, "_root.status_bars.%s.us_bg._visible", b->bar); gfx_setvar(mv, q, gv_bool(0));
    b->pill[0] = b->pill[1] = 0;
    snprintf(q, sizeof q, "_root.status_bars.%s.amount_txt._y", b->bar); if (!gfx_getnum(mv, q, &b->ty)) b->ty = -0.5;
    snprintf(q, sizeof q, "_root.status_bars.%s.amount_txt._x", b->bar); if (!gfx_getnum(mv, q, &b->tx0)) b->tx0 = 3.2;
    b->txNow = b->tx0; b->maxDef = def_of(b->maxCls, b->maxType); b->regDef = def_of(b->regCls, b->regType);
    b->made = 1; b->done = 0; b->vis = 0; b->txt[0] = 0; b->ipath[0] = 0; b->dirty = 1; b->tinted = 0; b->noteW = 0;
}
// green / red text on the green HP bar (or the purple energy bar) is hard to read: a dark backing behind the coloured native number
// and behind our regeneration note (our clip us_bg at the timeline depth 9 of the bar - AS depth -16375: over the bar's fill at 4,
// under amount_txt at 10 - drawn with the AS drawing API, the number's part sized to its textWidth)
static void bar_rect(void *mv, const char *clip, double x0, double y0, double x1, double y1) {
    char p[128]; int n = snprintf(p, sizeof p, "%s", clip);
    double pts[5][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 }, { x0, y0 } };
    GfxVal f[2] = { gv_num(0), gv_num(60) }; snprintf(p + n, sizeof p - n, ".beginFill"); invoke_n(mv, p, f, 2);
    GfxVal m[2] = { gv_num(x0), gv_num(y0) }; snprintf(p + n, sizeof p - n, ".moveTo"); invoke_n(mv, p, m, 2);
    for (int i = 1; i < 5; i++) { GfxVal l[2] = { gv_num(pts[i][0]), gv_num(pts[i][1]) }; snprintf(p + n, sizeof p - n, ".lineTo"); invoke_n(mv, p, l, 2); }
    snprintf(p + n, sizeof p - n, ".endFill"); invoke_n(mv, p, NULL, 0);
}
static void bar_pill(void *mv, Bar *b, int num, int note) {
    char q[96], c[96]; double tw = 0;
    snprintf(q, sizeof q, "_root.status_bars.%s.amount_txt.textWidth", b->bar);
    if (num && (!gfx_getnum(mv, q, &tw) || tw <= 0)) num = 0;
    double x0 = num ? b->txNow + 70.9 - tw / 2 - 3 : 0, x1 = note ? 145 - b->noteW - 4 : 0;   // where each part starts (0 = none)
    if (x0 == b->pill[0] && x1 == b->pill[1]) return;
    b->pill[0] = x0; b->pill[1] = x1;
    snprintf(c, sizeof c, "_root.status_bars.%s.us_bg", b->bar);
    snprintf(q, sizeof q, "%s._visible", c); gfx_setvar(mv, q, gv_bool(num || note));
    snprintf(q, sizeof q, "%s.clear", c); invoke_n(mv, q, NULL, 0);
    double y0 = b->ty + 1.5, y1 = b->ty + 15.5;
    if (num) bar_rect(mv, c, x0, y0, b->txNow + 70.9 + tw / 2 + 3, y1);
    if (note) bar_rect(mv, c, x1, y0, 145, y1);
}
static void bar_native_x(void *mv, Bar *b, double x) {
    if (x == b->txNow) return;
    char q[96]; snprintf(q, sizeof q, "_root.status_bars.%s.amount_txt._x", b->bar); gfx_setvar(mv, q, gv_num(x)); b->txNow = x;
}
static void bar_set(void *mv, Bar *b, const Effs *fx, int on) {
    char p[160], q[96];
    if (!b->made) { if (!on) return; snprintf(q, sizeof q, "status_bars.%s.amount_txt", b->bar); if (!gfx_exists(mv, q)) return; bar_make(mv, b); }
    if (b->maxDef < 0 || b->regDef < 0) return;
    // the native number: max HP / max energy
    int dir = 0; char v[32];
    if (on && eff_on(&EFFDEF[b->maxDef], &fx->e[b->maxDef])) { float d = eff_text(b->maxDef, fx->e[b->maxDef].mul, fx->e[b->maxDef].add, v, sizeof v); dir = d > 0 ? 1 : d < 0 ? -1 : 0; }
    if (dir || b->tinted) {
        snprintf(q, sizeof q, "_root.status_bars.%s.amount_txt", b->bar);
        GfxVal a[2] = { gv_str(q), gv_num(dir > 0 ? RGB_GOOD : dir < 0 ? RGB_BAD : RGB_NATIVE) }; invoke_n(mv, "Receive_SetTextColor", a, 2);
        if (g_verbose && !b->logged) { double c = -1; snprintf(p, sizeof p, "%s.textColor", q); gfx_getnum(mv, p, &c); logmsg("v7: %s bar number coloured, textColor now %06X", b->bar, (unsigned)c); b->logged = 1; }
        b->tinted = dir != 0;
    }
    // regeneration: icon + the change at the bar's right end
    char html[240] = "";
    if (on && eff_on(&EFFDEF[b->regDef], &fx->e[b->regDef])) {
        float d = eff_text(b->regDef, fx->e[b->regDef].mul, fx->e[b->regDef].add, v, sizeof v);
        item_html(html, sizeof html, BAR_FS, v, NULL, d > 0 ? C_GOOD : d < 0 ? C_BAD : C_NEUTRAL);
    }
    if (strcmp(html, b->txt)) {
        snprintf(b->txt, sizeof b->txt, "%s", html); b->dirty = 1;
        if (*html) { char sh[240]; bar_path(b, p, sizeof p, "t.htmlText"); gfx_setvar(mv, p, gv_str(html));
                     html_shadow(html, sh, sizeof sh); bar_path(b, p, sizeof p, "s.htmlText"); gfx_setvar(mv, p, gv_str(sh)); }
    }
    int vis = *html != 0; double is = BAR_FS * 1.15;
    if (vis) {
        const Icon *ic = &g_eicon[b->icon];
        if (strcmp(b->ipath, ic->path)) {                    // a fresh child per texture, as the effect icons
            char f[48];
            if (b->gen) { snprintf(f, sizeof f, "i.h.m%d.removeMovieClip", b->gen); bar_path(b, p, sizeof p, f); invoke_n(mv, p, NULL, 0); }
            b->gen++; snprintf(f, sizeof f, "m%d", b->gen); GfxVal a[2] = { gv_str(f), gv_num(b->gen % 1000 + 1) };
            bar_path(b, p, sizeof p, "i.h.createEmptyMovieClip"); invoke_n(mv, p, a, 2);
            snprintf(f, sizeof f, "i.h.m%d", b->gen); bar_path(b, p, sizeof p, f);
            GfxVal l[2] = { gv_str(p), gv_str(ic->path) }; invoke_n(mv, "Receive_LoadMovie", l, 2);
            snprintf(b->ipath, sizeof b->ipath, "%s", ic->path); b->done = 0;
            bar_path(b, p, sizeof p, "i._visible"); gfx_setvar(mv, p, gv_bool(0));
        } else if (b->done <= 0) {
            char f[48]; snprintf(f, sizeof f, "i.h.m%d", b->gen); bar_path(b, p, sizeof p, f);
            double w = clip_w(mv, p), h = clip_h(mv, p);
            if (w > 0 && h > 0) {
                double sc = ic->scale * is / (w > h ? w : h);
                bar_path(b, p, sizeof p, "i._xscale"); gfx_setvar(mv, p, gv_num(sc)); bar_path(b, p, sizeof p, "i._yscale"); gfx_setvar(mv, p, gv_num(sc));
                bar_path(b, p, sizeof p, "i._visible"); gfx_setvar(mv, p, gv_bool(1)); b->done = 1; b->dirty = 1;
            } else b->done--;
        }
        if (b->dirty) {                                      // right aligned at the bar's right end
            bar_path(b, p, sizeof p, "t"); double w = clip_w(mv, p); if (w < 0) w = 3 * BAR_FS;
            double x = 142 - w, y = b->ty + 1.5, ov = is * (ic->scale - 100) / 200.0;
            bar_path(b, p, sizeof p, "t._x"); gfx_setvar(mv, p, gv_num(x)); bar_path(b, p, sizeof p, "t._y"); gfx_setvar(mv, p, gv_num(y));
            bar_path(b, p, sizeof p, "s._x"); gfx_setvar(mv, p, gv_num(x + 1)); bar_path(b, p, sizeof p, "s._y"); gfx_setvar(mv, p, gv_num(y + 1.2));
            bar_path(b, p, sizeof p, "i._x"); gfx_setvar(mv, p, gv_num(x - 1.5 - is - ov));
            bar_path(b, p, sizeof p, "i._y"); gfx_setvar(mv, p, gv_num(y + (BAR_FS * 1.3 + 4 - is) / 2 - ov));
            b->noteW = w + 1.5 + is; b->dirty = 0;
        }
        // the native number, centred in its field (-2..143.8 from its _x): moved left only as far as needed to end 4 before the
        // note, never further than its field's left edge at the bar's start
        double tw = 0; snprintf(q, sizeof q, "_root.status_bars.%s.amount_txt.textWidth", b->bar);
        if (gfx_getnum(mv, q, &tw) && tw > 0) {
            double right = b->tx0 + 70.9 + tw / 2, limit = 142 - b->noteW - 4, sh = right > limit ? limit - right : 0, lo = 1 - (70.9 - tw / 2);
            bar_native_x(mv, b, b->tx0 + (sh < lo ? lo : sh));
        }
    } else bar_native_x(mv, b, b->tx0);
    if (vis != b->vis) { b->vis = vis; bar_path(b, p, sizeof p, "_visible"); gfx_setvar(mv, p, gv_bool(vis)); }
    bar_pill(mv, b, b->tinted, vis && b->done > 0);
}
static void bars_update(void *mv, const Effs *fx, int on) { for (int i = 0; i < 2; i++) bar_set(mv, &g_bar[i], fx, on && fx); }
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
        for (int i = 0; i < 2; i++) { g_bar[i].made = 0; g_bar[i].tinted = 0; }   // v7: made again on first use
        g_barsOk = gfx_exists(mv, "status_bars.health.amount_txt") && gfx_exists(mv, "status_bars.energy.amount_txt"); g_nObs = 0;
        logmsg("v7: HP / energy bars %s", g_barsOk ? "found (their effects go into them)" : "NOT found (their effects stay icons)");
    }
    if (now - g_lastCalc < 250) return;                  // four times a second is plenty
    g_lastCalc = now;
    Sel s; memset(&s, 0, sizeof s);
    AT("selection");
    if (g_enabled && !g_dead && run_guarded(get_selection, &s, "the selection") && s.n == -1) return;   // the selection is busy: as it was
    if (!g_enabled || g_dead || s.n != 1) { show(mv, 0); bars_update(mv, NULL, 0); g_lastSq = NULL; return; }
    AT("stats"); Stats st; Job j = { s.squad, &st };
    if (!run_guarded(squad_stats, &j, "the squad's numbers") || !st.models) { show(mv, 0); bars_update(mv, NULL, 0); return; }
    // the effects (v3); v7: also with [Effects] Enabled=0 when they colour the HP / energy bars
    static Effs fx; int haveFx = 0;
    if (!g_dead && ((g_effOn && g_effMax > 0) || (g_buffColors && g_barFx))) {
        memset(&fx, 0, sizeof fx); fx.squad = s.squad; EJob ej = { s.squad, &fx };
        AT("effect values"); haveFx = run_guarded(eff_values, &ej, "the squad's effects");
    }
    AT("stats panel"); set_stats(mv, &st);
    show(mv, 1);
    AT("bars"); bars_update(mv, haveFx ? &fx : NULL, g_buffColors && g_barFx);
    if (g_verbose && s.squad != g_lastSq) {
        logmsg("sel %s: ranged %.2f (tooltip %.2f, engine %.2f), melee %.2f (tooltip %.2f, engine %.2f), skill %d (v1 %d), %s, range %.1f, speed %.2f, sight %.1f",
               p_sqName ? p_sqName(s.squad) : "?", st.rdps, st.rtip, st.reng, st.mdps, st.mtip, st.meng, st.skill, st.skillOld, st.armourName, st.range, st.speed, st.sight);
    }
    if (g_verbose) {                                     // v7: each number the effects move, and its base (when that changes)
        static char last[512]; static char *lastSq; char t[512]; int o = 0; t[0] = 0;
        static const char *NM[NITEMS] = { "ranged", "melee", "skill", "armour", "range", "speed", "sight" };
        float base[NITEMS] = { st.rdpsB, st.mdpsB, (float)st.skillB, 0, st.range0, st.speed0, st.sightB }, cur[NITEMS] = { st.rdps, st.mdps, (float)st.skill, 0, st.range, st.speed, st.sight };
        for (int k = 0; k < NITEMS && o < (int)sizeof t - 80; k++) if (st.dir[k])
            o += snprintf(t + o, sizeof t - o, " %s %.2f %s (without effects %.2f);", NM[k], cur[k], st.dir[k] > 0 ? "GREEN" : "RED", base[k]);
        if (strcmp(t, last) || s.squad != lastSq) { logmsg("v7 numbers %s:%s", p_sqName ? p_sqName(s.squad) : "?", *t ? t : " none changed by effects"); strcpy(last, t); lastSq = s.squad; }
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
    // v3: the effect icons; v7: without the ones a number / a bar shows, right of the native widgets of the strip
    if (g_effOn && g_effMax > 0 && haveFx) {
        int fold = g_buffColors ? FOLD_STAT | (g_barFx && g_barsOk ? FOLD_BAR : 0) : 0;
        AT("native widgets"); scan_native(mv);
        AT("effects panel"); Cell cells[MAXEFF]; int n = compose(&fx, cells, fold);
        set_effects(mv, cells, n);
        static char last[1024]; static char *lastSq; char now[1024]; int o = 0; now[0] = 0;
        for (int k = 0; k < n && o < (int)sizeof now - 80; k++) o += snprintf(now + o, sizeof now - o, "%s%s", k ? "; " : "", cells[k].plain);
        if (g_verbose && (strcmp(now, last) || s.squad != lastSq)) {
            char per[400]; int q = 0; per[0] = 0;                 // the engine's numbers, and how many of the models have each
            for (int k = 0; k < n && q < (int)sizeof per - 60; k++) q += snprintf(per + q, sizeof per - q, " %s x%.3f %+.2f sum %g on %d/%d;", mod_name(EFFDEF[cells[k].def].cls, EFFDEF[cells[k].def].type),
                                                               cells[k].mul, cells[k].add, cells[k].sum, fx.e[cells[k].def].nEnt, g_effNEnts);
            char in[400]; int r = 0; in[0] = 0;                   // v7: the ones in a number or a bar instead
            for (int i = 0; i < NEFFDEF && r < (int)sizeof in - 60; i++) if ((EFFDEF[i].fold & fold) && eff_on(&EFFDEF[i], &fx.e[i]))
                r += snprintf(in + r, sizeof in - r, " %s x%.3f %+.2f (%s);", mod_name(EFFDEF[i].cls, EFFDEF[i].type), fx.e[i].mul, fx.e[i].add, EFFDEF[i].fold == FOLD_BAR ? "bar" : "number");
            logmsg("effects %s: %s%s (%d shown) |%s |in numbers / bars:%s", p_sqName ? p_sqName(s.squad) : "?", n ? now : "none", n > g_effShown ? " - not all fit" : "", g_effShown, per, r ? in : " none");
            strcpy(last, now); lastSq = s.squad;
        }
    } else if (g_nCell) set_effects(mv, NULL, 0);
}
static void __thiscall hk_sp_upd(char *form, float dt) { g_frames++; AT("the game's panel update"); o_spUpd(form, dt); AT("ours"); frame(form); AT("idle"); }

static void *sim(const char *name) { HMODULE m = GetModuleHandleA("SimEngine.dll"); return m ? (void *)GetProcAddress(m, name) : NULL; }
static void *plat(const char *name) { HMODULE m = GetModuleHandleA("Platform.dll"); return m ? (void *)GetProcAddress(m, name) : NULL; }
static int code_is(DWORD at, const unsigned char *pro, int n) { return rd((void *)at, n) && !memcmp((void *)at, pro, n); }
// ---------- v6 / 0.6 (19.11.2026, SirItos: "show them in the replay, maybe when you hover a unit, if there is a tooltip") ----------
// A replay has no selection panel (its per-frame update never runs there); the replay HUD is the observer player list
// (ui\movies\hud\observer_player_list, form vtable 0x01115D04 made in 0xB3C780; slot 4 = 0xB3DA00 as every HUD form),
// filled by BindingObserverPlayerList (vtable 0x01119224, slot 6 = its update 0xB65020):
//   * a row of squad icons per player: player_list_mc.slot0<s>.multi_select.mini_portrait0<i> - the binding's widgets at
//     binding + 0x128 + (s * 0x36 + i) * 4 (UpdatePlayerSquads 0xB66FA0); each one's squad id is at widget + 0x3C4
//     (= squad + 0x40), its tooltip (type 2: the squad's name, "Level N") at +0x254 / +0x25C;
//   * player_list_mc.unit_info_mc above the right team: the SELECTED squad (UpdateSelectionAndPortrait 0xB66200, from the
//     binding's selection container +0x64) - name, "Fire Support Infantry (infantry armor)", level, health. In a replay a
//     click on an icon or a unit selects it as in a game (the same selection object the panel version reads).
// Hovering an icon shows the game's tooltip (TooltipPanel, ui\movies\message_boxes\tooltip, vtable 0x0110D8AC made in
// 0xAE2030, slot 4 OnUpdate 0xAE35F0): the movie manager's tooltip info *(0x01335088) + 0x2C has +0x208 the type and
// +0x204 the hovered widget; type 2 -> the movie's ShowUpgrade (upgrade_mc: name_txt, brief_txt) every frame, then
// g_tooltip_frame0 (a TooltipFrame) is sized to it and the panel (+0x4C/+0x50 the size read back from _level0.g_tooltip_*)
// places the movie by the cursor (0x682070 -> OnUpdateViewport 0xAE3D70, kept on screen with that size).
// What 0.6 adds, only in a replay (game type settings+0x118 has 0x800) and with [UnitStats] Replay=1:
//   * the hovered icon's squad: our clip us_tt in upgrade_mc under the game's two lines (same icons and numbers as the
//     panel, the effects below), the frame grown to hold it (m_client_* + UpdateFrame of the game's own TooltipFrame) and
//     the movie placed again with the new size (+0x4C/+0x50, 0x682070) - after the game's own OnUpdate, every frame;
//   * the selected squad: our clip us_box in unit_info_mc, right above that panel, on a dark backdrop.
// Still read-only: the squad is found by id among the players' squads (Player::GetSquads, as the binding does), nothing is
// written into the simulation (the tooltip panel's own size fields are UI state) - a replay keeps its sync.
#define OBS_VT_UPD    0x01115D14u      // observer player list form (vtable 0x01115D04), slot 4
#define OBS_UPD_FN    0x00B3DA00u
#define OBS_BIND_UPD  0x0111923Cu      // BindingObserverPlayerList (vtable 0x01119224), slot 6
#define OBS_BIND_FN   0x00B65020u
#define TT_VT_UPD     0x0110D8BCu      // TooltipPanel (vtable 0x0110D8AC), slot 4 OnUpdate(float)
#define TT_UPD_FN     0x00AE35F0u
#define FN_VIEWPORT   0x00682070u      // the form's viewport placement, ECX = form (its OnUpdate ends with it, via 0xAE40E0)
#define G_MOVIEMGR    0x01335088u      // +0x2C the tooltip info: +0x204 widget, +0x208 type
#define G_SETTINGS    0x01335A78u      // +0x118 game type, 0x800 = playback (also LiveReplay)
#define T_PLAYBACK    0x800
static int g_replay = 1, g_rPanel = 1, g_rTip = 1, g_rSize = 18, g_rTipSize = 15, g_rBack = 55, g_rOffX = 0, g_rOffY = 0, g_rWidth = 520;
static void load_ini_replay(void) {
    g_replay = ini_int(L"Replay", 1, 0, 1);
    g_rPanel = GetPrivateProfileIntW(L"Replay", L"Selected", 1, g_ini) != 0;
    g_rTip = GetPrivateProfileIntW(L"Replay", L"Tooltip", 1, g_ini) != 0;
    g_rSize = GetPrivateProfileIntW(L"Replay", L"FontSize", 18, g_ini); if (g_rSize < 8 || g_rSize > 40) g_rSize = 18;
    g_rTipSize = GetPrivateProfileIntW(L"Replay", L"TooltipFontSize", 15, g_ini); if (g_rTipSize < 8 || g_rTipSize > 40) g_rTipSize = 15;
    g_rBack = GetPrivateProfileIntW(L"Replay", L"Backdrop", 55, g_ini); if (g_rBack < 0 || g_rBack > 100) g_rBack = 55;
    g_rOffX = GetPrivateProfileIntW(L"Replay", L"OffsetX", 0, g_ini); g_rOffY = GetPrivateProfileIntW(L"Replay", L"OffsetY", 0, g_ini);
    g_rWidth = GetPrivateProfileIntW(L"Replay", L"Width", 520, g_ini); if (g_rWidth < 150 || g_rWidth > 2000) g_rWidth = 520;
}
static int is_replay(void) {
    char *st = rd((void *)G_SETTINGS, 4) ? *(char **)G_SETTINGS : NULL;
    return st && rd(st + 0x118, 4) && (*(DWORD *)(st + 0x118) & T_PLAYBACK);
}
static int gfx_getnum(void *mv, const char *path, double *out) {        // movie vtable +0x30 = GetVariable(GFxValue *, path)
    GfxVal r; memset(&r, 0, sizeof r);
    if (!(*(char (__thiscall **)(void *, GfxVal *, const char *))(*(char **)mv + 0x30))(mv, &r, path)) return 0;
    if (r.type == GV_NUMBER) { *out = r.v.num; return 1; }
    if (r.type == GV_BOOL) { *out = r.v.b; return 1; }
    return 0;
}
static double invoke_num(void *mv, const char *m) {
    GfxVal r; memset(&r, 0, sizeof r);
    return gfx_invoke(mv, m, &r, NULL, 0) && r.type == GV_NUMBER ? r.v.num : -1;
}

// a box of ours in a movie: two rows of icon + number (as the panel) and the effects under them, laid out from its top left
typedef struct {
    const char *parent, *name, *what; int depth, back;       // the clip <parent>.<name>; back: a dark backdrop under it
    char box[128]; void *mv; int fs, efs; double iconS, eIconS, lineH, w, h, bw, bh;
    char txt[NITEMS][300]; char ipath[NITEMS][200]; int idone[NITEMS], isc[NITEMS];
    char ctxt[MAXEFF][260]; char cpath[MAXEFF][200]; int cdone[MAXEFF], cgen[MAXEFF], csc[MAXEFF], ncell;
    int dirty, shown; char *sq; DWORD calc; double x, y;
} RBox;
static RBox g_rbSel = { "_root.player_list_mc.unit_info_mc", "us_box", "the observer panel (selected squad)", 16110, 1 };
static RBox g_rbTip = { "_root.upgrade_mc", "us_tt", "the tooltip (hovered squad icon)", 16110, 0 };
static void rb_path(const RBox *b, char *o, size_t n, const char *fmt, int k) { char t[64]; snprintf(t, sizeof t, fmt, k); snprintf(o, n, "%s.%s", b->box, t); }
static void rb_make(RBox *b, void *mv, int fs) {
    char p[200], nm[16];
    b->mv = mv; b->fs = fs; b->efs = fs * 5 / 6; b->iconS = 1.2 * fs; b->eIconS = 1.3 * b->efs; b->lineH = 1.3 * fs + 4;
    snprintf(b->box, sizeof b->box, "%s.%s", b->parent, b->name);
    snprintf(p, sizeof p, "%s.createEmptyMovieClip", b->parent);
    GfxVal a[2] = { gv_str(b->name), gv_num(b->depth) }; invoke_n(mv, p, a, 2);
    if (b->back) { rb_path(b, p, sizeof p, "createEmptyMovieClip", 0); GfxVal c[2] = { gv_str("bg"), gv_num(1) }; invoke_n(mv, p, c, 2); }
    for (int k = 0; k < NITEMS + MAXEFF; k++) {
        int e = k >= NITEMS, q = e ? k - NITEMS : k;
        for (int s = 0; s < 2; s++) {                     // s = 0 the black shadow, 1 the text
            snprintf(nm, sizeof nm, e ? (s ? "f%d" : "g%d") : (s ? "t%d" : "s%d"), q);
            GfxVal c[6] = { gv_str(nm), gv_num((e ? 200 : 10) + 2 * q + s), gv_num(0), gv_num(0), gv_num(10), gv_num(b->lineH) };
            rb_path(b, p, sizeof p, "createTextField", 0); invoke_n(mv, p, c, 6);
            char f[32]; snprintf(f, sizeof f, "%s.html", nm); rb_path(b, p, sizeof p, f, 0); gfx_setvar(mv, p, gv_bool(1));
            snprintf(f, sizeof f, "%s.selectable", nm); rb_path(b, p, sizeof p, f, 0); gfx_setvar(mv, p, gv_bool(0));
            snprintf(f, sizeof f, "%s.autoSize", nm); rb_path(b, p, sizeof p, f, 0); gfx_setvar(mv, p, gv_str("left"));
            if (!s) { snprintf(f, sizeof f, "%s._alpha", nm); rb_path(b, p, sizeof p, f, 0); gfx_setvar(mv, p, gv_num(75)); }
            snprintf(f, sizeof f, "%s._visible", nm); rb_path(b, p, sizeof p, f, 0); gfx_setvar(mv, p, gv_bool(0));
        }
        snprintf(nm, sizeof nm, e ? "e%d" : "i%d", q);
        GfxVal c[2] = { gv_str(nm), gv_num((e ? 300 : 100) + q) }; rb_path(b, p, sizeof p, "createEmptyMovieClip", 0); invoke_n(mv, p, c, 2);
        char f[48]; snprintf(f, sizeof f, "%s.createEmptyMovieClip", nm); rb_path(b, p, sizeof p, f, 0);
        GfxVal d[2] = { gv_str("h"), gv_num(1) }; invoke_n(mv, p, d, 2);
        if (!e) { snprintf(f, sizeof f, "%s.h.createEmptyMovieClip", nm); rb_path(b, p, sizeof p, f, 0); GfxVal im[2] = { gv_str("img"), gv_num(1) }; invoke_n(mv, p, im, 2); }
        snprintf(f, sizeof f, "%s._visible", nm); rb_path(b, p, sizeof p, f, 0); gfx_setvar(mv, p, gv_bool(0));
    }
    memset(b->txt, 0, sizeof b->txt); memset(b->ipath, 0, sizeof b->ipath); memset(b->idone, 0, sizeof b->idone);
    memset(b->ctxt, 0, sizeof b->ctxt); memset(b->cpath, 0, sizeof b->cpath); memset(b->cdone, 0, sizeof b->cdone);
    for (int k = 0; k < NITEMS; k++) b->isc[k] = 100;
    for (int k = 0; k < MAXEFF; k++) b->csc[k] = 100;
    b->ncell = 0; b->dirty = 1; b->shown = -1; b->sq = NULL; b->calc = 0; b->w = b->h = b->bw = b->bh = 0; b->x = b->y = -1e9;
    snprintf(p, sizeof p, "%s.%s._visible", b->parent, b->name); gfx_setvar(mv, p, gv_bool(0)); b->shown = 0;
    rb_path(b, p, sizeof p, "t0", 0);
    logmsg("v6: clip %s in %s (movie %p): %s (font %d)", b->box, b->what, mv, gfx_exists(mv, p) ? "ok" : "NOT THERE", fs);
}
static void rb_show(RBox *b, int vis) {
    if (!b->mv || vis == b->shown) return;
    b->shown = vis; char p[160]; snprintf(p, sizeof p, "%s._visible", b->box); gfx_setvar(b->mv, p, gv_bool(vis));
}
static void rb_settext(RBox *b, int sh, int k, const char *html, int eff) {     // the text, and its black copy as the shadow
    char p[200], s[300], f[24];
    snprintf(f, sizeof f, eff ? "f%d.htmlText" : "t%d.htmlText", k); rb_path(b, p, sizeof p, f, 0); gfx_setvar(b->mv, p, gv_str(html));
    html_shadow(html, s, sizeof s); (void)sh;
    snprintf(f, sizeof f, eff ? "g%d.htmlText" : "s%d.htmlText", k); rb_path(b, p, sizeof p, f, 0); gfx_setvar(b->mv, p, gv_str(s));
}
// an icon into <box>.i<k>.h.img (items) or a fresh <box>.e<k>.h.m<gen> (effects), scaled once its size is known
static void rb_icon(RBox *b, int eff, int k, const Icon *ic) {
    char p[200], f[48]; char *path = eff ? b->cpath[k] : b->ipath[k]; int *done = eff ? &b->cdone[k] : &b->idone[k];
    double size = eff ? b->eIconS : b->iconS;
    if (strcmp(path, ic->path)) {
        snprintf(path, 200, "%s", ic->path); *done = 0;
        if (eff) {
            if (b->cgen[k]) { snprintf(f, sizeof f, "e%d.h.m%d.removeMovieClip", k, b->cgen[k]); rb_path(b, p, sizeof p, f, 0); invoke_n(b->mv, p, NULL, 0); }
            b->cgen[k]++; snprintf(f, sizeof f, "e%d.h.createEmptyMovieClip", k); rb_path(b, p, sizeof p, f, 0);
            char nm[16]; snprintf(nm, sizeof nm, "m%d", b->cgen[k]); GfxVal a[2] = { gv_str(nm), gv_num(b->cgen[k] % 1000 + 1) }; invoke_n(b->mv, p, a, 2);
            snprintf(f, sizeof f, "e%d.h.m%d", k, b->cgen[k]); b->csc[k] = ic->scale;
        } else { snprintf(f, sizeof f, "i%d.h.img", k); b->isc[k] = ic->scale; }
        rb_path(b, p, sizeof p, f, 0);
        GfxVal a[2] = { gv_str(p), gv_str(ic->path) }; invoke_n(b->mv, "Receive_LoadMovie", a, 2);
        snprintf(f, sizeof f, eff ? "e%d._visible" : "i%d._visible", k); rb_path(b, p, sizeof p, f, 0); gfx_setvar(b->mv, p, gv_bool(0));
        b->dirty = 1; return;
    }
    if (*done > 0) return;
    if (eff) snprintf(f, sizeof f, "e%d.h.m%d", k, b->cgen[k]); else snprintf(f, sizeof f, "i%d.h.img", k);
    rb_path(b, p, sizeof p, f, 0);
    double w = clip_w(b->mv, p), h = clip_h(b->mv, p);
    if (w <= 0 || h <= 0) { if (--*done == -12 && g_verbose) logmsg("v6: icon %s not there after 12 checks", ic->path); return; }
    double sc = ic->scale * size / (w > h ? w : h);
    snprintf(f, sizeof f, eff ? "e%d._xscale" : "i%d._xscale", k); rb_path(b, p, sizeof p, f, 0); gfx_setvar(b->mv, p, gv_num(sc));
    snprintf(f, sizeof f, eff ? "e%d._yscale" : "i%d._yscale", k); rb_path(b, p, sizeof p, f, 0); gfx_setvar(b->mv, p, gv_num(sc));
    *done = 1; b->dirty = 1;
}
static void rb_xy(RBox *b, const char *fmt, int k, double x, double y) {
    char p[200], f[32];
    snprintf(f, sizeof f, fmt, k); strcat(f, "._x"); rb_path(b, p, sizeof p, f, 0); gfx_setvar(b->mv, p, gv_num(x));
    snprintf(f, sizeof f, fmt, k); strcat(f, "._y"); rb_path(b, p, sizeof p, f, 0); gfx_setvar(b->mv, p, gv_num(y));
}
static void rb_vis(RBox *b, const char *fmt, int k, int on) { char p[200], f[32]; snprintf(f, sizeof f, fmt, k); strcat(f, "._visible"); rb_path(b, p, sizeof p, f, 0); gfx_setvar(b->mv, p, gv_bool(on)); }
static void rb_layout(RBox *b, double maxW) {
    double pad = b->back ? 0.35 * b->fs : 0, gapI = 0.2 * b->fs, gapItem = 0.7 * b->fs, w = 0, y = pad;
    char p[200];
    for (int row = 0; row < 2; row++) {
        double x = pad;
        for (int k = 0; k < NITEMS; k++) {
            if (ITEM_ROW[k] != row) continue;
            double ov = b->iconS * (b->isc[k] - 100) / 200.0;
            rb_xy(b, "i%d", k, x - ov, y + (b->lineH - b->iconS) / 2 + 1 - ov); rb_vis(b, "i%d", k, b->idone[k] > 0);
            x += b->iconS + gapI;
            rb_path(b, p, sizeof p, "t%d", k); double tw = clip_w(b->mv, p); if (tw < 0) tw = 3 * b->fs;
            rb_xy(b, "t%d", k, x, y); rb_xy(b, "s%d", k, x + 1.2, y + 1.5); rb_vis(b, "t%d", k, 1); rb_vis(b, "s%d", k, 1);
            x += tw + gapItem;
        }
        if (x - gapItem > w) w = x - gapItem;
        y += b->lineH;
    }
    double limit = maxW > w ? maxW : w, elh = 1.3 * b->efs + 4, x = pad; int any = 0;
    for (int k = 0; k < MAXEFF; k++) {
        if (k >= b->ncell) { rb_vis(b, "e%d", k, 0); rb_vis(b, "f%d", k, 0); rb_vis(b, "g%d", k, 0); continue; }
        rb_path(b, p, sizeof p, "f%d", k); double tw = clip_w(b->mv, p); if (tw < 0) tw = 3 * b->efs;
        double cw = b->eIconS + gapI + tw;
        if (any && x + cw > limit) { x = pad; y += elh; }
        if (!any) any = 1;
        double ov = b->eIconS * (b->csc[k] - 100) / 200.0;
        rb_xy(b, "e%d", k, x - ov, y + (elh - b->eIconS) / 2 + 1 - ov); rb_vis(b, "e%d", k, b->cdone[k] > 0);
        rb_xy(b, "f%d", k, x + b->eIconS + gapI, y); rb_xy(b, "g%d", k, x + b->eIconS + gapI + 1.1, y + 1.3);
        rb_vis(b, "f%d", k, 1); rb_vis(b, "g%d", k, 1);
        x += cw + 0.6 * b->efs; if (x - 0.6 * b->efs > w) w = x - 0.6 * b->efs;
    }
    if (any) y += elh;
    b->w = w + pad; b->h = y + pad;
    if (b->back && (b->bw != b->w || b->bh != b->h)) {          // the backdrop: a dark rectangle drawn in bg (the AS drawing API)
        b->bw = b->w; b->bh = b->h;
        rb_path(b, p, sizeof p, "bg.clear", 0); invoke_n(b->mv, p, NULL, 0);
        GfxVal f[2] = { gv_num(0), gv_num(g_rBack) }; rb_path(b, p, sizeof p, "bg.beginFill", 0); invoke_n(b->mv, p, f, 2);
        double pts[5][2] = { { 0, 0 }, { b->w, 0 }, { b->w, b->h }, { 0, b->h }, { 0, 0 } };
        GfxVal m[2] = { gv_num(0), gv_num(0) }; rb_path(b, p, sizeof p, "bg.moveTo", 0); invoke_n(b->mv, p, m, 2);
        for (int i = 1; i < 5; i++) { GfxVal l[2] = { gv_num(pts[i][0]), gv_num(pts[i][1]) }; rb_path(b, p, sizeof p, "bg.lineTo", 0); invoke_n(b->mv, p, l, 2); }
        rb_path(b, p, sizeof p, "bg.endFill", 0); invoke_n(b->mv, p, NULL, 0);
    }
    b->dirty = 0;
}
static void rb_set(RBox *b, const Stats *s, Cell *cells, int n) {
    char v[NITEMS][64], sm[NITEMS][48], h[300];
    stat_strings(s, v, sm);
    for (int k = 0; k < NITEMS; k++) {
        item_html(h, sizeof h, b->fs, v[k], sm[k], dir_col(s->dir[k]));
        if (strcmp(h, b->txt[k])) { strcpy(b->txt[k], h); rb_settext(b, 1, k, h, 0); b->dirty = 1; }
        rb_icon(b, 0, k, item_icon(s, k));
    }
    if (n > MAXEFF) n = MAXEFF;
    for (int k = 0; k < n; k++) {
        cell_text_fs(&cells[k], b->efs);
        if (strcmp(cells[k].html, b->ctxt[k])) { snprintf(b->ctxt[k], sizeof b->ctxt[k], "%s", cells[k].html); rb_settext(b, 1, k, cells[k].html, 1); b->dirty = 1; }
        rb_icon(b, 1, k, &g_eicon[EFFDEF[cells[k].def].icon]);
    }
    for (int k = n; k < MAXEFF; k++) b->ctxt[k][0] = 0;
    if (n != b->ncell) { b->ncell = n; b->dirty = 1; }
}
// the numbers of a squad into a box (four times a second, or at once for a new squad); 0 = nothing to show
static int rb_fill(RBox *b, char *sq, DWORD now) {
    if (sq == b->sq && now - b->calc < 250) return 1;
    b->calc = now;
    static Stats st; Job j = { sq, &st }; memset(&st, 0, sizeof st);
    if (!run_guarded(squad_stats, &j, "the squad's numbers (replay)") || !st.models) return 0;
    static Effs fx; Cell cells[MAXEFF]; int n = 0;
    if (g_effOn && g_effMax > 0) {
        memset(&fx, 0, sizeof fx); fx.squad = sq; EJob ej = { sq, &fx }; g_detailN = 0; g_detail[0] = 0;
        if (run_guarded(eff_values, &ej, "the squad's effects (replay)")) n = compose(&fx, cells, g_buffColors ? FOLD_STAT : 0);   // v7: no bar there
    }
    rb_set(b, &st, cells, n);
    if (g_verbose && sq != b->sq)
        logmsg("v6 %s: %s - ranged %.2f, melee %.2f, skill %d, %s, range %.1f, speed %.2f, sight %.1f, %d effects", b->what,
               p_sqName ? p_sqName(sq) : "?", st.rdps, st.mdps, st.skill, st.armourShort, st.range, st.speed, st.sight, n);
    b->sq = sq;
    return 1;
}

// the squad behind a squad icon of the observer panel (the binding's widget array), found among the players' squads by id
typedef unsigned (__thiscall *Cnt_t)(void *pm);
typedef void *(__thiscall *At_t)(void *pm, unsigned i);
static Cnt_t p_plCount; static At_t p_plAt; static P0_t p_plSquads, p_sqBegin, p_sqEnd;
static volatile char *g_obsBind; static volatile DWORD g_obsBindT;
typedef void (__thiscall *BindUpd_t)(char *binding, void *ctx);
static BindUpd_t o_bindUpd;
static void __thiscall hk_bind_upd(char *binding, void *ctx) { o_bindUpd(binding, ctx); g_obsBind = binding; g_obsBindT = GetTickCount(); }
typedef struct { char *widget, *squad; int slot, icon; DWORD id; } WJob;
static void widget_squad(void *arg) {
    WJob *j = arg; j->squad = NULL; j->slot = -1;
    char *b = (char *)g_obsBind; if (!b || GetTickCount() - g_obsBindT > 2000 || !rd(b + 0x128, 6 * 0xD8)) return;
    for (int s = 0; s < 6 && j->slot < 0; s++) for (int i = 0; i < 7; i++)
        if (*(char **)(b + 0x128 + (s * 0x36 + i) * 4) == j->widget) { j->slot = s; j->icon = i; break; }
    if (j->slot < 0 || !rd(j->widget + 0x3C4, 4)) return;
    DWORD id = *(DWORD *)(j->widget + 0x3C4); j->id = id; if (!id) return;
    char *w = *(char **)G_WORLD; if (!rd(w + 0x194, 4)) return;
    void *pm = w + 0x194; unsigned n = p_plCount(pm); if (n > 16) return;
    for (unsigned i = 0; i < n; i++) {
        char *pl = p_plAt(pm, i); if (!rd(pl, 4)) continue;
        char *g = p_plSquads(pl); if (!rd(g, 4) || !rd(*(char **)g, 8)) continue;
        char *c = g + ((int *)*(char **)g)[1];                       // the container is its virtual base (as 0xB66FA0)
        char **sb = p_sqBegin(c), **se = p_sqEnd(c);
        if (!sb || se < sb || se - sb > 512 || !rd(sb, (se - sb) * 4)) continue;
        for (char **q = sb; q < se; q++) if (*q && rd(*q + 0x40, 4) && *(DWORD *)(*q + 0x40) == id) { j->squad = *q; return; }
    }
}

// the selected squad: our box right above unit_info_mc
typedef void (__thiscall *Upd_t2)(char *form, float dt);
static Upd_t2 o_obsUpd, o_ttUpd;
static DWORD g_obsCheck;
static void frame_obs(char *form) {
    if (!g_replay || !g_rPanel || g_dead || !g_enabled) { if (g_rbSel.mv && g_rbSel.mv == gfx_movie_of_form(form)) rb_show(&g_rbSel, 0); return; }
    if (!is_replay()) return;
    void *mv = gfx_movie_of_form(form); if (!mv) return;
    DWORD now = GetTickCount();
    char p[160]; snprintf(p, sizeof p, "player_list_mc.unit_info_mc.%s.t0", g_rbSel.name);
    if (mv != g_rbSel.mv || g_rbSel.fs != g_rSize || (now - g_obsCheck > 1000 && (g_obsCheck = now, !gfx_exists(mv, p)))) {
        if (!gfx_exists(mv, "player_list_mc.unit_info_mc")) return;
        g_obsCheck = now; rb_make(&g_rbSel, mv, g_rSize);
    }
    Sel s; memset(&s, 0, sizeof s);
    if (!run_guarded(get_selection, &s, "the selection (replay)")) return;
    if (s.n == -1) return;
    if (s.n != 1) { rb_show(&g_rbSel, 0); g_rbSel.sq = NULL; return; }
    if (!rb_fill(&g_rbSel, s.squad, now)) { rb_show(&g_rbSel, 0); return; }
    if (g_rbSel.dirty) rb_layout(&g_rbSel, g_rWidth);
    // unit_info_mc's own frame is 560 x 144 from 0,0. Above it when the movie has room there; with 3 players a side the panel
    // is at the top of the movie's stage (player_list_mc frame 4, unit_info_mc at y -234 under player_list_mc at 234.65) and
    // the movie draws nothing outside its stage - then left of it, top aligned (the stage is empty there, over the left team)
    static DWORD posT; static double top = 1e9;
    if (now - posT > 500) {
        posT = now; double py = 0, uy = 0;
        if (gfx_getnum(mv, "_root.player_list_mc._y", &py) && gfx_getnum(mv, "_root.player_list_mc.unit_info_mc._y", &uy)) top = py + uy;
    }
    double x = g_rOffX, y = -(g_rbSel.h + 6) + g_rOffY;
    if (top + y < 0) { x = -(g_rbSel.w + 6) + g_rOffX; y = g_rOffY; }
    if (x != g_rbSel.x || y != g_rbSel.y) {
        g_rbSel.x = x; g_rbSel.y = y;
        snprintf(p, sizeof p, "%s._x", g_rbSel.box); gfx_setvar(mv, p, gv_num(x));
        snprintf(p, sizeof p, "%s._y", g_rbSel.box); gfx_setvar(mv, p, gv_num(y));
    }
    rb_show(&g_rbSel, 1);
    static char *dbgSq;
    if (g_verbose && dbgSq != s.squad) {                             // where the box is (the panel moves with the number of players)
        dbgSq = s.squad; double v[8] = { 0 };
        static const char *K[8] = { "._visible", "._x", "._y", "._width", "._height", "", "", "" };
        for (int i = 0; i < 5; i++) { snprintf(p, sizeof p, "%s%s", g_rbSel.box, K[i]); gfx_getnum(mv, p, &v[i]); }
        gfx_getnum(mv, "_root.player_list_mc.unit_info_mc._y", &v[5]); gfx_getnum(mv, "_root.player_list_mc.unit_info_mc._visible", &v[6]);
        gfx_getnum(mv, "_root.player_list_mc._currentframe", &v[7]);
        logmsg("v6 box: visible %g at %g,%g size %gx%g; unit_info_mc y %g visible %g; player_list_mc frame %g", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
    }
}
static void __thiscall hk_obs_upd(char *form, float dt) { g_frames++; o_obsUpd(form, dt); AT("ours (replay panel)"); frame_obs(form); AT("idle"); }

// the hovered squad icon: our box in the game's tooltip, under its two lines, the frame grown to hold it
typedef void (__thiscall *Vp_t)(char *form);
static void frame_tt(char *form) {
    void *mv = gfx_movie_of_form(form); if (!mv) return;
    if (!g_replay || !g_rTip || g_dead || !g_enabled) { if (g_rbTip.mv == mv) rb_show(&g_rbTip, 0); return; }
    char *mm = rd((void *)G_MOVIEMGR, 4) ? *(char **)G_MOVIEMGR : NULL;
    char *info = mm && rd(mm + 0x2C, 4) ? *(char **)(mm + 0x2C) : NULL;
    WJob j; memset(&j, 0, sizeof j);
    if (info && rd(info + 0x204, 8) && *(int *)(info + 0x208) == 2 && (j.widget = *(char **)(info + 0x204)) && g_obsBind && is_replay())
        if (!run_guarded(widget_squad, &j, "the hovered squad icon")) j.squad = NULL;
    double up = 0;
    if (j.squad && (!gfx_getnum(mv, "_root.upgrade_mc._visible", &up) || !up)) j.squad = NULL;   // the game shows no tooltip now
    if (!j.squad) { if (g_rbTip.mv == mv) rb_show(&g_rbTip, 0); g_rbTip.sq = NULL; return; }
    DWORD now = GetTickCount();
    char p[160]; snprintf(p, sizeof p, "upgrade_mc.%s.t0", g_rbTip.name);
    if (mv != g_rbTip.mv || g_rbTip.fs != g_rTipSize || !gfx_exists(mv, p)) {
        if (!gfx_exists(mv, "upgrade_mc.name_txt")) return;
        rb_make(&g_rbTip, mv, g_rTipSize);
    }
    char *was = g_rbTip.sq;
    if (!rb_fill(&g_rbTip, j.squad, now)) { rb_show(&g_rbTip, 0); return; }
    if (g_verbose && was != j.squad) logmsg("v6 tooltip: icon %d of slot %d, squad id %lu", j.icon, j.slot, j.id);
    // the game's lines as UpdateUpgrade laid them out this frame: name, brief, description one under the other
    static const char *LINES[3] = { "_root.upgrade_mc.name_txt", "_root.upgrade_mc.brief_txt", "_root.upgrade_mc.description_txt" };
    double y0 = 0, w0 = 0;
    for (int i = 0; i < 3; i++) {
        double vis = 0; snprintf(p, sizeof p, "%s._visible", LINES[i]);
        if (!gfx_getnum(mv, p, &vis) || !vis) continue;
        double h = clip_h(mv, LINES[i]), w = clip_w(mv, LINES[i]);
        if (h > 0) y0 += h; if (w > w0) w0 = w;
    }
    if (g_rbTip.dirty) rb_layout(&g_rbTip, w0 > 200 ? w0 : 200);
    double x = 2, y = y0 + 4;
    if (x != g_rbTip.x || y != g_rbTip.y) {
        g_rbTip.x = x; g_rbTip.y = y;
        snprintf(p, sizeof p, "%s._x", g_rbTip.box); gfx_setvar(mv, p, gv_num(x));
        snprintf(p, sizeof p, "%s._y", g_rbTip.box); gfx_setvar(mv, p, gv_num(y));
    }
    rb_show(&g_rbTip, 1);
    double W = g_rbTip.w + 4 > w0 ? g_rbTip.w + 4 : w0, H = y + g_rbTip.h;
    gfx_setvar(mv, "_root.g_tooltip_frame0.m_client_width", gv_num(floor(W)));
    gfx_setvar(mv, "_root.g_tooltip_frame0.m_client_height", gv_num(floor(H)));
    invoke_n(mv, "_root.g_tooltip_frame0.UpdateFrame", NULL, 0);
    double fw = invoke_num(mv, "_root.g_tooltip_frame0.GetWidth"), fh = invoke_num(mv, "_root.g_tooltip_frame0.GetHeight");
    if (fw <= 0 || fh <= 0) return;
    gfx_setvar(mv, "_level0.g_tooltip_width", gv_num(fw)); gfx_setvar(mv, "_level0.g_tooltip_height", gv_num(fh));
    if (rd(form + 0x4C, 8)) {                                  // the panel's size of the tooltip, then placed again by it
        *(int *)(form + 0x4C) = (int)fw; *(int *)(form + 0x50) = (int)fh;
        ((Vp_t)FN_VIEWPORT)(form);
    }
}
static void __thiscall hk_tt_upd(char *form, float dt) { o_ttUpd(form, dt); AT("ours (replay tooltip)"); frame_tt(form); AT("idle"); }
static void hook_replay(void) {
    p_plCount  = (Cnt_t)sim("?GetPlayerCount@PlayerManager@@QBEIXZ");
    p_plAt     = (At_t)sim("?GetPlayerAt@PlayerManager@@QAEPAVPlayer@@I@Z");
    p_plSquads = (P0_t)sim("?GetSquads@Player@@QBEABVSquadGroupTemp@@XZ");
    p_sqBegin  = (P0_t)sim("?begin@?$ObservableContainer@PBVSquad@@$00@ast@@QAEPAPBVSquad@@XZ");
    p_sqEnd    = (P0_t)sim("?end@?$ObservableContainer@PBVSquad@@$00@ast@@QAEPAPBVSquad@@XZ");
    int sq = p_plCount && p_plAt && p_plSquads && p_sqBegin && p_sqEnd;
    int a = gfx_hook_slot(OBS_VT_UPD, OBS_UPD_FN, (void *)hk_obs_upd, (void **)&o_obsUpd);
    int b = sq && gfx_hook_slot(OBS_BIND_UPD, OBS_BIND_FN, (void *)hk_bind_upd, (void **)&o_bindUpd);
    int c = b && code_is(FN_VIEWPORT, (const unsigned char *)"\x6A\xFF\x68", 3) && gfx_hook_slot(TT_VT_UPD, TT_UPD_FN, (void *)hk_tt_upd, (void **)&o_ttUpd);
    logmsg("v6 replays (Replay=%d): selected squad over the observer panel %s, squad icons %s, tooltip %s", g_replay,
           a ? "hooked" : "NOT as in 3.19", b ? "hooked" : sq ? "NOT as in 3.19" : "(SimEngine exports missing)", c ? "hooked" : "NOT hooked");
}

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
    hook_replay();
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
    logmsg("UnitStats 0.8 (DPS as the attribute editor counts it%s; selected squad's DPS / melee skill / armour / range / speed / sight and its active effects with the game's icons left of Retreat, in the game's own selection panel movie; %s; in a replay: the hovered squad icon's tooltip and the selected squad over the observer panel)", g_showEngine ? "; ShowEngineCycle=1: the engine's real cycle shown instead" : "", g_buffColors ? (g_barFx ? "BuffColors=1: effects colour the numbers (green / red), HP / energy ones in the bars under the portrait" : "BuffColors=1: effects colour the numbers (green / red), BarEffects=0") : "BuffColors=0: every effect an icon, numbers white");
    if (!g_enabled) { logmsg("disabled in UnitStats.ini"); return TRUE; }
    s2t_install();
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
