# All DLLs are 32-bit (DOW2.exe is 32-bit). Needs MinGW-w64: apt install gcc-mingw-w64-i686
CC = i686-w64-mingw32-gcc
CFLAGS = -O2 -s -Wall -Wno-unused-function -Wno-misleading-indentation -Wno-stringop-truncation
OUT = build
# stats service host for LobbyStats, baked into the DLL (make STATS_HOST=other.host, or later in LobbyStats.ini: [LobbyStats] Server=host)
STATS_HOST ?= rts.fintebtc.de
STATS = $(if $(STATS_HOST),-DSTATS_HOST='L"$(STATS_HOST)"',)

all: $(OUT)/ObsFix.dll $(OUT)/LobbyStats.dll $(OUT)/LoadingRatings.dll $(OUT)/Cooldowns.dll $(OUT)/MatchTimer.dll $(OUT)/winmm.dll $(OUT)/FrameProbe.dll $(OUT)/ReinforceFix.dll $(OUT)/JoinSound.dll
all: $(OUT)/EliteAI.dll $(OUT)/Reverse.dll $(OUT)/MMFind.dll $(OUT)/Relay.dll

$(OUT):
	mkdir -p $(OUT)
$(OUT)/ObsFix.dll: obsfix/obsfix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc
# LobbyStats carries the match clock (matchtimer.c with -DMT_EMBED) and the cooldown numbers (cooldowns.c): one Direct3D hook for all
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

# experiment: our own lobby server instead of Relic's for the automatch rooms (launcher add-on ownlobby; loaded early by winmm.dll)
$(OUT)/OwnLobby.dll: ownlobby/ownlobby.c ownlobby/ca_pem.h | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ ownlobby/ownlobby.c -lws2_32 -static-libgcc -Wl,--exclude-all-symbols

# the loader for vanilla (and the early add-ons of the launcher): forwards everything to the system winmm.dll
$(OUT)/winmm.dll: vanilla-loader/winmm_proxy.c vanilla-loader/winmm.def | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ vanilla-loader/winmm_proxy.c vanilla-loader/winmm.def -static-libgcc

$(OUT)/FrameProbe.dll: frameprobe/frameprobe.c | $(OUT)
	$(CC) $(CFLAGS) -Wno-unused-variable -shared -o $@ $< -lpsapi -lwinmm -static-libgcc

$(OUT)/ReinforceFix.dll: reinforcefix/reinforcefix.c | $(OUT)
	$(CC) $(CFLAGS) -shared -o $@ $< -static-libgcc
