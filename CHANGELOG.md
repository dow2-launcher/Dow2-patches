# Changelog

Every version gets an entry here before it reaches `main`. CI then tags `vX.Y.Z` on its own; for a minor or major
version (`X.Y.0`) it also publishes a GitHub Release with the DLLs and the entries since the previous `X.Y.0`.
The version of the set lives only in `VERSION`; each DLL still writes its own version to its log.

## 0.2.76 — 2026-11-14
LiveReplay 4 (store livereplay-4, experiment, off by default): Watch at any minute of a match, 8x up to the live edge, the game's
"Connecting" box with the scout's line.
- Why a late Watch did not work: since LiveReplay 3 it starts (answers cut by the UDP tunnel are used as far as they go), but it never
  caught up - players' logs 09.11: "catching up: 1500 steps behind" was "caught up" 22 minutes later, at the end of the match (1.1x
  instead of 4x). SimulationController::Update 0x438613 runs at most one tick per drawn frame, so the replay was as fast as the frames.
- Now the catch-up runs at CatchUpSpeed=80 (8x, was 40) for real: while it catches up, the main loop's passes between two shown frames
  skip the visual update and the draw (call sites 0x40E58D / 0x40E6DC, as ReplaySpeed's 8x; chained with its stubs when it is there,
  hooked only for the catch-up and put back after it), a frame shown every 1/30 s (CatchUpFps), the rate set raised (at most 2 x
  CatchUpSpeed) until the replay really runs CatchUpSpeed ticks a second; the real speed in the log every 5 s. The player's own speed
  button ends the catch-up. A catch-up can start whenever the replay is more than 30 s behind (not only at the first step).
- The game's own "Connecting" box (common/connbox.h) from the accepted watch to the start of the replay's loading, second line the
  scout's "See, but do not be seen." - DOW2.ucs 9027524 through Localizer (each language its own; a language without it, as the
  Russian DOW2.ucs, gets 550002 "Connecting to the game..."). LiveReplay.ini ConnectingBox=0 / ConnectingText=<id>.
- No server change needed (dow2-ladder fix/live-replay-cut-answers stays optional): even cut to the tunnel's first burst the answers
  bring the records far faster than 8x plays them.

## 0.2.75 — 2026-11-14
RoomJoin 14 (store roomjoin-8, experiment, off by default) and common/connbox.h (siritos 14.11.2026).
- common/connbox.h: the game's own "Connecting" box of RoomJoin 13 (MessageBoxManager 0x45A3CD / 0xAD5FE0 / 0xAD6200, title
  550001, text 550002 or a DOW2.ucs id of the caller, no buttons) as shared source, so RoomJoin, MMFind, LiveReplay and ReplayOpen
  show the same box. Its three code places are checked for 3.19 before the first use (else: logged, never shown); a text id the
  game's language does not have falls back to 550002. Only from the game's thread, only in the front end.
- RoomJoin 14: the box from common/connbox.h (as before: from the start of the join to the lobby screen). New: into a lobby as an
  observer - {"cmd":"join","room":id,"name":"...","as":"obs"}: the join as always, then on the lobby screen a free observer seat
  with the screen's own command for a click on that seat (fscommand player_list_panel_mc.OnObserverItemClick "<position>" ->
  0x491D51, its menu item "Move to this slot" 0x492EF1 run at once - MMFind 0.12-0.44's watchers did it the same way); observer
  seats = each team's positions from max_players/2 on (the lobby's Receive_SetMaxPlayers, as 0x491D51 decides), free = GameInfo slot
  type 0 without a person; my seat from the player list's own row. Answers {"ev":"in_lobby","room":id,"as":"obs"}; no free observer
  seat (or 6 changes that did not take) -> {"ev":"failed","room":id,"reason":"no_obs_slot"} and Back out of the lobby. The seat menu
  hook is installed only on 3.19's prologue under the shared patch lock; without it a join "as obs" fails at once ("error").
  Status "version":"14" (the launcher sends "as" to 14+ only). _WIN32_WINNT for older mingw headers (CancelIoEx), as MMFind.
- Stand (stand 1, Elite 3.2.1, one Steam account - the only stand with a working Steam login on 14.11): see the PR.

## 0.2.74 — 2026-11-14
New AI 25 (store newai-25, experiment, off by default, Elite only): step 6 of the ork brain (brain 0.6). eliteai branch newai-25
e7b1802 = brain 6c6fd43 with main.ai, mechanics.ai and the vanilla-base sources of strategy_tech_purchase.ai /
strategy_unit_purchase.ai of a3a378a (newai-23 / 23.1), exactly as newai-24, so every non-ork slot still plays newai-23.1.
EliteAI.dll 0.11 unchanged. Against newai-24 only two served files change: default.ai (the brain chunks 0.5 -> 0.6) and
elite.scar (ork abilities 32-38 for casts in a fight; an unload order 14 the brain does not send by default) - additions only.
- The brain (ork new-AI slots, [Lua] Brain default 1; Brain=0 = newai-23.1 for all): cover spots in a fight
  (Squad_FindCoverCompareCurrent), going round a set-up team's arc, casts in a fight (stikkbombs, mortar, shootas' suppression,
  warboss stomps / rapid fire) with the target checked alive and not retreating, no melee squad into a building (Relic's 'Hold'
  tactic refused for mobs of a brain player only), a power crisis (no power income: the tier keeps no money, a power point is
  the objective), the first two hero pieces do not wait for the tier, a fallen enemy hero is no enemy, burnt ground after a rout,
  enemy turrets known by sight and sieged by machines / tankbustas, retreat at a third of the models in pressed fights.
- Known weak spots: the army arrives in pieces, the late game is weak, nobz are rare; turrets and the power crisis are not tested
  against people (the bench's computer opponent rarely builds turrets and never takes the brain's power point).
- Check (independent verifier, fastbench_1113, own worktrees): lib3 32, lib7 31, lib8 24 + 24 more seeds, lib6 3 and lib8 24
  multithreaded - 0 hangs, 0 TAKE FAILED, 0 Lua / SCAR errors. 48 live 25-min games vs a computer space marine, a647d94 ->
  6c6fd43: ahead on tickets 27 -> 40 (13 games flip to ahead, 0 the other way), squads lost 79 -> 27, melee-in-building seconds
  216 -> 0. Brain=0 = vanilla-s9 on the 9-scene library (0 squad-seconds differ). Package: ork slots brain on, 0 squad-seconds
  differ from 6c6fd43; eldar / space marines brain off, 0 squad-seconds differ from newai-23.1.

## 0.2.73 — 2026-11-13
New AI 24 (store newai-24, experiment, off by default, Elite only): the new AI "brain" for orks. eliteai branch newai-24 eceeed9 =
brain a647d94 (step 5 d4969a0 + "release default Brain=1": [Lua] Brain defaults to 1, Brain=0 turns it off) with main.ai,
mechanics.ai and the vanilla-base sources of strategy_tech_purchase.ai / strategy_unit_purchase.ai of a3a378a (newai-23 / 23.1),
so every non-ork slot plays newai-23.1 (the unreleased steps 9-11 of vanilla-s9 are not in it). EliteAI.dll 0.11 unchanged.
Against newai-23.1 only two served files change: default.ai (the dispatcher's Brain block + the 13 brain modules mod/brain/*.ai
as chunks) and elite.scar (the brain's orders: melee attack 8, self-casts 300+k, the player's purse 13 and the cast / retreat
watches - read only for everybody else).
- Brain = 1 (default): a new-AI slot (A / B / C) whose race is orks plays the vanilla base with our old layer off and the brain's
  own rules at Hard (one brain for A, B and C); any other race takes the newai-23.1 path unchanged. Brain = 0: newai-23.1 for all.
- What the brain does: hero wargear and squad upgrades early, T2 ~7 min / T3 ~13 min, Waaagh into a fight in the balance or a
  stall (not into a fight won 2 : 1), lootas hold a point, the walker charges, single squads retreat instead of the whole army,
  1 squad a capture, the race for the victory points, honest fog of war.
- Known weak spots: long games against space marines mostly lost, nobz rarely bought, no targeted abilities yet (grenades, hero
  abilities), the late push is weak; a generator waits too long after an enemy leaves its post.
- Check (reports/ai_doctrine/BRAIN_DESIGN_RU.md "Проверка шага 5", independent): 229 runs of d4969a0 with the brain on the
  headless bench - lib..lib7, test scenes, 63 live games, 29 multithreaded - 0 hangs, 0 Lua / SCAR errors; Brain=0 = vanilla-s9
  in 27 scenes. The one known hang (tick 3323) is the engine's melee move loop, fixed by the Melee freeze fix add-on (0.2.72) -
  recommended together. Release build on the bench (fastbench_1113, no ini Brain key, the store's EliteAI.dll): ork scenes
  r1_ork_mekboy_0676f5 / waaagh_ork_273d1f - brain on, 0 squad-seconds differ from d4969a0 with Brain=1; eldar r33_af63a8 /
  space marines r37_71f5f0 - brain off, 0 squad-seconds differ from newai-23.1; Brain=0 on the ork scene - no brain lines.

## 0.2.72 — 2026-11-13
Melee freeze fix (store moveloopfix-1, new, experiment order 1110, off by default, games elite + vanilla): MoveLoopFix.dll. Found
on the headless bench (fastbench_1113, repro hang_s5_12da6e): the game froze for good at 5:32 of a match and its memory grew
~15 MB/s until it died.
- The bug (DOW2.exe 3.19.10320, Relic's): the per-unit movement step 0x009141A0 loops while the tick's time left ([esp+0x54]) is
  > 0. An ork slugga's last melee step landed bit-exactly on a banshee's centre (x/z equal to the bit) with 0.00057 s left: state 2
  (path end, no time spent) -> 4 (EntityUtil::IsWithinDistShifted(self, target, 0) false at equal centres) -> 0 (PathHigh::FindPath
  from cell (127,134) to itself "succeeds") -> 2 ... for ever. Any melee unit can hit it (players and computers); whole human
  games never came closer than 0.57 m to a target's centre, so it is very rare.
- MoveLoopFix 1: the exit test 0x009144F3 (xorps / comiss [esp+0x54] / jae 0x0091451A) goes through a stub that counts the passes
  of one call whose time left is bit-equal to the previous pass; at 256 it writes 0 there and the loop leaves by its own exit. The
  count (per mover, 256-slot table by address) is reset at every entry of 0x009141A0 (its first 6 bytes moved into a stub), unlike
  the EliteAI prototype (fastbench fix/moveloop_guard.h), whose count could add up across calls. 24 + 56 bytes checked first,
  otherwise "NOT installed" with the bytes in MoveLoopFix.log; each patch one cmpxchg8b write. The game's thread makes no calls:
  a fire notes its tick, a side thread logs "fired (N): game m:ss (tick T)". The bytes are the same for vanilla and Elite: both run
  the one DOW2.exe (md5 488b18c4..., stands 1-3 and the Feral bundle's copy; Elite ships no DOW2.exe).
- Tests (MoveLoopFix.dll loaded as an add-on, without the EliteAI prototype):
  - repro hang_s5_12da6e on fastbench: without it the watchdog stops the game at tick 3323 (hang); with it the guard fires once
    ("game 5:32 (tick 3323) ... 0.000574 s of the tick left") and the sim goes on to tick 7889 (where the bench's missing RLink
    stops it, 0x457487, the same as in runs without any guard); CRC equal to the prototype's run up to its end (4001 ticks).
  - 10 whole human 1v1 replays (3.2.1 x4, 3.2 x6; orks, SM, CSM, IG, eldar, tyranids) on fastbench: every tick equal to the replay's
    own hashes (tick 0 of 2 replays differs with and without the DLL - the bench's known tick-0 case) and bit-identical to the
    runs without the DLL (127k ticks); the guard never fired. Vanilla replay (b2_vanilla_live, 3190 ticks): own hashes and the
    live game's CrcTrace equal, never fired.
  - Stand 1 (dow2-patches, -sync_high + CrcTrace, md5 restored): Elite, replay ae388df8 (orks x orks) 4022 ticks: own hashes 0 differ,
    the bench's CrcTrace 0 differ, "installed". Vanilla
    (no -modname, the launcher's loader), replay b2_vanilla_live 2966 ticks: own hashes and the live game's CrcTrace 0 differ,
    "installed". The CI-style build (make on rts) gave the same repro result (fires once at 3323, CRC equal).

## 0.2.71 — 2026-11-13
Match speed 3 (store matchspeed-3): the buttons were gone in a tester's observer match against computers (launcher 1.6.3, Elite
3.2.1.0.0): MatchSpeed.log showed slots 2, 3, 5, 6, 7 as type 8 (player id 0), unknown to MatchSpeed 2, which counted them as
other people and stayed out.
- What 8 is (DOW2.exe 3.19): the match start 0x0044618C builds the GameInfo slots from the session's members; a member slot of a
  person kind (0 / 3 / 4 / 5, not the local observer) whose peer is none (0xFFFFFFFF) gets type 8 (0x00446703) - nobody is
  connected behind it. Everything after treats 8 like the closed 7: no Player is made (0x004269D5: 7 or 8 -> NULL), its name
  is "missing player" (0x0044EAF8), its race is not loaded (0x00434F3E skips 7, 8, 2, 5).
- MatchSpeed 3 counts type 8 with player id 0 as an empty slot (with 6 / 7); with another id it stays "other" (no change). The
  separate check that the match session is no longer connected stays, so a real person in the match still turns it off.
- Stand 1: an observer custom game NEW AI A vs B with a stand build (-DMS_TEST8: the closed slots judged as 8) - slots 2, 3,
  5, 6, 7 = 8 exactly as in the tester's log, "ONLY THIS PLAYER", buttons at the top, x2 / x4 and a mark by its button; Elite
  player vs Elite AI with the real build: buttons, keys, marks, live vs replay sim CRC 0 differ (1540 ticks).

## 0.2.70 — 2026-11-13
Match marks (store marks-1, new, experiment, order 1100, off by default, games elite + vanilla): Marks.dll, and Match speed 2
(store matchspeed-2). AI testers 13.11: "15:26 bot 1 retreats for no reason" - one press should note such a moment so that it can be
found in EliteAI.log and in the replay (and later on the headless replay bench: replay + time); the Match speed buttons on the
minimap frame were awkward and every click on them moved the camera to the map's corner.
- Marks: a button and Ctrl+M (free in Elite's and vanilla's keydefaults.lua; Marks.ini Hotkey=) append one line to Marks.log
  next to the DLL: real time, game time (World+0x178 tick: m:ss, seconds, tick), live / replay and the .rec played
  (*(settings+0xEC4) "PLAYBACK:x.rec"; live: the game records into Playback\temp.rec), map (settings+0x3FC, its name
  *(settings+0x1AC)), players (GameInfo slots +4/+8 name, +0x10/+0x14 race, +0x2C AI difficulty, +0x58 team, +0x9C player id =
  "p1000" of the AI log; difficulty 0-2 = NEW AI A-C when EliteAI plays), camera (top of GameObj's camera stack as SCAR's
  Misc_GetCameraPos: eye +0x34, forward +0x24, the point it looks at on y = 0). Live, any match (player or observer, other people
  too: it only reads); in a live game with EliteAI.dll the mark also goes into <game>\Elite_DLLs\EliteAI.log (appended as EliteAI
  does, same real clock - no call into EliteAI). Feedback in the game's own movie: the button turns gold, "Mark N m:ss" for 3 s.
  "Send logs" of the launcher takes it (Elite_DLLs / dow2-patches *.log, launcher main.rs).
- Buttons at the top: common/topbar.h - the full-screen HUD movie resource_notifications (form vtable 0x01116478: slot 4 per-frame
  update, slot 3 input, both the game's no-ops), 1600 x 1200 with showAll (1280 x 800: 1.5 units a pixel, left edge at -160;
  the movie draws only inside its stage), a row where a replay's panel has its pause / speed buttons (that movie's 211, 58 from
  the window's top left) at their size. A click over a button is handled in the form's input slot (seen called for press /
  release over the clip), so it reaches neither the world nor the minimap: selection stays, the camera stays. Marks goes after
  Match speed's two buttons when those are shown. In a replay Marks' button sits left of the replay panel's pause button
  (playback_panel_v2 form slot 4, chained with ReplaySpeed; right of the panel the movie's stage ends).
- MatchSpeed 2: the same two buttons (replay panel textures) moved from the minimap movie to the top row (Scale default 100 = the
  replay panel's size); keys, the "nobody else" rule and the rate code unchanged.
- Stand 1 (1280 x 800): Elite 3.2.1 player vs Elite AI and vanilla player vs AI, live + replay: buttons at the top, x2 / x4 /
  pause by click, Ctrl+M and the button mark (also paused), camera the same before / after the clicks (it moves with the arrow
  keys, so the reading is live), a selected hero stays selected; sim CRC of every tick, live vs its replay (x2, x4, pause in it):
  Elite 1585 ticks 0 differ, vanilla 1597 ticks 0 differ. Replays: Ctrl+M and the panel button, the .rec name, 8x with
  ReplaySpeed. Observer custom game with NEW AI A vs NEW AI B: buttons right under the observer panel, x4 / pause / marks,
  MARK lines in EliteAI.log between the AI's lines. The final DLLs again (Elite): CRC 1635 ticks, 0 differ. A new AI match with
  the local slot handed to the AI replays with differences from tick 700 on the stand - the same without Marks / MatchSpeed (control
  run: 1079 of 1779 ticks differ from 700): the replay runs without EliteAI, not a thing of these add-ons.

## 0.2.69 — 2026-11-12
New AI 23.1 (newai-23.1): an observer's match with the new AI no longer stops in its first second - elite.scar variant_show called
Player_GetSquads(Game_GetLocalPlayer()) and an observer has no player of its own (found by the Match speed checks); now it skips.

## 0.2.68 — 2026-11-12
Match speed (store matchspeed-1, new, experiment, order 1090, off by default, games elite + vanilla): MatchSpeed.dll, speed x2 / x4
and a pause for a live match where the player is the only person (AI testers: "посмотреть битву ботов с возможностью увеличить
скорость игры x2 и x4"; a pause to look at the map).
- Speed = sim ticks per second through the sim rate interface ReplaySpeed uses (*(*(0x01335720)+8)+0x68, vtable 0x0108DFAC +0x28 /
  +0x2C): 10 / 20 / 40, nudged up to +15 % while a small shortfall remains (Elite at 60 fps: 39 -> 40 ticks/s); pause = rate
  0.0001 (the simulation and the AI stand, camera / minimap / selection work). SimulationController::Update runs at most one tick
  a frame: x4 needs 40 fps.
- "Nobody else": live game (settings+0x118 not 0 / 0x800), every GameInfo slot (settings+0x118+0x810, 0xA8 each, +0 type) is the
  local human 0, a computer 1, the local observer 2 or empty (6 open / 7 closed), and the match session is not connected; checked at
  the start and every 2 s; otherwise nothing is shown and a rate of ours goes back to 10. Seen on the stand: a private custom game
  with the host in an observer slot and computers in the team slots starts as game type 4 (the game cuts the session of a match
  with one person); replays of people's matches: remote humans are type 3 (MS_TEST build, replays of 1v1 and 3v3).
- Controls: two buttons on the left edge of the minimap frame (the minimap movie, Receive_LoadMovie of the replay panel's own
  textures hud/playback_panel/large_btn*, pause / play and the 1x / 2x / 4x icons, gold toggled_on when paused / faster), clicks by
  GetAsyncKeyState + _xmouse/_ymouse with the movie's default mouse callbacks; Ctrl+= / Ctrl+NumPad+, Ctrl+- / Ctrl+NumPad-, Ctrl+P.
  Hooks: the minimap form's per-frame update (chained with ChatHide / APM / MatchTimer) and the Menu button's (keys without a
  minimap). No Direct3D, nothing in the simulation.
- Stand 1 (12.11, 1280x800, DXVK 60 fps cap): Elite 3.2.1 player vs Elite AI: x2 20.0, x4 40.0 ticks/s; with the new AI (newai-23,
  local slot handed to the AI): x2 20.0, x4 38.9-39.6 (before the nudge); vanilla: x2 20.0, x4 40.0; observer custom game (Elite):
  clicks on both buttons, all hotkeys, x4 40.0, pause 0.0 ticks/s, the camera moves while paused, the game's own Menu still pauses
  (its dialog covers the map; an observer's Pause key does nothing). The new AI stands while paused and goes on after it (EliteAI.log
  silent during the pause, its 30-game-second telemetry 7.7 real s apart at x4). Replays of the sped-up matches: the sim state CRC
  of every tick (-sync_high + CrcTrace) equals the live match - vanilla 3005 ticks, Elite 3012 ticks (x2, x4, pause, resume, x1).
  Found on the way (not this add-on): the new AI hangs an observer's match at its first second (elite.scar variant_show:
  Player_GetSquads(Game_GetLocalPlayer()) with no local player, inside pcall) - also without MatchSpeed.

## 0.2.67 — 2026-11-12
UI Scale (store uiscale-1, new, experiment, order 1080, off by default, games elite + vanilla): UIScale.dll, the in-match interface
bigger than the game allows. No launcher change: the value is the launcher's existing UI scale slider (configuration.lua uiscale).

Case: mid++ (11.11.2026, Elite 3.2.1, cursor x2) and the owner on 4K: "UI scale" in the launcher (uiscale 75-200) does nothing in Elite.
- Cause (DOW2.exe 3.19, the same exe for vanilla and Elite): the 12 in-match interface movies (minimap, unit panel, abilities,
  resources, portraits, objectives, Menu button) take their size from 0x00B7C130(h, k) = clamp(h / 1200 * uiscale / 100 * k, 0.5,
  1.0) - never above the movies' own pixel size. 1280x800: 100 = 0.667, 150 = 200 = 300 = 1.0; 4K: 100 = 200 = 1.0 (stand, vanilla
  and Elite alike: c_v4k_100 / c_v4k_200 / c_e4k_100 / c_e4k_200 identical). So above 1200 lines the setting can only shrink the HUD.
  Not Windows (mid's game writes its logs in Documents\My Games, so the folder is not blocked; the setting is read through the
  game's own SystemConfig) and not Elite (Elite follows uiscale exactly like vanilla at 800p and at 4K, Elite 75/100/150 11.11 too). The front end (main menu, multiplayer,
  lobby) does not use uiscale at all and already grows with the screen height (4K screenshots).
- UIScale puts a jump at 0x00B7C130 (bytes checked) to a stub with the same contract (2 stack args, ret 8, the result in xmm0,
  eax/ecx/edx kept): scale = S * k, 0.25-4.0; Scale=auto (default): S = h / 1080 * uiscale / 100 (uiscale read the game's way,
  Util.dll SystemConfig::GetSetting with DOW2.exe's key 0x013AAFB4, once per start); Scale=<percent> in UIScale.ini: fixed.
  Every form has k = 1.0. Interface layout only - no simulation, sync or replays.
- Stand 1 (12.11, sess.sh, shots in /var/www/media/dow2_archive/reports/uiscale_1112/): 4K auto 2.0 (Elite and vanilla), 4K 150 /
  250 = 1.5 / 2.5, 4K auto x slider 150 / 75 = 3.0 / 1.5, 800p auto x slider 200 = 1.48, 1440p auto 1.333, 800p 75 / 100 / 150 / auto = 0.75 / 1.0 / 1.5 / 0.74,
  Enabled=0 = the game's own; Elite started from the main menu (Profiler, InjectedFiles) loads it without trouble; the minimap stays
  inside the screen at every size.

## 0.2.66 — 2026-11-12
New AI 23 (store newai-23, experiment, off by default, Elite only): eliteai branch vanilla-ours a3a378a (steps 5-8 of the vanilla base
on top of newai-22 = 53cfa69; EliteAI.dll 0.11 unchanged). eliteai/mod = mod/ of a3a378a byte for byte (the same 57 files as newai-22,
6 changed: main.ai 0.27, mechanics.ai 1.9, the served default.ai, strategy_tech_purchase.ai TECH 0.11, strategy_unit_purchase.ai
SPEND 0.14 / ENERGY 0.18 / REINF 0.8, targets.ai); the dispatchers regenerated by tools/vanilla_dispatch.py from a3a378a are the
same as the committed ones. The Elite branch of the dispatchers is unchanged (elite+ours regression check: TECH 0.7, no new lines).
- Generators on people's clock (3 by ~3:45-5:00 where there is a place), lost generators rebuilt, power points first for the
  engine and the runners when there is no place; power nodes as people build them (2 by 10:00, no re-request while one is being
  built, a razed point cooled from its first loss, a node razed while being built counts as a loss).
- Tier 3: room under the cap kept for the tier's units (Room_FullGap 20 / Room_LeadFull 240 s), the first T3 unit a median 51 s
  after the tier (newai-22 ~216 s; tyranids' doom zoanthrope +54 s); the dear unit's episode (Leman Russ, battlewagon, Baneblade;
  with a rich power bank up to eta 120 s).
- Walkers (vehicles that cannot retreat): below 0.5 health to the rear of the army or home (only within 120 m of the HQ), home
  first when the army retreats - no lone trips across the map.
- Check (reports/ai_doctrine/VANILLA_BASE_RU.md section 23, series s8_* on stand 1, 13 matches): 0 hangs / crashes / Lua errors;
  generators + nodes 609 / 809 req a player at 12:00 / 21:00 (newai-22 584 / 912); power bank at the end 664 (687); against Elite
  Normal army at the end 2.28 (2.0-2.1); walkers lost / bought 14 / 21 (newai-22 17 / 21) - still the open point; against
  elite+ours army at 12:00 0.92 (in the noise, below newai-22's 1.035). Release decided by the lead.

## 0.2.65 — 2026-11-12
Documents fix (store docsfix-1, new, experiment, order 1070, off by default, games elite + vanilla) and the early loaders' hold for it.

Case: Malum (12.11.2026) - the volume reset after every start, no replay since 30.08 (EliteAI.log: "no temp.rec of this match ...
temp.rec 2026-08-30 18:46:41"), the game kept READING Documents\My Games\Dawn of War II - Retribution, our logs in the game folder
were written. His Logfiles\warnings.txt there is the same in every bundle of 10.-12.11: a vanilla start of 09.11 17:31 - no Elite
start since wrote it.
- How the game gets the folder: Platform.dll Plat::File::Setup (0x10002335) SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0) +
  "My Games" + "Dawn of War II - Retribution" (CreateDirectoryA each), dbCrash::SetWorkingFolder; kept in Platform's buffer
  0x10015708 = Plat::File::GetPathUserStorage, which DOW2.exe imports (slot 0x00F87D08) to mount its Logfiles/Screenshots/Settings/
  Playback/Stats/Cache/Saved Games/Badges aliases once at the start (InitCoreSystems 0x0041453A) and to read Settings/Local.ini.
- common/docscheck.h (the proof, from the game process, at the start and after every match): the folder in Platform's buffer,
  Documents (usual / OneDrive / moved, ANSI round trip), attributes of Documents / Settings / Playback, when configuration.lua and
  temp.rec were written, configuration.lua opened for writing, a test file created + written + closed + deleted in Settings and
  Playback with GetLastError of the failing step, Controlled folder access (registry: policy, user setting, DOW2.exe allowed?), the
  exe's signature / integrity level / parent. "docs check (...)" + "DOCSCHECK k=v" / "DOCSEXE" lines; after a match "temp.rec
  WRITTEN / NOT written". The log summary on rts prints it on one line.
- DocsFix.dll: when the game process may not write there (or redirected before: DocsFix.txt), the game's Documents becomes
  %LOCALAPPDATA%\EliteLauncher\Documents (write-tested; 8.3 name when not ANSI-safe), the old folder copied the first time (never
  overwriting; Logfiles not), Platform's buffer and dbCrash's folder set, SHGetFolderPathA/W(CSIDL_PERSONAL) answer the new place to
  modules of the game folder (shell32 export table + loaded modules' imports) - EliteAI / MMFind / LiveReplay follow without a
  change. DOW2.exe's GetPathUserStorage and Platform's SHGetFolderPathA imports wait (max 20 s) for the decision. DocsFix.redirect
  next to the DLL for the launcher (1.5.9: hotkeys, UI scale, "Send logs"). Nothing changes when Documents is writable; no system
  setting is touched; untick = back to Documents. DocsFix.ini Mode=auto|always|never.
- vanilla-loader/winmm_proxy.c (build/early/winmm.dll of Automatch, build/launcher/winmm.dll): DocsFix.dll loaded first; when it is
  there, DllMain already points DOW2.exe's GetPathUserStorage import at a hold (max 15 s, event Local\dow2_docsfix_hold_<pid>) that
  is released once DocsFix.dll is loaded - the game cannot build its paths before DocsFix has decided, however late the loader's
  thread runs. Without DocsFix.dll nothing changes. In Elite the add-on needs this winmm.dll in Automatch (mmfind-0.51, a hand
  release); loaded later (Elite's injector) it only checks and logs "NOT redirected ... too late".
- Stand 1 (Wine 11.20, Elite 3.2.1; "may not write" = the game's folder bind-mounted read-only): writable - nothing redirected,
  temp.rec of a match written; read-only + Mode=never - "Settings FAILED at create, error 2", "configuration.lua open for writing
  FAILED, error 5", the game writes nothing there (not even warnings.txt); read-only + auto - REDIRECTED, Settings etc. copied in
  3 ms, Playback 100 MB in 79 ms, Platform's Setup and the game's first question (0.2 s) got the new place, LiveReplay / EliteAI
  too, warnings.txt and the match's temp.rec in the new place; second start - kept without a copy; Elite's injector only (9 s) -
  "NOT redirected ... too late", the game as without the add-on; vanilla with the launcher's loader - DocsFix first (0.05 s),
  redirected. Final build (the loaders' hold): Elite early winmm and vanilla launcher loader - "the early loader holds the game's
  first question" (Loader.log: "DocsFix.dll: the game's first folder question held until DocsFix.dll is in"), redirect + match ->
  temp.rec WRITTEN in the new place; Options > Audio master volume 255 -> 105, Accept, Quit: the game wrote configuration.lua
  (mastervolume = 105) into the new place on quit, the read-only Documents untouched; the new winmm without DocsFix.dll - the game
  as before; late load - "NOT redirected ... too late"; writable Documents - "nothing redirected". Every run: game files and
  Documents settings back by md5, the mount undone, the new folder removed.

## 0.2.64 — 2026-11-12
Two new add-ons from tester Orca (12.11.2026), both experiment, off by default, games elite + vanilla; one feature - one DLL.

LangSwitch (store langswitch-1, order 1050) - Alt+Shift / Ctrl+Shift / Win+Space did not switch the keyboard language in the game window.
- Cause: the game's message loop, Plat::Window::ProcessMessages (Platform.dll 0x1000B7B0), takes messages with PeekMessageA(&msg,
  <game window>, 0, 0, PM_REMOVE). Messages posted to the main thread's other windows (Default IME, the text services' hidden
  windows) and to the thread itself are never taken out nor dispatched (proven on the stand, below); Windows 8+ does the language
  switch through the text services of the focused thread with such messages (the likely cause - Wine has no such switch to show it). Alt-tab works because the layout is then SENT to the game on activation.
  The window procedure chain is not the cause: Platform 0x1000B190 -> DOW2.exe 0x0067F7F0 -> Win32InputDevice 0x10003210 -> class
  proc 0x1000B380 pass WM_INPUTLANGCHANGEREQUEST / WM_INPUTLANGCHANGE to DefWindowProcA (Spooge's subclass passes everything). The
  chat follows the thread's layout by itself (TranslateMessage -> WM_CHAR -> 0x0067F7F0 -> CP_ACP -> UTF-16 Scaleform char event).
- LangSwitch.dll: Platform.dll's PeekMessageA import wrapped (ProcessMessages' bytes checked): before the game takes its next
  message, the ones queued for other windows of the thread / for the thread are taken out and dispatched (max 64 per call; WM_QUIT
  and the game's own messages and their order untouched). Fallback: a clean Alt+Shift / Ctrl+Shift that Windows has as the
  language / layout hotkey (HKCU\Keyboard Layout\Toggle) and no layout change in 500 ms -> WM_INPUTLANGCHANGEREQUEST(next
  layout) to the game window, then ActivateKeyboardLayout; the log says which way it went.
- Stand 1 (Wine 11.20): probe build (-DSTAND_PROBE) with a hidden window of the game thread fed from another thread every 200 ms:
  pump off 0 of 300 messages dispatched, 0 of 300 thread messages taken out (= the bug); pump on 599 of 600 / 599 of 600 (the last
  in flight). Alt+Shift and Ctrl+Shift in the game recognised (menu and match), nothing to switch to under Wine (one layout).
  Vanilla and Elite, -dev skirmish and normal launch through the main menu (Custom game -> match -> Exit -> post-game): no crash.
  The real switch and typing Russian in the chat can only be checked on Windows.

NoHints (store nohints-1, order 1060) - the beginner hints of a match ("Select a unit", "Build a generator", victory points, "Spend
your Resources", population cap, WAAAGH/faith bar).
- They are simulation/scar/skirmishquicktips.scar, started by Skirmish_Start (skirmish.scar, vanilla and Elite) only when
  Misc_QuickTipsEnabled() - DOW2.exe 0x00AFE940 = SystemConfig "ShowQuicktips" (configuration.lua showquicktips, the in-game Options
  radio of ingame_game_options.gfx): a per-player setting, the tips run for the local player only.
- NoHints.dll: "xor eax,eax; ret" over the first 3 bytes of 0x00AFE940 (its 26 bytes, the registration at 0x00B023F9 and the name
  checked first). No archive touched (data checksum, Join unchanged), the player's setting not written.
- Stand 1: SCAR state every 10 s: without NoHints enabled=true, the select-squad hint at 20 s (screenshot: "Select a unit", "Select
  one of your available units."); with it enabled=false, the tips never initialised, no cues (vanilla and Elite, also a
  menu-launched Elite match with 10441 requisition: no "Spend your Resources"). Sync: replays crossed both ways (recorded with tips,
  played with NoHints; recorded with NoHints, played without) - SCAR state of both players every 5 s identical to the live game,
  23/23 lines, vanilla and Elite.

## 0.2.63 — 2026-11-12
New AI 22 (store newai-22, experiment, off by default, Elite only): the new AI moves to the vanilla base ("vanilla + ours", eliteai
branch vanilla-ours 53cfa69 = be78608 + the default below; EliteAI.dll 0.11 unchanged).
- Package: EliteAI/simulation/ai = 53 generated dispatchers (tools/vanilla_dispatch.py): `if EliteAI_VANILLA then <vanilla 3.19 AI + Elite
  data compatibility + our SPEND / TECH / ENERGY> else <Elite 3.2.1 / the released purchase files> end`. The served default.ai carries the
  decision, the people's plan (data/human_plan.ai: tier windows and openings from 1v1 replays of strong players), main.ai 0.23 and
  mechanics.ai 1.5 (the DLL serves it as it is and appends nothing). main.ai, mechanics.ai and data/ are shipped as the source of what
  is inlined; append/scar/elite/elite.scar - World_Pos only for move / attack-move orders.
- Mode: [Lua] NewAI of EliteAI.ini (the launcher neither ships nor touches it). No key and the old forms (normal | all | ids) now play
  vanilla+ours on the Easy / Normal / Hard slots = NEW AI A / B / C; Insane stays Elite's AI. elite+ours[:who] = the newai-21 base, for tests.
  The vanilla base sees Hard (VanillaDiff), the variant comes from the slot as before.
- Stand check of the base (reports/ai_doctrine/VANILLA_BASE_RU.md section 15): 7 matches ~22 min, no hang, no Lua error; mirrors against
  newai-21 played both ways round: army at 12:00 ~1.0, 40 % fewer squads lost, tier-3 units bought, vehicles lost in the army's retreat 6 vs 23.
  Install check as a player (files laid out by this addons.json, main menu skirmish, NEW AI B): see the PR.

## 0.2.62 — 2026-11-12
Ctrl attack ground 2 (store ctrlattack-2, experiment, Elite and vanilla): CtrlAttack.dll 2.
- Owner: Ctrl + right-click is Reverse's (backing off) - attack ground goes to Ctrl + LEFT click on the ground. The right click
  is not touched any more (CtrlAttack 1's DefaultAction hook is gone).
- How: a WM_LBUTTONDOWN with Ctrl (game window subclassed), a selected squad that can (the answer of BindingAttackGround's own
  per-frame update 0x00B703B0, vftable +0x0C wrapped), no targeting mode and no unit within 3 m of the cursor's ground point ->
  the game's own "enter targeting" 0x00AD5B50 for Attack ground, exactly what G does (0x00B73570), before the game sees the
  click; the click then goes to Attack ground (0x00B70610: SCMD_Attack with the point, Shift = queued). Still in the mode 0.4 s
  after the button is up (no order, e.g. inside the minimum range): left the way 0x00AD59C0 leaves it (hooked there).
  Ctrl + left click on a unit or with no squad that can: the game's own click. Replays: nothing.
- Stand 1 (vanilla): Ctrl+LMB ground -> wraithguard attacks the point; RMB move then Ctrl+Shift+LMB -> moves, then attacks;
  Ctrl+RMB behind the fire prism -> Reverse backs it off; the replay without DLLs gives the same SCAR log as the live game.
## 0.2.61 — 2026-11-12
New AI 21 (store newai-21, experiment, off by default, Elite only) = newai-20 + army tempo and trading, real purchases
(main.ai, mechanics.ai, simulation/ai/strategy_unit_purchase.ai, strategy_tech_purchase.ai, append/scar/elite/elite.scar from eliteai e141310; EliteAI.dll unchanged).
- Tempo: a population target (30 + 5 per minute up to 80 % of the cap); below it the next unit goes first in every variant.
- Trading: a march is judged before it starts (strength at the goal and on the way, allies counted; Conf_Engage A/B/C), a fight on the spot
  (leave at 1.4 against us), the army gathers before a fight; no walking back into the same force after a retreat.
- Extra generators and nodes only when power is short; not rebuilt on a timer after the enemy razes them; T2 from 250 s, T3 from 13:00.
- A spawn request is not a purchase: confirmed by the bank or the task, unconfirmed ones paused (no repeated orders the engine refuses);
  the price of a node on its way is reserved; the answer to the enemy army is picked first among the variants.
- Map: a free point within 40 m of the army is taken by one squad; a second runner at 6 squads on a quiet map; hard per-type cap in every path.

## 0.2.60 — 2026-11-12
Speech streak fix (store speechstreakfix-1, new, experiment order 1040, off by default, games elite + vanilla) - TvangeS's crash of 11.11.2026
(Access Violation DOW2.exe 0x00B8FC24, read 0x84B23004, 3v3 automatch at 1:34; report reports/crash_tvanges_1111).
- The cause is DOW2.exe 3.19 itself: the unit speech counts kills per hero entity ("Killing_Streak" of speechtuning) in a sorted
  vector map {Entity*, count} (listener +0x24/+0x28). FUN_00B8FB80 starts from end() and reads it->count also when equal_range
  (0x815E30) found nothing - 4 bytes past the buffer for an entity's first kill when the list is not empty (e.g. a hero bought back
  = a new entity). Mostly garbage; a crash when the buffer ends at an uncommitted page (the dump: begin 0x84B22FF8, end = capacity
  = 0x84B23000). None of our DLLs was on the crashed thread's stack; FastBuffers/FastLocStrings do not touch this memory.
- SpeechStreakFix.dll: 4 bytes at 0x00B8FC1C, "je +2; mov ebp,eax" -> "mov ebp,eax; je 0x00B8FC2D" (not found = 0 kills, nothing
  read); one aligned store after checking the 13 bytes 0x00B8FC18..24, another build is left alone (logged). Speech only, no sync impact.
- Stand 1: SM vs IG skirmish (AI off), waves of enemy squads at four lanes, the hero replaced by a new entity at 80 s (as a hero
  bought back). Only hero entities are counted (health_ext +0x23C). Probe build (-DSTAND_PROBE, same logic + counters + killer's
  blueprint): the first sm_force_commander 1 first kill on an empty list + 2 known, the new hero entity 1 first kill ON A NON-EMPTY
  LIST (= the read past the list the fix removes) + 13 known (it counts from 1), no crash. Release DLL, Elite and vanilla: patched (13 bytes read back from the
  process: 3B 44 24 14 8B E8 74 0D 85 ED 74 09 8B), same scenario, no crash. Replays of the Elite and the vanilla match played back
  without the DLL: SCAR state (both players' squads, positions, health, req) every 5 s identical to the live game (33/33 lines each).
  Normal launch through the main menu (Steam profile), Elite (Profiler InjectedFiles) and vanilla (winmm loader): patched, menu up,
  alive 3 min. Unit tests under Wine: other bytes / no code at 0x00B8FC18 -> logged, nothing written.

## 0.2.59 — 2026-11-12
- Sound bug test removed from the launcher (store version "off", out of the list): its job is done; the game gets the normal sound library back.
- PerfLog off for everyone (store version "off"): the performance measurements are closed.

## 0.2.58 — 2026-11-11
Ctrl attack ground (store ctrlattack-1, NEW, experiment, order 1030, off by default, Elite and vanilla): CtrlAttack.dll (ctrlattack/).
- Owner's request: Ctrl + click = attack the ground at a point (wraithguard and many other squads can), only while Ctrl is held.
- The game has it: the core action Attack ground (BindingAttackGround, hotkey G) sends WorldDoCommandSquads(SCMD_Attack = 48) with a
  position to the selected squads passing 0x00B70240 (a weapon with attack_ground = true) and the min-range check 0x00B70290. The DLL
  hooks CommandProxy::DefaultAction (vftable slot +0x74 of 0x0110E514 and 0x01117704): Ctrl held + right-click on the ground -> the
  capable squads get that same order (Shift = queued), the others the plain right-click through 0x00AF62E0 - Reverse.dll's hook there
  still backs them off; vehicles stay with Reverse when it is on with the same key. Nothing in replays; no simulation state touched.
- Stand 1 (vanilla and Elite, Eldar): Ctrl+RMB -> wraithguard holds SCMD_Attack and fires the wraithcannon at the bare ground
  (screenshots: lightning and crater every ~5 s), without Ctrl it walks there; Shift+RMB move then Ctrl+Shift+RMB -> moves, then
  attacks; fire prism alone -> Reverse backs it off (heading kept); mixed selection -> each its own. Replays of the matches (without
  the DLLs) give the same SCAR log (squads, commands, positions, health every 0.5-5 s) as the live games, vanilla and Elite.
- Survey of the game's own check on every pvp squad but heroes (default loadout, stand): vanilla 19 of 83 squads can, Elite 47 of 220;
  more get it with a weapon upgrade (missile launchers, flamers, autocannons...) - the check is live.
## 0.2.57 — 2026-11-11
New AI 20 (store newai-20, experiment, off by default, Elite only) = newai-19 + EliteAI.dll 0.11; the AI scripts are newai-19's, unchanged.
- The replay of a match with the new AI goes to the replay site by itself (MMFind sends only matches with other people): back in
  the menu after a single-player match in which a new AI played, Playback\temp.rec goes to `/replay/api/upload` as
  `newai_<race>-<variant>[_...]_<date>.rec`, `X-Lobby: newai`. Not for replays (also not a replay taken over with ReplayTakeover).
- The mirrors (rts, dow2, dow2ru) are checked first, all at once, 2.5 s at most (`GET /replay/api/by_lobby/<x>`, the site's `{}`);
  the upload goes to the first that answered, then the next that answered; its own thread, nothing waits for it, 4 minutes at most.
- The game closed on the score screen or during the upload: `Elite_DLLs\EliteAI.replay` keeps the match, the next start sends it.
- temp.rec missing or older than the match: EliteAI.log gets the Documents path the game itself gets (SHGetFolderPathW), whether
  Playback is there, temp.rec size/time, the newest .rec there, and %USERPROFILE%\Documents / OneDrive\Documents when they differ
  (the start of the game logs the first part too).
- `EliteAI.ini [Replays] Upload=0` turns it off, `Server=host;host` sets the mirrors; -dev runs (our stands' AI series) send only with `Upload=2`.

## 0.2.56 — 2026-11-11
New AI 19 (store newai-19, experiment, off by default, Elite only) = newai-18 + map defence, tyranid play, economy and purchase
(main.ai, mechanics.ai, simulation/ai/strategy_unit_purchase.ai, strategy_tech_purchase.ai, append/scar/elite/elite.scar from eliteai 22ef1c2).
- Generators: our own plan over Elite's personality cap (tyranids had 1/2/3 per tier): 2 at once, +1 every 150 s, at least 4 on T2 and 6 on T3;
  power-node requests only for own power points, judged after 25 s; builders are not pulled away while a request is open.
- Purchase: the answer to the enemy army is really bought (Demand_Gamma, COUNTER force), at most 3 of a kind and 2 of a vehicle/expensive unit;
  the new tier's units are preferred after a tier-up; no unit request within 8 of the population cap (phantom buys).
- Reinforcements by the squad's worth: runners/cappers to 0.6, bleeding squads late in a lost game to 0.5-0.75; expendable broods only from spare req.
- Map: own threatened or lost points (power first) before the enemy base; 1-2 squads capture while the army covers or goes on; no orders into avoided turret zones.
- Tyranids: tyrant guard against light infantry and set-up teams, warriors' suppression and the tyrant's bioplasma as counter upgrades,
  lictor as a raider, lictor jump and alpha hook abilities.

## 0.2.55 — 2026-11-11
Elite maps 3 (store elitemaps-3, experiment, off by default, vanilla only) - owner: Elite's maps in public rooms too ("only
friends could test it otherwise; we kick strangers or pick the map once people are in").
- The public-room guard of EliteMaps 2 (0x00487A7B hook) is now only EliteMaps.ini [EliteMaps] BlockPublic=1; by default
  no hook, Elite's maps in every room as in EliteMaps 1.
- Card text: in any room; only with players who have it on; a player without it joining a room with an Elite map already
  picked crashes - pick it once everyone is in (or kick); with such a player already in the lobby an Elite-map match does not start.

## 0.2.54 — 2026-11-11
Elite maps 2 (store elitemaps-2, experiment, off by default, vanilla only) - back after 0.2.53 switched it off.
- Why it was off: a vanilla player WITHOUT the add-on who joins a lobby whose map he does not have crashes at once
  (c0000005 at DOW2.exe 0x00470581: the map info at GameInfo+0x50 is NULL, read +0x54; twice on the stands, 11.11). A guest
  already in the lobby when the host switches to an Elite map does not crash, but the match never starts (host "Waiting for
  players"). Anyone can walk into a public room from the games list (a stranger did, within 2 minutes, on a stand room).
- EliteMaps 2: no Elite map in a public room. The game setup's map handler 0x00487A7B(form, index) (map list form+0x64..+0x68,
  0x7AC bytes per map, scenario name at +0x280; it also gives a new room its first map, the last used one) is hooked (5-byte jmp,
  prologue checked); in a public room (GameInfo +0xC4 = 1, private 0 - diffed on the stand) a pick of an Elite map (names read
  from the archives' tables: 60 pvp + 2 FFA) becomes the room's previous vanilla map, or the first vanilla map for as many
  players; the dropdown shows the vanilla map. Private rooms keep Elite's maps. No guard (other DOW2.exe) = no Elite maps.
- Stands (11.11, cherpekat host + pivolan guest, vanilla, no -dev): public room created with the last used map Bay of Grots ->
  Calderis Refinery / Ashes of Typhon, picking Bay of Grots there is turned back (log "public room: Elite map ... not allowed"),
  a guest without the add-on joins that public room fine; private room: Bay of Grots allowed, a guest WITH the add-on plays it
  ~1.5 min (2 humans, drop=0, no sync error); a guest WITHOUT it still crashes joining the private room (stays, the card says so).

## 0.2.53 — 2026-11-11
Elite maps (vanilla) turned off for everyone (store version "off"): a vanilla player WITHOUT the add-on who joins a lobby whose host
has an Elite map selected crashes (stand test with two accounts, 11.11). Back after the DLL keeps Elite maps out of public lobbies.

## 0.2.52 — 2026-11-11
Overwatch once (store overwatchonce-1, experiment, order 1020, off by default, Elite and vanilla): OverwatchOnce.dll (overwatchonce/).
- Owner's request: a right-click (overwatch) on a unit button kept building that unit forever (players got a second batch of
  tankbustas/dreads by accident). Now it orders one squad: built when affordable, then the overwatch entry goes away.
- Overwatch is the HUD's only (no command, nothing in the simulation): map { building id -> squad blueprint } in the HUD overwatch
  object (*(*(0x01335720)+8)+0xCC, +0x0C); its tick 0x00B26890 (called from 0x00ACBFB6) sends the plain CMD_BuildSquad
  (CommandProxy +0x0C 0x00AF4380) when the building can build it. The DLL wraps the call, notes the tick's build order (return
  0x00B26AD8) and, once the production queue (EntityController::QI(7)) has the squad, erases the entry with the game's own erase
  0x00B26BD0 (the second right-click's) and redraws the tech tree (BindingBuildTech +0x18, +0x1ED). Bytes/slots checked first.
- Stand 1 (vanilla and Elite, SM vs AI, overwatch on tactical marines, 600 req given 4 times): without the DLL 4 squads, with it 1,
  erased 100 ms after the order; replays of the matches with and without the DLL give the same SCAR state every 5 s (both players'
  squads, positions, health, resources) as the live game.
## 0.2.51 — 2026-11-11
Elite maps (store elitemaps-1, NEW, experiment, off by default, VANILLA only - `games: ["vanilla"]`): EliteMaps.dll.
- Vanilla Retribution gets Elite's maps: DOW2.exe's import of ConfigFile::GetString answers the missing archive.NN keys of the
  module's [data:common] with Elite\Archives\Elite_Data_21_Maps_01.sga, Elite_Data_22_Maps_02.sga, Elite_Data_20_UI_Maps.sga
  of the player's own Elite installation (after the game's archives = lowest priority, no vanilla file replaced). No big download
  (the archives are already on the disk), nothing written to DOW2.module / DOW2Multiplayer.module: untick = plain vanilla.
- Stand 1 (cherpekat, vanilla 3.19 + vloader-2): hooked 49 ms after the start, archives mounted at ~170 ms (archive.29-31 of
  DOW2.module); the data checksum stays vanilla's 371005917 (0x007A0A30 = the CRC blocks + maps\pvp\_cameras only); the maps
  are in the custom game's map list; a private online game (no -dev) on (2p) Bay of Grots vs the computer loads and plays, the
  lobby's requests to Relic carry dataChecksum=371005917; -dev skirmishes vs AI with the 6 vanilla races spread over all slots:
  all 60 Elite pvp maps load (LOAD_Done, game alive 20 s later), no error line beyond the vanilla control map's except the
  harmless "Health command ... doesn't have a HealthExt" of 3 props (6p_ashesarena, 6p_ulthanetemple). The 2 FFA maps
  (maps\freeforall) are not offered by vanilla ("invalid MP scenario"). Without the add-on an Elite map is "invalid MP scenario".
  Not tested: a second real player in the lobby (stand 2's Steam is offline).
- Limits: vanilla has no map download - a new map only in a lobby where everyone has it on (and the same Elite version);
  the automatch and the vanilla maps are unchanged. Under Elite (-modname) the DLL does nothing.

## 0.2.50 — 2026-11-11
New AI 18 (store newai-18, experiment, off by default, Elite only) = newai-17 + safer SCAR-to-AI path (elite.scar from eliteai 3550da8).
- elite.scar no longer calls AI_DoString by default (the enemy squad names every 2 s, the dodge note): the only place where our
  scripts crossed from the SCAR state into the AI states, a suspect of a rare stand hang. The AI reads the names itself
  (Squad_GetBlueprintName); the grenade dodge works without the note. Switches: CounterScarNames=1, DodgeTell=1.

## 0.2.49 — 2026-11-11
New AI 17 (store newai-17, experiment, off by default, Elite only) = newai-16 + no retreat for suppression alone
(mechanics.ai from eliteai df2d0f5).
- Inside an enemy turret zone a suppressed squad walks out; it retreats only when deep in the zone or hurt (squads pressed retreat
  at every banshee scream or ranger volley near the enemy base).

## 0.2.48 — 2026-11-11
New AI 16 (store newai-16, experiment, off by default, Elite only) = newai-15 + answer to the enemy army, spending fixes
(main.ai, mechanics.ai, simulation/ai/strategy_unit_purchase.ai, strategy_tech_purchase.ai, append/scar/elite/elite.scar from eliteai ad510cb).
- Counter layer: seen enemy squads (fog of war honest, AIPlayer_CanSeeSquad) are classed by blueprint name (light infantry, swarm,
  heavy, vehicles, melee heroes, jump, set-up teams, turrets) and remembered 120 s; answers of the own race and anti-vehicle upgrades
  get more demand (Conf_CounterVsPlan A/B/C); detectors and set-up teams are focus targets; melee heroes are stunned/slowed early.
- Turret siege: the decision is held, units that outrange a turret fire from outside its range (manticore loop fixed).
- Set-up teams: a grenade/stun squad walks into throw range of any seen set-up team; damaged light vehicles pull back by damage rate.
- Spending: upgrades no longer bypass the drought (variant C), one wargear item per slot, a unit within 15 s goes first,
  director reinforcements obey the drought; grenade dodge ignores shells seen too late.

## 0.2.47 — 2026-11-11
New AI 15 (store newai-15, experiment, off by default, Elite only) = newai-14 + grenade dodge
(main.ai, mechanics.ai, simulation/ai/strategy_unit_purchase.ai, strategy_tech_purchase.ai, append/scar/elite/elite.scar from eliteai 6592623).
- Grenade dodge in elite.scar (4 times a second): a thrown grenade or a fused one on the ground near a New AI squad - the squad steps just out
  of the radius after a human-like 0.5-1 s and goes back to what it did; vehicles, garrisons, retreating and suppressed squads stay;
  a weak grenade never pulls a squad off a capture (Conf_DodgeBusy A/B/C for capture and melee). Seen mines are noted. Switch: Mech_Dodge=0.
- Turrets: one shortest way out of all zones, a squad under fire retreats (suppressed, deep in the zone, below 0.75 health).
- Order ceiling: a standing squad gets another order instead of waiting the window out.
- Race call-ins without a fight land next to the army; RaceAbilities watch line logs why a call-in is not available.

## 0.2.46 — 2026-11-11
New AI 14 (store newai-14, experiment, off by default, Elite only) = newai-13 + base defences, vehicles, set-up teams
(main.ai, mechanics.ai, simulation/ai/strategy_unit_purchase.ai, strategy_tech_purchase.ai, append/scar/elite/elite.scar from eliteai 6caf4bc).
- Base turrets: found from the entity list (or a 55 m zone at an enemy HQ); entered only with a 1.5 lead (1.1 with vehicles, anti-armour
  or artillery), which hit the turret itself; otherwise squads leave the zone and the army picks another goal; losses there make it "stronger".
- Vehicles: a damaged one (below 0.4) moves back behind the infantry, one ahead of the army comes back to it.
- Set-up teams and support heroes: focus priority, grenades, jump and melee from the flank; anti-set-up units wanted against 2+ set-ups.
- Call-in or unit decided before the unit buy; tech-vs-army decision held per tier; an unbuildable building request no longer locks the bank
  (pauses 120-600 s); builders are left alone; refill stops when no models arrive; SCAR order mode 7 (attack a building).

## 0.2.45 — 2026-11-11
New AI 13 (store newai-13, experiment, off by default, Elite only) = newai-12 + race abilities back, safer runners, reinforcements
(main.ai, mechanics.ai, simulation/ai/strategy_unit_purchase.ai, strategy_tech_purchase.ai, append/scar/elite/elite.scar from eliteai 8affec3).
- Race call-in abilities count as a unit buy through another channel: cast into the army's fight, on a raid at the base or power points,
  or when the unit is far; save holds at most 90 s; abilities with no target are not picked. Squad buffs (no script way to target) removed.
- Runners: none for a small army (Conf_RunnerArmy A/B/C), no target where the enemy is or was seen 45 s ago or on the path,
  a target where a runner died is closed for 150 s, own lost points near the army first; the hero is a runner only in the opening.
- Reinforcements: weak squads out of combat go home to refill, one model a minute always passes (expensive Grey Knights models),
  squads with ordered models wait for them and are not taken as runners; dir_lost counted from the squad standing.
- Tech by power float yields to a unit waited for and within 75 s of income; gen book counts generators that stood 20 s.

## 0.2.44 — 2026-11-11
New AI 12 (store newai-12, experiment, off by default, Elite only) = newai-11 + unit-first spending
(main.ai, mechanics.ai, simulation/ai/strategy_unit_purchase.ai, strategy_tech_purchase.ai, append/scar/elite/elite.scar from eliteai ac0be55).
- The next unit of the plan is claimed before the pick (EliteAI_PlanClaim): upgrades, tech, reinforcements and race abilities see it.
- Race abilities do not take the money of a waiting unit (Conf_AbilityVsUnit A/B/C); a held ability is no longer cast under save.
- Reinforcements yield to a unit within 45 s (except weak squads and squads in a fight); save starts after 45 s short of a unit.
- Spend books count paid amounts (reinf by arrived models, gen by live tasks); the upgrade share uses them.
- Refill at home: a squad down to a third waits for its models (up to 3 per tick), no early return with one model.

## 0.2.43 — 2026-11-11
New AI 11 (store newai-11, experiment, off by default, Elite only) = newai-10 + spending fixes
(main.ai, mechanics.ai, simulation/ai/strategy_unit_purchase.ai, strategy_tech_purchase.ai, append/scar/elite/elite.scar from eliteai b5f0320).
- Upgrades capped by a share of the req spent on the army (0.15 / 0.20 / 0.30 per tier, halved while the army is small or behind).
- Dead upgrade requests are no longer re-placed every tick (they held money and the request slot); the log counts real buys only.
- The director reinforces its own squads at base (SCAR SCMD_ReinforceUnit, mode 6) within the tech reserve and half the req income;
  a squad down to a third of its models goes home to refill. Switches: Reinf_Dir=0, RetreatModels=0.
- Tier research only as the tech plan wants it (no early T3 from Elite's upgrade demand); T2 earlier.
- No pcall around engine calls in main.ai and strategy_unit_purchase.ai: explicit checks and direct calls.

## 0.2.42 — 2026-11-11
New AI 10 (store newai-10, experiment, off by default, Elite only) = newai-9 + fixes found by the scene test bench
(main.ai, mechanics.ai, simulation/ai/strategy_unit_purchase.ai, strategy_tech_purchase.ai, append/scar/elite/elite.scar from eliteai 48765e1).
- Idle squads: only squads that can capture get the capture role; a squad idle 12 s out of combat goes to the army or the next goal.
- One order gate for the director and the mechanics: the same order is not repeated within 12 s, no attack-move loops.
- Garrisoned enemy on a point is detected (Conf_Garrison A/B/C: assault / hold / bypass); on assault grenade carriers walk into
  throw range and the building is attacked; counter units are wanted when the garrison holds.
- Floating power goes into the tier research first (reserve kept from unit buys and reinforcement saving); power-heavy picks ranked up.
- Conf_AddonVsUnit arbiter with hysteresis: upgrades do not drain the bank right before an affordable unit.
- Secure (power node) requests: one at a time, a pause after two dropped ones; the budget log counts real spends only.

## 0.2.41 — 2026-11-10
New AI 9 (store newai-9, experiment, off by default, Elite only) = newai-8 + reinforcement budget and spending gates in
simulation/ai/strategy_unit_purchase.ai, main.ai (Conf_ReinforceVsSave), strategy_tech_purchase.ai (EliteAI_TechReserve) (eliteai f16a17d).
- Reinforcement budget (REINF): while power piles up and req is short for the next tier or a power unit, cheap squads are reinforced
  only by the variant's rule (A: weak and out of a fight, B: weak, C: in a fight or below full HP) and within a share of the req income
  (0.2 / 0.35 / 0.5), squads with power included. REINF lines in EliteAI.log.
- Forbidden blueprints (cap 0, super units before their time/army) are dropped before the demand, in the watchdog and in the purchase multiplier (GATE drop).
- The idle-bank watchdog buys over the money other budgets hold (abilities, reinforcements), but not over the tier research the tech plan holds.
- Not run in game after the last change (static check, luac, spend_stub). Elite's AIs run Elite's code.

## 0.2.40 — 2026-11-10
New AI 8 (store newai-8, experiment, off by default, Elite only) = newai-7 + spending in simulation/ai/strategy_unit_purchase.ai.
- Idle-bank watchdog (SPEND): when the bank stays above 400 req or 300 power without a spawn, the new logic buys the best affordable unit;
  with only power piling up it buys units with >= 40 power (T2/T3, vehicles), not 0-power T1. SPEND lines in EliteAI.log.
- While power >= 300, units that use < 40 power (guardsmen, heavy weapons, termagaunts) get x0.25 demand so the req goes to power units.
- Not run in game after the last change (static check, luac, spend_stub). Elite's AIs run Elite's code.

## 0.2.39 — 2026-11-10
New AI 7 (store newai-7, experiment, off by default, Elite only) = newai-6 + simulation/ai/strategy_tech_purchase.ai
(EliteAI.dll gives it to the game instead of Elite's script, like strategy_unit_purchase.ai).
- Tech plan for the new logic: tier 2 from 7 min (earlier with a rich bank), tier 3 from 15 min; the research money is reserved
  first, the army buys from the surplus; arbiter Conf_TechVsArmy (A/B/C), TECH lines in EliteAI.log. Elite's AIs run Elite's code.
- Unit purchase: a forbidden unit (tyranid spore mines, IG spotters) no longer cuts off the rest of the HQ list; new-tier bonus; tier in the buy log.
- Stand 1, one 2v2 (tyranids + IG on the new logic vs SM + eldar on Elite, 24 min game): T2 at 6-7.5 min, T3 at 10-14.5 min,
  no hangs, PCALLERR 0.

## 0.2.38 — 2026-11-10
New AI 6 (store newai-6, experiment, off by default, Elite only) = newai-5 + simulation/ai/strategy_unit_purchase.ai
(EliteAI.dll gives it to the game instead of Elite's script: Elite_DLLs\EliteAI\simulation\ai\...).
- Unit purchase: diversity demand (a more varied army), super units (Baneblade etc.) gated by game time (not before 18 min) and army size.
- Lua syntax checked (luac 5.1); the file loads without errors on stand 1. No new stand runs for this package.

## 0.2.37 — 2026-11-10
New AI 5 (store newai-5, experiment, off by default, Elite only): EliteAI.dll splices mechanics.ai after main.ai; main.ai 0.10, mechanics.ai 0.2, elite.scar.
- mechanics.ai: 8 combat mechanics of the director (FocusFire, ArmyRetreat, PreRetreat, PunishTurn, MeleeTie, Repair,
  RaceAbilities, ArmyRoles) and 11 conflict arbiters Conf_* with hysteresis (a decision holds 6 s, every switch logged as
  `MECH Conf_<name> <who> X->Y <why>`). No pcall around engine calls; no engine queries on enemy squads beyond position/health.
- Variants A/B/C: same parameters, all mechanics on; they differ ONLY by Conf_* (A = rule 1, B = 2, C = 3). EliteAI.ini [Lua]
  still overrides any Conf_* / Mech_* key.
- elite.scar: order modes 4 (attack squad), 5 (capture point), 100+k / 200+k (squad ability at a position / on a squad).
- Verified on stand 1 (Elite 3.2.1, 4 matches ~27 min game time, 1v1 and 2v2, IG/Tyranids/Eldar/SM/Orks): no hang, no crash,
  PCALLERR 0; fixed before the release: runner loop on neutralised points, squad-targeted global abilities holding the money,
  three ability paths. Stand md5 back.

## 0.2.36 — 2026-11-10
Replay takeover 3 (store replaytakeover-3, experiment, off by default) + New AI 4 (store newai-4: EliteAI.dll 0.10, scripts unchanged).
- ReplayTakeover.ini Others = elite (old "ai") | newai_a | newai_b | newai_c | idle. The new AI picks its variant by the
  computer's difficulty (Easy = A, Normal = B, Hard = C), so newai_* gives the former humans' computers that difficulty
  (Player+0xBC, GameInfo slot +0x2C) instead of Difficulty. EliteAI.dll not loaded or EliteAI.ini [Lua] NewAI=none -> a line
  in ReplayTakeover.log and Elite's AI at Difficulty. Computers of the match keep their own AI (made by the replay).
- EliteAI.dll: the game type is already 4 (skirmish) when ReplayTakeover makes the computers, so their AI scripts get main.ai
  as in a skirmish; but the replay loaded SCAR as playback (0x800), without the order-queue rule. With ReplayTakeover.dll in
  the game a replay's SCAR now gets EliteAI\append too (the rule takes an empty queue in a plain replay - no commands); the
  AI scripts of a replay stay Elite's.
- Stand 1 (Elite 3.2.1, rt_h2h321, slot 4 taken at tick 120, Others=newai_b, 3 min): EliteAI.log `variant B retreats`,
  kicker "New AI - variant B (imperial_guard)", director orders 2 -> 37, points taken, no crash; stand md5 back.

## 0.2.35 — 2026-11-10
New AI 3 (store newai-3, category experiment, order 1000, off by default, Elite only; EliteAI.dll + main.ai 0.9 + elite.scar).
- Three variants of the new AI to compare, picked in the skirmish setup by the computer slot's level: Easy = "NEW AI A",
  Normal = "NEW AI B", Hard = "NEW AI C" (Localizer labels 555026-555028 / 555035-555037, single player only); Expert stays
  Elite's AI. All three play with the standard personality and build orders (AIPlayer_GetDifficultyLevel -> Standard);
  variants can be set against each other in one match.
- Variant table at the top of main.ai (EliteAI_VARIANTS): A "focus + purchase", B "retreats" (earlier retreat, short fights,
  one attack target), C "attack" (holds fights longer, three targets, higher inertia); Mech_* switches for the mechanics
  module (not in this release - they do nothing yet). EliteAI.ini [Lua] still overrides any value; Variant / Variant<player id> for tests.
- At the start of the match a kicker text over the player's first squad: "New AI - variant B (orks)"; EliteAI.log: `variant B`.
  (Game_ShowSystemMessage is not used: it pauses the simulation.)

## 0.2.34 — 2026-11-10
New AI 2 (store newai-2, category experiment, order 1000, off by default, Elite only; scripts only, EliteAI.dll unchanged).
- Fix for the freeze of AI-vs-AI matches (2-4 min into a 2v2 on stand 1): the combat director read the health of a squad
  that was not spawned (AISquad_ConvertToSimSquad -> nil, Squad_GetHealth(nil) raised a Lua error). The error, raised
  inside the AI job on an xthread worker, left the engine's Lua mutex (LuaBinding::GetMutex) locked; pcall caught the
  error but the main thread then waited for the mutex forever. Squads are now checked before any engine call
  (converted squad exists, has models; position only for squads with models; order only with a squad and a position;
  SCAR side: no order for a squad without models).
- Every caught error is written to EliteAI.log as `PCALLERR <place> <count> <text>` (first 5, then every 50th).
- main.ai script version 0.8.

## 0.2.33 — 2026-11-10
New AI 1 (new, store newai-1, category experiment, order 1000, off by default, Elite only; EliteAI.dll 0.7 + eliteai/mod/main.ai,
append/scar/elite/elite.scar). The store id is new (`newai`); the old `eliteai_off` stays off.
- Same source as the store EliteAI 0.7; Elite 3.2.1 AI scripts and ai_tuning are byte for byte those of 3.2, so nothing to port.
  On 3.2.1 the lua_load and Localizer hooks install, the skirmish setup shows "Add NEW AI Computer" (in place of Normal).
- Stand 1 (3.2.1, -dev, 5x speed) AI vs AI against Elite Normal: no crash to desktop; two long matches froze at ~26 min of game
  time (all threads waiting, under investigation; an Elite-vs-Elite control run is going). Store text: skirmish against the
  computer only, untick for multiplayer with people.

## 0.2.32 — 2026-11-09
UIFilter 1 (new, store uifilter-1, category performance, order 60, off by default, Elite + vanilla; UIFilter.dll).
- uifilter/: the Scaleform GFx D3D9 renderer in Spooge.dll draws through a proxy device (BeginDisplay 0x100513B8 /
  EndDisplay 0x10051F10, renderer+0xD4 and the buffer helper +0x18C); repeats of render/sampler state, texture, shaders,
  stream source, indices, declaration/FVF inside one display are dropped. Works with any d3d9 (owner 09.11).
- Stand 2 (late 3v3, DXVK): D3D9 calls per frame 16604 -> 13341 (-20 %), main-thread CPU unchanged (DXVK drops repeats
  cheaply itself); Verify mismatches 0 in menus, Army Painter, HUD, minimap, replay. The store text says the gain is expected
  with the usual Direct3D 9 and not with DXVK.

## 0.2.31 — 2026-11-09
DXVK 1 and FpsUnlock 1 (new, store dxvk-1 / fpsunlock-1, new category performance, order 10 / 20, off by default, Elite + vanilla;
DXVK.dll, FpsUnlock.dll). Both are in the EARLY list of the winmm loader (OwnLobby, DXVK, FpsUnlock), so they load ~0.05 s after
the start, before the game creates Direct3D: in vanilla through the launcher's loader (vloader), in Elite through Automatch's early
winmm (Elite's own injector is ~1 s after Direct3D, too late).
- dxvk/: DXVK.dll points only Spooge.dll's import of d3d9.dll!Direct3DCreate9 at DXVK 2.7.1 x32 (DXVK_d3d9.bin, byte for byte
  the d3d9.dll Elite ships; zlib licence DXVK_LICENSE.txt); DXVK_CONFIG_FILE / DXVK_LOG_PATH = the add-on folder (DXVK.conf has
  comments only). Nothing when the game's d3d9.dll is DXVK already (Elite IsDXVK) or when loaded > 5 s after the start (Elite's
  injector without Automatch). No Vulkan check and no fallback to the system d3d9 (owner 09.11): an unsupported PC fails to start
  the game and the player unticks it.
- fpsunlock/: FpsUnlock.dll appends d3d9/dxgi.maxFrameRate = 0 to DXVK_CONFIG (lifts Elite's dxvk.conf 60 and any DXVK cap);
  a maxFrameRate already there (SoundFix FpsCap) is kept. Vertical sync is the game's option, untouched. Store: requires dxvk.
- Store category performance (09.11): DXVK, FPS unlock, FastBuffers, FastLocStrings, PerfLog (order 10-50).
- addons.json: games / requires / excludes of an add-on go to the store with a deploy (launcher 1.5.2 reads requires/excludes).
- Stand 2: vanilla and Elite (early winmm) both render through DXVK on the RTX 4000 (replays ~80 fps); Elite IsDXVK: DXVK.dll
  does nothing, FpsUnlock lifts the 60 cap (control without it: 60.0 fps). Both winmm builds (Automatch's early one, vloader) are
  released by hand.

## 0.2.30 — 2026-11-09
ChatHide 2 (store chathide-2). Crash fix: v1 kept the chat handler (arg 1 of 0xB214F0) captured from the first chat line of a
match and called it for its "Chat hidden / shown" line; in the next match that handler was already freed with the old HUD,
so toggling crashed (Grantovich 09.11 19:18: DOW2.exe+0x282451, ebx = the handler from the 19:01 match). Now the handler is
dropped at every new match (new minimap movie or no minimap update for 3 s) and the status line is shown only through a handler
seen in the current match; before the first chat line of a match the toggle shows no line (the button shows the state). The
untested Receive_AddMessage fallback is removed. Hiding itself is unchanged.

## 0.2.29 — 2026-11-09
CursorScale 1 (new, store cursorscale-1, experiment, order 990, off by default, Elite + vanilla; CursorScale.dll). A bigger mouse
cursor for high-resolution screens; only the cursor changes, the interface stays as it is:
- the game makes its 45 cursors from 48x48 TGAs (DOW2.exe 0x0068CCC0 -> Plat::Input::CreateCursor -> CreateIconIndirect in
  Platform.dll). CursorScale hooks that import, scales the image (premultiplied-alpha bicubic, or nearest with Filter=nearest)
  and the hotspot with it (centre of the source hotspot pixel), and rebuilds in place the cursors made before it was loaded
  (Elite: Profiler loads add-ons at the main menu).
- size: one launcher dropdown \"Size\" = Auto / 1.25x / 1.5x / 2x / 2.5x / 3x (CursorScale.ini [CursorScale] Scale). Auto follows
  the game window height (GetClientRect, not the desktop): 1440 -> 64 px (x1.33), 2160 -> 96 px (x2), up to 1080 unchanged.
- Stand: Elite main-menu start 1.25 / 1.5 / 2 / 2.5 / 3 -> 60 / 72 / 96 / 120 / 144 px; Auto 1440p and 4K fullscreen and a
  2560x1440 window on a 4K desktop; vanilla main-menu start x2 = 96 px, no crash; Elite skirmish x2 and x3: clicking a Scout
  under the cursor centre selects the squad. Not testable under Wine: Windows DPI virtualization (Auto may pick the logical size).

## 0.2.28 — 2026-11-09
ChatHide 1 (new, store chathide-1, experiment, order 980, off by default, Elite + vanilla; ChatHide.dll). Hides incoming player chat
(all, team, observer, referee) during a match; client-side only, nothing sent, no effect on sync:
- display: the one call site 0xB207EB (call 0xB214F0, the chat line display) - while hidden, types 0/3/5/6 return at once.
  System lines (type 4) and the player's own lines stay: the own send 0x401D97 -> 0x429672 echoes synchronously, marked there.
- toggle: a speech-bubble button inside the minimap frame (minimap_panel movie, per-frame slot +0x10 of form vtable 0x011160D8;
  the game's bubble_up / bubble_over textures, crossed and dimmed when hidden; default mouse callbacks, so a click doesn't reach
  the world) and Ctrl+Shift+C (GetAsyncKeyState, game window in front). Each toggle shows a system line; the state is kept in
  ChatHide.ini ([ChatHide] Enabled, Hidden, Hotkey, Button, OffsetX/OffsetY).
- Stand 2: button in both states, click and hotkey toggle, injected all/team lines hidden and shown again, ini persists (Elite
  replay, vanilla skirmish); Elite 3.2.1 skirmish: Stronghold selected + Ctrl+Shift+C toggles chat and queues no Scouts (plain C
  does), clicking the button keeps the squad selected; main-menu start Elite and vanilla, no crash. The own-message echo online
  is untested (stand Steam offline).

## 0.2.27 — 2026-11-08
PerfLog 1 (new, store perflog-1, experiment, order 970, off by default, Elite + vanilla; PerfLog.dll). A performance log for
players: what limits the FPS on their PC. Measures only - no simulation, drawing or Direct3D object is changed.
- PerfLog.log, a line every 5 s: mode (menu / match / replay xN, "loading"), game time, fps avg / min / 1% low frame ms, and ms
  per shown frame: frame, main thread CPU (GetThreadTimes of "XThread 0"), Present + EndScene wait, 3D world 0xB7DAA0 (with the
  unit bars), HUD panels 0x681110, other drawing, game frame 0x4240F5 (sim + HUD model), visual update 0x439CE8, UI update
  0x680C00, rest of the loop; sim ticks and loop passes, visuals (Viz proxies), players, memory. Once: CPU, RAM, Windows / Wine,
  d3d9.dll path, DXVK cap, GPU name / vendor / driver, resolution / fullscreen / vsync, the other add-ons of the folder.
  PerfLog_summary.log: a line per minute with a verdict (vsync / FPS cap; GPU/driver-bound = Present >= 30 % of the frame;
  CPU-bound (main thread) = its CPU >= 80 % of the frame; else mixed; the biggest part named). Logs trimmed above 1 MB.
- How: shark::update import slot (pass / frame), entry jumps on the main-loop functions that time the outermost call on the main
  thread with two QueryPerformanceCounter, Spooge's own sp::Device Present / EndScene vtable slots (0xC0 / 0xB8). The
  IDirect3DDevice9 vtable is not hooked (overlays hook it; see SoundFix 2). All writes under the shared patch mutex; the main
  loop's call sites stay free for ReplaySpeed / ReplayTakeover (tested together with ReplaySpeed 8x: frames shown match).
- Tested: stand 2 (Wine + DXVK + NVIDIA) Elite 3.2.1 started normally (inj.exe Profiler.dll DOW2.exe -modname Elite, no -dev /
  -init), Game History -> 6p replay, with ReplaySpeed + FastBuffers + FastLocStrings: lines from the menu on, replay x1 / x8,
  values match the earlier profile (frame 13 ms, main thread 7-9 ms, world 3.2, visuals 1.6-2.5). Vanilla normal launch
  (vloader-1): stand 1 Custom Games -> Private -> 1v1 skirmish vs AI (match, 2 players, "capped by the FPS limit (60)").
- Overhead (stand 2, vanilla 3v3 replay ticks 1200-2400, uncapped, PerfMeter): without 77.1 / 77.5 / 77.8 fps, main thread
  6.19 / 6.10 / 6.06 ms per frame; with 77.7 / 77.8 / 77.9 fps, 6.08 / 6.04 / 6.06 ms - within noise. ~8 timed calls per frame
  x 0.3 us = ~2.4 us per frame (0.02 %).

## 0.2.26 — 2026-11-08
SoundFix 2 (store soundfix-2, experiment, off by default, Elite + vanilla). Crash fix.
- SoundFix 1 crashed vanilla on a real Windows PC (native Direct3D 9, no DXVK, NVIDIA, 2560x1440 fullscreen) every launch,
  ~0.2-1 s after "ScaleformFrontEnd - Loading Front End" = the first frames of the menu; with SoundFix off the same set works.
  WER: APPCRASH DOW2.exe in BugSplat.dll c0000005 at +0xeca7 = BugSplat's stack probe (_chkstk): its crash handler ran on a
  thread that had already used up its stack - an endless call chain. The only per-frame code of SoundFix 1 that exists only on a
  native Direct3D 9 is its Present hook (vtable hooks of IDirect3DDevice9::Present / IDirect3DSwapChain9::Present, installed from
  a hooked IDirect3D9::CreateDevice via Spooge's Direct3DCreate9 import); a Present hook of another program on that PC chaining
  with it is the likely loop. Not reproduced under Wine (stands: DXVK and wined3d, the player's exact add-on set, normal front
  end with Steam, menus - no crash with SoundFix 1 either).
- SoundFix 2: no Direct3D hooks at all (FpsCap stays a DXVK_CONFIG setting; without DXVK it does nothing - vsync); no COM on
  the thread that starts FMOD (the output mix format is read once on SoundFix's own thread); the Windows audio device change
  callbacks only with AudioLog=1 in SoundFix.ini (diagnostics, default off). Queue merging and the FMOD 4.32.20 swap unchanged.
- Tested on stand 1 (vanilla, vloader-1, the player's set of 13 add-ons + SoundFix 2, DOW2.exe -nomovies through the front end
  with Steam): menu, 9/9 hooks, ~19 k merges/min in the menu, no crash.

## 0.2.25 — 2026-11-08
FastBuffers 1 and FastLocStrings 1 (new, store fastbuffers-1 / fastlocstrings-1, fixes, off by default, Elite + vanilla).
Performance, drawing / interface only. Profile of a 3v3 replay (Wine+DXVK+NVIDIA, ~47 fps): the sim tick is ~1.5 % of the game
thread; the frame goes to drawing (~11 ms), the HUD model, the visual update of all units and the UI.
- FastBuffers.dll: Spooge's IsBadWritePtr after every dynamic VB/IB Lock (0x1004D414, 0x1004D4FA; 8.7 % of the game thread) ->
  IAT stub that rejects only NULL.
- FastLocStrings.dll: Localizer 0x10004A80 (release of a formatted LocString, linear search from the start of the vector of ~16 k
  live strings; 4.5-5.8 %) -> the same function searching from the end; entry patched by one atomic 8-byte jmp.
- Tested (stands, the binaries of this commit's sources; -sync_high + CrcTrace, CRC of every tick vs a run without them):
  vanilla 3v3 calderis 2026-02-21 ticks 0..2424 identical; Elite 3.2.1 rt_test321 (6p Graesark) ticks 0..2430 identical on
  stand 1 and stand 2. Both load and work in Elite and vanilla (FastBuffers ~1.9 M probes skipped / 2 min; FastLocStrings
  ~29-52 entries looked at per release in a list of ~16-19 k), no crash.
- FPS (stand 2, DXVK uncapped): vanilla 3v3 calderis 2026-05-15 ticks 4200-4800 47.5 -> 52.5 fps (+10.4 %, main thread
  14.25 -> 12.40 ms/frame); Elite rt_test321 ticks 1200-2400 74.1 -> 74.1 fps (that replay is not bound by these two).

## 0.2.24 — 2026-11-08
ReplaySpeed 1 (new, store replayspeed-1, fixes, off by default, Elite + vanilla; ReplaySpeed.dll). Two more replay speeds, 8x and
Max, as two buttons right of the replay panel's own speed button:
- the game's speed: the panel's one button (/playback_panel_mc/fast_forward_btn, click 0xB1FCC0) steps through {5, 10, 20, 40}
  (table 0x012C07DC) = sim ticks per second (10 = 1x) and calls the sim rate interface *(*(0x01335720)+8)+0x68 (vtable
  0x0108DFAC, +0x28 = Game_SetSimRate 0x43ECE9 -> 0x425106: SimulationController+0x2C rate, +0x28 1/rate, no clamp; only the
  icon lookup 0xACC2E0 clamps to 5..40).
- SimulationController::Update (0x438613) runs at most one tick per pass of the main loop and keeps at most one interval of
  backlog, so ticks/s = min(rate, passes/s); a drawn pass is ~20 ms on a 6p replay, so 8x would stay at ~4.7x.
- 8x = rate 80 (raised up to 160 by a once-a-second controller until the replay really runs 80 ticks/s: each drawn frame drops
  the rest of an interval); Max = 1000. While one of them runs in a replay (settings+0x118 & 0x800), the visual update
  (call 0x40E58D -> 0x439CE8) and draw+Present (call 0x40E6DC -> 0x405935) are skipped on the passes between shown frames
  (30 fps at 8x, 15 at Max; ReplaySpeed.ini Fps8x/FpsMax); paused / replay end / live replay waiting = every pass drawn.
  Scaleform still advances every pass: buttons answer at once. No Direct3D code.
- buttons: two Receive_DuplicateMovieClip copies of fast_forward_btn (its up/over/down/toggled_on frames), "8x" a $NormalFont
  text, "Max" the game's unused icon hud/playback_panel/large_btn_ff_8x; toggled_on follows the rate we set (the game's own
  button or a takeover changing it turns ours off). Clicks: GetAsyncKeyState + the copy's _xmouse/_ymouse. Pressing the game's
  speed button during 8x/Max sets 0.5x first, so its click lands on 1x. The elapsed time and the progress bar move right by
  the two buttons (the bar keeps its right end).
- Stand 2 (vanilla, 6p calderis 2026-05-15, DXVK): 8x 8.0 game s per real s (80 ticks/s, 27 frames/s), Max 20.7x (207 ticks/s,
  14 frames/s; simulation-bound), back to 1x and the game's button from 8x -> 1x work. Elite 3.2.1 (stand 1, rt_test321): 8x
  7.95x, Max 17.4x, 1x again, no crash.

## 0.2.23 — 2026-11-08
SoundBugTest 1 (new, TEST, store soundbugtest-1, experiment, off by default, Elite + vanilla; SoundBugTest.dll +
SoundBugTest_fmodex.bin; conflicts with SoundFix). The sound bug's WORST case for players who try to provoke it, plus a log:
- Stock FMOD Ex 4.32.3 (SoundBugTest_fmodex.bin = Relic's fmodex.dll from the Steam depot, md5 9bea3b58) and Shark's stock FMOD
  version check, in Elite too: Elite copies Elite_fmodex.bin (4.32.20) / Elite_Shark.bin (Shark+0x3907A 8C, SMOL) over the root
  files, so after a game of Elite vanilla runs them as well. While Shark's Driver::m_instance (Shark+0x6BDA8) is 0 SoundBugTest
  points Shark's 89 fmodex imports at its 4.32.3 and puts Shark+0x3907A back to 85 (jne) in memory - only once 4.32.3 is in use
  (a jne Shark refuses 4.32.20 = silent game). Elite loads Elite_DLLs before FMOD starts (stand), so this works there too; FMOD
  already started -> logged "NOT the worst case". No game file changes.
- No merging of Shark's queues: holds SoundFix's mutex (SoundFix-<pid>) so a later SoundFix stays idle; refuses to work (logged)
  when SoundFix came first. Store: conflicts Elite_DLLs/SoundFix.dll (SoundFix wins). No frame cap: DXVK_CONFIG maxFrameRate = 0
  before Spooge's Direct3DCreate9 (vanilla; Elite creates Direct3D before its add-ons - cap of dxvk.conf stays, logged).
- Log next to the DLL, written through (FILE_FLAG_WRITE_THROUGH, shared so the launcher can read/truncate it), rotated at 3 MB
  (SoundBugTest.1.log): one line a second - match number of the game session / players / game time, frames (DOW2 calls of
  shark::update), Shark thread ticks + busy avg/max (inline hook of the tick Shark+0x45B30, 8-byte atomic), messages per second in
  its 7 queues (ContainerLogic/ControlSource) + biggest batch + still pending, FMOD update time, channels playing / real / virtual
  (System::getChannel + isVirtual on the Shark thread), FMOD CPU, FMOD_Memory_GetStats (3 args), streaming/static buffers in use
  of the sample loader (this of Shark+0x33410 via its vtable slot Shark+0x4F6EC, free stacks +0x14/+0x8), buffer overflows,
  process memory, largest free address block. Events at once: FMOD format vs the Windows mix format, FMOD init (output, rate, DSP
  buffer), FMOD system callbacks (Shark's wrapped: device list changed / device lost / ...), System::update errors, Shark's own
  trace lines (dbInternal::TimeStampedTracef, max 6/s) and Fatalf, Windows audio device changes incl. a changed sample rate
  (IMMNotificationClient, PKEY_AudioEngine_DeviceFormat), match start/decided/end (SimEngine imports), Shark thread not ticking
  (+ its stack). SoundBugTest_events.log (1 MB): the events + one line a minute, so a long evening fits the launcher's 256 KB.
- Tests (stand 2, 6p replays, 5 min each; root files = Elite's fmodex 4.32.20 + SMOL Shark as after a game of Elite): vanilla -
  SoundBugTest_fmodex.bin mapped, 89 imports redirected, jl -> jne, FMOD 4.32.3 init 0 on WASAPI 44100 Hz (Windows mix 48000),
  70-89 fps (DXVK cap of dxvk.conf lifted), up to 81 channels, match/6 players/game time logged; Elite 3.2.1 - add-ons load
  before FMOD starts: the same redirect + jne, FMOD 4.32.3 init 0, sound plays (up to 106 channels), 60 fps (stand dxvk.conf cap
  stays: Direct3D before Elite_DLLs). ~255 bytes a second in SoundBugTest.log. No simulation change (sound only), no CrcTrace.

## 0.2.22 — 2026-11-08
HeroLabelFix 1 (new, store herolabelfix-1, fixes, off by default, Elite + vanilla; HeroLabelFix.dll). The bar over a hero
(squad_leader_decorator, vtable 0x011129E4: icon, health, energy) sometimes never came back after the hero was downed and
revived - mostly in replays, where every player counts as hostile:
- while the hero is down the decorator's update (slot 0x30, 0x00B24370) clears "valid" (+0x9C) and the next draw (slot 0x10,
  0x00B25070) hides the movie (+0x1C = 0); the movie manager's per-frame walk (0x00680C00) asks only SHOWN elements whether
  they are on screen (slot 4, 0x00B92520), and only that test calls the draw - so after the revive nobody shows it again until
  the decorator happens to be re-created ("sometimes" = the hero was on screen when he went down).
- HeroLabelFix wraps the update (vtable slot, bytes checked, otherwise off): a valid-but-hidden decorator that the manager has
  set up (+0x28/+0x2C) gets the game's own draw once, under the manager's lock (Plat::RecursiveMutex::TryEnter). UI only.
- Stand 2, 6p replay calderis 2026-02-21 (probe): 9 revives, 0 bars stuck hidden > 20 s (17-22 without the fix), 9 restores;
  CrcTrace to tick 2425 identical with/without (vanilla) and against the Elite 3.2.1 baseline; Elite 3.2.1 loads and hooks.

## 0.2.21 — 2026-11-08
SoundFix 1 (new, store soundfix-1, fixes, off by default, Elite + vanilla; SoundFix.dll + SoundFix_fmodex.bin). The "sound bug"
(late big fights at high FPS: sound stutters, one sound loops, unit responses vanish, then a crash) is NOT reproduced on the
stands (Wine, null audio), its cause is not proven; this is hardening plus the community's SMOL fix for vanilla:
- Shark's sound queues: DOW2 0x760E00 pushes setPosition/Orientation/Velocity/Volume and ControlSource::setData for every
  emitter every FRAME into Shark's queues (Shark+0x6ABFC / +0x6A1B8), the Shark thread applies them 20 times a second - work
  linear in FPS. SoundFix (DOW2 import slots of Shark) replaces the payload of the still-waiting message of the same emitter and
  field instead of queueing another (under Shark's queue mutex); play/stop/free are barriers. Stand 2, 6p replay, ~77 fps:
  ~390k of ~545k state messages a minute merged.
- FMOD Ex 4.32.20 for vanilla: DOW2.exe imports Shark.dll, Shark imports fmodex.dll (89 names, all in 4.32.20), so 4.32.3 is
  mapped first; while Shark's Driver::m_instance (Shark+0x6BDA8) is 0 SoundFix loads SoundFix_fmodex.bin (= Elite_bin/
  Elite_fmodex.bin, md5 10b85787) and points Shark's fmodex import table at it, then Shark+0x3907A 85 -> 8C (jne -> jl of the
  FMOD version check, the SMOL byte) in memory; no game file changes. Elite already runs 4.32.20 + the patched Shark: untouched.
  Accepts both Shark builds (only that byte differs). Unknown Shark/Platform build -> off.
- No frame cap: the game has none of its own (app_fixedframerate = dev-only frame-time setting App+0xFAC, default 0; the main
  loop sleeps 50 ms only while the window is inactive); DXVK's maxFrameRate (dxvk.conf, Elite.exe writes 60 with IsDXVK=true)
  is overridden through DXVK_CONFIG (d3d9/dxgi.maxFrameRate) before Spooge's Direct3DCreate9 (import hooked). Vsync untouched.
  SoundFix.ini FpsCap=N (launcher option: none/60/120/144/240): DXVK cap + a wait in IDirect3DDevice9/SwapChain9::Present.
  Elite loads Elite_DLLs ~1 s AFTER Direct3D is created: there the DXVK cap of IsDXVK=true stays (logged).
- Diagnostics in SoundFix.log: the game's FMOD rate (configuration.lua soundfrequency, 44100) vs the default output's mix format
  (IAudioClient::GetMixFormat), DSP buffer (2048 x 4), FMOD output type and real rate, Windows audio device changes
  (IMMNotificationClient), Direct3D device lost/back.
- Tests (stand 2, real GPU via DXVK, stand dxvk.conf maxFrameRate = 60): vanilla Steam fmodex/Shark - without 60.0 fps,
  with ~77 fps, 4.32.20 mapped from dow2-patches\SoundFix_fmodex.bin, FMOD initialised; 12 min to tick 7010 without a problem;
  FpsCap=40 -> 40.0 fps. Elite 3.2.1 (rt_test321) - add-on loaded, 9/9 hooks, 12 min fine, fps 60 (DXVK cap stays, see above).
  -sync_high + CrcTrace: CRC of every tick 0..2425 identical with and without, vanilla and Elite.
- Code reading of MP-only audio: voice chat plays through Shark::Util::playVoiceStream (FMOD user stream, PCM16 mono 11025 Hz),
  created only when voice from a player arrives, silence on underrun, unbounded buffer; on player-left only Channel::stop (the
  FMOD Sound is never released). Nobody talks -> no stream. "SOUND: streaming buffer overflow" = Shark's sample loader has no
  free stream slot (speech dropped).

## 0.2.20 — 2026-11-07
ReplayTakeover 2 (store replaytakeover-2; the add-on's code, released from feature/replay-takeover as replaytakeover-1 on
06.11, is in main from here on - its entry of that day is in the branch's history). Players (WarGood, 06.11): the computer put
in for the others is weak, "walks single squads from point to point"; owner: the maximum difficulty.
- The AI of a former human took his Player's difficulty (Player+0xBC; the AIPlayer constructor 0x80E6A0 copies it to
  AIPlayer+0x900 = AIPlayer_GetDifficultyLevel; -1 = Hard). Now Player+0xBC and the GameInfo slot +0x2C (what the lobby's
  SetPlayerAIDifficulty 0x44EEEB sets) get Expert (AD_Hardest = 3, the game's highest, "Add Expert Computer") before his AI is
  made. Option Difficulty = expert (default) | hard | normal | easy | keep. Computers of the match keep their own AI.
- Stand 2 (Elite 3.2, rt_test 3v3, slot 5 taken at tick 1500 by the stand-only TestTakeAt, 4 minutes, a telemetry-only AI
  script): keep - s_diffLevel 2 for the five computers (this replay's humans had -1 = Hard); expert - 3 for all five; with
  Expert pop 256 vs 217, 26 vs 23 squads, squads in combat 170 vs 117 samples, models lost 26 vs 12.

LiveReplay 3 (store livereplay-3, lrbridge.exe as in livereplay-2) and CrashLog 2 (store crashlog-2). Players 06.11 (noofancy,
l'Alerion): "joining as an observer does not work / crashes".
- LiveReplay: a "Watch" of a match running for a few minutes never started - "the ladder: no answer we read (HTTP 200, 24000 /
  121936 / 268336 bytes)" until "failed: server". The node's udp_gateway sent FIN right after battle_relay's answer with its last
  packets still unsent; FIN has no number, the ladder's end of our UDP tunnel closed at once - the answer arrived cut off (24000 =
  the first burst of 20 x 1200 bytes). LiveReplay 3 uses a cut answer as far as it goes: the header, the whole records in it (the
  next request asks from there), the file's length in the header = its steps. (Server side: dow2-ladder fix/live-replay-cut-answers -
  the gateway waits for the acks before FIN, the ladder repairs cut answers of nodes not updated yet.)
- LiveReplay: Elite 3.2.1 refused every live replay ("replay start: REFUSED by the game") - the file's 2nd word is the game data
  checksum (DOW2.exe 0x42B2B8 compares it with 0x7A0A30(), Relic's "dataChecksum"); the nodes write Elite 3.2's (0xD6805B7D), 3.2.1
  has 0x819567F8. LiveReplay 3 sets the game's own value before the start (logged).
- CrashLog: "APPCRASH DOW2.exe!CrashLog.dll c0000005 at +0x21C7 / +0x34E1" and "40000015 at +0x90FF" when the game closed (3 players,
  each on exit, never in a match): the exception watch read a __thread flag (MinGW emulated TLS: __emutls_get_address +0x61 and
  veh+0x17; +0x90FF its abort) for every exception, and at the exit the DLL's runtime had freed that memory. Now the code check
  first, the flag in a system TLS slot, the watch off from ExitProcess / the DLL detach on.
- The observer crash at map loading (DOW2.exe+0x12511 after "Loading Animators", l'Alerion 06.11 15:30 CET) was the Random army header,
  fixed on the nodes 06.11 15:24-15:47 CET (live replay 2); nothing new of it after that.
- Stand 1 (Elite 3.2.1; fake ladder giving a stand match 200 s in, its answers cut off as the tunnel did): LiveReplay 2 - "no
  answer we read" x30, "failed: server"; LiveReplay 3 - the cut answers taken (1353 records first, then ~450 per answer), the data
  word set to this game's, started, caught up, the match to its end, 2400 / 2400 steps in sync; whole answers - the same, 1800 / 1800;
  LiveReplay 2 with the ladder's repair of cut answers (and a header of this game's data) - started, 2400 / 2400. CrashLog 2 loaded
  in the game, its minute lines as before.

## 0.2.19 — 2026-11-07
UnitStats 0.5 - a new add-on (store unitstats-0.5, category experiment, off by default; Elite and vanilla): the selected
squad's ranged / melee DPS, melee skill, armour, range, speed, sight and active effects left of Retreat, in the game's own
selection panel (0.1-0.4 were an experiment of the archive branch, shared by hand with grantovich).
- The DPS is now the attribute editor's (grantovich checks balance with it): shots x damage / (shots x (wind_up + firing +
  wind_down + cooldown) + reload), shots = reload frequency (min + max) / 2. His shotgun (20 dmg, cd 1.5, wd 0.5, 9 shots,
  reload 3 s): 8.57; 0.4 said 8.3 (the engine's real cycle: +1 tick per burst, randomised cooldown in ticks, F = min..max-1),
  0.3 said 10.3 (the game's tooltip: no wind_down, +0.1 s for a burst_info weapon). The engine's number stays in the log;
  ShowEngineCycle=1 shows it instead.
- The weapon times as written (0.49, 0.75, 0.83 - the engine keeps only 0.1 s ticks): UnitStats notes the seconds the
  WeaponBag loader reads, through the SecondsToTicks import of DOW2.exe (the game's result unchanged; the seven call sites
  checked first, else ticks).
- DPS with two decimals (8.57, 14.58), the log starts over past 2 MB, no ini needed (defaults in the code).
- Stand 1 (Elite 3.2.1): nine weapons (single, wind_up, wind_down 0.49, cooldown 0.75, real bursts, a one-shot burst_info,
  3 projectiles, melee) equal to the hand count from the attribute files - docs/UNIT_STATS_HOWTO.md "v5".

## 0.2.18 — 2026-11-06
MMFind 0.49 (store automatch mmfind-0.49 = mmfind-0.48 with this MMFind; OwnLobby 2, Relay 25, early winmm, mmbridge as there).
Players 06.11 (noofancy, siritos): "the map ban does not work, and when you ban, it resets after a game".
- Cause: MMFind's own Map Preferences kept at most 3 unchecked maps per mode, and a Save with more kept the FIRST 3 of the list
  (alphabetical: Ashes of Typhon, Ashes Redux, Bay of Grots...) - not the player's - while the dialog went on showing what he had
  clicked; the next Find a Game showed the 3 kept ones ("reset"). Players' logs: 4, 9 and 36 maps unchecked -> 3 alphabetical kept.
- The game's own rule (MapPreferencesPanel, DOW2.exe 0x48C45E / 0x48C491): at most min(round(0.4 x maps of the mode), 4) vetoes; the
  engine kept them in the player's profile (LuaConfig "MapPreferences", 0x48BCBE) and sent them with automatch/startpolling
  (vetoMapKey / vetoMaps). MMFind 0.49 does the same in its dialog: Save only with at most that many unchecked (the header says how
  many, in the game's language), saved exactly as shown, shown as saved when the dialog opens again; the bans stay in MMFind.ini
  [Maps] (as before - across matches and restarts).
- The dialog lists only the maps a match of the mode can get: the ladder names them in every mm/native answer ("pool"); bans of maps
  outside it were wasted.
- mm/native carries "bans"; the ladder (dow2-ladder fix/nm-map-veto) takes them as a veto: the match's map is one nobody of it banned
  (all banned by someone - one of the least banned); a party member's own bans count.
- DebugPipe=1 (stands): fs with "hook":1 goes through MMFind's own handler, as the player's click.
- Stand 1, 06.11 (test lobby / ladder of dow2-ladder #11, a 9-map 1v1 pool): the game's own header of that dialog is "You may
  deselect up to 4 maps."; with real clicks 5 unchecked = Save off, 4 = saved exactly those; opened again in the same visit, in the
  next visit and after a restart of the game - the same 4 shown; 4 matches against fake opponents, none on a banned map.

## 0.2.17 — 2026-11-06
RoomJoin 13 (store roomjoin-7). Join from the launcher showed nothing of the game between the main menu and the lobby (RoomJoin 12
has no cover, and its log never had a message box): the game browser's Join (GameListForm 0x0046BF34) makes the same engine call
and then opens a MessageBoxManager box - 550001 "Connecting" / 550002 "Connecting to the game...", no buttons - kept as
m_joiningMBID and closed by the form (CancelConnection 0x0046B520).
- RoomJoin opens that box itself where the cover used to come (0x0045A3CD the manager, 0x00AD5FE0 open, texts from Localizer: the
  game's language) and closes it (0x00AD6200) in the room, in the lobby, on any failure / Cancel / timeout and when a world starts.
  Our own box is not counted as the game's "no". RoomJoin.ini ConnectingBox=0 turns it off; with Cover=1 there is no box.
- Stand, 06.11 (siritos): the box shown at the join call, closed "in the room" after 4.9 s, the lobby screen as before.

## 0.2.16 — 2026-11-06
LiveReplay 2 (store livereplay-2) after the players' test of 06.11 (siritos, l'Alerion): "the server did not give the game", a long
wait in the menu, then the game crashed on the map's loading screen.
- The crash was the servers' header: both players of m3 had a Random army; the lobby has race / hero 255 for it, the replay got that
  (dow2-ladder #9 takes what the game rolled - the slot carries it - and gives no header rather than a wrong one). The DLL is the same.
- "The server did not give the game": the ladder's TCP to the Moscow nodes stalled (now over our UDP tunnel, dow2-ladder #9). The DLL
  now waits 90 s for its first answer (it said "server" after 3 failed tries); the launcher shows "fetching" meanwhile.
- Stand 1: a Random-army stand match (c7) served as a running one - loads, plays in sync.

## 0.2.15 — 2026-11-06
Watch a running match (owner 06.11.2026: "зритель открывает матч в лаунчере и смотрит его как реплей, который не заканчивается и
подтягивается вживую, с задержкой, заданной сервером"): LiveReplay.dll 1 (store livereplay-1, experiment, Elite and vanilla) +
Elite_DLLs/lrbridge.exe (its pipe for the Linux launcher: rjbridge.c with -DPIPE_PREFIX=L"EliteLiveReplay"). Launcher 1.4.18: "Watch"
on a running match of our servers in the Online block. Nothing in the players' games: our battle server builds the replay.
- Server (dow2-ladder gameserver/live_rec.py, battle_relay "DWRL", ladder /ladder/api/live/<match>/rec?from=N): the .rec of the match
  from the ticks the node sends and the host's lobby state; LIVE_DELAY s behind (120 by default, the node's floor). Byte for byte the
  host's own temp.rec on six stand matches (Elite / vanilla, VPC / annihilate / hero assault).
- LiveReplay.dll: the pipe \\.\pipe\EliteLiveReplay-<pid> (RoomJoin's code; {"cmd":"watch","match","base"}, "stop", "status";
  events accepted / fetching / starting / playing / over / failed); its network thread writes Playback\live_watch.rec (the replay so
  far) and fetches the rest every 2 s; the game's thread starts the file as Game History -> Load Replay does (0x40F526 with
  "playback:live_watch.rec", EDX 0x01335A74) once the menu is up; the playback reader's "next record" (vtable 0x0108BA30 slot 1)
  takes the new records (parsed by the game's own 0x42B451 from a byte stream of ours); with nothing new yet
  PlaybackSynchronization::GetSyncCommand (0x010B9E2C slot 4) answers "no step now" and SimulateInternal's replay end (call 0x42761A
  at 0x43809B) is skipped for it; the replay panel's length (world+0x174) follows the data; a faster start (CatchUp, 4x) up to the
  live edge. Sync check: each step's crc (slot 7) against the players' turn crc of that step - LiveReplay.log and the status.
- Stand 1: stand matches served as running ones: the replay starts from the menu, 2841 and 1040 steps in sync with the players,
  a 20 s stall of the data waited out; end to end: stands 1+2 play a vanilla 1v1 on a test node, a second game on stand 1 (its own
  network namespace, no sign-in) watches it 30 s behind - every checked step in sync.

## 0.2.14 — 2026-11-06
MMFind 0.48 (store automatch mmfind-0.48 = mmfind-0.47-relay25 with this MMFind; Relay 25, OwnLobby 2, mmbridge as there).
Live 06.11 (room d59lrf): a player's room said Space Marines, his game searched with the army it had on Find a Game (Eldar / Farseer).
Stand: Find a Game already open with a committed army - MMFind 0.47 set the room's race into the closed hero panel, never committed
it (and on the stand the game crashed: FATAL EXIT propertybaggroup.h). 0.48:
- the walk of a launcher room always opens Find a Game itself: one that was open before gets Back first, then in again (its hero panel
  comes up fresh); the race / hero are set there and committed with the hero panel's Ready (always);
- before Start Searching the army is checked where the game keeps it: its own "Your Party" row (Receive_PopulateList: the hero's xs
  image, the same in every language) against the room's race and hero (the image names of the racebps hero_list, Elite 3.2 and
  vanilla); another army -> out and in again (twice at most), then failed "army"; no row in 6 s -> searched (the ladder checks the race);
- `ttl` in the room command: the walk gives up ("expired") once the room's start is over - a game started late never walks into it.
The ladder now calls a start off when a game searches with another race than its room (never a match with the wrong army).

## 0.2.13 — 2026-11-06
Relay 25 (store automatch mmfind-0.47-relay25, relay_fwd relayfwd-25). Owner 06.11.2026: "не видно совсем - больше и в центр",
"кнопка «Не ждать»; если все оставшиеся нажали - игра продолжается без ушедшего; голоса согласованы у всех".
- The waiting lines moved from the minimap movie to the middle of the screen: the game's own full-screen HUD movie
  ui\\movies\\hud\\resource_notifications (stage 1600 x 1200; its form vtable 0x01116478, slot 4 = an empty per-frame update,
  chained): "Waiting for <name>" (44, orange) and "0:13 – 2:41 left" (30, white), each over a black copy for any ground;
  en / ru / es / fr / de by the game's language (its own text of ucs 9032352 "Resume"; Relay.ini Language= overrides).
- "Don't wait" under them: a text field with a background and border; the player's own left click released over it
  (GetAsyncKeyState + the movie's _xmouse/_ymouse, only while the game is the foreground window) queues frame 7d <number>;
  du_tunnel.h follows the game's frames going up and puts ours between two of them. battle_relay (dow2-ladder, votes) counts
  the votes of the other connected players; all of them -> the player is out of the next tick for everyone at once (as when
  the window runs out), his reconnect refused. The notices carry "votes / asked" -> "Don't wait: 1 of 3" under the button.
- Stands 1+2 (1280x800 and 1024x768, en and ru): the lines in the middle, the click -> vote 1 of 1 -> dropped at the same
  tick for both; a server test with three players (1 of 2 shown, both -> out).

## 0.2.12 — 2026-11-06
Back into a running match after a network drop (owner 06.11.2026: "вернуться в идущий матч ... окно ~3 мин, надпись «Ждём игрока»"):
Relay 24 (store automatch mmfind-0.47-relay24 = 0.47's files with Relay.dll 24; relay_fwd relayfwd-24 - the same source).
- The game's relay client (BattleServerRelay::Process 0x63B910) reconnects by itself after RL_ClientReconnectMS (15 s) of
  silence with "15 sid 0 id 02 <ticks got>" and gives the match up after 40 s (cmp eax, 40000 at 0x63B979). For a native game
  on our node (g_native: set at the redirect of a connect to our battle port, cleared at a connect to a Relic relay) the cmp
  calls rejoin_stub: reconnect after 5 s (Relay.ini ReconnectSec), give up after 180 s (RejoinSec); Rejoin=0 = all off.
- A native stream starts with "DWR5" + 6 bytes (dow2-ladder 9fd52fb+ on the nodes BEFORE this release: an older
  battle_relay refuses DWR5): the server keeps such a player in the ticks as silent 170 s after the drop (45 s for others),
  answers the reconnect hello with its count of his game messages and sends the missed ticks; to DWR5 games it sends 0x7e
  frames once a second (number, s left / 0xFFFE = connected but silent, s away, name from his peer announce) - du_tunnel.h
  takes them out of the stream (du_filter), rejoin.h shows them in the minimap panel's movie (its per-frame update chained,
  vtable 0x011160D8 slot 4): "Ждём <name>… 0:23 (ещё 2:37)" / "Waiting: <name>… 0:23 (2:37 left)".
- Stands 1+2, a real 1v1 from a launcher room on a TEST battle_relay + udp_gateway (rts, units rr-test-*) through udpcut.py
  (all UDP of one stand's game dropped): 30 s, 45 s, 40 s, 129 s outages -> back into the match; 230 s -> the returning game
  gave up at 180 s, the other one got the drop at ~220 s (server grace then 200 s, now 170 s); per-tick sync hashes of both
  replays identical (1311 ticks). After the return BOTH games fast-forward the gap at ~60 ticks/s (129 s gap -> ~30 s).

## 0.2.11 — 2026-11-06
Launcher rooms (store automatch mmfind-0.47; launcher 1.4.15, ladder lroom.py). Owner 05.11.2026: "a lobby in the launcher like the
game's own - map, places, race and hero, everybody Ready, a 10 s countdown, then the game starts for everyone and they are in one match".
- MMFind 0.47: the pipe `\\.\pipe\EliteMMFind-<pid>` (RoomJoin 9's code: only this user, no remote clients, JSON lines; the pipe
  thread never touches the engine). `{"cmd":"room","room":id,"mode","race","hero"[, "win","vptk","sloc","rsst"]}`: MMFind walks to
  Find a Game straight from the main menu (`.OnClickMatchMaking`; Multiplayer first asks a fresh account "Play against CPU?" and the
  menu then ignores every command), sets the mode / race / hero (the saved-choice code), presses the hero panel's Ready and Start
  Searching with the room's id in mm/native; events back: accepted, walking, army, searching, matched, in_room, loading, failed
  (+reason). The menu movie and its own fscommand handler are taken from the engine's calls when MMFind came in after the menu.
- MMFind 0.47: the room's match options for its host - the match's set-options entry (vtable +0x48 = 0x5F74A0) swapped once,
  a string with MATY rewritten only while searching for a launcher room (victory condition, VPTK, SLOC, RSST; values measured on
  the stand: 0 Annihilate / 3 VPC / 4 / 6 Hero Assault, 250 / 500 / 1000 = 0 / 1 / 2, Random / Fixed = 0 / 1, Standard / High = 0 / 1).
- mmbridge.exe / mmpipe.exe: rjbridge / rjpipe built for this pipe (`-DPIPE_PREFIX`); mmbridge.exe ships in the automatch package
  (the Linux launcher talks to the game through it). rjbridge.c: builds with older mingw headers too.
- Stands 06.11 (test lobby / ladder, then the live ones): full 1v1 Elite and vanilla from a room; 1000 VP and High Resources in the match.

## 0.2.10 — 2026-11-05
Automatch with our UDP transport inside (store automatch mmfind-0.46, relay_fwd relayfwd-23; relay_r2 removed from the store).
Owner 05.11.2026: "the tick 'Games through our servers' is named stupidly, remove it. Two variants only: Automatch - always
through our servers and over UDP; Forwarding - through our forwarding servers, over UDP too."
- MMFind 0.46: loads Relay.dll from its own folder at the start (a thread, never under the loader lock): Elite's injector loads
  only the DLL an add-on names in InjectedFiles (MMFind.dll for the automatch); in vanilla the launcher's loader loads every
  DLL of dow2-patches\ anyway. MMFind.log says "Relay.dll loaded" / "already loaded" / "no Relay.dll next to MMFind.dll".
  Nothing else changed in MMFind; its relay_ping (Relay's pings, "0.0.0.22:22" first) and own_server go to mm/native as before.
- The automatch package (mmfind-0.46) = MMFind.dll 0.46 + OwnLobby.dll 2 (unchanged, the store's binary) + Relay.dll 23 + the
  early winmm.dll (unchanged). Store entry: conflicts += dow2-patches/Relay.dll (a hand copy of the stand-alone package is put
  aside as relay_r2's was), descriptions: "the whole search and the game go through our servers over UDP".
- Relay 23 (from feature/relay-23, below) is the transport, unchanged code: a native game (the game connects to our node's
  battle port itself) over UDP to that node or a chain through the nearest; Relic games are never forwarded by Relay.dll
  (DOW2_RELAY_FWD is set only by RelayForward.dll). With both DLLs the first loaded carries the connections of both (the
  RelayCore mutex: one hook), so no connection is handled twice.
- Relay 23 (relay_fwd / Relay.dll): brakes in the UDP tunnel, after the 2v2 of 05.11.2026 (Fov: ~650 packets/s for 10 minutes,
  sent 399559, sent again 16495, copies 1071947; the other three players the same, the host 2380 packets/s). The flood itself
  came from our battle server (proxy messages to every peer, fixed in dow2-ladder fix/relay-udp-storm); the tunnel had no brakes,
  so it carried and multiplied whatever came. The delivery moved to relay/du_tunnel.h, the C twin of dow2-ladder
  gameserver/du_proto.py: one resend timer per connection (200 ms .. 3 s doubling, Karn), a packet resent 10 times = the
  connection is dead ("ev=dead"), resends budgeted (20/s or 25% of the fresh), a packet copied at most twice and only while
  young, an ACK2 at most every RTT/4, a tail probe, 64 packets / 64 KB in flight with back pressure, 250 packets/s paced, a
  256 KB UDP receive buffer, acks paced, a storm fuse ("ev=storm"), a 1 MB queue for a game that does not read. Same wire
  format both ways. relay/test: du_harness, storm_test.py (netns + netem, 4-6 player model), run_matrix.sh, wine_smoke.sh.
- addons.json / docs/store-texts.json: netprobe (removed from the store 05.11 by the owner) and relay_r2 out; relay_fwd's name
  as in the store.
- Launcher 1.4.14 (Dow2-elite-launcher): Settings -> Online play shows our game servers and the lobby servers with the Automatch
  tick; a unit test of the move (relay_r2 on / off, start checks first or a sync first, with forwarding, vanilla, hand copy).

## 0.2.9 — 2026-11-05
RoomJoin 12 (store roomjoin-6; on top of RoomJoin 11 of PR #23, which never reached the store - roomjoin-5 is RoomJoin 10;
0.2.8 is PR #24). Owner 05.11.2026 after trying Join himself: "не надо там перекрывать весь экран ... и когда в игру зайдёт
не надо тоже закрывать плашкой".
- No cover over the game any more by default: the join runs by itself exactly as before (main menu -> the game browser's join
  by the room id -> the lobby screen), and the player sees the game's own menu, its "Connecting" and the lobby. No click or Esc
  is taken from the game, nothing of the menu is hidden. What happens and why not is told by the launcher (the pipe events are
  unchanged). A "no" from the game (full, closed...) opens the public games list at once instead of a 3 s notice on the cover.
- RoomJoin.ini [RoomJoin] Cover=1 brings the old cover back (tests only); the log says which.
- Stand 1, 05.11 (RoomJoin 10 binary, the same join): real open Relic lobbies joined in ~4 s from Elite and from vanilla, with
  and without "Games through our servers" / "Forwarding servers"; the owner's "connecting, nothing happens" of 05.11 evening was
  the Moscow forwarding bridge closing every forwarded game (du_egress.py, fixed on the server).
## vanilla-loader (merged 05.11 without a number of its own, PR #25; in 0.2.9)
- vanilla-loader: -DLAUNCHER_LOADER (`make build/launcher/winmm.dll`) for the launcher's "Vanilla Retribution" (launcher 1.4.11,
  owner 05.11.2026). One file for both games, decided by the command line: `-modname` (Elite.exe, Elite_Test, old Elite as a mod)
  - only the early add-ons in Elite_DLLs (OwnLobby.dll), exactly as -DEARLY_ONLY; no -modname (vanilla) - every DLL of
  dow2-patches\ (OwnLobby.dll first, before RLink), never Elite_DLLs, a fresh dow2-patches\Loader.log with what was loaded.
  A game started past the launcher therefore never loads the vanilla copies into Elite. The folder name is not in the binary
  (ASCII or UTF-16): launchers up to 1.4.10 put aside any winmm.dll naming it as the old stand-alone package. Store: vloader-1,
  launcher.json "vanilla": {"loader": "vloader-1"}; the add-ons that work in vanilla have "games": ["elite", "vanilla"].

## 0.2.8 — 2026-11-05
Cooldowns 9 (store cooldowns-9; branches fix/cooldowns-9, feature/cooldowns-9).
- A frozen number on a button after a selection change (siritos 05.11; the trace of 15:20: the Techmarine's 75 s on
  ability_btn01, then the scouts selected - the panel updated only their buttons 03 / 05 / 06 and never 01 again; the same with
  the scouts' grenade on 03 and the Techmarine). The number was taken off only in an update of its own button, and the panel does
  not update a place the selected unit has nothing on. Now a number whose button was not updated in the last 2 passes of the panel over its buttons (and 50 ms; 0.4 s was
  still seen on the other unit) is taken off by
  the update of any other button of the same HUD movie (alive at that moment); a unit with no ability buttons at
  all: by the global ability buttons after 120 ms; it comes back with its button's updates. What is
  shown is kept per HUD movie and place. Trace=1 logs each number on / off with its widget and movie, and where a number is up
  every 2 s; it shows the real owner (type + its pointer), not the stack words around it.
- Nothing of a match stays for the next one (siritos 05.11): the recharges are kept by squad / entity addresses and the next
  match gets the same addresses again - a squad there could take a recharge of the match before. A new match (the match core's
  ticks going back to 0) now forgets every recharge, rate and button state; a number still shown in a HUD movie kept for the
  next match is set right at its first update. After a match the hooks only pass through (no match: no query, no number).
- The table of recharge starts per owner (unused since Cooldowns 6: its key was always 0) is gone.
- Cooldowns.log over 1 MB at the game's start becomes Cooldowns.log.old (05.11: it had grown to 1 MB).
- A number only for a recharge the game itself shows (owner 05.11: numbers on abilities not bought yet - no wargear / upgrade /
  tier - and while generators are built; grantovich 05.11: a number left hanging on a transport's unload button). Cause: after
  every button update with any reason Cooldowns 6-8 asked the engine vt+0xB4 (0xAF41F0) for the share of a recharge. That is not
  this squad's recharge: when the owner's ability component (0x89C440) has a running instance of that ability (0x8929E0: an
  active, timed phase - building, unloading, channelling) it answers that instance's progress, otherwise a timer of the ability
  blueprint; the game itself asks it only for reason 0xD / 0xE (recharge). A locked ability (reason 3), a builder's buttons
  while it builds and buttons with no recharge could thus get a moving share and a number. Now the reasons the panel works out (0xB9DA80, its call at 0xB9B075 detoured; Trace=1 logs them) decide: the share is
  only the game's own of reason 0xD / 0xE, no engine query any more (Ask= in Cooldowns.ini is gone, so is the guarded call);
  a recharge the game has shown goes on from its rate while the reasons are others for a moment (no energy right after the
  cast, busy, targeting) and stratagems keep it while usable (the bunker); with reason 3 never a number. Reasons seen on the
  stand: 2 targeting, 3 requirements not met, 4/6/8 not blocking, 9 the squad casting, 0xB / 0xC busy with another ability,
  0xD recharge. The Chimera's unload button is no ability button of 0x0111AFBC (Trace=1 has no update of it): a number there
  was one left on that place from the unit before - taken off now by the stale place sweep above.
  Stand 1 (Elite, IG, Trace=1): Kasrkin "Move it!" locked (reason 3, no number), the Sergeant bought - the number at once and
  down to 0 (35 s), the other Kasrkin squad without the Sergeant - none; the Inquisitor's Crippling Volley (wargear bought) 65 s
  down; Kasrkin (recharging) into the Chimera, out, in, out - no number on unload, the Kasrkin's number goes on; grenade 75 s;
  sandbags built by guardsmen - no number. Every number of the session came with reason 0xD; a usable button: no number, no sweep.
## 0.2.7 — 2026-11-05
RoomJoin 11 (store roomjoin-5; branch fix/roomjoin-11): the review of RoomJoin 10 (05.11.2026) and leaving one lobby for another.
- A second Join for the room still watched for a late answer (after a timeout or a Cancel) was let through and made a second
  engine join on top of the first; now it says accepted / joining and goes on watching.
- The cover stayed and took every click and Esc when the menu movie was rebuilt while a "could not join" notice was up (nothing
  behind it, Cancel did nothing). The rebuild now drops a cover with no join, and Cancel / Esc with no join takes any cover off.
- "Already in this room": the walk to the lobby screen only from the main menu or the public games list - never from Find a
  Game or another screen. With the menu movie found late, the same room on the main menu / the games list no longer comes out as
  "automatch room" / "another lobby".
- A room name of 64+ UTF-16 units came out as "the custom game" (the conversion into 64 failed); it is cut to 63 now.
- The JSON reader stopped a broken surrogate escape (`\uD800\u` at the end of a line) only after 4 more bytes: it stops at once.
- A join whose engine call is never made (the lobby screen of no room, a Relic sign-in that never ends) gives up after 65 s from
  the main menu (the game's start is not counted: up to 90 s before that).
- `{"cmd":"cancel","room":<id>}` cancels only that join (a late Cancel no longer stops the next Join); without a room as before.
- `{"cmd":"join",...,"leave":true}` (launcher 1.4.10, after the player said yes): on the lobby screen of another room the DLL
  presses Back (up to 3 times, 9 s), waits until the game is out of that room, then joins as usual; a `leaving` event tells the
  launcher. An experiment: every command of the lobby screen goes to RoomJoin.log ("lobby screen: ..."), to find its real
  leave command if Back is not it. The status says `"version":"11","proto":2`.
  On the stand (05.11) leaving a lobby by hand logged `...panel_mc.OnClickBack` on its screen: Back is that command.
- Leaving for another lobby (stand 05.11): Back put the games list up in 0.4 s while the game was still in the old room, and
  that came out as "another lobby" - the list then, no join. Now off the lobby screen the DLL waits (9 s at most) until the game
  is out of the room it leaves, then goes on as from that screen (the list: Back, the main menu, the join). The front end is
  rebuilt after leaving a lobby: the 15 s for finding the menu count from the new menu movie.
- The cover takes no click in its first 0.8 s (stand 05.11: the second click of a double click on the launcher Join came to
  the game, just brought to the front, right on the cover Cancel 0.5 s after it came up); Esc still works at once.

## 0.2.6 — 2026-11-05
Cooldowns 8 (store cooldowns-8) and APM 3 (apm-3), the reports of 04.11.2026 (siritos, berserk, TvangeS).
- Cooldowns: the recharge key was a hash of all 16 bytes of ConstOwnerPointer at button+0x2A8. Its constructors (SimEngine.dll)
  set the type and the one pointer of that type only (entity +4, squad +8, player +0xC); the other words are stack left-overs, and
  Trace=1 showed the same squad coming with two different key words in every second update. One recharge thus had several
  records, a remembered one was lost after the selection went to another unit or a building, and the number stuck or did not
  follow ("it blocks the cooldown when you click a generator under construction"). The key is now the type + that pointer,
  exactly what ConstOwnerPointer::operator== compares.
- Cooldowns: the time left was worked out again from every sample of the share, also when the share did not move - a share that
  stands still gave a number that stands still ("11" instead of a count down). A recharge now has to move: an engine answer the
  game itself has not shown as a recharge (reason 0xD/0xE) counts only after it has grown, and a share that stays put for 15 sim
  ticks (1.5 s) of samples is no recharge - no number.
- Cooldowns: the game's own verdict of the update (+0x208: usable - no reasons, or only the non-blocking 4-8) decides. A usable
  ability gets no number, and the dark sweep is never put back on a usable button. Cooldowns 7 could do that whenever the engine
  still gave a share for an ability the game called usable (a record under a stale key, charges): the button the player could
  press looked unavailable. berserk 04.11 ("abilities broke, the cursor says it can't be used"): his log has none of JumpRetry /
  RoomJoin / SteamKeep (new that evening: NetProbe 2, ReinforceFix 1); on the stand with his exact add-on set the abilities
  worked and no such push was seen; the HUD button's click checks only +0x208, not the sweep - not reproduced, this closes the
  one way Cooldowns could make it look so. Stratagems keep their number while usable (the bunker, siritos 03.11), no sweep.
- Cooldowns: Trace=1 in Cooldowns.ini writes every change of a button's state (owner words, reasons, the game's and the engine's
  share, the decision) to Cooldowns.log; off by default.
- The Techmarine's zeal buttons (mines, turret) are not ability buttons of 0x0111AFBC (Trace=1 shows no update of them):
  Cooldowns puts nothing on them, what is written there is the game's own.
- APM: a new match was only seen through a frame with no sim ticks (APM 2 g_matchMs0 was always 0); after a match left through
  the menu or ended on the game's own screen no HUD frame came any more, so the next match went on with the old numbers
  (TvangeS' log 04.11: four matches, one "match: counting"; stand: quit through the menu - no "match over"). A new match is now
  the match core's tick count going back to 0. The log names the version (APM 3) and the Place it read.
- APM: on an APM.ini change the number is made again where Place says (g_host reset too); only a movie updating now is touched.
  Place at the start and Place changed while the game runs checked on the stand (minimap / top fallback in annihilation).

## 0.2.5 — 2026-11-05
RoomJoin 9 (store roomjoin-4, staging; branch feature/roomjoin-9-pipe): the launcher talks to the game over a named pipe instead
of a request file the DLL polled (owner: no files), and the review of RoomJoin 8 (reports/roomjoin8_review.md) is closed.
- The pipe: `\\.\pipe\EliteRoomJoin-<pid of DOW2.exe>` - one per game process (two copies of the game = two pipes, the launcher
  picks the one in the main menu). DACL: only the user the game runs as (D:P(A;;GA;;;<user SID>)), PIPE_REJECT_REMOTE_CLIENTS,
  FILE_FLAG_FIRST_PIPE_INSTANCE (nobody can hold the name before us); the launcher checks GetNamedPipeServerProcessId and opens
  it with SECURITY_IDENTIFICATION. Up to 4 clients at once. One JSON object per line:
  launcher -> game `{"cmd":"join","room":<id>,"name":"..."}`, `{"cmd":"status"}`, `{"cmd":"cancel"}`;
  game -> launcher `accepted` / `walking` / `joining` / `in_lobby` (`"late":true` for a late answer) / `failed` with `reason`
  (full, closed, password, version, timeout, busy_search, busy_match, busy_join, not_menu, other_lobby, cancelled, error) and the
  game's own message in `text`; `status` (screen main / find_game / games_list / lobby / other / unknown / match, in_match,
  searching, room, joining, version "9", proto 1, pid, stalled). A status line goes out as soon as a client connects - the
  launcher's heartbeat: no pipe in a running game = no RoomJoin 9 in it. The reasons "full" etc. come from the game's message box,
  matched against its own texts in its language (Localizer::GetString of the DOW2.ucs ids 550050...), English words as a fallback.
- Linux (launcher native, the game under Proton): roomjoin/rjbridge.c = Elite_DLLs/rjbridge.exe, shipped with the add-on
  (addons.json "files"). The launcher starts it with the environment of the game's own wine process (/proc/<pid>/environ:
  the prefix, ESYNC/FSYNC - a wine client with other sync settings is refused by the game's wineserver - DLL paths; without
  WINESERVERSOCKET / WINELOADERNOEXEC / WINEPRELOADRESERVE) and its loader, so it joins the game's wineserver; it opens the
  game's pipe (server process id checked, SECURITY_IDENTIFICATION) and passes bytes stdin -> pipe, pipe -> stdout unchanged.
  Ends when the pipe closes or stdin ends; no game / no pipe: one line {"ev":"bridge","error":"no_game|no_pipe"}, exit 2.
  Chosen after checking the others (owner: no port if possible): AF_UNIX works in Wine 11.20 (the socket file is a real
  host socket a native process connects to; Windows' sun_path is 107 characters) but Proton 9 / 10 (GE-Proton9-27 = Wine 9.0,
  GE-Proton10-34 = Wine 10.0) answer socket(AF_UNIX) with WSAEAFNOSUPPORT; `proton run` does not pass a program's stdout
  (its steam.exe starts it). TCP 127.0.0.1 with a key was not needed. Measured from the bridge's start to the game's status
  line: 23-29 ms on stand 1 (Wine 11.20, the real game), 30-62 ms with GE-Proton 9 / 10 (a stand-in game with the same pipe).
- The pipe thread never touches the engine: join / cancel go into a queue the game's thread takes in its frame hook (as the
  request file before); "status" is answered from what that thread noted last ("stalled": no frame for 3 s). Clean exit (BeArD's
  exit hang is being looked into, RoomJoin was a suspect): nothing in DLL_PROCESS_DETACH, all I/O overlapped (the thread only
  ever waits in WaitForMultipleObjects), no C runtime or heap lock held by it (the log is one handle + WriteFile, rotated at
  512 KB), 64 KB stack. The pipe thread is the frame-hook installer thread: no frame hook = no pipe.
- The old file: Elite_DLLs/RoomJoin.txt of launcher 1.4.8 is still read (once a second in the menu, 3 minutes at most) so 1.4.8
  keeps working with roomjoin-4; launcher 1.4.9 only uses the pipe and deletes a leftover file. RoomJoin_result.txt is gone.
  The file goes in RoomJoin 10.
- Review p.1: a panel that is not one of ours (Options, Army Painter, Campaign, Replays, the match summary) is SC_OTHER, not the
  last screen; the probe looks at every visible panel (Options sits over a transition movie) and skips the transition movie; no
  "after 6 s taken as the main menu" - an unknown screen for 15 s, or another one for 4 s, is "not_menu".
- p.3: one join call per request; no second one on top of a slow first; after a timeout or Cancel with the call made, the room is
  watched 15 s: if the game lets us in late, the lobby screen opens and "in_lobby" late goes out. No public games list on a timeout.
- p.4: never Back on Find a Game (with or without a search: "busy_search" / "not_menu"), nor anywhere while in a room; the dead
  mm_cover() (no _level0.mmcover in MMFind 0.45) is gone.
- p.5: the menu movie is forgotten at World::SimulatePre (a match or a replay starts; the world flag ends at SimulatePost or when a
  menu panel loads) and - new - the moment it is deleted: the movie class's deleting destructor (vtable 0x0104BBFC slot 0 =
  0x00DCE310) is wrapped. Found on the stand: after Options a Join crashed the game (the front end had been rebuilt, the old
  movie pointer dangled).
- p.6: the import-slot hooks set *orig before the slot points to them. p.7: the room name and the game's message are
  HTML-escaped on the cover. p.8: the cover takes clicks and Esc - the menu under it is hidden (_visible = 0) and the game
  window's procedure is wrapped while the cover is up (Esc = Cancel; a click = the logical primary button, so a left-handed
  mouse works; fallback GetAsyncKeyState with SM_SWAPBUTTON). p.10 (launcher): the game window by its process id. p.11: the
  log rotates, no profile id in it, the name buffers end with a NUL.
- roomjoin/rjpipe.c: a console test client of the pipe (make build/rjpipe.exe; works under Wine in the game's prefix).
- Linux path on stand 1: the launcher's own bridge code (roomjoin.rs BridgeConn, a test build) from the Linux host with an
  empty environment -> rjbridge.exe -> the game: status in 28 ms, join, events, cancel; the game closed through its menu in
  2.3 s with the bridge attached, the bridge ended with it.
- Tested on stand 1 (Wine, Elite 3.2, MMFind 0.36, LobbyStats, Relay, EliteAI): status; join of a room that does not exist
  (timeout after 25 s, 15 s watch, cover gone, the menu back); cancel from a second client, from Esc and from the cover's Cancel
  button; a click on the hidden menu under the cover does nothing; busy_join; not_menu from Options and from Find a Game (no Back
  pressed); from the public games list: Back, then the join; a live public Elite room (3v3, 6 players): in the lobby 4.6 s after
  the join, a second join of it = in_lobby at once, another room meanwhile = other_lobby; the launcher's own Rust client
  (roomjoin.rs) under Wine; the old RoomJoin.txt; the game closed through its menu (Exit, Yes) in 2.4 s twice - with a client
  connected and with a 15 s watch running; the wrapped destructor ran at the exit. Not on the stand: busy_search with a real
  search (it would match real players), busy_match.
## 0.2.4 — 2026-11-05
elitefix_wmic 2 (store elitefix-nowmic-2, Elite.exe, released by hand; branch fix/elitefix-nowmic-2): Elite.exe ("Universal
launcher for Dawn of War II mods") stayed in the task manager after every game - closed from the menu or crashed (BeArD, fb 242).
- Cause: Elite.exe ends only through its game watch, a detached thread (0x140008f40) started right after the game's window
  shows up: every second it looks for DOW2.exe and exit(1)s the starter once the game is gone. Its main thread never ends by
  itself: after 30 s it joins the splash thread (Elite.bmp in a STATIC window, a message loop that never gets WM_QUIT).
  elitefix-nowmic-1 jumped from 0x14002095d - the start of the watch - straight to the 30 s sleep (0x140020c31), past the wmic
  check AND the watch: no watch, the starter waited for the splash thread forever. The launcher also counts a running Elite.exe
  as a running game (add-ons locked, Play refused) until it is killed. None of our DLLs is involved (same without any add-on).
- Fix: the original jump site is back; `jmp 0x140020c31` now sits at 0x140020976, after the watch thread is started and
  detached - only the wmic check (0x140007ff0) is skipped. `elitefix/make_elite_exe.py` makes it from Elite's Elite.exe
  (md5 ba5b6bdd... -> eb6b56d5...).
- Tested on stand 1 (Wine, Elite.exe the players' way, BeArD's add-on set, quit from the main menu): nowmic-1 - the game is
  gone in 2 s, Elite.exe still there after 60 s (2 threads: main + splash; the same without any add-on); nowmic-2 - Elite.exe
  (3 threads, the watch among them) gone 1 s after the game, also when the game process is killed (the crash case).

## 0.2.3 — 2026-11-04
ShellFix 1 (store shellfix-1, new add-on, default off; branch fix/shellpatch-hook): BeArD's crash at the start of every match.
- Cause (CrashLog dump 04.11 23:12, 0xC0000005 at DOW2.exe+0x285450 reading 0x00000209, ecx=0, return to ShellPatch.dll+0x3E65):
  Elite's ShellPatch.dll (the race meter above the command panel) patches four jumps into the HUD code and wraps the meter
  shell's activate 0x00B6CF40 with Detours (ShellPatch+0x3A10, which reads the player's race). Its Detours 3 looks for a
  trampoline only in target +-2 GB without 0x70000000-0x80000000 - for 0x00B6CF40 the lower 1.75 GB. With that full when
  ShellPatch starts (Intel + NVIDIA drivers in the process, the game's reservations, the add-ons loaded before it), the
  attach fails, ShellPatch does not check it: the jumps are in, the wrapper is not, the race string stays empty and its
  jump at 0x00B6D0A7 calls SetVisible(NULL, 1) = DOW2.exe 0x00685450 (writes widget+0x209) on the first frame of the
  match HUD. BugSplat closes the game (TerminateProcess -3). In the dump the game's frame returns straight to 0x00AD5A92 (no
  wrapper), "address space free 2024 MB" of 4 GB at the start, heap objects at 0x7Dxxxxxx/0x9xxxxxxx. None of our add-ons
  hooks this code; they only take address space (thread stacks, 64 KB VirtualAllocs) before ShellPatch starts.
- ShellFix waits for ShellPatch.dll (LoadLibrary of it waits for its DllMain), checks its build (stamp 669C7309), its jumps
  in and 0x00B6CF40 untouched with its Detours pointer (ShellPatch+0xF2A0) still 0x00B6CF40 - then puts the wrapper in as
  Detours would: trampoline (push ecx/ebx/ebp/esi/edi + jmp 0x00B6CF45, MEM_TOP_DOWN), the pointer, jmp ShellPatch+0x3A10 at
  0x00B6CF40 (shared patch mutex, WriteProcessMemory). Otherwise nothing. Its own thread reserves 64 KB of stack, not 1 MB.
- Tested: stand 1 (Wine, IG vs AI, -dev -init, Profiler's loader released at the start): the lower address space filled
  before ShellPatch starts (stand-only test DLL) = the same crash (CrashLog: the same offsets as BeArD's) 1 s into the match;
  with ShellFix "REPAIRED", the match goes on, ShellPatch's race string set; BeArD's full add-on set + ShellFix: "hooked in as
  it should - nothing to do", no exceptions.

## 0.2.2 — 2026-11-04
Relay 22 (store relay-22 / relayfwd-22, released by hand; branch feature/relay-22): the relay chain between our game nodes.
- A native game (our lobby server's room: the game connects to one of our nodes on the battle port) whose node this player
  can't reach - no UDP answer, or the stream check of Relay 18 failed (DPI) - goes to the player's own nearest usable node
  with the preamble "DWR4" + IPv4 + port (BE) of the room's node; that node passes it on to the room's node over our UDP
  tunnel between the nodes (dow2-ladder feature/relay-chain: battle_relay.py DWR4, du_client.py). Player -> his node -> the
  session's node. No other node answers: the room's node itself over UDP, then TCP, as in Relay 21. Before, such a player
  could not join a native match at all.
- The pings offered to the matchmaker (DOW2_RELAY_PING, through MMFind's relay_ping) start with "0.0.0.22:22" - "this player
  can chain": the ladder (_best_relay chain) may then put the match on a node he doesn't reach, choosing the node with the
  lowest worst delay of all players (a chain = ping to his nearest node + node -> node round trip + 10 ms). Older ladders
  ignore the key (it is not a node of relays.json); MMFind is unchanged.
- Unchanged: Relic games / forwarding (DWR3), our automatch of older clients (DWR2), the pings and the stream check.
- Needs the nodes with battle_relay.py of dow2-ladder relay-chain (DWR4); an older node answers DWR4 with nothing (closes).
- Tested: stand 1 under Wine (stands lock), native room of the TEST lobby on Moscow 83.217.192.34:47190 (test instance),
  UDP+TCP to it dropped by iptables: "the room's node ... is not usable - chain on to the room's node 83.217.192.34" through
  Germany 116.202.106.150:47190; Germany: chain -> pass_on via du; Moscow: the game's session joined, the game's
  battleserverrelay Connected, pings both ways for 40 s; iptables removed, stand restored (md5 ok).

## 0.2.1 — 2026-11-04
Texts for players (siritos 04.11.2026: the add-on texts and the news were written for us, not for players) and RoomJoin 8.
- RoomJoin 8 (store roomjoin-2; review of roomjoin-1, 03.11): the automatch first - while MMFind's cover is up (its walk
  through the menus to a found match) or a search runs on Find a Game (the game's own search animation, or Start Searching
  pressed there), a Join is dropped ("fail automatch" in RoomJoin_result.txt) and the screen is not touched: no Back on Find a
  Game any more (a notice to leave it first; over MMFind's cover nothing is shown). The fscommand hook is now the handler's
  vtable slot 0x010BAC9C (its only reference in DOW2.exe 3.19), not a patch of 0x681AB0: the code MMFind checks when it
  starts stays original, so RoomJoin can load before or after MMFind (the deferred first-frame hook is gone). The screen is
  forgotten when a match loads (after a match: a false "You are in another lobby"). The per-request state (menu movie known,
  probe, Back timer) is reset with each request, not by a frame within 50 ms. RoomJoin.ini [RoomJoin] Enabled=0: off.
  Built, not yet tested in the game.
- docs/store-texts.json: new name, description and category of every add-on in the store, in plain words, 5 languages;
  copied into addons.json (its 14 add-ons). docs/ADDON_TEXTS.md: how to write them. scripts/store_texts.py: check them, copy
  them into addons.json, or into a store launcher.json for the hand-released add-ons (automatch, relay_r2, relay_fwd,
  elitefix_wmic, rusifikator; nothing in the store is changed by this commit).
- Every add-on names its group and its place in the group in its own entry: "category" (network / interface / gameplay /
  fixes / experiment; no "other": the translation is interface) and "order" (a number, smaller = higher) - in
  addons.json, docs/store-texts.json and roomjoin/catalog-entry.json; the deploy carries both into the store, launcher
  1.4.8 places add-ons by them.
- Player notes: changelog/<version>/en.md + ru.md (from 0.2.1 on; what is released stays as it is). The GitHub Release of an X.Y.0 is
  made from them instead of this file, and the launcher shows them as news (ru.md for Russian players, as for the launcher);
  scripts/release_notes.py check fails without them.

## 0.2.0 — 2026-11-04
Everything released to players on 03-04.11.2026 is in main (PR #11, release/main-0311, merge 31f3ea5); README, addons.json and
docs now match the launcher store.

**In the store on 04.11.2026** (store3/launcher.json). Store id -> version -> what is inside (the version each DLL writes to its
log) -> the folder. The source of each released DLL in main is the same as in the commit it was built from (listed in the
sections below).
- automatch -> mmfind-0.45 -> "Automatch v2.0", on by default: Elite_DLLs/MMFind.dll (MMFind 0.45, c845fbc) + Elite_DLLs/OwnLobby.dll
  (OwnLobby 2, 7cc5f42) + winmm.dll next to DOW2.exe (vanilla-loader -DEARLY_ONLY). One tick by the owner's decision of
  03.11.2026 (docs/ADDON_RULES.md). Only the native automatch on our servers; no classic flow, Relic's search is never started.
- relay_r2 -> relay-21 -> Relay.dll (Relay 21, 4693a39), on by default; relay_fwd -> relayfwd-21 -> RelayForward.dll (Relay 21).
  UDP only, copies of recent packets; from Moscow the way on to Relic goes through iggy (dow2-ladder gameserver/egress).
- steamkeep -> steamkeep-2 -> SteamKeep 2 (c6d8d01)
- jumpretry -> jumpretry-2 -> JumpRetry 2 (add4085)
- reinforcefix -> reinforcefix-1 -> ReinforceFix v1
- roomjoin -> roomjoin-1 -> RoomJoin 7 (72d832c, PR #10) - siritos's experiment
- cooldowns_r2 -> cooldowns-7 -> Cooldowns 6 (a38a5a6)
- matchtimer_r2 -> matchtimer-8 -> MatchTimer v5 (a38a5a6)
- apm -> apm-1 -> APM 1 (a38a5a6)
- lobbystats_r3 -> lobbystats-0.11.0 -> LobbyStats 0.11.0
- loadingratings_r3 -> loadingratings-4 -> LoadingRatings v0.10.1 (0f725df)
- joinsound -> joinsound-1 -> JoinSound 1
- netprobe -> netprobe-2 -> NetProbe 2 (experiment)
- crashlog -> crashlog-1 -> CrashLog 1
- obsfix_r2 -> obsfix-1 -> ObsFix v1
- reverse_r2 -> reverse-4 -> Reverse 4
- elitefix_wmic -> elitefix-nowmic-1 -> Elite.exe without the wmic check (not built here)
- rusifikator -> rusifikator-text-1 -> Gosudar's Russian text (not built here)
- frameprobe_r2 -> frameprobe-off, eliteai_off -> eliteai-off: turned off for everyone.

**Gone from the store:** ownlobby (in automatch since mmfind-0.44-r2 / 0.45), continue-1 (taken out on 03.11.2026: Relic's relay
closes the match session 61 s after the match; continue/ stays here as research), the classic automatch, the fast automatch with
private rooms and the menu rematch (MMFind 0.45).

**This version (docs and build only, no DLL changes):**
- README: the add-ons as the store has them - what each does, store id and version, folder, whether it can touch sync; what is
  gone; how a release really goes (elite-release on rts; the dow2-patches-deploy timer is off on purpose).
- addons.json: the store's ids, versions and texts for the 15 add-ons it can describe; the hand-released ones are listed in its
  `_readme`. Nothing to publish: every version in it is already live.
- docs/launcher-catalog-timer-apm.json removed (its entries are in the store and in addons.json); docs/ADDON_RULES.md: the
  Automatch v2.0 exception.
- Makefile: `make` also builds RelayForward.dll, OwnLobby.dll, CrashLog.dll and NetProbe.dll (released, but only built by name
  before), so the GitHub Release carries every released DLL of this repo; build/early/winmm.dll stays a target of its own.
- The sections below that said "not released" / "branch ..." are marked with what was released.

## MMFind 0.45 (branch feature/mmfind-0.45; released 2026-11-03 as mmfind-0.45, "Automatch v2.0")
- MMFind 0.45: only the native automatch (owner 03.11.2026: "the automatch is tested - remove the old one, keep only the
  new one"; "never start Relic's search - it does not work anyway"). Start Searching asks the ladder (mm/native) and, on its
  "yes", goes to the game: the game's own Find a Game search on our lobby server (OwnLobby.dll), as in 0.38-0.44 (poll 1 s,
  countdown 2 s, X-Lobby NM- replays, the info block and the queue on the screen, searching again after a match the server
  called off). On "no" the click is NOT passed to the game - Relic's matchmaker matches nobody - and the screen says why and
  what to do: "Automatch v2.0 is not available: restart the game from the Elite launcher" (no OwnLobby.dll, or the game did
  not sign in through our lobby server), "our lobby server does not answer - try again in a minute", "switched off on the
  server", "Team FFA is not in the automatch"; the next click asks again. A search of the game that runs anyway without our
  native search behind it (on for 1.5 s on Find a Game while MMFind is idle; 5 s grace after a native search of ours) is
  stopped at once (Stop Searching, 3 times at most). 10 s without the ladder seeing the native search: the game's search
  stopped, the reason on the screen (was: the classic flow).
- Removed with the classic flow (0.12-0.44): mm/join / mm/host / mm/room / mm/started / mm/ruined, the menus walked
  (Back / Public Match / Host / Join / Private Match), the "Match Found" cover and its Cancel, the host's game options,
  slots, kicks, swaps and observers (the OnClickHost, slot menu and swap-request detours are no longer installed), the
  "Watch a match" item (Team FFA is the game's own again), the ruined-loading marker, DirectStart / Fast / Manual / TestBots
  / AutoSwap / ReadyField / Native=, and the 0.43 menu rematch (RematchMenus / RematchTest; the game's own Continue is
  Continue.dll). The DLL is 2455 -> about 1180 lines.
- Package mmfind-0.45 = Elite_DLLs/MMFind.dll + Elite_DLLs/OwnLobby.dll + winmm.dll (EARLY_ONLY), both from ownlobby-3
  (launcher "Automatch v2.0", one tick).

## Store on 2026-11-03 (released; superseded by 0.2.0 above)
What the launcher store (store3/launcher.json) gave players on 03.11.2026, all in this tree (branch release/consolidate-0311).
Store id -> the version the DLL writes to its log -> the commit the released DLL was built from. Each module's source here is
the same as in that commit (`git diff <commit> HEAD -- <module dir> common` is empty); rebuilt with the same compiler (and the
same image base / link name), the DLLs are byte for byte the released ones but for the PE timestamps.
- automatch: mmfind-0.44 -> MMFind 0.44 (with 0.39 native fast, 0.42 search kind, 0.43 rematch, off by default) -> fe58795
  (feature/native-continue). Built as MMFind044.dll and renamed. Continue.dll (Continue 1, same branch) stays in the tree but
  was taken out of the store on 03.11.
- relay_r2 / relay_fwd: relay-21 / relayfwd-21 -> Relay 21 (on Relay 20) -> 4693a39 (feature/relay-21, on feature/relay-20 773d547)
- steamkeep: steamkeep-2 -> SteamKeep 2 -> c6d8d01 (feature/steamkeep)
- jumpretry: jumpretry-2 -> JumpRetry 2 -> add4085 (feature/jumpretry)
- roomjoin: roomjoin-1 -> RoomJoin 7 -> 72d832c (feat/roomjoin, PR #10)
- cooldowns_r2: cooldowns-7 -> Cooldowns 6 -> a38a5a6 (fix/cooldowns-6-timer-apm)
- matchtimer_r2: matchtimer-8 -> MatchTimer v5 (Place=menu etc.) -> a38a5a6
- apm: apm-1 -> APM 1 -> a38a5a6
- ownlobby: ownlobby-3 -> OwnLobby 2 + the early winmm.dll (build/early/winmm.dll) -> 7cc5f42 (fix/ownlobby-2-moscow)
- reinforcefix: reinforcefix-1 -> ReinforceFix v1 (main)
- unchanged on main, checked the same way: lobbystats-0.11.0 (LobbyStats 0.11.0), joinsound-1 (JoinSound 1), netprobe-2
  (NetProbe 2), crashlog-1 (CrashLog 1).
- loadingratings-4 was built on 02.11 from 0f725df (lobbystats.c with -DLS_LOADING, "LoadingRatings v0.10.1"); the tree now builds
  LoadingRatings from the later lobbystats.c (0.11.0: the join sound moved to JoinSound.dll), not released as LoadingRatings yet.
The sections below with "branch ..." in the heading are the details of these versions.

## Relay 21 (branch feature/relay-21; released 2026-11-03 as relay-21 / relayfwd-21)
- Relay 21 (Relay.dll + RelayForward.dll). No global lock on the hot path: Relay 20 took one critical section and walked 64
  slots on every send / WSASend of every socket in the game (the IAT of all modules is hooked) and on every connect, so all the
  game's network threads queued on it. A socket is ours only while its preamble waits (marked lock-free at the redirect, a
  counter of waiting slots); every other send / WSASend / connect passes with one memory read, the lock is taken only when a
  preamble is really due. Only TCP connects to 27000-27099 and to our nodes' 47100 are looked at: a UDP socket that connect()s
  and everything else (launcher / ladder / lobby server API and chat, Relic's HTTPS lobby) pass untouched before any work.
  NetProbe.dll is never hooked: its TCP 27015 test to our nodes went into our UDP tunnel as if it were the game (stand 03.11).
  Stand 1: online Private custom game on Relic (relay_r2 on, forwarding off) - pass-through, no "ms between calls".
  Measured under Wine 11.20 (the send hook path, the original send a stub, 16 threads on their own sockets): Relay 20 mean
  6.2 us per call, 80% of calls over 1 us, 2.26 M calls/s; Relay 21 0.23 us, 0.007% over 1 us, 27.7 M calls/s; on 4 cores
  Relay 20 had 1935 calls over 1 ms, Relay 21 99 (the scheduler).

## Relay 20 (branch feature/relay-20; in relay-21, released 2026-11-03)
- Relay 20 (Relay.dll + RelayForward.dll). Redundancy in the UDP tunnel: the SYN carries version 2; a gateway that knows it
  answers SYNACK + 2 and both sides send DATA2 (each data packet also carries copies of the newest 2 earlier packets not
  acknowledged yet) and ACK2 (an ack with such copies; answered only when a copy filled something). The copies go only while
  the datagram stays within 1240 bytes, so turns and ticks get them and big transfers do not. An old gateway answers a bare
  SYNACK -> the old DATA, no copies; Relay 18/19 with a new gateway likewise. Native connections of our lobby server's rooms
  (the game connects to one of our nodes on 47100 itself) now go over UDP to that node's udp_gateway, TCP to the node when
  UDP does not answer (3 s). Needs udp_gateway with DATA2 (dow2-ladder feature/relay-20) for the copies; works without it.
  Tested: Linux netns, netem 40 ms each way, the tunnel code of Relay 19 / 20 under Wine against the old / new gateway:
  loss 10% - p99 game->server 422 -> 141 ms, server->game 242 -> 140 ms (p95 138 -> 41); no loss - no change.

## Continue 1 + MMFind 0.44 (branch feature/native-continue; mmfind-0.44 released 2026-11-03, continue-1 taken out of the store)
- Continue 1 (new add-on, owner 03.11.2026: "the rematch is mostly a crutch - the game has this logic, the room is put together
  again: the host presses Continue, the others have it greyed out, everyone goes straight into the room"). The game's own
  path is all there (PostGameStatsForm::OnClickNext 0x480A71): the host sends PPMT_TransitionToGameSetup over the match
  session (0x456976), every game goes to game_setup_movie of the SAME session (0x481880; the others get "You have been
  pulled back to game setup by the host"), and the host puts the room back: SetMatchState 0, SetVisible 1. Why it never ran:
  canContinue (0x47F7EF) returns false at once for match type 1 = custom game (settings+0xEAC, from GameInfo+0xC0 at the
  start, 0x447538) - Continue was grey for everyone after every custom game. Continue.dll: canContinue for a custom game is
  asked again with the type out of the way, only while the match session is connected (host + everybody on the post-game
  screen, as for the other types); the SetVisible on Continue (0x480C79) keeps a private room private (the game would list
  it). Guests need nothing (the move is the game's own message). Relic answers [4] to the host's advertisement/update of a
  started custom room - the room is back for the players, only not in the list; dow2-ladder feature/native-continue
  (own_lobby "reopen") then sends updateState 0 and the update again -> [0]. Test option `[Continue] KeepSoloSession=1`: a
  match against computers keeps its session (0x46FE8E), so the host's path runs on one machine.
  Stand 1 (Elite 3.2, OwnLobby -> a test lobby with reopen): Private Match vs computer, Exit Game, Continue enabled by
  itself, pressed: game setup of the same room (same session, slots, map), "a private room - stays out of the list",
  Relic: updateState 0 [0], update [0]; a second match started from that room and its Continue worked again. Without
  reopen: the same, Relic [4] only logged (LogFailure WorldwideUpdateAdvertisementAsync). Not tested: a real second player.
- MMFind 0.44: the 0.43 rematch (menus walked, a new room Rematch-xxxxxx) is off by default - `[MMFind] RematchMenus=1`
  (the old `Rematch=` the launcher wrote is ignored), and it never runs while Continue.dll is in the game.

## MMFind 0.43 (branch feature/mmfind-rematch; in mmfind-0.44, released 2026-11-03)
- MMFind 0.43: rematch on Continue (owner 03.11.2026: "Continue after a match from an ordinary lobby - a new lobby with the same
  people"). The game's own post-game Continue only throws everyone to the main menu after a Custom Games match (Relic ~2016).
  While a lobby that is NOT our automatch is up, MMFind keeps who sits where (room id, slots with Relic profile ids and names,
  my slot, the map, my race / chapter / hero, whether I made the room: Host in the list or Private Match); when its match
  loads that lobby is kept. On the post-game screen Continue is switched on (Receive_SetNextEnabled - the game had it off even
  for the host) and its click (OnClickNext) is taken: the screen is left the way Back leaves it, and the ladder
  (`mm/rematch`, needs dow2-ladder feature/mm-rematch; the key = the old room id, the same in every player's game) picks who
  makes the new room: the old host if he pressed Continue within 6 s of the first press, else the first one who pressed (45 s
  without a room = the next one). He opens a public Custom Games room "Rematch-xxxxxx" (the classic flow: Public Match, Host
  without the name box), takes his old army, sets the old map if the lobby has another one, gives the ladder the room id; the
  others get it in their poll and join it by the id (the list by name after 12 s), with their old army, and move to their old
  slot (free: move; the one sitting there wants mine: a swap, the other MMFind says yes; after 20 s any free slot of the old
  team). Nobody waits for anybody: no auto-start (the host starts as in any lobby), a later Continue (10 min) still gets in,
  players without MMFind can find the room by its name; for 3 minutes the host kicks people who were not in the old lobby.
  Cover "Rematch" with Cancel while the menus are walked; the saved Find a Game choice is back after. When the rematch itself
  is played its key closes (a late Continue: "This rematch is over already") and a Continue after it makes the next rematch.
  `[MMFind] Rematch=1` (default; the launcher's option "Rematch on Continue" under Automatch) / 0 = the game's own Continue.
  `RematchTest=1`: one person is enough (a match against computers), a Private Match against a computer from the main menu.
  Stand 1 (Elite 3.2, test ladder): Private Match vs computer, Exit Game, post-game Continue enabled and taken, a fake second
  player pressed first (no old host flag -> he was picked, gave no room, after 45 s the stand took over), the stand made
  "Rematch-2zcurm" on the old map in 1.7 s and the fake players' poll got its room id. Not tested: a real second player
  joining (needs two stands), 2v2/3v3 slot moves.

## MMFind 0.42 (branch feature/mmfind-0.42-mode; in mmfind-0.44, released 2026-11-03)
- MMFind 0.42: the kind of search shown on Find a Game (owner 03.11.2026: "when we press search, show right away which search
  it is"; "change the search screen so it is clear it is a different one"). Two text fields of the game's own Find a Game panel
  (Scaleform, docs/UI_RULES.md, no Direct3D), made right after the engine's per-frame call into the panel: right of the screen
  title "ELITE: AUTOMATCH ON OUR SERVERS" / "ELITE: LADDER AUTOMATCH (CLASSIC)", and under "Your Party" "Automatch: our
  servers" or "Automatch: classic (MMFind)" with why (not signed in through our lobby, players without OwnLobby are waiting,
  switched off on the server, the lobby server is unreachable, OwnLobby is off, ...), "MMFind 0.42", and the queue of the
  match type and who is online (mm/hello every 5 s), while searching "Searching... 0:12". Russian when the game's own texts are
  Cyrillic (Localizer, as EliteAI; or the Start button). From the moment the screen opens: a peek at mm/native (`"peek":true`:
  the ladder answers as for Start Searching and changes nothing; on entering, every 20 s and on a match type change, only
  while idle); after the click by the path really taken. Hidden while the hero panel or Map Preferences is up.
  `[MMFind] ShowMode=1` (default, 0 = off), `Lang=auto|ru|en`; HeadX/HeadY/HeadSize, ModeX/ModeY/ModeSize, ModeProbe=1 (a grid
  of panel units) for placing. Needs dow2-ladder feature/mm-native-peek (peek without side effects, per-mode queue in mm/hello);
  an older ladder answers a peek like a real ask, so no peek is sent while a native search or its search-again is pending.
  Stand 1 (Elite 3.2, OwnLobby ownlobby-3, production ladder, test group): shown on entering, native search starts and stops.

## SteamKeep 2 (branch feature/steamkeep; released 2026-11-03 as steamkeep-2)
- SteamKeep 2 (owner 03.11.2026: Steam's servers can be away for an hour - the match must be played to the end): no time
  limit. Any number of drops and returns (and the every-frame "logged on?" check 0x4E5900) never end the match; Steam's close
  request (a bare WM_CLOSE) is ignored for the rest of the match once Steam went away in it or while it is not logged on.
  The player's own exits still work: the game's Alt+F4 / menu Quit (OnQuitFinal, untouched), the close button / taskbar
  Close (WM_SYSCOMMAND SC_CLOSE, seen by a window procedure put in front of the game's), F4 held at WM_CLOSE, Windows
  shutdown. After the match the game is not closed, so the result and the replay go out. ini: CloseWindowSec is gone,
  IgnoreSteamClose=1.

## SteamKeep 1 (branch feature/steamkeep; released 2026-11-03 as steamkeep-1, then steamkeep-2)
- SteamKeep 1 (new add-on, not in the launcher catalog yet): the match goes on when Steam goes away. DOW2.exe turns a Steam
  drop (SteamLoginService SetConnected(false) 0x004F6F20 -> bus events 0x0C / 1 / 2) into an end reason in GameObj
  (0x0041FB0E) and leaves the match (GameQuitForSteamInternal 0x00426D9C); a close request that the Steam client sends
  with it (players' logs: SteamServerShutdown -> "GameApp::Loop - shutdown requested") quits the game. SteamKeep skips
  both while a game is running (not menus, not replays); no simulation change. Stand 1 (skirmish vs AI, Steam CM sockets
  destroyed for 60 s + WM_CLOSE to the window): without it the game left the match (end reason 4) and quit; with it the
  match went on (clock 1:55, Steam back after 44 s), a later plain WM_CLOSE still closed the game.

## JumpRetry 1-2 (branch feature/jumpretry; released 2026-11-03 as jumpretry-2)
- JumpRetry 2: the retry goes as soon as the squad can walk again (the ability's own check 0x008645A0/0x00864670, polled
  every 30 ms; LandTimeoutMs 600 as a fallback) instead of a fixed 600 ms, and an order the game still drops (no Ability
  state within TakeMs 250) is sent again; MaxRetries 3, RetryDelayMs 0. Stand 1: the jump starts ~400 ms after falling
  out of the ability (v1 ~1000 ms).
- JumpRetry 1 (new add-on, own tick): an Autarch jump ordered out of range is lost when he hops an obstacle on the way
  (the ability order ends while he is in the air, nothing spent). The local player's own order is remembered and sent
  again after the landing, at most MaxRetries (2) times within WindowSec (10); any other order from the HUD to the squad
  cancels it. Hooks CommandProxy +0x38/+0x34 (0x00AF51A0/0x00AF53C0) and watches WorldDoCommandSquad(s)/Entity imports;
  only the game's own orders, no change to the simulation.

## Cooldowns 6, MatchTimer, APM 1 (branch fix/cooldowns-6-timer-apm; released 2026-11-03 as cooldowns-7, matchtimer-8, apm-1)
- Cooldowns 6 (siritos 03.11.2026: on Catachans one ability's recharge reset another's number, and with no energy the number
  went; the bunker stratagem never got one). Cooldowns 4-5 kept the recharge per button number and its end per ability
  blueprint (the same for every squad of a kind), and right after a cast the squad often lacks the energy, when the game
  sends only 0s. Now every recharge is kept per squad + ability (+0x2A8 owner, +0x74 ability) and asked from the engine after
  each button update (iface vt+0xB4, as LobbyStats 0.9.0 - its crash was GetBounds, not this call), guarded by a vectored
  handler. Stratagems (global_btnNN updated by the ability panel) are asked even with no reasons: the bunker has none 10 s
  after the cast while its 120 s recharge runs. Tested in the game (IG vs AI): Catachan W (30 s) and E (10 s) at once, the
  game sent only 0s for W (no energy) - engine 0.447, the number kept; both ended on time; two squads apart.
- MatchTimer: no second clock after alt-tab (the fallback under Menu fired on restore and the top field was never hidden);
  three places - top, under (the default fallback: annihilation has no victory point counter at all), menu; under set in the
  game: the Menu button stays where the game has it, plate 30 high, Gap 0, TextY 5; menu: the time 3 up, size 18.
- APM 1 (new add-on APM.dll, launcher entry apm): see README apm/. Place=top (right of the clock) | left | minimap (the
  bottom left corner of the minimap frame, also the fallback in annihilation); "apm" small.
- Launcher catalog entries with the Place picks: docs/launcher-catalog-timer-apm.json (matchtimer-8, apm-1, cooldowns-6; removed in
  0.2.0 - the store has them, and addons.json since 0.2.0).

## RoomJoin 1-7 (branch feat/roomjoin; released 2026-11-03 as roomjoin-1)
- RoomJoin 1 (new add-on, experiment, launcher 1.4.5+): Join on an Elite room without a password in the launcher's Lobbies tab -
  the game starts (or comes to the front) and walks into the room by itself under a cover that says what goes on; a no from the
  game (full, closed) is shown with its own text, then the public games list. MMFind is not touched (own DLL, own mutex, hooks
  chained). Tested 03.11 vs live rooms: joined in 1.2-5 s from the game's start; with the game running - fixed after the tests
  (the idle main menu sends nothing: mouse moves posted to it once it is in front; a second Join for the same room is dropped;
  Cancel never leaves a room we got into).

## MMFind 0.39 (branch feature/mmfind-0.39; in mmfind-0.44, released 2026-11-03)
- MMFind 0.39: the game's own automatch faster (owner 03.11.2026: "the ~12 s polling - make it 1 second; cut the 5 s countdown to
  2 s"). `[MMFind] NativeFast=1` (default; 0 = off): only while a native search runs (Start Searching passed to the game until the
  loading screen) two detours of DOW2.exe 3.19 act, put in once, both or none (every byte checked first, log "native fast: detours
  in"): automatch/polling every 1 s while SEARCHING (stock: more than 10 s, `cmp [esp+10h],0Ah` at 0x5FE29E) - in the room it stays
  as the game has it (a poll every second there kept the host busy and it readied up 5-6 s late); the ranked countdown 2 s instead
  of 5 s (SetupArbitrator +0x48 set at "GameMode is ranked, starting countdown", 0x4491BE) - the start still waits for every
  player's validation and match-info ACK. Stands (Elite 3.2, 1v1, test lobby): Start Searching to loading 6.3-6.8 s (0.38:
  9.3-9.6 s); a match that failed in the room: searched again and loading 11.1 s after the first click (0.38: 14.1 s).

## MMFind 0.38 (released as mmfind-0.38)
- MMFind 0.38: "native" mode (owner 03.11.2026: Start Searching straight to the loading screen, fully on our servers).
  `[MMFind] Native=1` (default): with OwnLobby.dll in the game, Start Searching first asks the ladder (`/ladder/api/mm/native`,
  the same body as mm/join, about 1.5 s at most). If our lobby server can serve the game's own automatch, the click goes to the
  game: its own Find a Game search (automatch/startpolling) runs on our lobby server, the ladder's matchmaker pairs the players,
  and the game hosts / joins the AUTOMATCH room and starts by itself - MMFind walks no menus and draws no cover. MMFind watches
  mm/poll every 2 s: no native search at the ladder for 10 s (our lobby server or the ladder away) -> the game's search is
  stopped (the same Start button) and the classic 0.37 flow runs. No OwnLobby.dll, `Native=0`, Manual / TestBots / DirectStart,
  Watch a match, or the ladder saying no -> the classic 0.37 flow as before. After a native match: ended and the replay
  upload as usual (lobby NM-<room>), no ruined-loading marker.
  A native match the server calls off while the game sets it up (the game ends its own search and goes back to Find a Game):
  the ladder answers mm/poll `{"state":"cancelled","again":true}` to everyone who was not the reason - MMFind presses Start
  Searching again by itself (the native path) once the game is on Find a Game with its search off (a notice box is answered
  with OK after 3 s, or ignored after 10 s), at most once per 30 s and 3 times in a row. Test option `[MMFind] TestLadder=host:port`:
  every ladder API call over plain HTTP to that place, no mirrors (the TEST ladder through an ssh tunnel).
  A "cancelled" answer naming a match counts as the native search seen even when it is the first answer (a match that formed
  and failed within 2 s): again:true searches again, otherwise idle - never the classic fallback; a match id already handled is ignored.

## 0.1.6 — 2026-10-30
- MMFind 0.29 (outside addons.json, released by hand): placement by team. The ladder lobby uses random start positions (the game
  shuffles them inside each team, checked on 6p maps), so only the team counts, not the slot. The top half goes to the team that
  already has more players there (a tie: the team of slot 1); the players in the wrong half pair up and the top one asks the bottom
  one to swap ("Request to swap slots", the same handler as "Move to this slot" 0x492E02); the other MMFind accepts it. 2v2 and 3v3
  need one swap at most (checked on all 746 arrangements), instead of the walk through observer slots. The host swaps too.

## 0.1.5 — 2026-10-30
- LobbyStats v0.9.3 - the cooldown numbers get their own tick in the launcher (off by default, as in 0.9.2): addons.json
  now lists the LobbyStats options (match timer, loading screen ratings, join sound and the new `[Cooldowns] Enabled`), so
  the tick writes the key into LobbyStats.ini. The DLL is the same as 0.9.2 but for its version in the log.
  - checked with GCC 14.2 (the server's compiler): 0.9.1 built with it crashes on the cultists' prayer exactly like the
    published DLL, 0.9.2 does not.

## 0.1.4 — 2026-10-30
- LobbyStats v0.9.2 - the crash on abilities fixed (a player: the game closed on W, the cultists' prayer):
  - cooldowns/: the game's GetBounds (0x6859E0) pops its own 4 arguments (ret 0x10) but was declared __cdecl, so the first
    time a button showed a recharge our stack moved by 16 bytes and the game crashed. Declared __stdcall now.
  - the ability button hook asks the engine nothing any more (0.9.0 asked vt+0xB4 for the share of every button with the
    owner cached in the button; the 0.9.1 attempt to ask vt+0xB0 first crashed the game at the start of a match). The
    numbers come from the panel's own SetProgress; hidden or empty buttons are cleared. No numbers out of energy for now.
  - the numbers stay off by default (`[Cooldowns] Enabled=1` in LobbyStats.ini turns them on), as in the 0.9.1 hotfix.
- MMFind 0.28 (outside addons.json, released by hand): slot swap requests are auto-accepted only in a ladder/automatch
  lobby, not in a plain public game; `[MMFind] AutoSwap=0` still always asks.

## 0.1.3 — 2026-10-29
- LobbyStats v0.9.0 (carries the match clock and the cooldown numbers, one Direct3D hook):
  - cooldowns/: seconds left on recharging abilities and global abilities in the middle of the button, over the game's
    own dark sweep, in the game's Caslon Antique New with its text shadow. Read-only (no desync): the share of each
    button from the panel's SetProgress 0x688620, abilities also asked from the engine after the panel's update (out of
    energy the game shows no sweep), the rate per sim tick and remembered per ability of each squad; global abilities
    read exactly (sim ticks left). `[Cooldowns] Enabled, Size (px at 1440), Color` in LobbyStats.ini.
  - main menu: the stats go into the game's own tooltip of the name bar (Relic rating as the title, win rate, games,
    Steam hours), no box of ours drawn over it; `[MenuTooltip] Game=0` brings the box back.
  - loaded late by the Elite injector (the main menu is up by then): the own name comes from Steam and gets its rating.
  - the match clock (and the numbers) go off when the match ends - the loading screen out of the match is built,
    the game is decided or the match scripts stop - instead of staying on the frozen loading screen.
- MatchTimer.dll on its own is gone (the clock has been inside LobbyStats.dll since 0.8.0); an old hand-installed copy
  is still recognised and left to draw the clock alone.

## 0.1.2 — 2026-10-28
- MatchTimer v2: the digits in the game's own Caslon Antique New, read from the player's game files
  (common/gamefont.h, nothing of the game shipped), above the minimap with the game's own text shadow, like the campaign
  timers; Style=2 box, Anchor=0 by the Menu button, Anchor=1 by the VP bar.
- DLLs are stripped (-s): 25-82 KB instead of ~220-280 KB.
- LobbyStats v0.7.5: own rating after the name in the main menu and the stats tooltip on hover over it (the game's own
  Arial Unicode MS, read from its files, in a frame like the game's tooltips); the stats host
  is baked in by default, so CI builds show numbers again; `[Debug] LogInvoke=1` logs the Flash calls of the UI.

## 0.1.1 — 2026-10-28
- MatchTimer v1: match clock (MM:SS of game time, pauses not counted) in a box next to the Menu button, experimental.

## 0.1.0 — 2026-10-26
- ObsFix v1: observer lag fix, the "which slot am I in" lookup is cached for 500 ms.
- LobbyStats v0.7.0: Relic rating next to player names in the lobby and a tooltip with win rate, games and Steam hours;
  the host filter and auto-kick are gone.
- ReinforceFix v1: squads inside an Eldar webway gate keep auto-reinforce.
- CGNumbers v1: control-group numbers above squads, experimental skeleton (render path only).
- FrameProbe v9: diagnostics DLL (frame times, sim ticks, game thread stack sampler).
- winmm.dll loader: loads every DLL from `dow2-patches\` in vanilla and in Elite; each patch refuses to load twice.
