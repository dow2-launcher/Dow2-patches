# All DLLs are 32-bit (DOW2.exe is 32-bit). Needs MinGW-w64: apt install gcc-mingw-w64-i686
CC = i686-w64-mingw32-gcc
CFLAGS = -O2 -s -Wall -Wno-unused-function -Wno-misleading-indentation -Wno-stringop-truncation
OUT = build
# stats service host for LobbyStats, baked into the DLL (make STATS_HOST=other.host, or later in LobbyStats.ini: [LobbyStats] Server=host)
STATS_HOST ?= rts.fintebtc.de
STATS = $(if $(STATS_HOST),-DSTATS_HOST='L"$(STATS_HOST)"',)

all: $(OUT)/ObsFix.dll $(OUT)/LobbyStats.dll $(OUT)/LoadingRatings.dll $(OUT)/Cooldowns.dll $(OUT)/MatchTimer.dll $(OUT)/winmm.dll $(OUT)/FrameProbe.dll $(OUT)/ReinforceFix.dll $(OUT)/HeroLabelFix.dll $(OUT)/JumpRetry.dll $(OUT)/JoinSound.dll $(OUT)/APM.dll $(OUT)/SteamKeep.dll
all: $(OUT)/EliteAI.dll $(OUT)/Reverse.dll $(OUT)/MMFind.dll $(OUT)/RoomJoin.dll $(OUT)/Relay.dll $(OUT)/Continue.dll
# released too (04.11.2026: the GitHub Release carries every released DLL; build/early/winmm.dll is built by its own name)
all: $(OUT)/RelayForward.dll $(OUT)/OwnLobby.dll $(OUT)/CrashLog.dll $(OUT)/NetProbe.dll
all: $(OUT)/rjbridge.exe
all: $(OUT)/ShellFix.dll
all: $(OUT)/LiveReplay.dll $(OUT)/lrbridge.exe
all: $(OUT)/ReplayOpen.dll $(OUT)/robridge.exe
all: $(OUT)/UnitStats.dll
all: $(OUT)/ReplayTakeover.dll
all: $(OUT)/SoundFix.dll
all: $(OUT)/SoundBugTest.dll
all: $(OUT)/ReplaySpeed.dll
all: $(OUT)/ChatHide.dll
all: $(OUT)/FastBuffers.dll $(OUT)/FastLocStrings.dll
all: $(OUT)/PerfLog.dll
all: $(OUT)/CursorScale.dll
all: $(OUT)/DXVK.dll $(OUT)/FpsUnlock.dll
all: $(OUT)/UIFilter.dll
all: $(OUT)/EliteMaps.dll
all: $(OUT)/OverwatchOnce.dll
all: $(OUT)/CtrlAttack.dll
all: $(OUT)/SpeechStreakFix.dll
all: $(OUT)/DocsFix.dll
all: $(OUT)/LangSwitch.dll
all: $(OUT)/NoHints.dll
all: $(OUT)/UIScale.dll
all: $(OUT)/MatchSpeed.dll
all: $(OUT)/Marks.dll
all: $(OUT)/MoveLoopFix.dll

$(OUT):
	mkdir -p $(OUT)
$(OUT)/ObsFix.dll: obsfix/obsfix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc
# LobbyStats 0.11.0: only the lobby ratings (the clock, cooldowns, loading ratings and join sound are add-ons of their own)
$(OUT)/LobbyStats.dll: lobbystats/lobbystats.c lobbystats/join_sound.h | $(OUT)
	$(CC) $(CFLAGS) $(STATS) -shared -o $@ lobbystats/lobbystats.c -lwinhttp -lpsapi -lgdi32 -lwinmm -static-libgcc -Wl,--kill-at

# 30.10.2026: every feature its own add-on - the loading screen ratings, the cooldown numbers, the match clock (MatchTimer.dll)
$(OUT)/LoadingRatings.dll: lobbystats/lobbystats.c matchtimer/matchtimer.c | $(OUT)
	$(CC) $(CFLAGS) $(STATS) -DLS_LOADING -DMT_EMBED -shared -o $@ lobbystats/lobbystats.c matchtimer/matchtimer.c -lwinhttp -lpsapi -lgdi32 -lwinmm -static-libgcc -Wl,--kill-at

# 02.11.2026: the lobby join sound its own add-on (soft / sharp), from the same source
$(OUT)/JoinSound.dll: lobbystats/lobbystats.c lobbystats/join_sound.h | $(OUT)
	$(CC) $(CFLAGS) $(STATS) -DLS_JOINSOUND -shared -o $@ lobbystats/lobbystats.c -lwinhttp -lpsapi -lgdi32 -lwinmm -static-libgcc -Wl,--kill-at

$(OUT)/MatchTimer.dll: matchtimer/matchtimer.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lpsapi -lgdi32 -static-libgcc -Wl,--exclude-all-symbols

$(OUT)/Cooldowns.dll: cooldowns/cooldowns_main.c cooldowns/cooldowns.c matchtimer/matchtimer.c | $(OUT)
	$(CC) $(CFLAGS) -DMT_EMBED -DCD_STANDALONE -shared -o $@ cooldowns/cooldowns_main.c cooldowns/cooldowns.c matchtimer/matchtimer.c -lpsapi -lgdi32 -lwinmm -static-libgcc -Wl,--exclude-all-symbols

# 03.11.2026: actions per minute beside the victory point counter, its own add-on (the match core of matchtimer.c)
$(OUT)/APM.dll: apm/apm.c matchtimer/matchtimer.c common/gfxui.h | $(OUT)
	$(CC) $(CFLAGS) -DMT_EMBED -shared -o $@ apm/apm.c matchtimer/matchtimer.c -lpsapi -lgdi32 -static-libgcc -Wl,--exclude-all-symbols

# 03.11.2026: the match goes on when Steam goes away (no leaving the match / closing the game on a Steam drop)
$(OUT)/SteamKeep.dll: steamkeep/steamkeep.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 04.11.2026: Elite's ShellPatch.dll without its Detours wrapper (no room for a trampoline in the lower 2 GB) crashed every
# match start (SetVisible(NULL)); ShellFix puts the wrapper in when it is missing, changes nothing otherwise
$(OUT)/ShellFix.dll: shellfix/shellfix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 07.11.2026: a bigger mouse cursor at high resolutions (the game's own Windows cursor, scaled when it is made; the UI scale stays)
$(OUT)/CursorScale.dll: cursorscale/cursorscale.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lgdi32 -static-libgcc

# 12.11.2026: the in-match interface bigger than its own size (the game caps its UI scale at 1.0: nothing above 100 % at 1440p/4K)
$(OUT)/UIScale.dll: uiscale/uiscale.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

clean:
	rm -rf $(OUT)
.PHONY: all clean


# EliteAI 0.11 (11.11.2026): the replay of a match with the new AI to the replay site (WinHTTP, SHGetFolderPathW)
$(OUT)/EliteAI.dll: eliteai/eliteai.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lwinhttp -lshell32 -static-libgcc

$(OUT)/Reverse.dll: reverse/reverse.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

$(OUT)/MMFind.dll: mmfind/mmfind.c common/connbox.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lwinhttp -lpsapi -lshell32 -lole32 -static-libgcc

# Relay 17: two DLLs from one source: Relay.dll (since 05.11.2026 part of the automatch package, loaded by MMFind.dll - the
# tick relay_r2 is gone) and RelayForward.dll (relay_fwd, forwarding servers); whichever loads first carries the connections for both
# Relay 23: the UDP tunnel's delivery is relay/du_tunnel.h (also built for Linux by relay/test/build.sh)
$(OUT)/Relay.dll: relay/relay.c relay/du_tunnel.h relay/rejoin.h common/gfxui.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lws2_32 -lpsapi -lwinmm -static-libgcc

$(OUT)/RelayForward.dll: relay/relay.c relay/du_tunnel.h relay/rejoin.h common/gfxui.h | $(OUT)
	$(CC) $(CFLAGS) -DRELAY_FWD -shared -o $@ $< -lws2_32 -lpsapi -lwinmm -static-libgcc

# what closed the game: serious exceptions with module+offset and stack, a minidump on a crash, ExitProcess / TerminateProcess callers
$(OUT)/CrashLog.dll: crashlog/crashlog.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lpsapi -static-libgcc -Wl,--exclude-all-symbols

# experiment: what passes from a player to our game servers (no hooks; NetProbe.log goes with "Send logs")
$(OUT)/NetProbe.dll: netprobe/netprobe.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lws2_32 -lwinhttp -static-libgcc -Wl,--exclude-all-symbols

# our lobby server instead of Relic's for the game's own automatch - part of Automatch v2.0 (launcher automatch; loaded early by winmm.dll)
$(OUT)/OwnLobby.dll: ownlobby/ownlobby.c ownlobby/ca_pem.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ ownlobby/ownlobby.c -lws2_32 -static-libgcc -Wl,--exclude-all-symbols

# the loader for vanilla (and the early add-ons of the launcher): forwards everything to the system winmm.dll
$(OUT)/winmm.dll: vanilla-loader/winmm_proxy.c vanilla-loader/winmm.def | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ vanilla-loader/winmm_proxy.c vanilla-loader/winmm.def -static-libgcc

# the same loader for Automatch v2.0 (launcher automatch, was the ownlobby add-on, 03.11.2026): only the early add-ons in Elite_DLLs, no dow2-patches folder
# (the launcher takes a winmm.dll naming that folder for the old stand-alone package and puts it aside before every start)
$(OUT)/early/winmm.dll: vanilla-loader/winmm_proxy.c vanilla-loader/winmm.def | $(OUT)
	mkdir -p $(OUT)/early
	$(CC) $(CFLAGS) -DEARLY_ONLY -shared -o $@ vanilla-loader/winmm_proxy.c vanilla-loader/winmm.def -static-libgcc

# the launcher's copy for its "Vanilla Retribution" mode (launcher 1.4.11, 05.11.2026): by the command line - a mod (-modname Elite)
# gets only the early add-ons, like build/early/winmm.dll; plain vanilla only the DLLs of the patch folder, never Elite_DLLs
$(OUT)/launcher/winmm.dll: vanilla-loader/winmm_proxy.c vanilla-loader/winmm.def | $(OUT)
	mkdir -p $(OUT)/launcher
	$(CC) $(CFLAGS) -DLAUNCHER_LOADER -shared -o $@ vanilla-loader/winmm_proxy.c vanilla-loader/winmm.def -static-libgcc

$(OUT)/FrameProbe.dll: frameprobe/frameprobe.c | $(OUT)
	$(CC) $(CFLAGS) -Wno-unused-variable -shared -o $@ $< -lpsapi -lwinmm -static-libgcc

$(OUT)/ReinforceFix.dll: reinforcefix/reinforcefix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 08.11.2026: the bar over a hero comes back after he is revived (a hidden squad_leader_decorator was never drawn again)
$(OUT)/HeroLabelFix.dll: herolabelfix/herolabelfix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 08.11.2026: replay speeds 8x and Max - two buttons next to the replay panel's speed button
$(OUT)/ReplaySpeed.dll: replayspeed/replayspeed.c common/gfxui.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 08.11.2026 performance, rendering/UI only (no simulation code touched): dynamic buffer locks without the page probe;
# formatted UI strings released by searching from the end
$(OUT)/FastBuffers.dll: fastbuffers/fastbuffers.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc
$(OUT)/FastLocStrings.dll: fastlocstrings/fastlocstrings.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 08.11.2026: performance log for players - FPS and what limits it (main loop parts, Present wait, main thread CPU), no game change
$(OUT)/PerfLog.dll: perflog/perflog.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 09.11.2026: hide incoming player chat - a button by the minimap and Ctrl+Shift+C
$(OUT)/ChatHide.dll: chathide/chathide.c common/gfxui.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 12.11.2026: speed x2 / x4 and a pause for a live match without other people (13.11 v2: buttons at the top left, Ctrl+= / Ctrl+- / Ctrl+P)
$(OUT)/MatchSpeed.dll: matchspeed/matchspeed.c common/gfxui.h common/topbar.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 13.11.2026: Match marks - a button / Ctrl+M notes the moment of a match or a replay in Marks.log (AI testers)
$(OUT)/Marks.dll: marks/marks.c common/gfxui.h common/topbar.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 13.11.2026: the game froze for good when a melee unit's last step landed exactly on its target's centre (the movement loop
# 0x009141A0 never spent the tick's time); cut after 256 passes with no time spent
$(OUT)/MoveLoopFix.dll: moveloopfix/moveloopfix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 03.11.2026: the game's own Continue after a custom game (host -> everyone back in the same room)
$(OUT)/Continue.dll: continue/continue.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 11.11.2026: an overwatch order (right-click on a unit button) builds one squad, then the HUD's overwatch entry is erased
$(OUT)/OverwatchOnce.dll: overwatchonce/overwatchonce.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 11.11.2026: the kill-streak speech read 4 bytes past its list for a unit's first kill (crash at 0x00B8FC24); 4 bytes patched
$(OUT)/SpeechStreakFix.dll: speechstreakfix/speechstreakfix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 12.11.2026: Alt+Shift / Ctrl+Shift / Win+Space switch the keyboard language inside the game window (the game's message loop
# left the text services' messages in the queue); Platform.dll's PeekMessageA import wrapped
$(OUT)/LangSwitch.dll: langswitch/langswitch.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 12.11.2026: no beginner hints in matches (skirmish quick tips): Misc_QuickTipsEnabled answers false, 3 bytes
$(OUT)/NoHints.dll: nohints/nohints.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 11.11.2026: Ctrl + right-click on the ground = the game's own attack-ground order (core action G) for the squads that can
$(OUT)/CtrlAttack.dll: ctrlattack/ctrlattack.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 03.11.2026: a jump order lost on an obstacle hop (Autarch) is sent again - client side, the game's own order
$(OUT)/JumpRetry.dll: jumpretry/jumpretry.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 03.11.2026 experiment: joining a room from the launcher (the Lobbies tab), its own add-on next to MMFind; RoomJoin 9 (05.11): the pipe
$(OUT)/RoomJoin.dll: roomjoin/roomjoin.c common/connbox.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lpsapi -static-libgcc -Wl,--exclude-all-symbols
# RoomJoin's pipe for a launcher outside Wine (Linux): shipped with the add-on as Elite_DLLs/rjbridge.exe
$(OUT)/rjbridge.exe: roomjoin/rjbridge.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $< -static-libgcc
# MMFind 0.46: the same bridge for MMFind's pipe (launcher rooms), shipped with the automatch package as Elite_DLLs/mmbridge.exe
$(OUT)/mmbridge.exe: roomjoin/rjbridge.c | $(OUT)
	$(CC) $(CFLAGS) -DPIPE_PREFIX='L"EliteMMFind"' -o $@ $< -static-libgcc
# the test client of MMFind's pipe (not shipped): make build/mmpipe.exe
$(OUT)/mmpipe.exe: roomjoin/rjpipe.c | $(OUT)
	$(CC) $(CFLAGS) -DPIPE_PREFIX='"EliteMMFind"' -o $@ $< -static-libgcc
# the test client of ReplayOpen's pipe (not shipped): make build/ropipe.exe
$(OUT)/ropipe.exe: roomjoin/rjpipe.c | $(OUT)
	$(CC) $(CFLAGS) -DPIPE_PREFIX='"EliteReplayOpen"' -o $@ $< -static-libgcc
# the test client of LiveReplay's pipe (not shipped): make build/lrpipe.exe
$(OUT)/lrpipe.exe: roomjoin/rjpipe.c | $(OUT)
	$(CC) $(CFLAGS) -DPIPE_PREFIX='"EliteLiveReplay"' -o $@ $< -static-libgcc
# the test client of RoomJoin's pipe (RoomJoin 9; not shipped): make build/rjpipe.exe
$(OUT)/rjpipe.exe: roomjoin/rjpipe.c | $(OUT)
	$(CC) $(CFLAGS) -o $@ $< -static-libgcc

# 04-07.11.2026 (grantovich): the selected squad's DPS (as the attribute editor counts it) / melee skill / armour / range / speed /
# sight and its active effects left of the Retreat button, in the selection panel movie - docs/UNIT_STATS_HOWTO.md
$(OUT)/UnitStats.dll: unitstats/unitstats.c common/gfxui.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ unitstats/unitstats.c -static-libgcc -Wl,--exclude-all-symbols

# 06.11.2026: watch a running match of our servers as a growing replay (the spectator's side; the servers build the replay)
$(OUT)/LiveReplay.dll: livereplay/livereplay.c common/connbox.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lwinhttp -lshell32 -static-libgcc
# its pipe for a launcher outside Wine (Linux), shipped with the add-on as Elite_DLLs/lrbridge.exe
$(OUT)/lrbridge.exe: roomjoin/rjbridge.c | $(OUT)
	$(CC) $(CFLAGS) -DPIPE_PREFIX='L"EliteLiveReplay"' -o $@ $< -static-libgcc
# 14.11.2026 (siritos): "Play" of a replay from the launcher - the game's Connecting box, then the replay (its own add-on)
$(OUT)/ReplayOpen.dll: replayopen/replayopen.c common/connbox.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lshell32 -static-libgcc -Wl,--exclude-all-symbols
$(OUT)/robridge.exe: roomjoin/rjbridge.c | $(OUT)
	$(CC) $(CFLAGS) -DPIPE_PREFIX='L"EliteReplayOpen"' -o $@ $< -static-libgcc
# 09.11.2026 (performance): the game's Direct3D 9 through DXVK 2.7.1 (dxvk/DXVK_d3d9.bin = the official x32 d3d9.dll, zlib licence
# dxvk/DXVK_LICENSE.txt): only Spooge's Direct3DCreate9 import is pointed at it; nothing when the game's d3d9.dll is DXVK already
$(OUT)/DXVK.dll: dxvk/dxvk.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc -Wl,--exclude-all-symbols

# 09.11.2026 (performance): no DXVK frame cap (d3d9/dxgi.maxFrameRate = 0 appended to DXVK_CONFIG before Direct3D); vsync untouched
$(OUT)/FpsUnlock.dll: fpsunlock/fpsunlock.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc -Wl,--exclude-all-symbols

# 09.11.2026 (performance): the interface (Scaleform) renderer's repeated Direct3D state calls dropped inside one display - a proxy
# device only for that renderer (BeginDisplay/EndDisplay patched in Spooge.dll), the real device untouched; UIFilter.ini Enabled/Verify
$(OUT)/UIFilter.dll: uifilter/uifilter.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc -Wl,--exclude-all-symbols

# 11.11.2026 (experiment, vanilla only): Elite's map archives of the same game folder appended to vanilla's [data:common] archive
# list (DOW2.exe's import of ConfigFile::GetString answers the missing archive.NN keys); maps only - the data checksum stays vanilla's
$(OUT)/EliteMaps.dll: elitemaps/elitemaps.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc -Wl,--exclude-all-symbols

# 06.11.2026 (experiment): continue a replay as a skirmish from any tick, as any of its players (the pause menu's
# "Concede match" button in a replay becomes "Continue as <name>")
$(OUT)/ReplayTakeover.dll: replaytakeover/replaytakeover.c common/gfxui.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 08.11.2026: the sound bug (sound stutters/loops/vanishes in late big fights at high FPS): Shark's sound queues no longer grow
# with the frame rate, FMOD Ex 4.32.20 (soundfix/SoundFix_fmodex.bin, the file Elite ships as Elite_bin/Elite_fmodex.bin) for
# vanilla's Shark, no frame cap (DXVK_CONFIG) while it is on, FMOD at the output device's mix rate, audio device changes in the log
$(OUT)/SoundFix.dll: soundfix/soundfix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lole32 -static-libgcc -Wl,--exclude-all-symbols

# 08.11.2026 (TEST): the sound bug's worst case for players who try to provoke it - Relic's own FMOD Ex 4.32.3
# (soundbugtest/SoundBugTest_fmodex.bin, from the Steam depot) and Shark's stock version check also in Elite, no queue merging,
# no frame cap - plus a detailed sound log (SoundBugTest.log / SoundBugTest_events.log). Conflicts with SoundFix.
$(OUT)/SoundBugTest.dll: soundbugtest/soundbugtest.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lole32 -lpsapi -static-libgcc -Wl,--exclude-all-symbols

# 12.11.2026 (experiment): Documents fix - the game's settings and replays to %LOCALAPPDATA%\EliteLauncher\Documents when the game
# process may not write to Documents\My Games (write test from the game; common/docscheck.h logs the proof at the start and after
# every match). Loaded first by the early winmm loaders (EARLY list / vanilla loader).
$(OUT)/DocsFix.dll: docsfix/docsfix.c common/docscheck.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lshell32 -ladvapi32 -static-libgcc -Wl,--exclude-all-symbols
