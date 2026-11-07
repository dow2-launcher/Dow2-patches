#!/bin/bash
# session.sh <name> - stand 1 (cherpekat, :99): UnitStats v2 under Elite (-modname Elite -dev -init us.lua, eldar vs SM AI).
# Elite_DLLs = Elite's own four + EliteAI.dll (the stand SCAR appended to elite.scar: spawns squads of several races and selects
# one every $STEP s) + UnitStats.dll/.ini; a screenshot 2 s after every selection change seen in UnitStats.log (into $SH/raw).
# Holds the stand until v2/stop appears (max $MAXMIN min); UnitStats.ini there can be edited live. The trap stops the game and
# puts Elite_DLLs back. Only through /mnt/dow2bench/dev/stand_lock.sh.
set -u
T=/mnt/dow2bench/dev/unitstats/v2; NAME=${1:-run}; O=$T/out/$NAME; mkdir -p $O; . /mnt/dow2bench/stand/env1.sh
SH=${SH:-/var/www/media/dow2_shots/unitstats/v2/raw/$NAME}; mkdir -p "$SH"
export WINEDLLOVERRIDES="mscoree,mshtml=;winmm=b"            # the stand's own dow2-patches loader off: only Elite_DLLs
G="$GAMEDIR"; E="$G/Elite_DLLs"; BK="$G/Elite_DLLs.bk_unitstats"
DLL=${DLL:-/mnt/rts/dow2_re/wt_unitstats/build/UnitStats.dll}; RACE=${RACE:-eldar}; HERO=${HERO:-0}; ENEMYR=${ENEMYR:-space_marines}
STEP=${STEP:-8}; MAXMIN=${MAXMIN:-40}
OWN=${OWN:-sbps/pvp/race_eldar/troops/eld_banshee sbps/pvp/race_eldar/troops/eld_guardian sbps/pvp/race_eldar/vehicles/eld_falcon sbps/pvp/race_eldar/vehicles/eld_wraithlord sbps/pvp/race_eldar/troops/eld_warp_spider}
ENEMY=${ENEMY:-sbps/pvp/race_marine/troops/sm_tactical_marine sbps/pvp/race_marine/troops/sm_assault_marine sbps/pvp/race_marine/vehicles/sm_predator sbps/pvp/race_ork/troops/ork_slugga sbps/pvp/race_chaos/troops/csm_tactical_marine sbps/pvp/race_tyranid/troops/tyr_genestealer sbps/pvp/race_tau/troops/tau_fire_warriors sbps/pvp/race_imperial_guard/troops/ig_guardsmen_squad}
[ -n "$GAMEDIR" ] && [ -d "$E" ] || { echo "no stand dir"; exit 1; }
[ -e "$BK" ] && { echo "$BK exists - previous session not restored"; exit 1; }
gpid() { for p in $(pgrep -f "DOW2.exe"); do tr "\0" " " < /proc/$p/cmdline 2>/dev/null | grep -q "^DOW2.exe" || continue; tr "\0" "\n" < /proc/$p/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && { echo $p; return; }; done; }
[ -n "$(gpid)" ] && { echo "stand 1 game already running"; exit 1; }
stopgame() { local p; for i in 1 2 3; do p=$(gpid); [ -z "$p" ] && return 0; kill $p; sleep 3; done; p=$(gpid); [ -n "$p" ] && kill -9 $p; sleep 2; }
restore() {
  stopgame
  for q in $(pgrep -f BsSndRpt); do tr '\0' '\n' < /proc/$q/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && kill $q; done
  for f in "$E"/*.log "$E"/*.ini; do [ -f "$f" ] && cp "$f" "$O/"; done
  cp "$WINEPREFIX/drive_c/users/root/Documents/My Games/Dawn of War II - Retribution/Logfiles/warnings.txt" $O/warnings.txt 2>/dev/null
  rm -rf "${E:?}"; mv "$BK" "$E"; rm -f "$G/us.lua" "$T/ready" "$T/stop"
  echo "$(date +%T) stand 1 restored"
}
mv "$E" "$BK" || exit 1
trap restore EXIT
mkdir "$E"; for f in CullspherePatch.dll DoW2_ArmyPainterFix.dll MiscFixes.dll ShellPatch.dll EliteAI.dll; do cp -p "$BK/$f" "$E/"; done
cp -a "$BK/EliteAI" "$E/"; printf '[EliteAI]\r\nVerbose=0\r\n[Lua]\r\nNewAI=normal\r\n' > "$E/EliteAI.ini"
q() { local o=""; for x in $1; do o="$o\"$x\", "; done; echo "${o%, }"; }
sed -e "s|@OWN@|$(q "$OWN")|" -e "s|@ENEMY@|$(q "$ENEMY")|" -e "s|@STEP@|$STEP|" $T/unitstats_v2.scar >> "$E/EliteAI/append/scar/elite/elite.scar"
cp "$DLL" "$E/UnitStats.dll" || exit 1; cp $T/UnitStats.ini "$E/UnitStats.ini"
sed -e "s/@RACE@/$RACE/g" -e "s/@HERO@/$HERO/" -e "s/@ENEMY@/$ENEMYR/" -e "s/@WIN@//g" /mnt/dow2bench/dev/bugs_0511/init.lua.in > "$G/us.lua"
echo "=== $(date +%T) $NAME: $(ls "$E" | tr '\n' ' ')"
(cd "$G" && nohup wine inj.exe Profiler.dll DOW2.exe -modname Elite -dev -nomovies -init us.lua > $O/game.log 2>&1 &)
p=""; for i in $(seq 1 60); do p=$(gpid); [ -n "$p" ] && break; sleep 2; done
[ -z "$p" ] && { echo "no game process"; exit 1; }
python3 /mnt/dow2bench/dev/beard_crash_1104/kick.py $p
touch $T/ready; rm -f $T/stop; t0=$(date +%s); n0=0; k=0
while [ ! -f $T/stop ] && [ $(( $(date +%s) - t0 )) -lt $(( MAXMIN * 60 )) ]; do
  sleep 1; [ -z "$(gpid)" ] && { echo "$(date +%T) game process GONE"; break; }
  n=$(grep -c " sel " "$E/UnitStats.log" 2>/dev/null); n=${n:-0}
  if [ "$n" -gt "$n0" ]; then
    sleep 2; k=$((k+1)); nm=$(grep " sel " "$E/UnitStats.log" | tail -1 | sed 's/.* sel \([^:]*\):.*/\1/; s/.*\\//')
    DISPLAY=:99 import -window root "$SH/$(printf %03d $k)_$nm.png"; echo "$(date +%T) shot $k $nm"; n0=$n
  fi
done
DISPLAY=:99 import -window root $O/last.png 2>/dev/null
echo "$(date +%T) $NAME done"
