#!/bin/bash
# vsession_v7.sh <name> - stand 1, VANILLA DOW2 3.19 (no -modname): UnitStats.dll in a fresh dow2-patches (no ini - the defaults,
# as the launcher copies it) + EliteAI.dll there only to run the stand SCAR (unitstats_v7.scar appended to skirmish.scar from
# Elite_DLLs/EliteAI/append, as dev/vanilla_scar_test/run.sh) + the launcher's vanilla loader winmm.dll ($LOADER, build/launcher);
# -dev -nomovies -init us7v.lua (Eldar vs IG, AI off). A screenshot 2.5 s after every SELECT line of EliteAI.log. Then the stand
# goes back exactly as it was (md5 checked). Only through /mnt/dow2bench/dev/stand_lock.sh.
set -u
T=${T:-/mnt/dow2bench/dev/unitstats07_1126/stand}; NAME=${1:-vrun}; O=$T/out/$NAME; mkdir -p $O; . /mnt/dow2bench/stand/env1.sh
SH=${SH:-/var/www/media/dow2_shots/unitstats/v7/raw/$NAME}; mkdir -p "$SH"
DLL=${DLL:?UnitStats.dll}; LOADER=${LOADER:?the launcher winmm.dll}; EAI=${EAI:-/mnt/dow2bench/dev/eliteai/dll/EliteAI.dll}
SCAR=${SCAR:-$T/unitstats_v7.scar}; INIT=${INIT:-$T/init_v3.lua.in}; MAXMIN=${MAXMIN:-7}
G="$GAMEDIR"; P="$G/dow2-patches"; D="$G/Elite_DLLs"; BK=$T/vbak
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
mkdir -p "$BK"; mv "$P" "$BK/dow2-patches" || exit 1; cp -p "$G/winmm.dll" "$BK/winmm.dll"; cp -a "$D" "$BK/Elite_DLLs"
restore() {
  kill_game
  cp "$P"/*.log "$P"/*.ini $O/ 2>/dev/null; cp "$D/EliteAI.log" $O/ 2>/dev/null
  rm -f "$G/us7v.lua"; rm -rf "${P:?}" "${D:?}"; mv "$BK/dow2-patches" "$P"; cp -a "$BK/Elite_DLLs" "$D"; cp -p "$BK/winmm.dll" "$G/winmm.dll"; rm -rf "${BK:?}"
  snap $O/md5_after.txt
  if cmp -s $O/md5_before.txt $O/md5_after.txt; then log "stand 1 restored: md5 identical"; else log "stand 1 restore: md5 DIFFER"; diff $O/md5_before.txt $O/md5_after.txt | tee -a $O/session.log; fi
}
trap restore EXIT
mkdir -p "$P"; cp "$DLL" "$P/UnitStats.dll"; cp "$EAI" "$P/EliteAI.dll"; cp "$LOADER" "$G/winmm.dll"
# the abilities vanilla 3.19 data does not have under these names are left out: in -dev a SCAR call with a missing blueprint
# stops the simulation (stand 26.11: the exarch aura froze the first vanilla run; guide / fortune exist)
rm -rf "$D/EliteAI"; mkdir -p "$D/EliteAI/append/scar"; rm -f "$D/EliteAI.log"
grep -v -E "auraexarch|commissar_kill_target_execute|ork_buff_self_frenzy" "$SCAR" > "$D/EliteAI/append/scar/skirmish.scar"
printf '[EliteAI]\r\nVerbose=1\r\n[Lua]\r\n' > "$D/EliteAI.ini"
sed -e "s/@RACE@/eldar/g" -e "s/@HERO@/0/" -e "s/@EHERO@/1/" -e "s/@ENEMY@/imperial_guard/" -e "s/@WIN@//g" "$INIT" > "$G/us7v.lua"
log "deployed: $(ls "$P" | tr '\n' ' ') winmm $(md5sum < "$G/winmm.dll" | cut -c1-8)"
cd "$G"; nohup wine DOW2.exe -dev -nomovies -init us7v.lua > $O/game.log 2>&1 &
p=""; for i in $(seq 1 60); do p=$(mypids); [ -n "$p" ] && break; sleep 2; done
[ -z "$p" ] && { log "no game process"; exit 1; }
log "game pid $p"; t0=$(date +%s); n0=0; k=0
shot() { DISPLAY=:99 import -window root "$SH/$1.png" && convert "$SH/$1.png" -crop 500x210+780+590 +repage -filter Lanczos -resize 200% "$SH/zoom_$1.png"; }
while [ ! -f $T/vstop ] && [ $(( $(date +%s) - t0 )) -lt $(( MAXMIN * 60 )) ]; do
  sleep 1; [ -z "$(mypids)" ] && { log "game process GONE"; break; }
  n=$(grep -c "SELECT" "$D/EliteAI.log" 2>/dev/null); n=${n:-0}
  if [ "$n" -gt "$n0" ]; then
    sleep 2.5; k=$((k+1)); nm=$(grep "SELECT" "$D/EliteAI.log" | tail -1 | sed 's/.*SELECT[ \t]*//; s/[ \t]*\(true\|false\)$//; s/[^A-Za-z0-9]\+/_/g; s/_$//' | cut -c1-60)
    shot "v$(printf %02d $k)_$nm"; log "shot $k $nm"; n0=$n
  fi
  grep -q "US7 DONE" "$D/EliteAI.log" 2>/dev/null && { sleep 3; log "scenario done"; break; }
done
log "$NAME done"
