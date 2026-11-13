# Add-on rule: one feature - one DLL - one tick

**Every feature is an add-on of its own: its own DLL and its own tick in the launcher.** Never bundle several
features into one DLL. Turning a feature on or off must be one tick, without touching the others, and a broken feature
must be switchable off alone (urgent: stub that one DLL, the rest keeps working).

Owner's rule (30.10.2026, again 02.11.2026: "каждую фичу стараемся отдельно делать, чтобы отключать/подключать было
просто и удобно; не объединять всё в одну dll").

## Why

- **Players choose:** each one turns on exactly what they want (the clock without the ratings, the sound without the stats).
- **Safety:** a crash or a bug in one feature is found and switched off alone; the others stay with the players.
- **Releases:** each feature gets its own version; a fix of one doesn't re-ship the rest.
- **Logs:** each add-on writes its own log - the cause is clear from the file name.

## Exception: Automatch v2.0

**Automatch v2.0 is one package of three files by the owner's decision of 03.11.2026** (launcher id `automatch`, store version
`mmfind-<ver>`, one tick): Elite_DLLs/MMFind.dll + Elite_DLLs/OwnLobby.dll + Elite_DLLs/Relay.dll (since mmfind-0.46) +
winmm.dll next to DOW2.exe (the early loader, `-DEARLY_ONLY`). They are one feature - the automatch on our servers: MMFind.dll is
the Find a Game side, OwnLobby.dll takes the game's lobby service to our lobby server, Relay.dll carries the match to our game
server over our UDP tunnel (owner 05.11.2026: "the automatch always goes through our servers and over UDP" - the separate tick
relay_r2 is gone; MMFind.dll loads Relay.dll, Elite's InjectedFiles names only MMFind.dll), and the early winmm.dll is only
there to load OwnLobby.dll at process start. None of them is any use without the others, so they ship and switch off together.
The network ticks are two: Automatch and Forwarding servers (relay_fwd, RelayForward.dll - Relic's games through our node). The separate `ownlobby` add-on is gone.
The sources stay separate (`mmfind/`, `ownlobby/`, `vanilla-loader/`), each DLL keeps its own log.

## Elite and vanilla (launcher 1.4.11, owner 05.11.2026)

Each store entry says in which game it works: `"games": ["elite", "vanilla"]` or `["elite"]` (no `games` = Elite only). The
launcher's "Vanilla Retribution" copies an add-on's Elite_DLLs files into `dow2-patches\` only when its entry names "vanilla" -
whatever the player's tick says; an add-on for Elite only (it patches Elite files or needs Elite: elitefix_wmic, shellfix,
rusifikator...) is never copied there. Its ini is read next to the DLL, so options work in both folders. The
vanilla game's loader is `build/launcher/winmm.dll` (store `vloader-<n>`, launcher.json `"vanilla": {"loader": ...}`). A new
add-on: test it in vanilla 3.19 on the stand before its entry gets "vanilla".

IG Rework (launcher 1.6.4, owner 13.11.2026): grantovich's mod is a version of its own in the launcher (channel `igrework`,
IgRework.exe -> DOW2.exe -modname IgRework, its DLLs in IgRework_DLLs). Its channel has `"game": "igrework"`; the launcher
copies the add-ons whose entry names "igrework" into `dow2-patches\` exactly as for vanilla, and the loader (vloader-3+) loads
that folder for any `-modname` that is not Elite. Our DLLs never go into IgRework.launchconfig: IgRework.exe refuses to start
the game ("Failed to find a specific injected file required by this mod") when a DLL named there is not in IgRework_DLLs.
Not for IG Rework: the add-ons tied to our servers' Elite/vanilla games (automatch - MMFind puts any non-Elite -modname into
the vanilla queue -, roomjoin, livereplay, replaytakeover), the ones that change Elite's own files or need Elite (elitefix_wmic,
rusifikator, elitetext_lang, shellfix, newai/eliteai_off, frameprobe, soundbugtest) and the vanilla-only elitemaps. A new
add-on: test it on the IG Rework channel's folder on the stand (dev/igrework_addons_1113/session.sh) before it gets "igrework".

Launcher 1.5.2: an entry may also have `"requires": [<store id>, ...]` (the tick works only with those ticked; fpsunlock
requires dxvk) and `"excludes": [<store id>, ...]` (never together). `games`, `requires` and `excludes` written in addons.json
go to the store with a deploy; an add-on without them there keeps what the store has.

## What happened (history)

- Up to 0.9.x LobbyStats.dll carried the lobby ratings, the loading screen ratings, the match clock, the cooldown
  numbers and the join sound. A crash in one (the clock, the cooldowns on cultists) took them all down and players had to
  give up all of it. 30.10.2026 split (d93c4ac): LobbyStats, LoadingRatings, MatchTimer, Cooldowns.
- 02.11.2026: the join sound (players: too loud) became JoinSound.dll with its own tick and a soft / sharp choice;
  LobbyStats 0.11.0 has no sound.
- 03.11.2026: OwnLobby.dll and its early winmm.dll moved from the `ownlobby` add-on into Automatch v2.0 (the exception above).

## How

- New feature -> its own folder and DLL, its own entry (and tick) in launcher.json, its own `.ini` / `.log`.
- Shared code is fine as **source**: `common/*.h`, or one source file built as several DLLs with a define
  (LoadingRatings `-DLS_LOADING`, JoinSound `-DLS_JOINSOUND` from lobbystats.c) - but each build is its own add-on.
- Shared game state without double hooks: one "match core" per game (matchtimer.c, `-DMT_EMBED`) that the other add-ons
  ask, as MatchTimer / Cooldowns / LoadingRatings do.
- An option inside an add-on is only for **how** that one feature looks or works (sound soft/sharp, clock place) - never
  a second feature behind a sub-tick.
- And how it is shown: docs/UI_RULES.md (only the game's own interface, no Direct3D).
