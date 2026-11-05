# Unit stats beside Retreat — how to do it (experiment, archived)

Request: grantovich, Elite Mod - Dev, 04.11.2026 — "dynamic DPS figures and other unit stats to the left of the retreat
button in that unused space: ranged DPS, melee DPS, melee skill, range, speed, sight radius".

Status: **experiment, archive branch `archive/unitstats-experiment` only.** Not in `addons.json`, not in the store, not
for players. `make build/UnitStats.dll` (not part of `make all`). Code: `unitstats/unitstats.c`.
The result of the stand test (screenshots, numbers, what did not work) is at the end of this file.

## The rule it follows

Only the game's own interface (docs/UI_RULES.md): two text fields of ours inside the game's selection panel movie,
written through the movie's own Scaleform API (common/gfxui.h). No Direct3D, no overlay, no new device.
Only reads: nothing is written into the simulation and no order is sent — no desync possible, only this player sees it.

## Where it is shown

* Movie: `ui\movies\hud\selection_panel` (in the game data: `ui/movies/hud/selection_panel.gfx`; stage 1180 x 700).
* Its form: made in `0xB39850` (`push "ui\movies\hud\selection_panel"; call 0x681B50` = the base form ctor), vtable
  `0x01115490`. form + 4 = the GFxMovieView (as every HUD form).
* Per-frame hook: **slot 4 of that vtable, `0x011154A0`, holds `0xB3DA00`** (`__thiscall(form, float dt)`, `ret 4`) — the
  panel's own per-frame update. Chain it with `gfx_hook_slot()` (the game's first, then ours), as APM does for the
  minimap form (0x011160E8 / 0xB3F6E0) and MatchTimer for the Menu button (0x0110C55C).
* The Retreat button is `core_btn03` inside the root clip **`core_actions`** (root depth 122, at 1068.4,542.75, scale
  0.7187). In `core_actions` local space: the five order buttons `core_btn05, 04, 00, 01, 02` are a row at y 81.6
  (x −135.2 … 79.1, scale 0.83, 64 px buttons), Retreat `core_btn03` is at **70.9,146.9** (scale 0.95, ~61 x 61). The
  empty space is left of Retreat, under the order row.
* So the fields are made with `Invoke("_root.core_actions.createTextField", name, depth, x, y, w, h)` in
  `core_actions` coordinates — they move and scale with the panel at any resolution / UI scale. Right-aligned text whose
  right edge is 6 units left of Retreat; two lines; `$NormalFont`; a black copy 1.2/1.5 lower right as a shadow (as the
  game's own count digits have). `UnitStats.ini`: FontSize (17), OffsetX / OffsetY to tune, re-read while running.
* The movie shows nothing outside its stage, and children of `core_actions` are drawn with it; text fields take no
  clicks unless `selectable` — set it to false so the order buttons and tooltips keep working.

How the sprite tree was read: `ffdec -swf2xml selection_panel.gfx` (FFDec at /mnt/ssd/opt/ffdec on prod_gpu), then the
PlaceObject tags of the root and of `core_actions` (sprite 302) with their matrices (twips / 20).

## Which squad

Exactly what the game's own SCAR `Misc_GetSelectedSquads` (`0xAFAD10`) does:

* selection object = `*(*(*(0x01335720) + 8) + 0x20)`;
  entity ids: `begin = *(sel + 0x35C)`, `end = *(sel + 0x360)` (`DWORD` ids), guarded by its mutex at `sel + 0x39D`
  (`Plat::RecursiveMutex::Enter/Leave`, Platform.dll exports) — copy the ids under the mutex, nothing else;
* id → entity: `em = *(*(0x013356A4) + 0xF8)`, `entity = em->vtbl[4](id)` (`__thiscall`);
* keep entities with `(*(entity + 0x50) & 0x18) == 0x18`; `squad = *(entity + 0x38)`;
* one squad selected → numbers; none or several → the fields hidden. Clicking an enemy squad selects it too, so the
  numbers show for enemies as well (the panel shows them anyway).

## Where the numbers come from (DOW2.exe 3.19)

Entities of the squad: `Squad::GetAllEntities` + `EntityGroupTemp::begin/end` (SimEngine.dll exports).
Extensions: `entity + 0x3C` = EntityController, `EntityController::QI(ctrl, id)` (export). Ids seen in the game's SCAR
functions: 0 health, 4 sight, 6 combat, 7 production, 0xC upgrades, 0xE infiltration, 0x13 crush, 0x29 attribution …
(`Entity_ExtensionName(id)` = ext vtable +0x14 gives the class name for any id, 0..0x2C).

| Number | Source | Dynamic? |
|---|---|---|
| ranged DPS | sum over the squad's entities of every ranged weapon's DPS (all guns of a vehicle fire) | yes — damage, cooldown, reload through the game's modifier function |
| melee DPS | sum over entities of each entity's best melee weapon DPS (one melee attack at a time) | yes, same |
| melee skill | entity blueprint, `melee_ext` bag → `MeleeExtInfo + 0x04` (`weapon_skill`, int), max over entities | no (blueprint) |
| range | max over ranged weapons of `Weapon::GetMaxRange` (weapon vtable slot 7, `+0x1C`, `__thiscall`, float in ST0) | yes (modifier 0x17) |
| speed | entity blueprint, `moving_ext` bag → `MovingExtInfo + 0x5C` (`speed_max`), min over entities | no (blueprint) |
| sight | `SightExt` (QI 4, vtable 0x010FA130) `+0x28` — what SCAR `Entity_GetSightRadius` (0x8C82B0) returns | yes |

Weapons: `CombatExt` = QI 6 (vtable `0x010DE6F0`): `+0x50` hardpoint pointer array, `+0x54` count; hardpoint `+0x50` =
its current `Weapon` (ShootingWeapon vtable 0x01105AC4 / MeleeWeapon 0x010FC2EC). Weapon: `+0x54` = `WeaponBag` (the
weapon blueprint as loaded, vtable `0x010D68B4`), `+0x4C` its PropertyBagGroup (`PropertyBagGroup::GetFilename` for
the log), `+0xAC` the hardpoint again (`+0x14` = the owning entity — pass this one to the modifier function).

WeaponBag fields (its loader is vtable slot 2, `0x8203F0`; times are sim ticks, 10 per second):
`+0x64` damage_hp, `+0x68` damage_courage, `+0xC0/+0xC4` range min/max, `+0xC8` is_melee, `+0xD4` moving accuracy
multiplier, `+0xE0` armour_penetration, `+0xF0` cooldown, `+0xF4` post_idle_delay, `+0xF8` setup, `+0x104` teardown,
`+0x10C` wind_up, `+0x110` wind_down, `+0x11C/+0x120` reload duration min/max, `+0x124/+0x128` reload frequency
(bursts until reload) min/max, `+0x170` burst on, `+0x174/+0x178` burst duration min/max, `+0x17C` burst rate of fire.

Blueprint bags: PropertyBagGroup `+0x0C` = array of `{key, bag}` pairs, `+0x10` = count (`+0x4C` type, `+0x55` loaded).
The key is the address of a .data slot holding the bag's name: `moving_ext 0x01301B60`, `melee_ext 0x01301B94`,
`sight_ext 0x01301BA4`, `combat_ext 0x01301B0C`, `weapon_bag 0x01301AB0`, `health_ext 0x01301B38` … (the slots
0x01301A84..0x01301BF8 hold the names; `0x436CDE` is the game's lookup for weapon_bag). Check the bag's vtable before
reading it (MovingExtInfo 0x010F1404, MeleeExtInfo 0x010F8F7C, SightExtInfo 0x010FA8F8, WeaponBag 0x010D68B4).

## DPS formula (the game's own)

The game has it: Lua `Util_GetWeaponDPS(weaponPBG)` = `0x934D70`. Melee: `damage / (wind_down + wind_up + cooldown) * 10`.
Ranged: `0x81FC40` (usercall, EAX = WeaponBag, result in xmm0), constants 0.5 (0x0105BB74), 1.0, 10 (0x0107D78C):

```
freq  = (reload_freq_min + reload_freq_max) / 2           # bursts until a reload
reload = (reload_min + reload_max) / 2
if burst:
    burst_t = (burst_min + burst_max) / 2;  dmg *= rate_of_fire (if > 0)
    if freq > 0: time = reload + (cooldown + burst_t) * freq; total = freq * dmg * burst_t
    else:        time = cooldown + burst_t;                   total = dmg * burst_t
else:
    time = wind_up + wind_down + cooldown
    if freq > 0: time = time * freq + reload;                 total = freq * dmg
    else:                                                     total = dmg
DPS = max(0, total / time * 10)
```

"Dynamic": damage, cooldown and both reload times are passed through the game's weapon modifier function
`0x8A6790` — `__thiscall(weapon, float base, Entity *owner, int type)`, `ret 0xC`, **result in xmm0** (needs a small asm
thunk, see `us_mod` in unitstats.c). Types seen at its call sites: 1 min range, 2 reload, 3 cooldown, 5, 6, 7 damage,
8 armour penetration, 9, 0xA, 0xB courage damage, 0x17 max range, 0x1D setup, 0x1E teardown. So wargear swaps (a new
weapon on the hardpoint), upgrades and weapon buffs show; dead models drop out of the sum.

## What it does not count (approximations)

* Damage is the weapon's own `damage_hp` — no armour of a target (weapon_damage_bag vs armour type), no accuracy,
  no cover, no suppression, no setup time of heavy weapons, no knockback/special attacks. It is the same number the game's
  own `Util_GetWeaponDPS` gives, i.e. "DPS against no armour, every shot hitting".
* Melee DPS ignores melee skill contests and sync kills; ranged-in-melee penalties (`in_combat_ranged_damage_modifier`)
  are not applied.
* Speed and melee skill are blueprint values: speed buffs/slows and melee-skill buffs (`MeleeSkillBuff`) are not
  applied (the modifier function above is per weapon; the entity-level one was not looked for).
* Abilities' own weapons, grenades and global abilities are not counted unless they sit on a hardpoint.
* A squad in a vehicle / building, a garrisoned squad: whatever the hardpoints of its entities say.

## Before it could be released (checklist)

1. Owner's decision: is it wanted at all (Elite's balance team may not want exact numbers on the HUD).
2. Its own add-on, its own tick (docs/ADDON_RULES.md), its own .ini / .log, `addons.json` entry, store texts.
3. Position checked by players at 1920x1080, 2560x1440, 4:3, UI scale changes; text does not cover the order buttons,
   the squad count or the level / owner lines; the font is the game's.
4. Elite: same DOW2.exe 3.19 (addresses match), but Elite blueprints may add hardpoints / special weapons — check
   several Elite squads of every race (vehicles with several guns, heroes with wargear, Tau if any).
5. Watch performance: it reads every 250 ms only; keep it that way, never per frame.
6. Long matches with many selection changes, observers and replays (the selection exists there too), alt-tab,
   `-dev`-less normal launch, the launcher's loader (Profiler.dll) and the vanilla loader (winmm.dll).
7. The fault guard must stay: any fault inside the reads ends the numbers for that game, never the game.
8. Decide labels / translation (ADDON_TEXTS.md) and whether enemy squads should show numbers.
9. Code review for D3D (`EndScene/Present/ID3DXFont/d3d9.h` must not appear) — UI_RULES.md.

## Stand test, 05.11.2026 (DOW2.exe 3.19 vanilla data, skirmish Eldar vs SM AI, under Wine)

`unitstats/stand/unitstats.scar` (appended to skirmish.scar through EliteAI.dll) spawns squads by the base, reveals the
map and selects one squad every 10 s with `Misc_SelectSquad`; the DLL logs each new squad with a per-weapon breakdown
(`unitstats/stand/UnitStats_stand1_0511.log`). What the panel showed:

| Squad | Ranged DPS | Melee DPS | Melee skill | Range | Speed | Sight |
|---|---|---|---|---|---|---|
| Farseer (hero) | 2.5 | 35 | 50 | 23 | 5.5 | 50 |
| Guardians (5) | 44 | 33 | 50 | 39 | 5.5 | 40 |
| Howling Banshees (5) | 12 | 135 | 15 | 23 | 5.5 | 40 |
| Falcon | 65 (3 guns) | 0 | 0 | 46 | 6.0 | 40 |
| Wraithlord | 12 | 85 | 60 | 45 | 4.5 | 40 |
| Warp Spiders (4) | 73 | 34 | 50 | 39 | 5.0 | 40 |
| enemy Tactical Marines (3) | 44 | 57 | 8 | 39 | 5.0 | 40 |
| enemy Assault Marines (3) | 23 | 81 | 10 | 23 | 5.0 | 40 |

Found on the way:
* Melee weapons are hardpoints of CombatExt like guns (MeleeWeapon is made in 0x87FA30, CombatExt code) — every
  infantry model has a gun and a melee weapon, so both DPS numbers are always there.
* The first layout (two lines centred on Retreat) lost its second line below the screen edge: the strip under the
  order row is only about two lines high at 1280x800. The fields now start right under the order row (y 135.7 in
  `core_actions`), `leading -2`.
* Enemy squads: selected only when visible (the SCAR test needed `FOW_RevealAll`); then the numbers show for them too.
* Melee "skill" of heroes (50) comes from their blueprint; the leveling tuning (`melee_skill` per level in the
  ModTuning tables, 0x886950) is not applied — a hero's skill may be higher in a real match.
* No faults, no visible frame cost (four reads a second).
