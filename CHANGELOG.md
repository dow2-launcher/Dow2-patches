# Changelog

Every version gets an entry here before it reaches `main`. CI then tags `vX.Y.Z` on its own; for a minor or major
version (`X.Y.0`) it also publishes a GitHub Release with the DLLs and the entries since the previous `X.Y.0`.
The version of the set lives only in `VERSION`; each DLL still writes its own version to its log.

## Unreleased (branch feature/relay-21)
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

## Unreleased (branch feature/relay-20)
- Relay 20 (Relay.dll + RelayForward.dll). Redundancy in the UDP tunnel: the SYN carries version 2; a gateway that knows it
  answers SYNACK + 2 and both sides send DATA2 (each data packet also carries copies of the newest 2 earlier packets not
  acknowledged yet) and ACK2 (an ack with such copies; answered only when a copy filled something). The copies go only while
  the datagram stays within 1240 bytes, so turns and ticks get them and big transfers do not. An old gateway answers a bare
  SYNACK -> the old DATA, no copies; Relay 18/19 with a new gateway likewise. Native connections of our lobby server's rooms
  (the game connects to one of our nodes on 47100 itself) now go over UDP to that node's udp_gateway, TCP to the node when
  UDP does not answer (3 s). Needs udp_gateway with DATA2 (dow2-ladder feature/relay-20) for the copies; works without it.
  Tested: Linux netns, netem 40 ms each way, the tunnel code of Relay 19 / 20 under Wine against the old / new gateway:
  loss 10% - p99 game->server 422 -> 141 ms, server->game 242 -> 140 ms (p95 138 -> 41); no loss - no change.

## Continue 1 + MMFind 0.44 (branch feature/native-continue, staging continue-1 + mmfind-0.44, not released)
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

## MMFind 0.43 (branch feature/mmfind-rematch, staging mmfind-0.43, not released)
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

## MMFind 0.42 (branch feature/mmfind-0.42-mode, staging mmfind-0.42, not released)
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

## Unreleased (branch fix/cooldowns-6-timer-apm)
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
- Launcher catalog entries with the Place picks: docs/launcher-catalog-timer-apm.json (matchtimer-8, apm-1, cooldowns-6).

## Unreleased (branch feature/mmfind-0.39 only, not released)
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
