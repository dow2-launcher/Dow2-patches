# All DLLs are 32-bit (DOW2.exe is 32-bit). Needs MinGW-w64: apt install gcc-mingw-w64-i686
CC = i686-w64-mingw32-gcc
CFLAGS = -O2 -s -Wall -Wno-unused-function -Wno-misleading-indentation -Wno-stringop-truncation
OUT = build
# stats service host for LobbyStats, baked into the DLL (make STATS_HOST=other.host, or later in LobbyStats.ini: [LobbyStats] Server=host)
STATS_HOST ?= rts.fintebtc.de
STATS = $(if $(STATS_HOST),-DSTATS_HOST='L"$(STATS_HOST)"',)

all: $(OUT)/ObsFix.dll $(OUT)/LobbyStats.dll $(OUT)/LoadingRatings.dll $(OUT)/Cooldowns.dll $(OUT)/MatchTimer.dll $(OUT)/winmm.dll $(OUT)/FrameProbe.dll $(OUT)/ReinforceFix.dll $(OUT)/JumpRetry.dll $(OUT)/JoinSound.dll $(OUT)/APM.dll $(OUT)/SteamKeep.dll
all: $(OUT)/EliteAI.dll $(OUT)/Reverse.dll $(OUT)/MMFind.dll $(OUT)/RoomJoin.dll $(OUT)/Relay.dll $(OUT)/Continue.dll
# released too (04.11.2026: the GitHub Release carries every released DLL; build/early/winmm.dll is built by its own name)
all: $(OUT)/RelayForward.dll $(OUT)/OwnLobby.dll $(OUT)/CrashLog.dll $(OUT)/NetProbe.dll
all: $(OUT)/ShellFix.dll

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

# 05.11.2026 EXPERIMENT (archive branch only, not in all): the selected squad's DPS / melee skill / range / speed / sight
# left of the Retreat button, in the selection panel movie - docs/UNIT_STATS_HOWTO.md
$(OUT)/UnitStats.dll: unitstats/unitstats.c common/gfxui.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ unitstats/unitstats.c -static-libgcc -Wl,--exclude-all-symbols

clean:
	rm -rf $(OUT)
.PHONY: all clean


$(OUT)/EliteAI.dll: eliteai/eliteai.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

$(OUT)/Reverse.dll: reverse/reverse.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

$(OUT)/MMFind.dll: mmfind/mmfind.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lwinhttp -lpsapi -lshell32 -lole32 -static-libgcc

# Relay 17: two add-ons from one source, two ticks (docs/ADDON_RULES.md): our servers for games (relay_r2) and the
# forwarding servers for all other games (relay_fwd); whichever loads first carries the connections for both
$(OUT)/Relay.dll: relay/relay.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lws2_32 -lpsapi -lwinmm -static-libgcc

$(OUT)/RelayForward.dll: relay/relay.c | $(OUT)
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

$(OUT)/FrameProbe.dll: frameprobe/frameprobe.c | $(OUT)
	$(CC) $(CFLAGS) -Wno-unused-variable -shared -o $@ $< -lpsapi -lwinmm -static-libgcc

$(OUT)/ReinforceFix.dll: reinforcefix/reinforcefix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 03.11.2026: the game's own Continue after a custom game (host -> everyone back in the same room)
$(OUT)/Continue.dll: continue/continue.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 03.11.2026: a jump order lost on an obstacle hop (Autarch) is sent again - client side, the game's own order
$(OUT)/JumpRetry.dll: jumpretry/jumpretry.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc

# 03.11.2026 experiment: joining a room from the launcher (the Lobbies tab), its own add-on next to MMFind
$(OUT)/RoomJoin.dll: roomjoin/roomjoin.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -lpsapi -static-libgcc -Wl,--exclude-all-symbols
