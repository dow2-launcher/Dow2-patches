#!/bin/bash
# vsession_v5.sh <name> - stand 1, VANILLA DOW2 3.19 (no -modname): UnitStats.dll alone in a fresh dow2-patches (no ini - the
# defaults, as the launcher copies it) + the launcher's vanilla loader winmm.dll (vloader-1); -dev -nomovies -init vskirm.lua
# (space marines vs an idle AI). Holds the game until $T/vstop or $MAXMIN min (the squads are selected by hand / xdotool);
# then the stand goes back exactly as it was (md5 checked). Only through /mnt/dow2bench/dev/stand_lock.sh.
set -u
T=/mnt/dow2bench/dev/unitstats/v5; NAME=${1:-vrun}; O=$T/out/$NAME; mkdir -p $O; . /mnt/dow2bench/stand/env1.sh
SH=${SH:-/var/www/media/dow2_shots/unitstats/v5/raw/$NAME}; mkdir -p "$SH"
DLL=${DLL:-/mnt/rts/dow2_re/wt_unitstats5/build/UnitStats.dll}; LOADER=${LOADER:?the vloader-1 winmm.dll}; MAXMIN=${MAXMIN:-8}
G="$GAMEDIR"; P="$G/dow2-patches"; BK=$T/vbak
log() { echo "$(date +%T) $*" | tee -a $O/session.log; }
mypids() { for p in $(pgrep -f 'DOW2.exe'); do tr '\0' '\n' < /proc/$p/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && echo $p; done; }
[ -n "$(mypids)" ] && { log "stand 1 game already running - stop"; exit 1; }
[ -e "$BK" ] && { log "$BK exists - previous session not restored"; exit 1; }
snap() { ( cd "$G" && find dow2-patches Elite_DLLs winmm.dll -type f -print0 | sort -z | xargs -0 md5sum ) > "$1"; }
kill_game() {
  for p in $(mypids); do log "kill DOW2 pid $p"; kill $p; done
  for i in $(seq 1 15); do [ -z "$(mypids)" ] && break; sleep 1; done
  for p in $(mypids); do log "kill -9 DOW2 pid $p"; kill -9 $p; done
  for p in $(pgrep -f BsSndRpt); do tr '\0' '\n' < /proc/$p/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && { log "kill BsSndRpt $p"; kill $p; }; done
  sleep 2
}
snap $O/md5_before.txt
mkdir -p "$BK"; mv "$P" "$BK/dow2-patches" || exit 1; cp -p "$G/winmm.dll" "$BK/winmm.dll"
restore() {
  kill_game
  cp "$P"/*.log "$P"/*.ini $O/ 2>/dev/null
  rm -f "$G/vskirm_us5.lua" "$T/vready" "$T/vstop"; rm -rf "${P:?}"; mv "$BK/dow2-patches" "$P"; cp -p "$BK/winmm.dll" "$G/winmm.dll"; rm -rf "${BK:?}"
  snap $O/md5_after.txt
  if cmp -s $O/md5_before.txt $O/md5_after.txt; then log "stand 1 restored: md5 identical"; else log "stand 1 restore: md5 DIFFER"; diff $O/md5_before.txt $O/md5_after.txt | tee -a $O/session.log; fi
}
trap restore EXIT
mkdir -p "$P"; cp "$DLL" "$P/UnitStats.dll"; cp "$LOADER" "$G/winmm.dll"; cp $T/vskirm.lua "$G/vskirm_us5.lua"
log "deployed: $(ls "$P" | tr '\n' ' ') winmm $(md5sum < "$G/winmm.dll" | cut -c1-8)"
cd "$G"; nohup wine DOW2.exe -dev -nomovies -init vskirm_us5.lua > $O/game.log 2>&1 &
p=""; for i in $(seq 1 60); do p=$(mypids); [ -n "$p" ] && break; sleep 2; done
[ -z "$p" ] && { log "no game process"; exit 1; }
log "game pid $p"; touch $T/vready; t0=$(date +%s)
while [ ! -f $T/vstop ] && [ $(( $(date +%s) - t0 )) -lt $(( MAXMIN * 60 )) ]; do
  sleep 2; [ -z "$(mypids)" ] && { log "game process GONE"; break; }
done
DISPLAY=:99 import -window root $SH/vlast.png 2>/dev/null
log "$NAME done"
