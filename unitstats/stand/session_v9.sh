#!/bin/bash
# session_v9.sh <name> - stand 1 (cherpekat, :99): UnitStats 0.9 under Elite (-modname Elite -dev -init us.lua; Tyranids vs IG,
# AI off; unitstats_v9.scar: as v8 + the hero (two regeneration items) and a Chimera with cargo - one
# squad selected at a time; model HP logged every 2 s). A screenshot 2.5 s after every "SELECT" line of EliteAI.log (full
# 1280x800 + the panel x2 in $SH). Elite_DLLs = Elite's own four + EliteAI.dll (+ the stand SCAR appended to elite.scar) +
# UnitStats.dll (no ini: the defaults, as the launcher ships it; INI=<file> to give one). Holds the stand until $T/stop, "US9
# DONE" or $MAXMIN min; the trap stops the game and puts Elite_DLLs / Elite.launchconfig back. Only through stand_lock.sh.
set -u
T=${T:-/mnt/dow2bench/dev/unitstats09_1126/stand}; NAME=${1:-run}; O=$T/out/$NAME; mkdir -p $O; . /mnt/dow2bench/stand/env1.sh
SH=${SH:-/var/www/media/dow2_shots/unitstats/v9/raw/$NAME}; mkdir -p "$SH"
export WINEDLLOVERRIDES="mscoree,mshtml=;winmm=b"            # the stand's own dow2-patches loader off: only Elite_DLLs
G="$GAMEDIR"; E="$G/Elite_DLLs"; BK="$G/Elite_DLLs.bk_unitstats"
DLL=${DLL:?UnitStats.dll to test}; SCAR=${SCAR:-$T/unitstats_v9.scar}; INIT=${INIT:-$T/init_v3.lua.in}
RACE=${RACE:-tyranid}; HERO=${HERO:-0}; ENEMYR=${ENEMYR:-imperial_guard}; EHERO=${EHERO:-1}; MAXMIN=${MAXMIN:-6}
[ -n "$GAMEDIR" ] && [ -d "$E" ] || { echo "no stand dir"; exit 1; }
[ -e "$BK" ] && { echo "$BK exists - previous session not restored"; exit 1; }
LC="$G/Elite.launchconfig"; [ -e "$LC.bk_unitstats" ] && { echo "$LC.bk_unitstats exists - previous session not restored"; exit 1; }
gpid() { for p in $(pgrep -f "DOW2.exe"); do tr "\0" " " < /proc/$p/cmdline 2>/dev/null | grep -q "^DOW2.exe" || continue; tr "\0" "\n" < /proc/$p/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && { echo $p; return; }; done; }
[ -n "$(gpid)" ] && { echo "stand 1 game already running"; exit 1; }
stopgame() { local p; for i in 1 2 3; do p=$(gpid); [ -z "$p" ] && return 0; kill $p; sleep 3; done; p=$(gpid); [ -n "$p" ] && kill -9 $p; sleep 2; }
restore() {
  stopgame
  for q in $(pgrep -f BsSndRpt); do tr '\0' '\n' < /proc/$q/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && kill $q; done
  for f in "$E"/*.log "$E"/*.ini; do [ -f "$f" ] && cp "$f" "$O/"; done
  cp "$WINEPREFIX/drive_c/users/root/Documents/My Games/Dawn of War II - Retribution/Logfiles/warnings.txt" $O/warnings.txt 2>/dev/null
  rm -rf "${E:?}"; mv "$BK" "$E"; mv -f "$LC.bk_unitstats" "$LC"; rm -f "$G/us.lua" "$T/ready" "$T/stop"
  echo "$(date +%T) stand 1 restored"
}
cp -p "$LC" "$LC.bk_unitstats" || exit 1
mv "$E" "$BK" || exit 1
trap restore EXIT
# Elite 3.2.1 (Profiler / DOW2Injector 1.9) loads only what Elite.launchconfig InjectedFiles names - as the launcher writes it
perl -pi -e 's/^InjectedFiles=.*?(\r?)$/InjectedFiles=CullspherePatch.dll, DoW2_ArmyPainterFix.dll, MiscFixes.dll, ShellPatch.dll, EliteAI.dll, UnitStats.dll$1/' "$LC"
mkdir "$E"; for f in CullspherePatch.dll DoW2_ArmyPainterFix.dll MiscFixes.dll ShellPatch.dll EliteAI.dll; do cp -p "$BK/$f" "$E/"; done
cp -a "$BK/EliteAI" "$E/"; printf '[EliteAI]\r\nVerbose=0\r\n[Lua]\r\nNewAI=normal\r\n' > "$E/EliteAI.ini"
cat "$SCAR" >> "$E/EliteAI/append/scar/elite/elite.scar"
cp "$DLL" "$E/UnitStats.dll" || exit 1; [ -n "${INI:-}" ] && cp "$INI" "$E/UnitStats.ini"
sed -e "s/@RACE@/$RACE/g" -e "s/@HERO@/$HERO/" -e "s/@EHERO@/$EHERO/" -e "s/@ENEMY@/$ENEMYR/" -e "s/@WIN@//g" "$INIT" > "$G/us.lua"
echo "=== $(date +%T) $NAME: $(ls "$E" | tr '\n' ' ')"
(cd "$G" && nohup wine inj.exe Profiler.dll DOW2.exe -modname Elite -dev -nomovies -init us.lua > $O/game.log 2>&1 &)
p=""; for i in $(seq 1 60); do p=$(gpid); [ -n "$p" ] && break; sleep 2; done
[ -z "$p" ] && { echo "no game process"; exit 1; }
python3 /mnt/dow2bench/dev/injector19_1105/kick19.py $p
touch $T/ready; rm -f $T/stop; t0=$(date +%s); n0=0; k=0
shot() {   # full screen + the selection panel (bottom right) x2
  DISPLAY=:99 import -window root "$SH/$1.png" && convert "$SH/$1.png" -crop 500x210+780+590 +repage -filter Lanczos -resize 200% "$SH/zoom_$1.png"
}
while [ ! -f $T/stop ] && [ $(( $(date +%s) - t0 )) -lt $(( MAXMIN * 60 )) ]; do
  sleep 1; [ -z "$(gpid)" ] && { echo "$(date +%T) game process GONE"; break; }
  n=$(grep -c "SELECT" "$E/EliteAI.log" 2>/dev/null); n=${n:-0}
  if [ "$n" -gt "$n0" ]; then
    sleep 2.5; k=$((k+1)); nm=$(grep "SELECT" "$E/EliteAI.log" | tail -1 | sed 's/.*SELECT[ \t]*//; s/[ \t]*\(true\|false\)$//; s/[^A-Za-z0-9]\+/_/g; s/_$//' | cut -c1-60)
    shot "$(printf %02d $k)_$nm"; echo "$(date +%T) shot $k $nm"; n0=$n
  fi
  grep -q "US9 DONE" "$E/EliteAI.log" 2>/dev/null && { sleep 3; echo "$(date +%T) scenario done"; break; }
done
DISPLAY=:99 import -window root $O/last.png 2>/dev/null
echo "$(date +%T) $NAME done"
