#!/bin/bash
# session.sh <name> - UnitStats 0.10 live-observer test on stand 1 (26.11.2026, from matchspeed_1112/ms_session4.sh KIND=fe).
# Front-end Elite launch (no -dev), UnitStats.dll + ini in dow2-patches (the stand's winmm loader); the menus are driven from
# outside with ui.sh (Custom Games -> Private, own seat -> observer, computers in the team slots, Start).
# Holds until $T/stop or HOLDMIN. Only through stand_lock.sh. The trap stops the game by PID and restores everything (md5 checked).
set -u
T=/mnt/dow2bench/dev/unitstats10_1126; NAME=${1:?name}; O=/var/www/media/dow2_shots/unitstats/v10/$NAME; mkdir -p $O
N=1; . /mnt/dow2bench/stand/env$N.sh; unset DXVK_CONFIG DXVK_CONFIG_FILE DXVK_FRAME_RATE
G="$GAMEDIR"; E="$G/Elite_DLLs"; P="$G/dow2-patches"; BK=$T/bak$N; HOLDMIN=${HOLDMIN:-40}
DOCS="$WINEPREFIX/drive_c/users/dowbench/Documents/My Games/Dawn of War II - Retribution"; PB="$DOCS/Playback"
gpid() { for p in $(pgrep -f "DOW2.exe"); do grep -q "Retribution/DOW2.exe" /proc/$p/maps 2>/dev/null || continue; tr "\0" "\n" < /proc/$p/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && { echo $p; return; }; done; }
[ -n "$(gpid)" ] && { echo "stand $N game already running"; exit 1; }
[ -e "$BK" ] && { echo "$BK exists - previous session not restored"; exit 1; }
snap() { ( cd "$G" && { find Elite_DLLs dow2-patches -type f -print0 | sort -z | xargs -0 md5sum; md5sum winmm.dll Elite.launchconfig; ls Playback 2>/dev/null; } ; ls "$PB" | grep -v '^temp.rec$' ) > "$1" 2>/dev/null; }
snap $O/md5_before.txt; mkdir -p "$BK"; cp -a "$E" "$BK/Elite_DLLs"; cp -a "$P" "$BK/dow2-patches"; cp -p "$G/winmm.dll" "$G/Elite.launchconfig" "$BK/"
[ -f "$PB/temp.rec" ] && cp -p "$PB/temp.rec" "$BK/temp.rec"
restore() {
  for i in 1 2 3; do p=$(gpid); [ -z "$p" ] && break; kill $p; sleep 3; done; p=$(gpid); [ -n "$p" ] && kill -9 $p; sleep 2
  for q in $(pgrep -f BsSndRpt); do tr '\0' '\n' < /proc/$q/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && kill $q; done
  cp "$P"/*.log "$P"/*.ini $O/ 2>/dev/null
  [ -f "$PB/temp.rec" ] && [ "$PB/temp.rec" -nt $O/md5_before.txt ] && cp "$PB/temp.rec" $O/temp.rec
  if [ -f "$BK/temp.rec" ]; then cp -p "$BK/temp.rec" "$PB/temp.rec"; else rm -f "$PB/temp.rec"; fi
  W="$DOCS/Logfiles/warnings.txt"; [ -f "$W" ] && [ "$W" -nt $O/md5_before.txt ] && cp "$W" $O/warnings.txt
  rm -rf "${E:?}" "${P:?}"; cp -a "$BK/Elite_DLLs" "$E"; cp -a "$BK/dow2-patches" "$P"; cp -p "$BK/winmm.dll" "$BK/Elite.launchconfig" "$G/"
  rm -rf "${BK:?}"; rm -f "$T/stop$N"
  snap $O/md5_after.txt; cmp -s $O/md5_before.txt $O/md5_after.txt && echo "$(date +%T) stand $N restored (md5 ok)" || echo "$(date +%T) RESTORE MD5 DIFFER"
}
trap restore EXIT
export WINEDLLOVERRIDES="mscoree,mshtml=;winmm=n,b"
rm -f "$P/EliteAI.dll" "$P/MatchSpeed.dll" "$P/UnitStats.dll" "$P/UnitStats.ini" "$P/UnitStats.log"; rm -rf "$E/EliteAI" "$E/EliteAI.ini" "$E/EliteAI.log" "$E/EliteAI.replay"
rm -f "$E/UnitStats.dll" "$E/UnitStats.ini" "$E/UnitStats.log"
cp $T/dll/UnitStats.dll "$P/" || exit 1
printf '[UnitStats]\r\nReplay=1\r\nVerbose=1\r\n' > "$P/UnitStats.ini"
LAUNCH="wine inj.exe Profiler.dll DOW2.exe -modname Elite -nomovies"
echo "=== $(date +%T) $NAME stand $N: $(ls "$P" | tr '\n' ' ') | $LAUNCH"
(cd "$G" && nohup $LAUNCH > $O/game.out 2>&1 &)
p=""; for i in $(seq 1 60); do p=$(gpid); [ -n "$p" ] && break; sleep 2; done; [ -z "$p" ] && { echo "no game process"; exit 1; }
echo "pid $p"; echo $p > $T/pid$N
rm -f $T/stop$N; t0=$(date +%s)
while [ ! -f $T/stop$N ] && [ $(( $(date +%s) - t0 )) -lt $(( HOLDMIN * 60 )) ]; do
  sleep 5; [ -z "$(gpid)" ] && { echo "$(date +%T) game process GONE"; break; }
done
[ -n "$(gpid)" ] && echo "$(date +%T) alive at the end" && import -window root $O/last.png
