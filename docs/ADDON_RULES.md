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

## What happened (history)

- Up to 0.9.x LobbyStats.dll carried the lobby ratings, the loading screen ratings, the match clock, the cooldown
  numbers and the join sound. A crash in one (the clock, the cooldowns on cultists) took them all down and players had to
  give up all of it. 30.10.2026 split (d93c4ac): LobbyStats, LoadingRatings, MatchTimer, Cooldowns.
- 02.11.2026: the join sound (players: too loud) became JoinSound.dll with its own tick and a soft / sharp choice;
  LobbyStats 0.11.0 has no sound.

## How

- New feature -> its own folder and DLL, its own entry (and tick) in launcher.json, its own `.ini` / `.log`.
- Shared code is fine as **source**: `common/*.h`, or one source file built as several DLLs with a define
  (LoadingRatings `-DLS_LOADING`, JoinSound `-DLS_JOINSOUND` from lobbystats.c) - but each build is its own add-on.
- Shared game state without double hooks: one "match core" per game (matchtimer.c, `-DMT_EMBED`) that the other add-ons
  ask, as MatchTimer / Cooldowns / LoadingRatings do.
- An option inside an add-on is only for **how** that one feature looks or works (sound soft/sharp, clock place) - never
  a second feature behind a sub-tick.
- And how it is shown: docs/UI_RULES.md (only the game's own interface, no Direct3D).
