# Dow2-patches

Add-ons for Dawn of War II: Retribution (DOW2.exe 3.19.1.10320, the current Steam build) and the Elite mod, shipped to
players as ticks in the Elite launcher. Plain C, built with MinGW. Every patch checks the bytes it is about to change and
does nothing if the exe is a different build.

**Add-on rule:** one feature - one DLL - one tick in the launcher — [docs/ADDON_RULES.md](docs/ADDON_RULES.md) (one exception:
Automatch v2.0). **UI rule:** add-ons show things only inside the game's own interface (its Scaleform movies), never drawn over
the frame through Direct3D — [docs/UI_RULES.md](docs/UI_RULES.md).

## What players get (launcher store, 04.11.2026)

The source of truth is the live store, `store3/launcher.json` on rts; this table is that file on 04.11.2026 (main = 31f3ea5,
PR #11). "Sync": whether the add-on can affect the game's synchronisation between players. None of the released ones can: they
are client-side, and the other players in the match don't need them.

| Tick in the launcher | What it does | Store id → version | Folder | Sync |
|---|---|---|---|---|
| **Automatch v2.0** (on by default) | Multiplayer → Find a Game → Start Searching finds a 1v1 / 2v2 / 3v3 match by rating on our servers and starts it within seconds. If it can't run, Find a Game says why. | `automatch` → `mmfind-0.45` | `mmfind/` + `ownlobby/` + `vanilla-loader/` | no |
| **Use our servers for games** (on by default) | Our automatch games go straight through our game server (Poland, Germany, Moscow - the nearest is picked by itself). UDP only. | `relay_r2` → `relay-21` | `relay/` | no |
| **Use forwarding servers in all games** | Every other online game (ordinary lobbies) goes through our nearest server on its way to Relic - for players whose connection to Relic gets cut or lags. | `relay_fwd` → `relayfwd-21` | `relay/` | no |
| **Lobby rating** | Relic rating next to each player in the lobby; on hover - win rate, games, Steam hours. Only shows, kicks nobody. | `lobbystats_r3` → `lobbystats-0.11.0` | `lobbystats/` | no |
| **Ratings on the loading screen** | The Relic rating after each player's name on the loading screen of an online match. | `loadingratings_r3` → `loadingratings-4` | `lobbystats/` (built with `-DLS_LOADING`) | no |
| **Sound when someone joins the lobby** | A soft or sharp sound and a flashing taskbar button when a player comes into your lobby while the game is minimised. | `joinsound` → `joinsound-1` | `lobbystats/` (built with `-DLS_JOINSOUND`) | no |
| **Match timer** | Match time in the game's own interface: under the score, under the Menu button, or in it. | `matchtimer_r2` → `matchtimer-8` | `matchtimer/` | no |
| **Cooldown seconds on ability buttons** | Seconds left on recharging abilities and global abilities, in the middle of the button. | `cooldowns_r2` → `cooldowns-7` | `cooldowns/` | no |
| **APM** | Your clicks and key presses per minute, next to the match timer or in the minimap corner. Only you see it. | `apm` → `apm-1` | `apm/` | no |
| **Auto-reinforce through the webway gate** | A squad that goes through an Eldar webway gate keeps auto-reinforce on. | `reinforcefix` → `reinforcefix-1` | `reinforcefix/` | no (the game's own orders) |
| **Jumps don't break on obstacles** | An Autarch (or another jumper) whose jump order broke on an obstacle on the way gets it repeated after landing. | `jumpretry` → `jumpretry-2` | `jumpretry/` | no (the game's own orders) |
| **Reverse (test)** | Ctrl + right-click behind your vehicles: they back off driving backwards, front armour to the enemy. | `reverse_r2` → `reverse-4` | `reverse/` | no (the game's own orders) |
| **Don't drop out of the match when Steam goes away** | A match is played to the end when Steam loses its connection, however long it is away. | `steamkeep` → `steamkeep-2` | `steamkeep/` | no |
| **Observer lag fix (experimental)** | Spectating a live game no longer stutters. | `obsfix_r2` → `obsfix-1` | `obsfix/` | no |
| **Join a lobby from the launcher (test)** | Experiment by siritos: a Join button on the launcher's Lobbies tab; the game starts and walks into the room by itself. | `roomjoin` → `roomjoin-2` | `roomjoin/` | no |
| **Fix: the game closes when a match starts** (new, not released yet) | Repairs Elite's ShellPatch.dll (the race meter) when it could not hook in at the game's start - then every match crashed a second in. Otherwise does nothing. | `shellfix` → `shellfix-1` (staging) | `shellfix/` | no |
| **Record game crashes (for the developers)** | Writes where the game crashed and what closed it, for "Send logs". | `crashlog` → `crashlog-1` | `crashlog/` | no |
| **EXPERIMENT: connection check to our servers** | Once per game start, 1-2 minutes in the background: what gets through from your computer to our game servers; sends us the result. | `netprobe` → `netprobe-2` | `netprobe/` | no |
| **Fix: Elite.exe crash on Windows 11 25H2** | An Elite.exe without its wmic check (wmic is gone in 25H2, Elite.exe crashed on it); it still closes itself when the game ends. | `elitefix_wmic` → `elitefix-nowmic-1`, `elitefix-nowmic-2` (staging) | `elitefix/make_elite_exe.py` (one jump patched in Elite's Elite.exe) | no |
| **Russian translation (by Gosudar)** | Russian text for Dawn of War II and Elite 3.2; the English files come back when it is off. | `rusifikator` → `rusifikator-text-1` | not in this repo (Gosudar's text files) | no |
| Developer diagnostics (off) | Turned off for everyone. | `frameprobe_r2` → `frameprobe-off` | `frameprobe/` | no |
| New AI (removed) | Removed, turned off for everyone. | `eliteai_off` → `eliteai-off` | `eliteai/` | was skirmish-only |

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

relay/ - **Relay.dll** (`relay_r2`) and **RelayForward.dll** (`relay_fwd`), Relay 22
    One source, two ticks. Relay.dll: a game goes straight through our game server when everyone in it reaches our servers
    (our automatch: the matchmaker knows every player's ping and picks the node). RelayForward.dll: every other online game goes
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

cooldowns/ - **Cooldowns.dll** (Cooldowns 6, cooldowns-7)
    Seconds left on recharging abilities, global abilities and stratagems, as the button's own count text. Each recharge is kept
    per squad and ability and asked from the engine with the same read-only question the panel asks (guarded). Read-only.
    Cooldowns.ini [Cooldowns]: Enabled, DigitSize, Color, Ask.

apm/ - **APM.dll** (APM 1)
    Presses of mouse buttons and keys over the last 60 s of game time while a match runs, read with GetAsyncKeyState; nothing goes
    to the game. Place=top | left | minimap in APM.ini. Chained with MatchTimer on the per-frame form updates.

reinforcefix/ - **ReinforceFix.dll** (ReinforceFix v1)
    The HUD's auto-reinforce list drops squads with no models on the map - also a squad sitting inside an Eldar webway gate.
    ReinforceFix keeps such squads; the reinforce orders stay the game's own.

jumpretry/ - **JumpRetry.dll** (JumpRetry 2)
    An Autarch ordered to jump out of range walks toward the point inside the ability order; a hop over a low wall on the way ends
    that order. JumpRetry sends your own order again as soon as he can walk (up to 3 times within 10 s), through the HUD's
    CommandProxy - the game's own order. Abilities: JumpRetry.ini Abilities=.

reverse/ - **Reverse.dll** (Reverse 4)
    Ctrl + right-click: the selected squads back off to the point, vehicles driving backwards (25 m legs, so the game's own turn
    plans reverse). Only the point of your own right-click is moved. Reverse.ini: Key, StepMeters, LeadMeters.

steamkeep/ - **SteamKeep.dll** (SteamKeep 2)
    A Steam drop during a match is not turned into an end reason, and Steam's close request (a bare WM_CLOSE) is ignored for the
    rest of the match. Alt+F4, menu Quit, the close button and Windows shutdown still work. Not in menus or replays.

obsfix/ - **ObsFix.dll** (ObsFix v1)
    When you spectate a live game the client asks "which slot am I in" dozens of times per frame and rebuilds the whole match
    info every time. ObsFix caches the answer for 500 ms. Write-up: docs/OBSERVER_LAG.md

roomjoin/ - **RoomJoin.dll** (RoomJoin 8, roomjoin-2) - siritos's experiment
    The launcher writes Elite_DLLs/RoomJoin.txt (room id, name) and starts the game or brings it to the front; in the main menu
    RoomJoin shows a cover, joins by the room id (the game browser's own join), opens the lobby screen and steps aside. A no from
    the game or 25 s: the reason, then the public games list. Never during the automatch (MMFind's cover, a search on Find a
    Game): the Join is dropped. Any load order with MMFind (the fscommand hook is a vtable slot). RoomJoin.ini Enabled=0 = off.
    Store entry: roomjoin/catalog-entry.json.

shellfix/ - **ShellFix.dll** (ShellFix 1)
    Elite's ShellPatch.dll wraps the HUD meter shell 0x00B6CF40 with Detours; with no free 64 KB in the lower 2 GB when it
    starts, the wrapper is missing while its jumps are in, and the first match crashes (SetVisible(NULL) at 0x00685450).
    ShellFix waits for ShellPatch, and only in that state puts the wrapper in itself (trampoline + jmp ShellPatch+0x3A10).
    Elite_DLLs/ShellFix.log; ShellFix.ini Enabled=0 = off.

crashlog/ - **CrashLog.dll** (CrashLog 1)
    No hooks into the game: serious exceptions with module+offset and stack, a minidump on a crash, and who called ExitProcess /
    TerminateProcess (the game's built-in BugSplat closes it silently). Elite_DLLs/CrashLog.log, sent only with "Send logs".

netprobe/ - **NetProbe.dll** (NetProbe 2), experiment
    No hooks. 15 s after the game starts: TCP and UDP to our servers' probe service (dow2-ladder gameserver/probe_server.py) on
    several ports, ~5 MB, 1-2 minutes; writes NetProbe.log and sends it to us.

vanilla-loader/ - **winmm.dll**
    Forwards everything to the real winmm.dll. Full build: loads every *.dll from <game>\dow2-patches\ (vanilla and Elite) and the
    early add-ons in Elite_DLLs. -DEARLY_ONLY (build/early/winmm.dll, in Automatch v2.0): only the early add-ons (OwnLobby.dll) -
    Elite's injector loads Elite_DLLs ~9 s after the start, too late for them.

continue/ - Continue.dll (Continue 1) - research, not released
    The game's own Continue after a Custom Games match (the host presses it, everyone is back in the same room): canContinue
    answers "no" for custom games, Continue.dll lets it answer as for the other match types. Taken out of the store on 03.11.2026:
    Relic's relay closes the match session 61 s after the match. With it: own_lobby "reopen" on the dow2-ladder side.

frameprobe/ - FrameProbe.dll (off), eliteai/ - EliteAI.dll (removed)
    FrameProbe: the diagnostics DLL that found the observer lag (frame times, sim ticks, a stack sampler, a probe on Continue).
    EliteAI: the new AI for skirmishes - Elite's default.ai gets eliteai/mod/main.ai; only in single-player games (online it
    would desync everyone). Both are in the store only as "off" entries.

## Building

    make                          # all DLLs into build/
    make build/early/winmm.dll    # the early loader of Automatch v2.0
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
- **The others** (automatch, relay_r2 / relay_fwd, elitefix_wmic, rusifikator) are published by hand on rts with elite-publish
  into store3 (their store version is not `<id>-<version>`, or they are more than one DLL of this repo).
- A store version is published once and never changes: new code needs a new version. Going back = the old version back into
  launcher.json (it is still in the store). Urgent: stub the one add-on, the rest keeps working.
- `/elite/dev/` (elite-devstatus) compares `addons.json` of main with the store; nothing there should be "pending" unless a
  release really waits.

MIT.
