# Dow2-patches

[![build](https://github.com/dow2-launcher/Dow2-patches/actions/workflows/build.yml/badge.svg)](https://github.com/dow2-launcher/Dow2-patches/actions/workflows/build.yml)
[![CodeQL](https://github.com/dow2-launcher/Dow2-patches/actions/workflows/codeql.yml/badge.svg)](https://github.com/dow2-launcher/Dow2-patches/actions/workflows/codeql.yml)
[![secret scan](https://github.com/dow2-launcher/Dow2-patches/actions/workflows/secrets.yml/badge.svg)](https://github.com/dow2-launcher/Dow2-patches/actions/workflows/secrets.yml)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/dow2-launcher/Dow2-patches/badge)](https://scorecard.dev/viewer/?uri=github.com/dow2-launcher/Dow2-patches)

Add-ons for Dawn of War II: Retribution (DOW2.exe 3.19.1.10320, the current Steam build) and the Elite mod, shipped to
players as ticks in the Elite launcher. Plain C, built with MinGW. Every patch checks the bytes it is about to change and
does nothing if the exe is a different build.

**Add-on rule:** one feature - one DLL - one tick in the launcher — [docs/ADDON_RULES.md](docs/ADDON_RULES.md) (one exception:
Automatch v2.0). **UI rule:** add-ons show things only inside the game's own interface (its Scaleform movies), never drawn over
the frame through Direct3D — [docs/UI_RULES.md](docs/UI_RULES.md).

## What players get (launcher store, 04.11.2026; network add-ons 05.11.2026)

The source of truth is the live store, `store3/launcher.json` on rts; this table is that file on 04.11.2026 (main = 31f3ea5,
PR #11). "Sync": whether the add-on can affect the game's synchronisation between players. None of the released ones can: they
are client-side, and the other players in the match don't need them.

| Tick in the launcher | What it does | Store id → version | Folder | Sync |
|---|---|---|---|---|
| **Automatch v2.0** (on by default) | Multiplayer → Find a Game → Start Searching finds a 1v1 / 2v2 / 3v3 match by rating on our servers and starts it within seconds; the game itself runs through our game server over our UDP tunnel (since 05.11.2026 - the separate tick "Games through our servers" is gone). If it can't run, Find a Game says why. | `automatch` → `mmfind-0.46` | `mmfind/` + `ownlobby/` + `relay/` (Relay.dll) + `vanilla-loader/` | no |
| **Forwarding servers (if Relic lags)** | Every other online game (ordinary lobbies) goes through our nearest server on its way to Relic - for players whose connection to Relic gets cut or lags. | `relay_fwd` → `relayfwd-23` | `relay/` | no |
| **Lobby rating** | Relic rating next to each player in the lobby; on hover - win rate, games, Steam hours. Only shows, kicks nobody. | `lobbystats_r3` → `lobbystats-0.11.0` | `lobbystats/` | no |
| **Ratings on the loading screen** | The Relic rating after each player's name on the loading screen of an online match. | `loadingratings_r3` → `loadingratings-4` | `lobbystats/` (built with `-DLS_LOADING`) | no |
| **Sound when someone joins the lobby** | A soft or sharp sound and a flashing taskbar button when a player comes into your lobby while the game is minimised. | `joinsound` → `joinsound-1` | `lobbystats/` (built with `-DLS_JOINSOUND`) | no |
| **Match timer** | Match time in the game's own interface: under the score, under the Menu button, or in it. | `matchtimer_r2` → `matchtimer-8` | `matchtimer/` | no |
| **Cooldown seconds on ability buttons** | Seconds left on recharging abilities and global abilities, in the middle of the button. | `cooldowns_r2` → `cooldowns-9` | `cooldowns/` | no |
| **APM** | Your clicks and key presses per minute, next to the match timer or in the minimap corner. Only you see it. | `apm` → `apm-3` | `apm/` | no |
| **Auto-reinforce through the webway gate** | A squad that goes through an Eldar webway gate keeps auto-reinforce on. | `reinforcefix` → `reinforcefix-1` | `reinforcefix/` | no (the game's own orders) |
| **Hero bar comes back after a revive** (new, staging) | The bar over a hero (icon, health, energy) comes back after he is downed and revived; before, it sometimes stayed gone for the rest of the match (mostly in replays). | `herolabelfix` → `herolabelfix-1` (staging) | `herolabelfix/` | no (interface only) |
| **Sound without fixes (sound bug test)** (new, staging) | Turns the sound fixes off on purpose (the game's old FMOD 4.32.3, no FPS cap) and writes a detailed sound log, for players who try to provoke the sound bug. Conflicts with the sound fix. | `soundbugtest` → `soundbugtest-1` (staging) | `soundbugtest/` | no (sound only) |
| **Replay fast-forward 8x and max** (new, staging) | Two more buttons next to the replay speed button: 8x, and Max (as fast as the PC simulates, the picture a few times a second). | `replayspeed` → `replayspeed-1` (staging) | `replayspeed/` | no (replays only) |
| **Faster drawing: no needless buffer check** (new, staging) | The game no longer re-checks every piece of memory it writes effects and interface quads to: more FPS (with the next one +10 % on a 3v3 replay). | `fastbuffers` → `fastbuffers-1` (staging) | `fastbuffers/` | no (drawing only) |
| **Faster interface: quick texts** (new, staging) | A dropped interface text (tooltip, counter) is found from the end of the list of live texts instead of its start. | `fastlocstrings` → `fastlocstrings-1` (staging) | `fastlocstrings/` | no (interface texts only) |
| **PerfLog** (new, staging, experiment) | Every 5 s writes the FPS and what limits it (graphics card / driver wait, main game thread, which part of the frame) to PerfLog.log, a line with a verdict per minute to PerfLog_summary.log; for "Send logs". Changes nothing in the game. | `perflog` → `perflog-1` (staging) | `perflog/` | no (measures only) |
| **DXVK** (new, staging, performance) | The game's Direct3D 9 through DXVK 2.7.1 (Vulkan): often smoother late fights. Needs Vulkan 1.3; if the game does not start, untick it. In Elite it needs Automatch ticked. | `dxvk` → `dxvk-1` (staging) | `dxvk/` | no (drawing only) |
| **FPS unlock** (new, staging, performance) | Removes the DXVK frame cap (Elite's DXVK mode: 60 FPS); needs DXVK, vertical sync stays the game's option. | `fpsunlock` → `fpsunlock-1` (staging) | `fpsunlock/` | no (drawing only) |
| **UI state filter** (new, staging, performance) | The interface no longer sends the graphics driver the same settings again and again while it draws (repeats of render/sampler state, texture, shaders, buffers dropped inside one display); gain expected with the usual D3D9, none with DXVK (it drops repeats itself). UIFilter.ini: Enabled=0 off, Verify=1 checks every dropped value against the device. | `uifilter` → `uifilter-1` (staging) | `uifilter/` | no (drawing only) |
| **Chat Hide** (new, staging, experiment) | Hides incoming chat from other players during a match; a bubble button by the minimap or Ctrl+Shift+C toggles it. Your own messages and system lines still show. | `chathide` → `chathide-1` (staging) | `chathide/` | no (client-side display only) |
| **Jumps don't break on obstacles** | An Autarch (or another jumper) whose jump order broke on an obstacle on the way gets it repeated after landing. | `jumpretry` → `jumpretry-2` | `jumpretry/` | no (the game's own orders) |
| **Reverse (test)** | Ctrl + right-click behind your vehicles: they back off driving backwards, front armour to the enemy. | `reverse_r2` → `reverse-4` | `reverse/` | no (the game's own orders) |
| **Don't drop out of the match when Steam goes away** | A match is played to the end when Steam loses its connection, however long it is away. | `steamkeep` → `steamkeep-2` | `steamkeep/` | no |
| **Cursor Scale** (new, staging, experiment) | A bigger mouse cursor: Auto by the game's own resolution (height / 1080, so 1440p ×1.33, 4K ×2) or a chosen ×1.25 / ×1.5 / ×2 / ×2.5 / ×3 (launcher dropdown); the UI scale stays. Every cursor state, fullscreen and windowed, Elite and vanilla. | `cursorscale` → `cursorscale-1` (staging) | `cursorscale/` | no |
| **Elite maps** (new, staging, experiment, vanilla only) | Vanilla Retribution: Elite's 1v1-3v3 maps (60 of its 62; the 2 FFA maps are not offered by vanilla) in the map list of skirmish and custom games, read from the player's own Elite installation in the same game folder. EliteMaps 3: in any room, public ones too (EliteMaps.ini BlockPublic=1 keeps them out of public rooms, as EliteMaps 2 did); a player without it crashes joining a room with an Elite map already picked, and with him already in the lobby an Elite-map match does not start (vanilla has no map download) - pick the Elite map once everyone is in. Vanilla maps, the data checksum and the automatch stay vanilla's. | `elitemaps` → `elitemaps-3` (staging) | `elitemaps/` | maps only (no game data) |
| **Overwatch once** (new, staging, experiment) | Right-click (overwatch) on a unit's build button orders one squad: it is built as soon as it is affordable, then the overwatch mark goes off by itself (no surprise second squad). Elite and vanilla. | `overwatchonce` → `overwatchonce-1` (staging) | `overwatchonce/` | no (overwatch lives in the HUD only; at most one build order less) |
| **Speech streak fix** (new, staging, experiment) | Fixes a rare crash of the game when a unit gets a kill: the unit speech about kill streaks read past its list for a unit's first kill. Elite and vanilla. | `speechstreakfix` → `speechstreakfix-1` (staging) | `speechstreakfix/` | no (speech only) |
| **Computer army fix** (new, staging, experiment) | A computer added into a team slot a human left (the host moved to the observers or to another slot) had no army: pink "missing texture" squares where its badge goes, units and buildings in colour 0. Now it gets the default army of its race, live and in replays. Elite and vanilla. | `armybadgefix` → `armybadgefix-1` (staging) | `armybadgefix/` | no (team colours and badges are drawing only) |
| **LangSwitch** (new, staging, experiment) | Alt+Shift, Ctrl+Shift and Win+Space switch the keyboard language inside the game window (for the chat), no alt-tab. Elite and vanilla. | `langswitch` → `langswitch-1` (staging) | `langswitch/` | no (window messages only) |
| **NoHints** (new, staging, experiment) | No beginner hints in matches (select a squad, build a generator, capture victory points, lots of resources, population cap, WAAAGH/faith bar): the skirmish quick tips are not started; the player's own setting is not changed. Elite and vanilla. | `nohints` → `nohints-1` (staging) | `nohints/` | no (local hints only; the data checksum stays) |
| **Documents fix** (new, staging, experiment) | For players whose game forgets its settings and saves no replays because Windows does not let the game process write to Documents\My Games (Controlled folder access, permissions, read-only): the check runs from the game process at the start and after every match (DocsFix.log, one line in the log summary); when the game may not write there, its Documents becomes %LOCALAPPDATA%\EliteLauncher\Documents (the old folder copied there the first time, never overwritten); our add-ons and the launcher (1.5.9) follow it. Nothing changes when Documents is writable. Must be loaded at the game start: vanilla - the launcher's loader; Elite - Automatch's early winmm.dll with DocsFix.dll in its EARLY list (this version's build/early/winmm.dll). Elite and vanilla. | `docsfix` → `docsfix-1` (staging) | `docsfix/`, `common/docscheck.h` | no (file paths only) |
| **UI Scale** (new, staging, experiment) | The in-match interface (minimap, unit panel, abilities, resources, hero portraits, objectives, Menu button) bigger than the game allows: its own `uiscale` can only shrink it (DOW2.exe caps the scale at the movies' own pixel size, so from 1200 lines up - 1440p, 4K - anything above 100 % does nothing, vanilla and Elite alike). Tick on: screen height / 1080 (1440p 133 %, 4K 200 %) times the game's own `uiscale` (the launcher's Settings slider, 75-200 %: at 4K 300 % max), or a fixed percent in UIScale.ini. No launcher change. The main menu and the lobby are not the game's scaled interface and stay as they are. Elite and vanilla. | `uiscale` → `uiscale-1` (staging) | `uiscale/` | no (interface layout only) |
| **Match speed** (new, staging, experiment) | Speed x2 / x4 and a pause for a match where the player is the only person: against computers, or computers against computers watched from an observer slot of a custom game (host it, move to an observer slot, put computers in the team slots). Two buttons at the top left (the replay panel's own pause and 1x / 2x / 4x buttons, where a replay has them; v2 - before by the minimap, where a click also moved the camera) and Ctrl+= / Ctrl+- / Ctrl+P. Nothing happens in a match with another person (player or observer). Elite and vanilla. | `matchspeed` → `matchspeed-3` (staging) | `matchspeed/` | no (one machine only; same simulation, a replay of it plays as usual) |
| **Match marks** (new, staging, experiment) | For testers: a button (top left in a match, by the replay panel's pause in a replay) and Ctrl+M write the moment - game time, live / replay file, map, players, camera - into Marks.log (and EliteAI.log when the new AI plays), for the developers to find it in the AI log and replay it. Elite and vanilla. | `marks` → `marks-1` (staging) | `marks/` | no (only reads; writes a log) |
| **Melee freeze fix** (new, staging, experiment) | The game could freeze for good (and then run out of memory) when a melee unit's last step put it exactly on the centre of the enemy unit it was going for: the game's movement loop for that unit never ended. MoveLoopFix cuts that loop after 256 rounds that used no time, the game carries on. Players and computers alike, very rare. Elite and vanilla. | `moveloopfix` → `moveloopfix-1` (staging) | `moveloopfix/` | no (changes the simulation only where a game without it hangs for ever) |
| **Ctrl attack ground** (new, staging, experiment) | Hold Ctrl and LEFT-click the ground: the selected squads that can attack the ground (wraithguard, D-cannon, predators, tankbustas, zoanthropes...) fire at that point - the game's own Attack ground (core action G) without pressing G; Shift queues it. Ctrl + right-click is not touched (Reverse). Ctrl + left click on a unit, or with no squad that can: the game's own click. Elite and vanilla. | `ctrlattack` → `ctrlattack-2` (staging) | `ctrlattack/` | no (the game's own SCMD_Attack order, the same as G + click) |
| **Observer lag fix (experimental)** | Spectating a live game no longer stutters. | `obsfix_r2` → `obsfix-1` | `obsfix/` | no |
| **Join a lobby from the launcher (test)** | Experiment by siritos: a Join button on the launcher's Lobbies tab (and elite:// links of the ladder site); the game starts and walks into the room by itself. | `roomjoin` → `roomjoin-4` (staging; live: `roomjoin-3`) | `roomjoin/` | no |
| **Fix: the game closes when a match starts** (new, not released yet) | Repairs Elite's ShellPatch.dll (the race meter) when it could not hook in at the game's start - then every match crashed a second in. Otherwise does nothing. | `shellfix` → `shellfix-1` (staging) | `shellfix/` | no |
| **Record game crashes (for the developers)** | Writes where the game crashed and what closed it, for "Send logs". | `crashlog` → `crashlog-1` | `crashlog/` | no |
| **Fix: Elite.exe crash on Windows 11 25H2** | An Elite.exe without its wmic check (wmic is gone in 25H2, Elite.exe crashed on it); it still closes itself when the game ends. | `elitefix_wmic` → `elitefix-nowmic-1`, `elitefix-nowmic-2` (staging) | `elitefix/make_elite_exe.py` (one jump patched in Elite's Elite.exe) | no |
| **Russian translation (by Gosudar)** | Russian text for Dawn of War II and Elite 3.2; the English files come back when it is off. | `rusifikator` → `rusifikator-text-1` | not in this repo (Gosudar's text files) | no |
| Developer diagnostics (off) | Turned off for everyone. | `frameprobe_r2` → `frameprobe-off` | `frameprobe/` | no |
| New AI (removed) | Removed, turned off for everyone. | `eliteai_off` → `eliteai-off` | `eliteai/` | was skirmish-only |
| **New AI** (new, staging, experiment) | Early test of our rework of Elite's computer opponent: the army gathers and fights together, holds its target, pulls hurt squads back. In the skirmish setup "Add NEW AI Computer" (the Normal slot). Elite only. | `newai` → `newai-1` (staging) | `eliteai/` | no (single-player skirmish only; online and replays stay Elite's AI) |

The full texts in five languages are in the store; `addons.json` repeats them for the add-ons it manages (see below).

### Gone or never released

- **Classic automatch** (MMFind 0.12-0.44: the menus walked for you, Public Match / Host / Join, the "Match Found" cover),
  the **fast automatch with private rooms** and the **menu rematch** (MMFind 0.43) - removed in MMFind 0.45. Only the native
  automatch is left: the game's own Find a Game search on our lobby server. Relic's search is never started.
- **ownlobby** as a separate add-on (ownlobby-1..3) - gone: OwnLobby.dll and its early winmm.dll are part of Automatch v2.0.
- **Continue** (`continue/`, Continue.dll, continue-1) - research, not released: taken out of the store on 03.11.2026, Relic's
  relay closes the match session 61 s after the match, so the game's own Continue can't bring everyone back. The code stays here.
- **LobbyStats options** (match timer, cooldowns, loading ratings, join sound inside LobbyStats.dll, up to 0.10) - each is its own
  add-on since 30.10 / 02.11 (docs/ADDON_RULES.md).
- `linux-native/` - fixes for the native Feral Linux port, work in progress, not in the launcher.
- Mac (Feral) <-> Windows crossplay (vanilla, standalone): [dow2-launcher/Dow2-mac-crossplay](https://github.com/dow2-launcher/Dow2-mac-crossplay).

## The folders

mmfind/ + ownlobby/ + vanilla-loader/ - **Automatch v2.0** (`automatch`, mmfind-0.45)
    One tick, three files (owner's decision 03.11.2026, docs/ADDON_RULES.md): Elite_DLLs/MMFind.dll (MMFind 0.45),
    Elite_DLLs/OwnLobby.dll (OwnLobby 2) and winmm.dll next to DOW2.exe (the loader built with -DEARLY_ONLY, build/early/winmm.dll).
    Start Searching asks the ladder (rts.fintebtc.de/ladder/api/mm/native); on "yes" the game's own Find a Game search runs on
    our lobby server, which matches it from the ladder's queue, and the game hosts / joins its AUTOMATCH room and starts by
    itself - no menus walked. On "no" nothing searches (Relic's matchmaker matches nobody) and Find a Game says why and what to do.
    MMFind.ini: server, map wishes, rating spread, chosen race / hero / mode.
    OwnLobby.dll sends the game's lobby service (RLink, HTTPS to dow2r-lobby.reliclink.com) to our lobby server (dow2-ladder repo,
    lobby/own_lobby.py, rts.fintebtc.de:47443), which passes everything else on to Relic. It adds our CA (ownlobby/ca.pem; its
    key is only on the server) next to Relic's before RLink starts, so it must be loaded at process start - that is the early
    winmm.dll; loaded later it does nothing. OwnLobby.ini [OwnLobby] Servers=host:port;host:port. Wine/Proton: winmm=n,b.

relay/ - **Relay.dll** (inside `automatch` since 05.11.2026, loaded by MMFind.dll) and **RelayForward.dll** (`relay_fwd`), Relay 23
    (tunnel: `relay/du_tunnel.h`, tests: `relay/test/`). One source, two DLLs. Relay.dll: an automatch game (our lobby server's room
    on one of our nodes) goes over our UDP tunnel to that node, or through the player's nearest node; its pings go to the
    matchmaker, which picks the node best for all. RelayForward.dll: every other online game goes
    through our nearest node, which passes it on to the Relic relay. UDP only (TCP to our servers gets cut in Russia), our own
    delivery with copies of recent packets; the gateway side is dow2-ladder gameserver/udp_gateway.py. From Moscow the way on to
    Relic goes through iggy (dow2-ladder gameserver/egress). No UDP answer: straight to Relic.

lobbystats/ - **LobbyStats.dll**, **LoadingRatings.dll**, **JoinSound.dll**
    One source, three add-ons. LobbyStats 0.11.0: the Relic rating next to player names in the lobby and a tooltip with win rate,
    games and Steam hours, your own rating in the main menu. It only reads the lobby list. Data from a small web service that
    reads the public Relic API (rts.fintebtc.de, or STATS_HOST at build time, or Server= in LobbyStats.ini), in its own thread.
    LoadingRatings (-DLS_LOADING): ratings on the loading screen; the store's loadingratings-4 was built on 02.11 from 0f725df
    ("LoadingRatings v0.10.1"), the tree builds it from the later lobbystats.c. JoinSound 1 (-DLS_JOINSOUND): the join sound.

matchtimer/ - **MatchTimer.dll** (MatchTimer v5, matchtimer-8)
    MM:SS of game time since the match started (pauses not counted) in the game's own HUD movies (common/gfxui.h); off as soon as
    the match ends. MatchTimer.ini, read again while the game runs: Place=top (under the victory point counter; annihilation:
    under the Menu button) | under | menu; FontSize, Color, OffsetX / OffsetY; OnlineOnly=1 keeps it to live multiplayer.

cooldowns/ - **Cooldowns.dll** (Cooldowns 9, cooldowns-9)
    Seconds left on recharging abilities, global abilities and stratagems, as the button's own count text. Only a recharge the
    game itself shows (the panel's own reasons 0xD / 0xE and its share): no number on an ability not bought yet (reason 3), on
    buttons with no recharge or while something is being built. Each recharge is kept per squad and ability (the owner's own
    pointer, as ConstOwnerPointer::operator== compares); a button the game itself makes usable gets no number and no sweep; a
    share that does not move is no recharge; a number goes when its place shows another unit. Read-only. Cooldowns.ini
    [Cooldowns]: Enabled, DigitSize, Color, Trace (every change of a button's state and its reasons in Cooldowns.log).

apm/ - **APM.dll** (APM 3)
    Presses of mouse buttons and keys while a match runs (an exponential average, Smooth=5 s), read with GetAsyncKeyState; nothing
    goes to the game; starts from 0 in every match (the match core's tick count). Place=top | left | minimap in APM.ini, read
    again while the game runs. Chained with MatchTimer on the per-frame form updates.

reinforcefix/ - **ReinforceFix.dll** (ReinforceFix v1)
    The HUD's auto-reinforce list drops squads with no models on the map - also a squad sitting inside an Eldar webway gate.
    ReinforceFix keeps such squads; the reinforce orders stay the game's own.

fastbuffers/ - **FastBuffers.dll** (FastBuffers 1)
    Spooge.dll locks its dynamic vertex / index ring buffers and then calls IsBadWritePtr on the returned pointer (0x1004D414,
    0x1004D4FA): a page walk under SEH, 8.7 % of the game thread on a 3v3 replay under Wine/DXVK. A successful Lock always gives a
    writable pointer, so Spooge's import slot of IsBadWritePtr (used only there) points to a stub that rejects only NULL. One pointer
    write, no game code changed. FastBuffers.ini [FastBuffers] Enabled=0 turns it off, Stats=1 logs the skipped probes.

fastlocstrings/ - **FastLocStrings.dll** (FastLocStrings 1)
    Localizer.dll keeps all formatted LocStrings in one vector; releasing one (0x10004A80) searched it from the start, past the
    long-lived strings (~16 k on a 3v3 replay), 4.5-5.8 % of the game thread. The same function searching from the end (same
    swap-with-last, same mutex, same frees) is jumped to by one atomic 8-byte write over the entry. FastLocStrings.ini as above.

perflog/ - **PerfLog.dll** (PerfLog 1)
    Measures, changes nothing. Pass = shark::update through DOW2.exe's import slot 0x00F88068 (call 0x40E96A at the top of the main
    loop); a pass in which the draw 0x405935 ran = a shown frame. Entry jumps (prologue bytes checked, copied to a trampoline;
    written on the main thread between two passes) time only the outermost call on the main thread with two QueryPerformanceCounter:
    draw + Present 0x405935, world 0xB7DAA0 (SimVisPanel draw = GameSimVis: world render passes + the world movie with unit bars),
    HUD panels 0x681110, game frame 0x4240F5 (sim ticks + HUD model), visual update 0x439CE8, UI update 0x680C00. Present / EndScene
    of Spooge's device (sp::Device::Instance()->GetD3DDevice(), vtable 17 / swap chain 3 / 42). Main thread CPU by GetThreadTimes,
    mode (settings+0x118 & 0x800 = replay, world 0x013356A4), replay speed (sim rate getter 0x43ECCB), players (PlayerManager
    world+0x194), visuals (Viz manager **0x01335440, proxies +0x2C/+0x30 of 0x40 bytes). PerfLog.log: a line every 5 s (Interval=
    in PerfLog.ini); PerfLog_summary.log: a line per minute with a verdict (vsync / FPS cap, GPU-bound = Present >= 30 % of the
    frame, CPU-bound = main thread CPU >= 80 %, else mixed; the biggest part named). Both trimmed to the newest 256 KB above 1 MB.
    Call sites of the main loop stay free for ReplaySpeed / ReplayTakeover (they call through these entries).

chathide/ - **ChatHide.dll** (ChatHide 1)
    Chat lines go through one display call (0xB207EB -> 0xB214F0, type 0 all / 3 team / 4 system / 5 observer / 6 referee);
    while hidden, 0/3/5/6 return at once. The player's own send (call 0x401D97 -> 0x429672) echoes synchronously and is marked,
    so his lines stay. Toggle: a button clip in the minimap movie (slot +0x10 of form vtable 0x011160D8) and Ctrl+Shift+C;
    a system line per toggle; state in ChatHide.ini. Client-side only.

replayspeed/ - **ReplaySpeed.dll** (ReplaySpeed 1)
    Replay speed = sim ticks per second (10 = 1x) set through the sim rate interface *(*(0x01335720)+8)+0x68 (vtable 0x0108DFAC,
    +0x28 = Game_SetSimRate, no clamp); the panel's one speed button (0xB1FCC0) cycles 5/10/20/40. SimulationController::Update
    (0x438613) runs at most one tick per loop pass, so 8x/Max are limited by the draw: while 8x (80, raised up to 160 until 80 real
    ticks/s) or Max (1000) runs in a replay, the call sites of the visual update (0x40E58D) and draw+Present (0x40E6DC) skip the
    passes between shown frames (30 / 15 fps). Buttons: two copies of fast_forward_btn in playback_panel_v2 (Scaleform), clicks by
    GetAsyncKeyState + the copy's _xmouse/_ymouse; the elapsed time and the progress bar move right to make room.

herolabelfix/ - **HeroLabelFix.dll** (HeroLabelFix 1)
    A downed hero's squad_leader_decorator (vtable 0x011129E4) is hidden by its draw (slot 0x10, 0x00B25070: +0x1C = 0) when its
    update (slot 0x30, 0x00B24370) says not to show him (+0x9C = 0). The movie manager's per-frame walk (0x00680C00) asks only
    shown elements whether they are on screen (slot 4) - and only that test calls the draw - so after the revive (+0x9C = 1)
    nobody shows it again until the decorator happens to be re-created. HeroLabelFix wraps the update: a valid-but-hidden
    decorator that the manager has set up gets the game's own draw once, under the manager's lock (TryEnter).

jumpretry/ - **JumpRetry.dll** (JumpRetry 2)
    An Autarch ordered to jump out of range walks toward the point inside the ability order; a hop over a low wall on the way ends
    that order. JumpRetry sends your own order again as soon as he can walk (up to 3 times within 10 s), through the HUD's
    CommandProxy - the game's own order. Abilities: JumpRetry.ini Abilities=.

reverse/ - **Reverse.dll** (Reverse 4)
    Ctrl + right-click: the selected squads back off to the point, vehicles driving backwards (25 m legs, so the game's own turn
    plans reverse). Only the point of your own right-click is moved. Reverse.ini: Key, StepMeters, LeadMeters.

soundbugtest/ - **SoundBugTest.dll** (SoundBugTest 1, TEST)
    The sound bug's worst case: Shark's fmodex imports -> Relic's FMOD Ex 4.32.3 (SoundBugTest_fmodex.bin) before Shark starts
    FMOD, Shark+0x3907A back to jne in memory (Elite's SMOL byte), no queue merging (SoundFix kept idle through its mutex), no frame
    cap (DXVK_CONFIG). Logs per second (frames, Shark tick time and queue sizes, FMOD channels/virtual/memory, buffer pools,
    match/players/game time) and events (FMOD format vs Windows mix rate, device changes, FMOD callbacks, Shark's trace lines)
    to SoundBugTest.log / SoundBugTest_events.log, written straight through, rotated by size.

steamkeep/ - **SteamKeep.dll** (SteamKeep 2)
    A Steam drop during a match is not turned into an end reason, and Steam's close request (a bare WM_CLOSE) is ignored for the
    rest of the match. Alt+F4, menu Quit, the close button and Windows shutdown still work. Not in menus or replays.

elitemaps/ - **EliteMaps.dll** (EliteMaps 1, vanilla only)
    The game reads its .module (vanilla: DOW2.module or DOW2Multiplayer.module, by DLC ownership, 0x00415CE0) with Util.dll's
    ConfigFile: 0x008019A0 -> 0x008BA240 per section -> 0x008B9EB0 asks archive.01, archive.02 ... of [data:common] through
    ConfigFile::GetString until one is missing. EliteMaps points DOW2.exe's import of GetString at its own function: a missing
    archive.NN of [data:common] (ConfigFile::GetSection) is answered with the next of Elite\Archives\Elite_Data_21_Maps_01.sga,
    Elite_Data_22_Maps_02.sga, Elite_Data_20_UI_Maps.sga (the ones that exist), after the game's own = the lowest priority (no
    vanilla file replaced; Elite's map names are all new). Nothing written to the game's files. The data checksum (0x007A0A30 =
    the CRC blocks + data:maps\pvp\_cameras\*.*) does not see maps\pvp\<map>: it stays vanilla's 371005917. Has to be in
    before the mount (~170 ms after the start under Wine, after SteamAPI_Init); the vanilla loader loads it at ~50 ms. -modname
    (Elite): nothing. EliteMaps.ini Enabled=0: off. EliteMaps.log: the archives found and the archive.NN answered.
    EliteMaps 2: a guest without the maps crashes joining a lobby with one (0x00470581, map info GameInfo+0x50 NULL), so the
    game setup's map handler 0x00487A7B(form, index) is hooked: in a public room (GameInfo +0xC4 = 1) an Elite map pick (also
    the new room's first map) becomes the previous / first vanilla map. Names of Elite's maps from the archives' tables.
    EliteMaps 3: that guard only with EliteMaps.ini BlockPublic=1 (owner: public rooms too); by default no hook.

matchspeed/ - **MatchSpeed.dll** (MatchSpeed 3)
    Speed of a live match = sim ticks per second through the sim rate interface *(*(0x01335720)+8)+0x68 (vtable 0x0108DFAC +0x28
    set / +0x2C get, as ReplaySpeed): 10 = x1, 20 = x2, 40 = x4 (nudged up to +15 % while a small shortfall remains: each drawn frame
    drops the rest of an interval), 0.0001 = paused (the simulation and the AI stand; camera, minimap, selection work).
    SimulationController::Update (0x438613) runs at most one tick per frame: x4 needs 40 fps. Only when nobody else is in the match:
    the game is live (settings+0x118 not 0 / 0x800), every slot of the match's GameInfo (settings+0x118+0x810, 0xA8 each, +0 type)
    is the local human (0), a computer (1), the local observer (2) or empty (6 / 7, and 8 with player id 0 - MatchSpeed 3: the
    match start 0x0044618C writes 8 for a person-kind member slot with no peer, and the engine treats it as closed: no Player,
    0x004269D5), and the match session is not connected (the
    game cuts it for a match with one person, 0x46FE8E); checked at the start and every 2 s, otherwise nothing is shown and the rate
    goes back to 10. Buttons (MatchSpeed 2, 13.11.2026): at the top left of the screen, where a replay's panel has its pause / speed
    buttons and at their size - common/topbar.h: the full-screen HUD movie resource_notifications (form vtable 0x01116478, slot 4 =
    per-frame update, slot 3 = input), an empty clip with the replay panel's own textures (hud/playback_panel/large_btn, _over,
    _down, _toggled_on; icons pause / play, ff = 1x, ff_4x = 2x, ff_4 = 4x), each picture in a holder clip (a load resets the
    target's _visible); clicks by GetAsyncKeyState + the clip's _xmouse/_ymouse, the movie's default mouse callbacks on it, and a
    click over a button is handled in the form's input slot. MatchSpeed 1 had them on the minimap frame, where every click also
    went to the minimap and moved the camera to the map's corner. Keys: Ctrl+= / Ctrl+NumPad+ faster, Ctrl+- / Ctrl+NumPad- slower,
    Ctrl+P pause. Hooks: that movie's per-frame update and input (chained with Marks) and the Menu button's per-frame update
    (0x0110C55C, the keys when that movie stands). MatchSpeed.ini: Enabled, Button, Hotkeys, Scale (100 = the replay panel's
    size), OffsetX / OffsetY. -DMS_TEST (stands only): a replay's slot table is judged and logged as if live, never acted on.

marks/ - **Marks.dll** (Marks 1, "Match marks")
    For the AI testers: a button and Ctrl+M (Marks.ini Hotkey=) write the current moment of a live match (player or observer,
    with other people too - nothing in the game changes) or of a replay into Marks.log next to the DLL: real time, game time
    (World+0x178 tick, 0.1 s each: m:ss, seconds, tick), live / replay and the .rec played (*(settings+0xEC4) "PLAYBACK:x.rec"),
    the map (settings+0x3FC, its name *(settings+0x1AC)), the players (GameInfo slots: +4/+8 name, +0x10/+0x14 race, +0x2C AI
    difficulty, +0x58 team, +0x9C player id = the AI log's "p1000"), the camera (top of GameObj's camera stack as
    Misc_GetCameraPos: eye at +0x34, forward at +0x24; the point it looks at on the plane y = 0). In a live match with EliteAI.dll
    loaded the mark also goes into EliteAI.log (appended next to EliteAI.dll as its own lines: same real clock). Button: live -
    common/topbar.h, after MatchSpeed's two buttons when they are shown; replay - in the replay panel movie (form vtable
    0x011119F4, slot 4, chained with ReplaySpeed), left of its pause button. Feedback: the button turns gold and "Mark N m:ss"
    shows next to it for 3 s, in the game's own movie. Marks.ini: Enabled, Button, ReplayButton, Hotkey, Scale, OffsetX /
    OffsetY, EliteAILog, Debug.

uiscale/ - **UIScale.dll** (UIScale 1)
    The 12 in-match interface movies (minimap 0x00B3F860, unit panel, abilities, resources, portraits, objectives, Menu button...)
    are forms whose layout (vtable +0x18: screen w, screen h, form factor k, &x, &y, &w, &h) multiplies the movie's own size by
    0x00B7C130(h, k) = clamp(h / 1200 * uiscale / 100 * k, 0.5, 1.0) (uiscale = configuration.lua, read once per start through
    SystemConfig::GetSetting("UIScale"), cached at 0x012FED7C). The cap of 1.0 is why the game's UI scale does nothing at 1440p/4K.
    UIScale checks the function's bytes and puts a jump at its start to its own stub with the same contract (2 stack args, ret 8,
    the result in xmm0, eax/ecx/edx kept): scale = S * k, 0.25 ... 4.0. Scale=auto: S = h / 1080 * uiscale / 100 (uiscale read the
    game's way, Util.dll SystemConfig::GetSetting with DOW2.exe's own key 0x013AAFB4, once per start; the launcher's slider writes
    it); Scale=<percent>: S = percent / 100. Every form has k = 1.0 (stand). The front end (main menu, lobby) is not laid out by it
    and stays. UIScale.ini: Enabled, Scale=auto|50-400. UIScale.log: the game's uiscale, one line per screen height / form factor.

cursorscale/ - **CursorScale.dll** (CursorScale 1)
    The game's cursor is an ordinary Windows cursor: 45 48x48 TGAs of UI/cursor/cursorlist.lua -> Plat::Input::CreateCursor ->
    Platform.dll CreateIconIndirect (DIB section + mask), SetCursor on WM_SETCURSOR. CursorScale points Platform.dll's import of
    CreateIconIndirect at its own function, which scales the picture (premultiplied-alpha bicubic or nearest) and the hotspot
    before the real call; cursors made before it was loaded (Elite's Profiler loads add-ons at the front end) are rebuilt in the
    input object's vector. No drawing of our own, no Direct3D. CursorScale.ini: Scale=auto|1.0-4.0 (auto = the game window's client
    height / 1080, i.e. the game's own resolution, not the desktop's), Filter=smooth|sharp.

ctrlattack/ - **CtrlAttack.dll** (CtrlAttack 2)
    The game has Attack ground: core action BindingAttackGround (vftable 0x0111977C, hotkey G). G puts the HUD into targeting
    mode; the next world click goes, through the HUD update's dispatch 0x00AD59C0 (ESI = targeting object: +0x08 the active
    binding, +0x0C the last world click: position, +0x0C, +0x10 button, +0x11 Shift, +0x12 consumed), to the binding's +0x34,
    0x00B70610: the selected squads passing the game's check 0x00B70240 (a weapon with attack_ground = true) and the min-range
    check 0x00B70290 get WorldDoCommandSquads(SCMD_Attack = 48) with the point. CtrlAttack 2 hooks 0x00AD59C0's first bytes: with
    no targeting mode, a new left click on the ground (not on a unit) and Ctrl held, it calls 0x00B70610 with that click, exactly
    as after G, and marks the click consumed when an order went out; otherwise the game's own click. The right click is not
    touched (CtrlAttack 1 used Ctrl + right-click, which is Reverse's). Replays untouched. CtrlAttack.ini: Enabled, Key, Verbose.

overwatchonce/ - **OverwatchOnce.dll** (OverwatchOnce 1)
    Overwatch is the HUD's, not the simulation's: a right-click on a unit button toggles { building id -> squad blueprint } in the
    HUD overwatch object (*(*(0x01335720)+8)+0xCC, map at +0x0C); every HUD update 0x00ACBFB6 calls its tick 0x00B26890, which
    sends the plain build order (CommandProxy +0x0C, CMD_BuildSquad 3) whenever the building can build it, and keeps the entry.
    OverwatchOnce wraps that call: a build order sent by the tick (return address 0x00B26AD8) is remembered; once the building's
    production queue (EntityController::QI(7) +0x54, item vftable +8 = blueprint, +0x60 added counter) has it, the entry is erased
    with the game's own erase 0x00B26BD0 (the second right-click's) and the tech tree is redrawn (BindingBuildTech +0x1ED). Nothing
    in the simulation is touched and no command is added. Bytes/slots checked before hooking. OverwatchOnce.ini: Enabled, Verbose.

armybadgefix/ - **ArmyBadgeFix.dll** (ArmyBadgeFix 1)
    A team slot keeps the race and hero of the human who leaves it, but not his army; a computer added there gets an army only
    when its race is Random or its hero screen is confirmed, so its chapter is empty ("chapterName : , badge : " in warnings.txt)
    and the game draws its badge as the missing texture. At the match start (TeamColourManager FUN_00806F80) the call at 0x0080716E
    goes through a stub: an empty chapter (no badge, no name) is replaced, before the player enters the list, with what the game's
    own GetPlayerChapter (0x00805F80) gives a player not in the list - the first default army of his race - copied with the game's
    chapter assignment (0x004893FB). Drawing only: the lobby data, the replay and the simulation are not touched. 33 bytes checked
    first. ArmyBadgeFix.ini: Enabled.

speechstreakfix/ - **SpeechStreakFix.dll** (SpeechStreakFix 1)
    The unit speech counts kills per entity ("Killing_Streak" of speechtuning) in a sorted vector map {Entity*, count} of its event
    listener (+0x24/+0x28). FUN_00B8FB80 starts from end() and reads it->count also when equal_range found nothing, i.e. 4 bytes
    past the buffer for a unit's first kill - garbage, or an Access Violation at 0x00B8FC24 when the buffer ends at an uncommitted
    page (TvangeS 11.11.2026). 4 bytes at 0x00B8FC1C ("je +2; mov ebp,eax" -> "mov ebp,eax; je 0x00B8FC2D"): not found = 0 kills,
    no read. The 13 bytes around it are checked first. Speech only, no sync impact. SpeechStreakFix.ini: Enabled.
    Built with -DSTAND_PROBE (stand only, not shipped): the same logic in a stub that counts known / new killers.

moveloopfix/ - **MoveLoopFix.dll** (MoveLoopFix 1, "Melee freeze fix")
    The per-unit movement step 0x009141A0 (the mover = its stack argument, ret 4) runs its state machine while the tick's time left
    ([esp+0x54]) is > 0. A melee mover going for an enemy entity with approach distance 0 can land bit-exactly on the target's centre
    with a little time left; then 2 (follow path: at its end, no time spent) -> 4 (EntityUtil::IsWithinDistShifted(self, target, 0)
    is false at equal centres) -> 0 (PathHigh::FindPath from cell X to X "succeeds") -> 2 ... for ever, ~15 MB/s, out of memory
    (fastbench_1113 repro hang_s5_12da6e: tick 3323, 0.00057 s left). Guard: the exit test 0x009144F3 (xorps / comiss [esp+0x54] /
    jae 0x0091451A) jumps to a stub that counts the passes of one call whose time left is bit-equal to the previous pass; at 256 it
    writes 0 there and the loop leaves by its own exit. The count is per mover (256-slot table by address) and reset at the start of
    every call (the step's first 6 bytes, xorps xmm1 / sub esp,50h, moved into a stub), so it never adds up across calls or ticks.
    Both places and the code around them (24 + 56 bytes) are checked first; each patch is one 8-byte cmpxchg8b write. Identical in
    vanilla and Elite (the same DOW2.exe 3.19.10320). No calls in the game's thread: a fire notes its tick, a side thread writes
    MoveLoopFix.log ("installed" / "NOT installed: why" / "fired (N): game m:ss (tick T)"). MoveLoopFix.ini [MoveLoopFix] Enabled.

langswitch/ - **LangSwitch.dll** (LangSwitch 1)
    The game's message loop, Plat::Window::ProcessMessages (Platform.dll 0x1000B7B0), takes messages with PeekMessageA(&msg,
    <game window>, 0, 0, PM_REMOVE): messages posted to the main thread's OTHER windows (Default IME, the text services' MSCTFIME UI /
    CicMarshalWnd...) and to the thread itself stay in the queue for good. Windows 8+ switches the input language through the text
    services of the focused window's thread with such messages (the likely cause; not reproducible under Wine), so Alt+Shift / Ctrl+Shift / Win+Space did nothing in the game;
    alt-tab worked (the switch happens elsewhere, the new layout is SENT to the game on activation). The window procedures pass
    WM_INPUTLANGCHANGEREQUEST / WM_INPUTLANGCHANGE to DefWindowProcA (Platform 0x1000B190 -> DOW2.exe 0x0067F7F0 -> Win32InputDevice
    0x10003210 -> class proc 0x1000B380; Spooge's subclass passes all), and the chat follows the thread's layout by itself
    (TranslateMessage -> WM_CHAR -> 0x0067F7F0, CP_ACP -> UTF-16 char event).
    LangSwitch wraps Platform.dll's PeekMessageA import: before the game takes its next message, the others queued before it are taken
    out and dispatched (at most 64 per call; WM_QUIT and the game's own messages untouched). Fallback: after a clean Alt+Shift /
    Ctrl+Shift that Windows has as the language / layout hotkey (HKCU\Keyboard Layout\Toggle), if the thread's layout has not
    changed in FallbackDelay ms, WM_INPUTLANGCHANGEREQUEST(next layout) to the game window, then ActivateKeyboardLayout. The log says
    which one did it. LangSwitch.ini: Enabled, Pump, Fallback, FallbackDelay (500), Verbose. -DSTAND_PROBE (stand only): a hidden
    window of the game thread fed from another thread, and layout requests posted to the game window, counted in the log.

nohints/ - **NoHints.dll** (NoHints 1)
    The hints are simulation/scar/skirmishquicktips.scar (vanilla), started by Skirmish_Start (skirmish.scar, vanilla and Elite):
    `if Misc_QuickTipsEnabled() then Rule_AddInterval(Skirmish_UpdateQuickTips, 1) end` - HintPoint markers and tutorial event cues
    for the local player only ("Non-deterministic EVIL! ... shouldn't touch the simulation"). Misc_QuickTipsEnabled = DOW2.exe
    0x00AFE940 = SystemConfig "ShowQuicktips" (configuration.lua showquicktips; the in-game Options radio), a per-player setting.
    NoHints writes "xor eax,eax; ret" over its first 3 bytes (26 bytes and the registration at 0x00B023F9 checked first): the tips
    are never started; the setting and the archives are not touched. NoHints.ini: Enabled.

docsfix/ - **DocsFix.dll** (DocsFix 1) and common/docscheck.h
    How the game gets its user folder: Platform.dll Plat::File::Setup (0x10002335) SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0)
    + PathAppendA "My Games" + CreateDirectoryA + PathAppendA "Dawn of War II - Retribution" + CreateDirectoryA, then
    dbCrash::SetWorkingFolder; the path stays in Platform.dll's buffer 0x10015708 (Plat::File::GetPathUserStorage). DOW2.exe calls
    GetPathUserStorage through its import (0x00F87D08) to build the Logfiles\ Screenshots\ Settings\ Playback\ Settings\Stats\
    Cache\ "Saved Games\" Badges\ aliases (0x0041453A, mounted once at the start) and Settings/Local.ini (0x00413569).
    docscheck.h (the proof): from the game process - the folder in Platform.dll's buffer, Documents (wide; %USERPROFILE%\Documents?
    OneDrive? ANSI round trip), attributes of Documents / Settings / Playback, when configuration.lua and temp.rec were written,
    configuration.lua opened for writing (nothing written), a test file created + written + closed + deleted in Settings and in
    Playback with GetLastError of the failing step, Controlled folder access from the registry and DOW2.exe among its allowed apps;
    the exe's signature (WinVerifyTrust, cache only), integrity level, parent. Lines "docs check (...)" + "DOCSCHECK k=v" / "DOCSEXE".
    The early winmm loaders (build/early/winmm.dll of Automatch, build/launcher/winmm.dll) load DocsFix.dll first and, when it is
    there, already in their DllMain (before the game's own code) point DOW2.exe's GetPathUserStorage import at a hold that waits
    (max 15 s) until DocsFix.dll is loaded - so the game cannot build its paths before DocsFix has decided, however late the
    loader's thread runs (stand: DocsFix in at ~0.06 s, the game's first question at ~0.2 s). Event Local\dow2_docsfix_hold_<pid>.
    DocsFix: DllMain holds DOW2.exe's GetPathUserStorage import and Platform.dll's SHGetFolderPathA import behind its own gate
    (max 20 s) until the worker has decided:
    redirect when the write test fails (or DocsFix.txt in the new place says it redirected before; DocsFix.ini Mode=always/never for
    tests). Redirect: %LOCALAPPDATA%\EliteLauncher\Documents (short name when not ANSI-safe; write-tested first), the old folder copied
    the first time (never overwriting; Settings and the rest before the game goes on, Playback/Screenshots after; Logfiles not),
    Platform.dll's buffer and dbCrash's folder set when Setup ran already, SHGetFolderPathA/W(CSIDL_PERSONAL) answer the new place to
    callers in the game folder's modules (shell32's export table for later modules, the imports of the loaded ones) - EliteAI, MMFind,
    LiveReplay find temp.rec there without a change. DocsFix.redirect next to the DLL tells the launcher (1.5.9); removed when not
    redirecting. Loaded without the hold and after Platform's Setup (Elite's injector ~9 s after the start - Elite without
    Automatch or with its older winmm.dll; an older vanilla loader): only the check and a log line ("NOT redirected ... too late").
    After every match (in-game state [*(0x01335A78)+0x114] for 20 s or more, then 0): the check again and "temp.rec WRITTEN / NOT written".

obsfix/ - **ObsFix.dll** (ObsFix v1)
    When you spectate a live game the client asks "which slot am I in" dozens of times per frame and rebuilds the whole match
    info every time. ObsFix caches the answer for 500 ms. Write-up: docs/OBSERVER_LAG.md

roomjoin/ - **RoomJoin.dll** (RoomJoin 9, roomjoin-4) - siritos's experiment
    The launcher (1.4.9+) talks to it over a named pipe, \\.\pipe\EliteRoomJoin-<pid of DOW2.exe> (only the current user, no
    remote clients; one JSON object per line: join / status / cancel -> accepted, walking, joining, in_lobby, failed + reason,
    status). The pipe exists only with the add-on working: no pipe in a running game = restart it with the add-on on. In the main
    menu RoomJoin shows a cover, joins by the room id (the game browser's own join), opens the lobby screen and steps aside. A
    no from the game: the reason, then the public games list; no answer in 25 s or Cancel: the room is watched 15 s more. Never
    on Find a Game, in another lobby or in a match. The pipe thread never touches the engine and never holds up the game's exit.
    Linux (the game under Proton): Elite_DLLs/rjbridge.exe (roomjoin/rjbridge.c, shipped with the add-on) - the launcher starts
    it with the game process's own Wine environment and talks JSON lines to its stdin/stdout, it passes them to the pipe.
    RoomJoin 10: only the pipe (launcher 1.4.9+); no request file is read. Test client: make build/rjpipe.exe.
    Any load order with MMFind (the fscommand hook is a vtable slot). RoomJoin.ini Enabled=0 = off.
    Store entry: roomjoin/catalog-entry.json.

shellfix/ - **ShellFix.dll** (ShellFix 1)
    Elite's ShellPatch.dll wraps the HUD meter shell 0x00B6CF40 with Detours; with no free 64 KB in the lower 2 GB when it
    starts, the wrapper is missing while its jumps are in, and the first match crashes (SetVisible(NULL) at 0x00685450).
    ShellFix waits for ShellPatch, and only in that state puts the wrapper in itself (trampoline + jmp ShellPatch+0x3A10).
    Elite_DLLs/ShellFix.log; ShellFix.ini Enabled=0 = off.

crashlog/ - **CrashLog.dll** (CrashLog 1)
    No hooks into the game: serious exceptions with module+offset and stack, a minidump on a crash, and who called ExitProcess /
    TerminateProcess (the game's built-in BugSplat closes it silently). Elite_DLLs/CrashLog.log, sent only with "Send logs".

netprobe/ - **NetProbe.dll** (NetProbe 2), experiment - removed from the store 05.11.2026 (owner), the source stays
    No hooks. 15 s after the game starts: TCP and UDP to our servers' probe service (dow2-ladder gameserver/probe_server.py) on
    several ports, ~5 MB, 1-2 minutes; writes NetProbe.log and sends it to us.

dxvk/ - **DXVK.dll** (DXVK 1) + DXVK_d3d9.bin (DXVK 2.7.1 x32 d3d9.dll), DXVK.conf, DXVK_LICENSE.txt (zlib)
    Early add-on. Points only Spooge.dll's import of d3d9.dll!Direct3DCreate9 at DXVK_d3d9.bin (loaded in the hook, not under the
    loader lock; .bin so the loader does not load it as an add-on); DXVK_CONFIG_FILE / DXVK_LOG_PATH = the add-on folder. Does
    nothing when the game's d3d9.dll is DXVK already (Elite IsDXVK) or when it is loaded > 5 s after the start. No Vulkan check.

fpsunlock/ - **FpsUnlock.dll** (FpsUnlock 1)
    Early add-on. Appends "d3d9.maxFrameRate = 0; dxgi.maxFrameRate = 0" to DXVK_CONFIG (read after dxvk.conf, so it lifts
    Elite's 60) unless a maxFrameRate is there already (SoundFix FpsCap wins). No hooks; logs which Direct3D the game got.

uifilter/ - **UIFilter.dll** (UIFilter 1)
    Patches Spooge.dll's GFx D3D9 renderer at BeginDisplay (0x100513B8) / EndDisplay (0x10051F10, 8-byte cmpxchg8b writes): its
    device at renderer+0xD4 and the buffer helper's copy at +0x18C become a proxy IDirect3DDevice9 (own vtable, everything
    forwarded; the real device and its vtable untouched). Inside one display it drops repeated SetRenderState / SetSamplerState /
    SetTexture / SetVertexShader / SetPixelShader / SetStreamSource / SetIndices / SetVertexDeclaration / SetFVF; the cache is
    emptied on Begin/EndScene, Present, Reset, SetRenderTarget, SetDepthStencilSurface, another thread. UIFilter.ini [UIFilter]
    Enabled=0 / Verify=1 (Get* check of each dropped value, mismatches logged); UIFilter.log: per-minute counts per type.

vanilla-loader/ - **winmm.dll**
    Forwards everything to the real winmm.dll. Full build: loads every *.dll from <game>\dow2-patches\ (vanilla and Elite) and the
    early add-ons in Elite_DLLs. -DEARLY_ONLY (build/early/winmm.dll, in Automatch v2.0): only the early add-ons (OwnLobby.dll, DXVK.dll, FpsUnlock.dll) -
    Elite's injector loads Elite_DLLs ~9 s after the start, too late for them.
    -DLAUNCHER_LOADER (build/launcher/winmm.dll, the launcher's "Vanilla Retribution", launcher 1.4.11; store vanilla.loader
    vloader-<n>): decides by the game's command line - a mod start (Elite.exe starts DOW2.exe -modname Elite) loads only the early
    add-ons in Elite_DLLs, like -DEARLY_ONLY; plain DOW2.exe (vanilla) loads only the DLLs of dow2-patches\ (OwnLobby.dll first),
    never Elite_DLLs, and writes dow2-patches\Loader.log. The launcher fills dow2-patches\ with the copies of the add-ons whose
    store entry has "games": [..., "vanilla"] and puts this loader next to DOW2.exe only for vanilla; for Elite it goes and
    Automatch's early one is back. The folder name is put together at run time (launchers up to 1.4.10 put aside a winmm.dll that
    names it). vloader-3 (13.11.2026): only "-modname Elite..." counts as Elite; another mod (IG Rework: -modname IgRework) gets
    dow2-patches\ like vanilla - launcher 1.6.4 fills it with the add-ons whose entry names "igrework" (docs/ADDON_RULES.md).

continue/ - Continue.dll (Continue 1) - research, not released
    The game's own Continue after a Custom Games match (the host presses it, everyone is back in the same room): canContinue
    answers "no" for custom games, Continue.dll lets it answer as for the other match types. Taken out of the store on 03.11.2026:
    Relic's relay closes the match session 61 s after the match. With it: own_lobby "reopen" on the dow2-ladder side.

frameprobe/ - FrameProbe.dll (off), eliteai/ - EliteAI.dll (store id newai since 0.2.33; the old eliteai_off stays off)
    FrameProbe: the diagnostics DLL that found the observer lag (frame times, sim ticks, a stack sampler, a probe on Continue).
    EliteAI: the new AI for skirmishes - Elite's default.ai gets eliteai/mod/main.ai; only in single-player games (online it
    would desync everyone). Both are in the store only as "off" entries.
    EliteAI 0.11 (newai-20): back in the menu after a single-player match with the new AI, sends Playback\temp.rec to the replay
    site (mirrors checked first, 2.5 s; EliteAI.ini [Replays] Upload=0 off, Server=host;host); MMFind sends only matches with people.
    newai-22 (0.2.63): eliteai/mod = the generated vanilla base (eliteai branch vanilla-ours, tools/vanilla_dispatch.py): 53 served
    dispatchers in simulation/ai, default.ai with the people's plan + main.ai + mechanics.ai inside; [Lua] NewAI default vanilla+ours.
    newai-23 (0.2.66): the same layout from vanilla-ours a3a378a (main.ai 0.27, mechanics.ai 1.9): generators and nodes on people's
    clock, the first T3 unit right after the tier, the dear unit, hurt walkers to the rear / home first.
    newai-24 (0.2.73): the brain for orks - default.ai also carries the 13 modules of eliteai mod/brain (branch newai-24) and the
    dispatcher's [Lua] Brain (default 1 = ork new-AI slots, 0 = off); main.ai / mechanics.ai as newai-23.1 for every other race.
    newai-25 (0.2.74): brain step 6 (0.6, branch newai-25): cover, flanking / grenading set-up teams, casts in a fight, no mob in
    a building, the power crisis, turrets; elite.scar gains ork abilities 32-38. Non-ork slots still newai-23.1.
    newai-26 (0.2.75): brain step 7 (0.7, branch newai-26): nobz at tier 3 and the population kept for them, late upgrades, the
    push from 13:00, the stage at the edge of the enemy's reach, turret rules; EliteAI.dll 0.12 carries the move-loop guard
    (eliteai/moveloop_guard.h = MoveLoopFix 1, idle when MoveLoopFix.dll is loaded). Non-ork slots still newai-23.1.
    newai-27 (0.2.77): brain 0.8.1 (steps 8 / 8.1, branch newai-27): a game without victory points ends by the HQ, no retreats at full
      health, the red resource is spent (Waaagh!, gretchin / kommandos call-ins, roks), no building with gretchin; EliteAI.dll 0.14
      keeps the log of the last match of the new AI (EliteAI.log / EliteAI.prev.log / EliteAI.more.log). Non-ork slots still newai-23.1.

## Building

    make                          # all DLLs into build/
    make build/early/winmm.dll    # the early loader of Automatch v2.0
    make build/launcher/winmm.dll # the launcher's loader for vanilla (by -modname)
    make STATS_HOST=other.host    # another stats host baked into LobbyStats (default rts.fintebtc.de)

Needs gcc-mingw-w64-i686. By hand: put the DLL into Elite_DLLs\ and add it to InjectedFiles= in Elite.launchconfig; without
Elite: winmm.dll next to DOW2.exe, patches into dow2-patches\.

## Versions and CI

- The version of the set lives only in `VERSION` and needs a `## X.Y.Z` entry in CHANGELOG.md (for us, technical) and
  player notes in `changelog/X.Y.Z/en.md` + `ru.md` (plain words, see docs/ADDON_TEXTS.md) - CI runs
  `scripts/release_notes.py check`. Each DLL writes its own version to its log; the store has its own version per add-on.
- CI (.github/workflows/build.yml) builds every push and PR and does a dry-run publish into a temporary store with a one-off key.
  It uploads nothing to the mirrors and has no store key: CI never changes what players get.
- After a merge to main CI tags `vX.Y.Z` if the tag is new; for `X.Y.0` it also makes a GitHub Release with the DLLs and the
  player notes (changelog/<version>/en.md) since the previous X.Y.0 - the launcher shows them as news. Tags are set by CI,
  not by hand.

## Releasing to players

Only on rts, by hand, after the DLL has run in a real game (owner's rule 29.10.2026). One writer of the store at a time:
`flock /run/elite-store.lock`.

- **Add-ons in `addons.json`** (key = store id without `_r2`/`_r3`, store version = `<key>-<version>`): raise the version, merge to
  main, then on rts `dow2-patches-deploy` builds it into the staging store (`store3-staging`, never synced; its timer
  `dow2-patches-deploy.timer` is **off on purpose** - run it by hand). `elite-release` shows what waits, `elite-release apply <id>`
  releases it to the live store, signs, verifies and syncs the mirrors. Players get it on the next launcher refresh (≤ 15 min).
- **The others** (automatch, relay_fwd, elitefix_wmic, rusifikator) are published by hand on rts with elite-publish
  into store3 (their store version is not `<id>-<version>`, or they are more than one DLL of this repo).
- A store version is published once and never changes: new code needs a new version. Going back = the old version back into
  launcher.json (it is still in the store). Urgent: stub the one add-on, the rest keeps working.
- `/elite/dev/` (elite-devstatus) compares `addons.json` of main with the store; nothing there should be "pending" unless a
  release really waits.

MIT.
