# Changelog

Every version gets an entry here before it reaches `main`. CI then tags `vX.Y.Z` on its own; for a minor or major
version (`X.Y.0`) it also publishes a GitHub Release with the DLLs and the entries since the previous `X.Y.0`.
The version of the set lives only in `VERSION`; each DLL still writes its own version to its log.

## 0.2.8 — 2026-11-05
Cooldowns 9 (store cooldowns-9; branch fix/cooldowns-9).
- A frozen number on a button after a selection change (siritos 05.11; the trace of 15:20: the Techmarine's 75 s on
  ability_btn01, then the scouts selected - the panel updated only their buttons 03 / 05 / 06 and never 01 again; the same with
  the scouts' grenade on 03 and the Techmarine). The number was taken off only in an update of its own button, and the panel does
  not update a place the selected unit has nothing on. Now a number whose button has not been updated for 0.4 s is taken off by
  the update of any other button of the same HUD movie (alive at that moment); it comes back with its button's updates. What is
  shown is kept per HUD movie and place. Trace=1 logs each number on / off with its widget and movie, and where a number is up
  every 2 s; it shows the real owner (type + its pointer), not the stack words around it.
- Nothing of a match stays for the next one (siritos 05.11): the recharges are kept by squad / entity addresses and the next
  match gets the same addresses again - a squad there could take a recharge of the match before. A new match (the match core's
  ticks going back to 0) now forgets every recharge, rate and button state; a number still shown in a HUD movie kept for the
  next match is set right at its first update. After a match the hooks only pass through (no match: no query, no number).
- The table of recharge starts per owner (unused since Cooldowns 6: its key was always 0) is gone.
- Cooldowns.log over 1 MB at the game's start becomes Cooldowns.log.old (05.11: it had grown to 1 MB).

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
