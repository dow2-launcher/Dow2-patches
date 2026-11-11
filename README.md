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
- `linux-native/`, `mac/` - fixes for the native Feral ports (Linux, Mac crossplay), work in progress, not in the launcher.

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

cursorscale/ - **CursorScale.dll** (CursorScale 1)
    The game's cursor is an ordinary Windows cursor: 45 48x48 TGAs of UI/cursor/cursorlist.lua -> Plat::Input::CreateCursor ->
    Platform.dll CreateIconIndirect (DIB section + mask), SetCursor on WM_SETCURSOR. CursorScale points Platform.dll's import of
    CreateIconIndirect at its own function, which scales the picture (premultiplied-alpha bicubic or nearest) and the hotspot
    before the real call; cursors made before it was loaded (Elite's Profiler loads add-ons at the front end) are rebuilt in the
    input object's vector. No drawing of our own, no Direct3D. CursorScale.ini: Scale=auto|1.0-4.0 (auto = the game window's client
    height / 1080, i.e. the game's own resolution, not the desktop's), Filter=smooth|sharp.

overwatchonce/ - **OverwatchOnce.dll** (OverwatchOnce 1)
    Overwatch is the HUD's, not the simulation's: a right-click on a unit button toggles { building id -> squad blueprint } in the
    HUD overwatch object (*(*(0x01335720)+8)+0xCC, map at +0x0C); every HUD update 0x00ACBFB6 calls its tick 0x00B26890, which
    sends the plain build order (CommandProxy +0x0C, CMD_BuildSquad 3) whenever the building can build it, and keeps the entry.
    OverwatchOnce wraps that call: a build order sent by the tick (return address 0x00B26AD8) is remembered; once the building's
    production queue (EntityController::QI(7) +0x54, item vftable +8 = blueprint, +0x60 added counter) has it, the entry is erased
    with the game's own erase 0x00B26BD0 (the second right-click's) and the tech tree is redrawn (BindingBuildTech +0x1ED). Nothing
    in the simulation is touched and no command is added. Bytes/slots checked before hooking. OverwatchOnce.ini: Enabled, Verbose.

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
    names it).

continue/ - Continue.dll (Continue 1) - research, not released
    The game's own Continue after a Custom Games match (the host presses it, everyone is back in the same room): canContinue
    answers "no" for custom games, Continue.dll lets it answer as for the other match types. Taken out of the store on 03.11.2026:
    Relic's relay closes the match session 61 s after the match. With it: own_lobby "reopen" on the dow2-ladder side.

frameprobe/ - FrameProbe.dll (off), eliteai/ - EliteAI.dll (store id newai since 0.2.33; the old eliteai_off stays off)
    FrameProbe: the diagnostics DLL that found the observer lag (frame times, sim ticks, a stack sampler, a probe on Continue).
    EliteAI: the new AI for skirmishes - Elite's default.ai gets eliteai/mod/main.ai; only in single-player games (online it
    would desync everyone). Both are in the store only as "off" entries.

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
