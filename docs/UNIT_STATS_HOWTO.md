# Unit stats beside Retreat — how to do it (experiment, archived)

Request: grantovich, Elite Mod - Dev, 04.11.2026 — "dynamic DPS figures and other unit stats to the left of the retreat
button in that unused space: ranged DPS, melee DPS, melee skill, range, speed, sight radius".

v2 (05.11.2026, after the Discord review - grantovich "15 melee skill banshees", adila "tacs are also wrong",
nootersnooter "the real melee skill is in leveling_ext", grantovich "icons, not text" and "armor type there as well"):
melee skill now the one melee combat uses, icons of the game instead of words, the armour type, squad total plus the
per-model number, range as written in the data, speed with its modifiers. Checked under Elite (the mod the reviewers
play; v1 was checked only on vanilla data). Section "v2" at the end.

v3 / 0.3 (05-06.11.2026, grantovich: "further left, active buffs, resists, auras and so on - like in DoW1"): the active
effects on the squad as a third block left of the stat rows. Section "v3" at the end.

v4 / 0.4 (06.11.2026, grantovich: "the armour type's name is so long that not all the modifiers fit"; "the DPS is not
what I count by hand - shotgun 20 damage, cooldown 1.5, wind_down 0.5, 9 shots, reload 3 s: 8.6, not 10.3"): DPS by the
weapon's real firing cycle (the engine's weapon state machine, checked against the shots the engine fires on stand 1),
the armour type as icon + short name in the game's language, the effects in three compact rows when two do not hold
them. Section "v4" at the end; the old formula below is now only logged as "tooltip".

Status: **experiment, archive branch `archive/unitstats-experiment` only.** Not in `addons.json`, not in the store, not
for players. `make build/UnitStats.dll` (not part of `make all`). Code: `unitstats/unitstats.c`.
The result of the stand test (screenshots, numbers, what did not work) is at the end of this file.

## The rule it follows

Only the game's own interface (docs/UI_RULES.md): a clip of ours (text fields + icon clips with the game's own textures) inside the game's selection panel movie,
written through the movie's own Scaleform API (common/gfxui.h). No Direct3D, no overlay, no new device.
Only reads: nothing is written into the simulation and no order is sent — no desync possible, only this player sees it.

## What is shown (v2)

```
[bolter] 44 (3x14.6)   [fist] 57 (3x19)   [red fist] 60          <- ranged DPS, melee DPS, melee skill
[armour icon] Heavy infantry   [arc] 38   [boot] 5.0   [eye] 40    <- armour type, range, speed, sight
```

Squad totals; the small "(3x14.6)" is models x per-model number when every model is the same - the number Elite's
unit tooltips quote ("Bolters (14.58 piercing DPS, 19 melee DPS)"). "-" = the squad has no weapon of that kind / no
melee extension (vehicles without melee: no melee skill).

Icons - all textures of the game itself, loaded into clips of the panel movie by the movie's own `Receive_LoadMovie`:

| Item | Texture | What it is in the game |
|---|---|---|
| ranged DPS | `ui/textures/space_marines/icon_movement/stance_ranged.dds` | the stance button of this panel (ranged): a bolter |
| melee DPS | `ui/textures/space_marines/icon_movement/stance_melee.dds` | the stance button (melee): a fist |
| melee skill | `ui/textures/generic/decorators/mod_melee.dds` | the decorator over a unit with a melee modifier: a red fist |
| armour | `ui/textures/generic/tooltips/ico_l_infantry / ico_h_infantry / ico_vehicles / ico_buildings.dds` | the weapon tooltip's "effective against" icons, by armour class |
| range | `ui/textures/generic/tooltips/ico_range.dds` | the weapon tooltip's range icon |
| speed | `ui/textures/eldar/icon_traits/eld_swift.dds` | the "Swift" trait: a boot |
| sight | `ui/textures/generic/fe/loading/tips/up_detector.dds` | the detector loading tip: an eye |

No exact "melee skill" icon exists in the game; the closest native one is the melee-modifier decorator (a red fist,
distinct from the yellow stance fist). Other candidates tried and rejected: the campaign attribute atlas
`ui/textures/generic/bridge/inv/attrib_icons.dds` (frameless bolter / fist - but one texture, and a cell cannot be cut
out: `setMask` is ignored by this GFx, the mask shows as a white square, screenshot `v2/rejected_atlas_mask.png`);
the melee attack cursor `ui/cursor/atk_melee.tga` (a fist in a crosshair - .tga is loaded garbled). Every icon path
can be changed in `UnitStats.ini [Icons]` ("img://data:UI/...dds|scale%"), re-read live.

Texture paths: **`img://data:UI/textures/....dds`** is what this game's GFx loader takes in `MovieClipLoader.loadClip`
(`Receive_LoadMovie(clip, path)` of every HUD movie); plain `DATA:UI/...` / `data:UI\...` (the form the movies import
their own bitmaps with) and paths without a prefix load nothing (stand 1, 05.11.2026). The image arrives a frame or
two later; its size is read back with the movie's `Receive_GetMovieClipWidth/Height`, then the clip is scaled.

Layout: our clip `us_box` in `core_actions` (depth 16110); per item a text field `t<k>` (+ black copy `s<k>` 1.2/1.5
lower right as a shadow, as the game's own counters) with `autoSize = "left"` and an icon clip `i<k>.h.img`. The rows
are right aligned 6 units left of Retreat (x 64.9 in core_actions), the first right under the order row (y 136.7),
line height 1.3 x FontSize + 4; the text widths are read back (`Receive_GetMovieClipWidth`) and the icons placed in
front of them. FontSize 22, icons 1.2x (26) fit two rows above the panel's bottom edge at 1280x800 in vanilla and
Elite (both movies have the same `core_actions`; Elite's movie is 2134x1200 instead of 1180x700, everything shifted).
`_visible` of `us_box` shows/hides the whole thing.

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
| melee skill (v2) | the game's own getter `0x862CB0` (ESI = entity, result EAX): leveling_ext attrib_levels[level].melee_skill, else melee_ext weapon_skill, then the entity modifiers type 0x2F; max over entities; "-" without MeleeExt (QI 0x20) | yes |
| melee skill (v1, wrong) | entity blueprint, `melee_ext` bag → `MeleeExtInfo + 0x04` (`weapon_skill`) - melee combat does not use it when leveling_ext exists | no |
| armour (v2) | `type_ext` bag (key 0x01301A84, TypeExtInfo vtable 0x010CFF8C) `+0x24` = armour type id; name: world tuning `armour_type_names` (LocStrings, 8 bytes each at tuning `+0x414..+0x418`, tuning = `0x445F50`, as `0x836F60`; c_str = DOW2.exe import 0x00F874C8), else the internal name `**0x013356B0 + 0xC83C` (0x100 each); the most common over entities | blueprint |
| range | max over ranged weapons of `Weapon::GetMaxRange` (weapon vtable slot 7, `+0x1C`, `__thiscall`, float in ST0) **minus the engine's range allowance**: at load the weapon bag postload `0x821E60` adds ModTuning CombatInfo `+0x2C` (1.0 in vanilla and Elite; CombatInfo = `0x7E2F50` with ESI = tuning group 7) to every weapon's max range, so the engine's number is the data +1 (bolter 39 for 38) - v1 showed that; v2 shows the range as written and quoted | yes (modifier 0x17) |
| speed | `moving_ext` `speed_max` through the entity modifier function `0x8A6AC0` (stdcall value, entity, type 6 = speed_maximum_modifier, 0; ret 0x10, xmm0 - what `0x928050` does; type 7 is the retreat speed), min over entities | yes (v2) |
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

## DPS formula (the game's own tooltip formula; v1-v3 showed it, v4 shows the real cycle - section v4)

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
* (v1 only) Speed and melee skill were blueprint values; v2 takes both through the game's own functions (see above).
* The level's damage multiplier (leveling_ext `melee_damage_modifier` / `ranged_damage_modifier` at the squad's level)
  is applied in v2 the way the damage code `0xA27720` does: `0x9688E0` (EDI = entity, byte melee on the stack,
  `ret 4`, xmm0; squad leveling QI 0xF `+0x2C` melee / `+0x28` ranged level index into the 0x20-byte entries,
  `+0x10` melee, `+0x1C` ranged multiplier; 1.0 without leveling). 1.0 at level 1 in Elite.
* Damage by distance (`damage_hp_range_modifier` short..distant, WeaponBag `+0x6C..+0x98`, used by `0xA27720`) is not
  applied - the game's Util_GetWeaponDPS ignores it too; most weapons have 1.0 everywhere.
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

## v2 - stand test under Elite, 05.11.2026

`unitstats/stand/session.sh` (stand 1, through stand_lock.sh): Elite `-modname Elite -dev -init`, Eldar (Farseer)
vs SM AI, Elite_DLLs = Elite's own four + EliteAI.dll (SCAR `unitstats/stand/unitstats_v2.scar` appended to
elite.scar: spawns 5 Eldar squads for us and 8 squads of 6 other races for the enemy, far away, enemy AI off with
`AI_Enable(false)`, reveals the map, selects one squad every 9 s) + UnitStats.dll; a screenshot 2 s after every
selection change. Log: `unitstats/stand/UnitStats_stand1_0511_v2_elite.log` (per weapon: damage, ticks, range).
Screenshots (media vault): `/var/www/media/dow2_shots/unitstats/v2/` (`full_*.png` 1280x800, `zoom_*.png` the panel
x3, `overview_*.png`).

Reference ("data"): `unitstats/stand/ref.py <sbps>` computes the same numbers straight from the blueprints (Elite
attrib over vanilla, gamedata of the stand's Elite 3.19.1): loadout x wargear weapons, the game's Util_GetWeaponDPS
with times in whole sim ticks, leveling_ext level 0 melee skill, moving_ext speed_max, sight_ext, type_ext armour.
(rd.py: an exact RBF tree dumper; key names of the dump are shifted in places - `caster_close_in_distance/max` is
range max, `type_ext/dca_action_name` is armour_type.) In-game = data for every squad below.

| Squad | was (v1 numbers under Elite) | now (v2 on the panel) | data |
|---|---|---|---|
| Farseer | ranged 12, melee 35, skill **50**, range **23**, speed 5.5, sight 50 | 12 / 35 / 70 / Commanders / 22 / 5.5 / 50 | 12.25 / 35 / 70 / commander / 22 / 5.5 / 50 |
| Guardians (5) | 44, 33, 50, **39**, 5.5, 40 | 44 (5x8.8) / 33 (5x6.7) / 50 / Infantry / 38 / 5.5 / 40 | 43.75 / 33.33 / 50 / infantry / 38 / 5.5 / 40 |
| Howling Banshees (5) | 12, 135, **15**, **23**, 5.5, 40 | 12 (5x2.5) / 135 (5x26.9) / 70 / Infantry / 22 / 5.5 / 40 | 12.35 / 134.62 / 70 / infantry / 22 / 5.5 / 40 |
| Falcon | 70, 0, **0**, **46**, 7.0, 40 | 70 / - / - / Vehicles / 45 / 7.0 / 40 | 69.63 / - / (no melee) / vehicle / 45 / 7.0 / 40 |
| Wraithlord | 12, 85, **60**, **39**, 4.5, 40 | 12 / 85 / 70 / Vehicles / 38 / 4.5 / 40 | 12.17 / 85 / 70 / vehicle / 38 / 4.5 / 40 |
| Warp Spiders (4) | 73, 44, 50, **39**, 5.0, 40 | 73 (4x18.3) / 44 (4x11) / 50 / Infantry / 38 / 5.0 / 40 | 73.04 / 44 / 50 / infantry / 38 / 5.0 / 40 |
| Tactical Marines (3) | 44, 57, **8**, **39**, 5.0, 40 | 44 (3x14.6) / 57 (3x19) / 60 / Heavy infantry / 38 / 5.0 / 40 | 43.75 / 57 / 60 / heavy_infantry / 38 / 5.0 / 40 |
| Assault Marines (3) | 23, 81, **10**, **23**, 5.0, 40 | 23 (3x7.7) / 81 (3x26.9) / 70 / Heavy infantry / 22 / 5.0 / 40 | 22.97 / 80.77 / 70 / heavy_infantry / 22 / 5.0 / 40 |
| Predator | 78, 0, **0**, **45**, 7.25, 40 | 78 / - / - / Vehicles / 44 / 7.2 / 40 | 77.77 / - / (no melee) / vehicle / 44 / 7.25 / 40 |
| Slugga Boyz (6) | 4.1, 150, **10**, **23**, 5.0, 40 | 4.1 (6x0.7) / 150 (6x25) / 60 / Infantry fire resist / 22 / 5.0 / 40 | 4.08 / 150 / 60 / infantry_fire_resist / 22 / 5.0 / 40 |
| Chaos Space Marines (3) | 40, 66, **15**, **39**, 5.0, 40 | 40 (3x13.4) / 66 (3x22) / 60 / Heavy infantry / 38 / 5.0 / 40 | 40.25 / 66 / 60 / heavy_infantry / 38 / 5.0 / 40 |
| Genestealers (6) | 0, 182, **9**, 0, 6.5, 38 | (log) - / 182 / 80 / Infantry fire resist / - / 6.5 / 38 | - / 181.5 / 80 / infantry_fire_resist / - / 6.5 / 38 |
| Fire Warriors (4) | 70, 10, **8**, **39**, 5.0, 40 | 70 (4x17.5) / 10 (4x2.5) / 50 / Infantry / 38 / 5.0 / 40 | 70 / 10 / 50 / infantry / 38 / 5.0 / 40 |
| Guardsmen (6) | 26, 12, **30**, **39**, 5.0, 40 | 26 (6x4.3) / 12 (6x2) / 50 / Infantry / 38 / 5.0 / 40 | 25.94 / 12 / 50 / infantry / 38 / 5.0 / 40 |

What was wrong and why:
* **Melee skill** (all infantry): v1 read melee_ext `weapon_skill`; melee combat uses leveling_ext
  `attrib_levels[level].melee_skill` whenever the entity has it (0x9762E0: squad leveling QI 0xF `+0x2C` = level index,
  entries of 0x20 bytes, skill at `+0x14`), plus melee skill modifiers. Tacs 8 -> 60, banshees 15 -> 70, sluggas 10 -> 60.
  It is live: a retreating squad gets +50 (tuning `on_toggled_retreat_actions_pvp`, skill_melee_modifier), the panel
  showed tacs 110 / assault 120 while the AI pulled them back.
* **Range**: v1 showed the engine's range = data + 1 (the range allowance above). v2 subtracts it.
* **"Tacs are wrong"**: their DPS was already right per model (14.58 / 19, the numbers of the Elite tooltip) but shown
  only as a squad total (44 / 57) next to a melee skill of 8; v2 shows "44 (3x14.6)" and the real skill. v1's published
  table was made on vanilla data, not Elite's (e.g. Falcon pulse laser and speed differ in Elite).
* DPS formula: unchanged, it is the game's Util_GetWeaponDPS (checked against the disassembly of 0x81FC40 again) on the
  weapon as loaded: times in whole 0.1 s ticks (truncated: wind_down 0.49 s = 4 ticks, bolt pistol 7.66 not 7.30), a
  `burst_info` table without duration = a 1-tick burst that skips wind up/down (pulse carbine 17.5 per model, not the
  12.6 a reading of the file without the burst would give). Suppression and buffs show live (suppressed guardsmen
  ranged 26 -> 2.9, speed 5 -> 1.75).

Not found / notes: genestealers were selected but the panel stayed empty in the final run (burrowed / infiltrated
squads are not in the selection); numbers from the earlier run's log. One run (e2) froze the game (main thread waiting
in the game loop, sim stopped) 2.5 min in, right after a dead start squad was deselected by the SCAR; three runs
(e1, e3, e4, 5-8 min each, the same DLL code) did not - not reproduced, not explained; the engine calls can be
switched off one by one in the ini (GameSkill / EntityMods / LevelMul) if it shows again.

## v3 - the active effects (0.3, 05-06.11.2026)

Request (grantovich, DM 05.11.2026 22:07): "further left show the active buffs, resists, auras and so on, like in DoW1".

### What is shown

Left of the two stat rows, in the same strip of `core_actions` (clips `e<k>` icon, `f<k>` text, `g<k>` its black shadow,
depths 300+k / 200+2k), up to `[Effects] Max` = 6 cells, filled from `Left` (-470, the left edge of the strip) towards the
stats, row 1 then row 2, never closer than `Gap` (14) to the leftmost stat item of that row (`layout()` records it in
`g_rowLeft[]`); what does not fit is dropped (the log says "not all fit"). A cell = a game icon + the net change in green
(good for the squad) or red (bad) + a small tag ("heavy", "vs veh", "no retreat"). Biggest change first.

| Modifier type(s) (the engine's names) | Icon (game texture) | Reads as |
|---|---|---|
| received_damage | icon_traits_xp2/damage_resistance (gold shield) | "-30%" damage taken |
| received_damage_melee (+light/heavy/power) | icon_traits_xp2/melee_def | "-70%" melee damage taken |
| received_damage_ranged / piercing / flame / explosive / plasma / detonation | icon_traits_xp2/ranged_def | "-50%" ranged damage taken |
| received_accuracy | icon_traits_xp2/force_shield | accuracy of shots at it |
| damage_weapon, damage_armor_* | icon_traits_xp2/damage (tag "vs inf"...) | "+30%" damage |
| damage_melee_weapon / damage_ranged(+projectile) | decorators/mod_melee / mod_ranged (what the game draws over a buffed unit) | melee / ranged damage |
| accuracy (+ranged / melee) | tooltips/ico_accuracy | its accuracy |
| cooldown, reload (as a rate: 1/x - 1), burst | icon_traits_xp2/rate_of_fire | "-95%" = fires 20x slower |
| range_maximum | tooltips/ico_range | range |
| speed_maximum | icon_traits_xp2/sprint | "-65%" or "+0.5" (an addition) |
| health_maximum / health_regeneration | health_generic / health_regeneration | |
| sight_radius | loading tip up_detector (eye) | |
| skill_melee | icon_traits_xp2/melee_paralyzes | "+50" |
| armour_penetration, courage_damage, setup | critical_hit, suppression, setup | |
| knockback_chance_* (5 kinds, one cell) | knockback_def | "-100%" |
| ability_recharge_time (entity, squad) | cool_down | |
| received_courage_damage (squad) | suppression_def | "-100%" = no suppression |
| energy_regen_rate / energy_maximum (squad) | energy | |
| camouflage_enable > 0 / shoot_enable < 0 / retreat_enable < 0 | invisibility / stun / fanaticism (IG) | "hidden" / "no fire" / "no retreat" |

All icons are game textures (`img://data:UI/textures/...dds`, vanilla 000_data.sga), `[EffectIcons]` in the ini overrides any.

### Where the numbers come from (DOW2.exe 3.19)

Modifiers in DoW2 are not kept per buff on a unit - every modifier extension keeps, per modifier TYPE, a 0x20-byte entry:
`+0` vector of `{handle, Modifier*}` pairs (each modifier applied, for exclusivity), `+0xC` multiplier, `+0x10` addition,
`+0x14/+0x18` the same of only the decorator-shown ones, `+0x1C` the enable sum - recomputed by `0x88B2B0` (usage 0 adds,
usage 1 multiplies **additively**: `1 + sum(v - 1)` - two x1.5 = x2.0, usage 2 sums the enable values; exclusive ones
counted once). Vectors: entity `ModifierExt` (QI 2, vtable 0x011013B0) `+0x14` entity types (0x39), `+0x20` weapon types
(0x1F); `SquadModifierExt` (`SquadController::QI(1)`, squad +0x48; vtable 0x01101470) `+0x14` squad types (0x18), `+0x20`
entity types, `+0x2C` weapon types, `+0x38` upgrade ones (dump: `0x9E8370` "SquadModifierExt: Entity Modifiers:...").
Type names: the game's own tables `0x8A7590` (entity), `0x8A7400` (weapon), `0x8A7910` (squad) - `jmp [eax*4+table]`,
`mov eax, "modifiers\entity_modifiers\received_damage_modifier"`. A Modifier (0x40, made by `0x8AD4D0`): `+0x10` value,
`+0x14` type, `+0x18` application (apply_to_entity / squad / weapon / player...), `+0x1C` class (0 entity, 2 squad,
3 weapon), `+0x20` usage, `+0x24` exclusive, `+0x28` exclusive type, `+0x3D` show_modifier_decorator.

The shown value is what the game's code gets, through the game's own functions, so player-level (research), entity,
squad and per-weapon (hardpoint-targeted, e.g. Guide's `hardpoint_01`) modifiers all count:
* entity types: `0x8A6AC0(value, entity, type, 0)` = `(value + add) * mul` over player (`0x7E6440`, per blueprint),
  entity and squad extensions; called with 1 and 0: `mul = f(1) - f(0)`, `add = f(0) / mul`;
* weapon types: `0x8A6790(weapon, value, entity, type)` the same way, per weapon of the entity; the weapon with the
  biggest change is shown (class-specific types - melee / ranged damage, accuracy - only touch weapons of that class);
* squad types: the squad extension's entry directly; enable types: the sums of the entries.
The model with the biggest change wins; the log says on how many models it is ("on 5/5").

### What is not shown: the time left

The timer of an applied modifier is a record (0x34: `+0` Modifier, `+4` handle, `+8` timer - vtable 0x010DDED8, `+0x50`
ticks left counted down by `0x91DC20`, `+0x4C` "while a condition holds" -, `+0xC` the Target, `+0x2C` the entity),
created by `0x91D7D0` and kept in the `ModifierActionState` (0x44, vtable 0x010DBC28, list at `+0x3C`, made by
`0x8547B0` from `ApplyModifiersAction::Execute 0x8545E0`). That state lives in the action tree of the CASTER: the
ActionUpdater of the ability that ran it, nested in Timed / Delay / Repeat / Requirement action states (each has its own
ActionUpdater: `0x9E19C0` +0x44, `0x94C4D0` +0x48, `0x94ADD0` +0xB8, `0x853FC0` +0x40; player abilities: ModPlayer
+0x3B8 -> per ability +0x14). Not on the buffed squad, not in the world's ActionUpdater (world +0x1E0 - only
`IsAliveAction` states, one per entity group, stand 1) and not in the world's modifier applier manager (+0x1DC - empty in a
skirmish). The squad's own ModifierExt has the pairs, not the time. Doing it: walk every squad's / player's ability
updaters (recursively through those action states) for 0x010DBC28 states whose records target the selected squad's
entities - read-only, but many more engine structures to trust; left for a later version.

### Stand test (stand 1, Elite, 05-06.11.2026)

`unitstats/stand/session_v3.sh` + `unitstats_v3.scar` + `init_v3.lua.in`: we are Eldar, the enemy IG with the Lord
Commissar (AI off); the script casts with `Cmd_Ability(..., skipCostPrereq = true)` and selects one squad at a time; log
`unitstats/stand/UnitStats_stand1_0511_v3_elite.log`, screenshots `/var/www/media/dow2_shots/unitstats/v3/` (`zoom_*`).

| Case | Shown | Data |
|---|---|---|
| Warp Spiders, nothing on them | nothing | - |
| Guardians + Fortune | damage taken -30% (and the lasting -50% melee / -50% ranged) | eld_buff_target_fortune: received_damage x0.7, 15 s (gone after 15 s) |
| + Guide | +30% damage, +30% range (5th cell did not fit) | eld_buff_target_guide: damage_weapon x1.3 hardpoint_01, range_maximum x1.3 |
| Banshees + exarch aura | speed +0.5 (stat row speed 5.5 -> 6.0) | eld_buff_aoe_auraexarch: speed_maximum, addition 0.5 |
| Guardians under heavy weapon fire | fire rate -95%, speed -65% (DPS 43.75 -> 6.7, speed 1.9) | suppression: cooldown x20, speed x0.35 |
| Nobz + Frenzy | suppression taken -100%, damage taken -75% | ork_buff_self_frenzy: received_courage x0, received_damage x0.25, 10 s |
| Guardsmen after the commissar's execution | damage +100% (ranged DPS 25.9 -> 43 for 5 models) | ig_commissar_execute_effects: damage_weapon x2 |
| Lord Commissar | melee damage taken -40% | lasting |
| fresh Guardians in the open | nothing | - |

The lasting "-50% melee / -50% ranged damage taken" (banshees, tactical marines -70% melee) shows on squads standing near
the bases and not on the same squads in the open - most likely cover; the engine's numbers, source not identified.
Stats (rows 1-2) as in v2 for every squad (same numbers as the v2 table). No faults.

### The freeze of v2 (one run in four): not UnitStats

Reproduced every time on the stand by the stand script itself: a squad is selected, `Squad_Kill` kills it, then SCAR
`Misc_SelectSquad` selects another squad -> simulation and UI both stop (the watchdog of 0.3: the panel's update stops
while our code is idle). The same with UnitStats switched off (`Enabled=0`, run e3off: the SCAR heartbeat stops right
after that select) and when the dead squad is not deselected first; killing the selected squad alone is fine (the game ran
on 10 s). v2's frozen run e2 was the same pattern ("gone start squad 2" = a dead squad, then a select). So it is a
game/Wine reaction to a script selection right after the selected squad died, not the DLL; the v3 stand script avoids it.
0.3 still hardens the DLL: the selection mutex is taken with `TryEnter` (never waits; busy = keep the last frame),
pointers are checked with VirtualQuery instead of IsBadReadPtr (inside the guard our own vectored handler would take
IsBadReadPtr's fault), a fault skips one update instead of ending the numbers (20 end them), and a watchdog thread logs if
the panel update stops ("watchdog: ..." in UnitStats.log, says where).

## v4 - DPS of the real firing cycle, short armour names, compact effects (0.4, 06.11.2026)

### Why the tooltip formula is not what a weapon does

grantovich counted by hand a shotgun of the newest Elite ("Hardened Veterans": 20 damage, cooldown 1.5 s, wind_down 0.5 s,
9 shots, reload 3 s) at 8.6 DPS; 0.3 showed 10.3. 0.3 used the game's own `Util_GetWeaponDPS` (0x81FC40): a weapon with a
`burst_info` table (even a one-tick one) goes through its burst branch, which has no wind_up / wind_down at all:
180 / (30 + (15 + 1) x 9) x 10 = 10.3. The engine's weapon does otherwise.

### The engine's firing cycle (DOW2.exe 3.19)

Ranged weapons: classes RangedWeapon (vtable 0x010FC38C, type 1) and ProjectileWeapon (0x010FC434, type 2), both with
the update `0xA4AA10` (vtable +0x5C); the state at weapon +0x58 (names from the debug printer 0xA4C8D0):

| State | Handler | What happens |
|---|---|---|
| 4 ready | 0xA4AE30 | aimed at the target (+0xD4 = 0, +0xB4 = 4): windup counter (+0x130) = wind_up, state 6 at once |
| 6 windup | 0xA4B590 | counter > 0: count down, return; else set up the burst - B = trunc(random(burst min, max)) ticks (>= 1), rate = burst rate_of_fire per tick (+0x150), the rest accumulator +0x154 = 1 - rate when rate < 1; no burst_info: B = 1, rate 1; state 7 (fires nothing this tick) |
| 7 firing | 0xA4B810 | per tick floor(acc + rate) shots (vtable +0x74, one call per shot), acc keeps the rest; ProjectileWeapon: projectiles_per_shot (bag +0x114) shots and the burst ends after this one tick; B ticks counted down (+0x14C), then vtable +0x78 = 0xA4A9C0 |
| burst end | 0xA4A9C0 -> 0xA4CB90 | reload due if the counter +0x148 <= 1 (and reload frequency min != 0); winddown end = now + wind_down; reload end = winddown end (+ reload); cooldown end = reload end + cooldown |
| 8 winddown | 0xA4B9A0 | at winddown end: counter +0x148 - 1, then reload (9) or cooldown (10) or, both over, ready (4: one more tick) |
| 9 reload | 0xA4BB60 | at reload end: counter = new F, cooldown |
| 10 cooldown | 0xA4BC30 | at cooldown end: windup (6) in the same tick |

* cooldown = trunc(trunc(cooldown x cooldown modifiers) x random(weapon_cooldown_randomization_min, _max) / 100)
  (`0x96E5A0`; tuning combat 80 / 120 in vanilla and Elite, CombatInfo +0xB8 / +0xBC) - on average about half a tick
  less than written;
* reload = trunc(min) + trunc(random x (trunc(max) - trunc(min))) (`0xA48F10`, both through the reload modifier);
* F (bursts per reload) = min + Random::GetMax(max - min) (`0x8221E0`; Util.dll `GetMax(n)` = rand % n, so max itself
  never comes: frequency 8-10 gives 8 or 9). The counter is checked at the burst's end and counted down at the
  winddown's end: a reload after every F-th burst - F, not F + 1;
* wind_up / wind_down are used as loaded (whole ticks), no modifier.

So per reload: F x (wind_up + B + wind_down + cooldown) ticks + reload, F x shots-per-burst x damage; without reloads
one burst per (wind_up + B + wind_down + cooldown) ticks, plus one tick when the cooldown is 0. Melee (MeleeWeapon,
update 0x9AAC70: 6 windup, 7 strike 0x9AAE10, 8 winddown 0x9AAEA0, 10 cooldown 0x9AAF20, start 0x9AB020):
max(wind_up, 1) + 1 + max(wind_down - 1, 0) + cooldown ticks per strike - the same as the tooltip's wind_up + wind_down
+ cooldown when wind_up and wind_down are >= 1. UnitStats takes the exact expectation over every random value the
engine can draw (`weapon_dps` in unitstats.c), damage / cooldown / reload through the modifier function as before, the
level multiplier as before; the tooltip number is still in the log ("tooltip").

grantovich's shotgun by this (wind_up 0 assumed): 1 + 5 + 14.5 = 20.5 ticks per shot, 9 x 20.5 + 30 = 214.5 ticks per
180 damage = **8.39 DPS** (his 8.6 has no firing tick and no cooldown truncation).

### Checked on stand 1 (06.11.2026)

`unitstats/stand/session_v4.sh` + `unitstats_v4.scar`: IG vs Eldar, AI off; nine of our squads (wargear / upgrade
applied by script: commander shotgun, devils with the guardsmen shotgun, scouts with shotguns), each attacking its own
target that cannot die (`SGroup_SetInvulnerable`) nor shoot (`shoot_enable_modifier` MUT_Enable -1); our squads take no
suppression (`Modify_ReceivedSuppression` 0). `ShotProbe.dll` (`unitstats/stand/shotprobe.c`, stand only) hooks the
fire-one-shot slots of both ranged classes and the weapon updates and logs every shot / strike / windup / reload with the
sim tick; `shots.py` gives the DPS per weapon over whole magazines (first windup after the first reload to the first
after the last). 4.5 minutes; logs `unitstats/stand/ShotProbe_stand1_0611_v4.log.gz`, `UnitStats_stand1_0611_v4_elite.log`,
table `shots_stand1_0611_v4.txt`.

| Weapon (squad) | kind | was (0.3, tooltip) | now (0.4) | measured | shots |
|---|---|---|---|---|---|
| ig_guardsmen_shotgun (devils, 12 weapons) | single, no reload | 8.82 | 8.57 | 8.57 | 1786 |
| ig_commander_shotgun (commander) | single, reload 8-10 x 4 s | 11.74 | 11.32 | 11.29 | 111 |
| sm_shotgun (scouts, 3) | single, reload 8-10 x 4 s | 14.67 | 14.15 | 14.12 | 325 |
| ig_guardsmen_lasgun (guardsmen, 12) | burst 1 s, reload 6-8 | 4.32 | 4.35 | 4.35 | 2366 |
| sm_bolter (tactical, 3) | burst 2 s, reload 6-8 | 14.58 | 14.66 | 14.63 / 14.78 / 11.72 | 736 |
| sm_bolter_devastator (2) | the same | 7.00 | 7.03 | 7.04 | 516 |
| sm_heavy_bolter (devastator, setup) | burst 2 s, reload 10-12 | 216.39 | 219.32 | 219.51 | 2400 |
| sm_autocannon_predator | projectile, reload every shot | 32.43 | 31.58 | 31.58 | 67 |
| sm_chain_sword_assault (assault, 3) | melee | 26.92 | 26.92 | 21.03 (86 % of strikes exactly 13 ticks apart) | 460 |
| ork_choppa (sluggas, 6) | melee | 25.00 | 25.00 | 22.56 (95 % exactly 10 ticks apart) | 1389 |

Ranged: within 0.3 % wherever the weapon kept firing (one tactical marine's 11.72: pauses in its fire, the other two
14.6 / 14.8). Melee: the strike cycle is exactly the formula's (13 / 10 ticks); the measured mean is lower because now
and then a melee unit stops striking for 3-5 s (repositioning, special attacks) - not a part of a weapon's own cycle,
not counted. The heavy bolter's setup is spent once per engagement, not in the cycle (not counted).

### Short armour names, language

`ARM_SHORT` in unitstats.c: infantry "Infantry / Пехота", infantry_fire_resist "Inf. (fire) / Пех. (огн.)",
heavy_infantry "Heavy inf. / Тяж. пех.", super_heavy_infantry, commander "Commander / Командир", vehicle "Vehicle /
Техника", building, building_light, building_defence, building_ig_turret. Russian when the game's own armour type names
(the LocStrings of the world tuning) contain Cyrillic, else English; `[UnitStats] Language=auto|en|ru`;
`[ArmourNames] <internal>=<text>` (UTF-8) overrides. The effect tags ("heavy", "vs veh", "no retreat") follow the same
language. The English game's $NormalFont draws Cyrillic (stand, `Language=ru`).

### Effects in three rows

Two rows at `[Effects] FontSize` (18) beside the stat rows hold about four cells; when a dry run at that size does not
hold all of them, the block switches to three rows at `CompactFontSize` (14) in the same height (back to two rows when
they fit again, 5 % margin). Grantovich's case (CQC infantry, fire resist, six effects): 0.3 showed 3 (his screenshot), two rows with the short
armour name 4 (`zoom_v4a_two_rows_4of6.png`), 0.4 all six
(screenshots `/var/www/media/dow2_shots/unitstats/v4/zoom_*`). Switching re-makes the cells on the next update (250 ms).
