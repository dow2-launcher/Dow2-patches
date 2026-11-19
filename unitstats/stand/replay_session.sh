#!/bin/bash
# replay_session.sh <name> (stand copy: /mnt/dow2bench/dev/usreplay_1119/session.sh, driver drive.sh = replay_drive.sh) - UnitStats replay test on stand 1 (copy of replayspeed/e_session.sh, 19.11.2026).
# env: MOD=elite|vanilla, REC=<path of a .rec>, DLLS="file ..." (from $T/dll), SYNC=1, MAXMIN, INI=<UnitStats.ini to use>.
# Driver: lines appended to $T/cmd are run one by one (shot NAME / move X Y / click X Y / sleep N / quit).
# Only through stand_lock.sh. The trap stops the game by PID and restores everything (md5 checked).
set -u
T=/mnt/dow2bench/dev/usreplay_1119; NAME=${1:-run}; O=$T/out/$NAME; mkdir -p $O
STAND=1; . /mnt/dow2bench/stand/env$STAND.sh; unset DXVK_CONFIG DXVK_CONFIG_FILE DXVK_FRAME_RATE
G="$GAMEDIR"; E="$G/Elite_DLLs"; P="$G/dow2-patches"; BK=$T/bak$STAND
DLLS=${DLLS:-}; REC=${REC:?}; MAXMIN=${MAXMIN:-15}; MOD=${MOD:-elite}; PM="$WINEPREFIX/drive_c/perfmeter.bin"
RN=$(basename "$REC" .rec)
CH="Profiler.dll"
gpid() { for p in $(pgrep -f "DOW2.exe"); do grep -q "Retribution/DOW2.exe" /proc/$p/maps 2>/dev/null || continue; tr "\0" "\n" < /proc/$p/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && { echo $p; return; }; done; }
[ -n "$(gpid)" ] && { echo "stand $STAND game already running"; exit 1; }
[ -e "$BK" ] && { echo "$BK exists - previous session not restored"; exit 1; }
snap() { ( cd "$G" && { find Elite_DLLs dow2-patches -type f -print0 | sort -z | xargs -0 md5sum; md5sum winmm.dll $CH; } ) > "$1" 2>/dev/null; }
snap $O/md5_before.txt; mkdir -p "$BK"; cp -a "$E" "$BK/Elite_DLLs"; cp -a "$P" "$BK/dow2-patches"; cp -p "$G/winmm.dll" "$BK/"
restore() {
  for i in 1 2 3; do p=$(gpid); [ -z "$p" ] && break; kill $p; sleep 3; done; p=$(gpid); [ -n "$p" ] && kill -9 $p; sleep 2
  for q in $(pgrep -f BsSndRpt); do tr '\0' '\n' < /proc/$q/environ 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" && kill $q; done
  [ -n "${DRV:-}" ] && kill $DRV 2>/dev/null
  cp "$E"/*.log "$P"/*.log $O/ 2>/dev/null; cp "$WINEPREFIX/drive_c/crctrace.log" $O/ 2>/dev/null
  for u in root dowbench; do W="$WINEPREFIX/drive_c/users/$u/Documents/My Games/Dawn of War II - Retribution/Logfiles/warnings.txt"; [ -f "$W" ] && [ "$W" -nt $O/md5_before.txt ] && cp "$W" $O/warnings.txt; done
  rm -rf "${E:?}" "${P:?}"; cp -a "$BK/Elite_DLLs" "$E"; cp -a "$BK/dow2-patches" "$P"; cp -p "$BK/winmm.dll" "$G/"
  [ -f "$BK/recs" ] && while read -r r; do rm -f "${r:?}"; done < "$BK/recs"; rm -rf "${BK:?}"; rm -f "$G/us.lua" "$T/stop$STAND"
  snap $O/md5_after.txt; cmp -s $O/md5_before.txt $O/md5_after.txt && echo "$(date +%T) stand $STAND restored (md5 ok)" || echo "$(date +%T) RESTORE MD5 DIFFER"
}
trap restore EXIT
rm -f "$PM" "$WINEPREFIX/drive_c/crctrace.log" "$WINEPREFIX/drive_c/crctrace_status.txt"
for PBX in "$WINEPREFIX/drive_c/users/root/Documents/My Games/Dawn of War II - Retribution/Playback" "$WINEPREFIX/drive_c/users/dowbench/Documents/My Games/Dawn of War II - Retribution/Playback"; do
  [ -d "$PBX" ] || continue; [ -f "$PBX/$RN.rec" ] || { cp "$REC" "$PBX/" && echo "$PBX/$RN.rec" >> "$BK/recs"; }; done
if [ "$MOD" = vanilla ]; then
  export WINEDLLOVERRIDES="mscoree,mshtml=;winmm=n,b"; D="$P"
  rm -rf "${P:?}"; mkdir -p "$P"; cp /mnt/dow2bench/dev/vanilla_pack_test_1104/winmm.dll "$G/winmm.dll"
  rm -rf "${E:?}"; mkdir "$E"
  MODARG=""; LAUNCH="wine DOW2.exe"
else
  export WINEDLLOVERRIDES="mscoree,mshtml=;winmm=b"; D="$E"
  rm -rf "${E:?}"; mkdir "$E"; for f in CullspherePatch.dll DoW2_ArmyPainterFix.dll MiscFixes.dll ShellPatch.dll; do cp -p "$BK/Elite_DLLs/$f" "$E/"; done
  MODARG="-modname Elite"; LAUNCH="wine inj.exe Profiler.dll DOW2.exe"
fi
for f in $DLLS; do cp "$T/dll/$f" "$D/" || exit 1; done
[ -n "${INI:-}" ] && cp "$INI" "$D/UnitStats.ini"
printf -- '-- unitstats replay test (stand only)\r\nfunction SCRIPT_Before()\r\n\tSetGamePlayback("playback:%s.rec")\r\n\treturn 1\r\nend\r\nfunction SCRIPT_Connect() return 1 end\r\nfunction SCRIPT_GameStart() return 1 end\r\n' "$RN" > "$G/us.lua"
EXTRA=""; [ "${SYNC:-1}" = 1 ] && EXTRA="$EXTRA -sync_high"
echo "=== $(date +%T) $NAME stand $STAND $MOD: $RN; $(ls "$D" | tr '\n' ' ')"
(cd "$G" && nohup $LAUNCH $MODARG -dev -nomovies -init us.lua $EXTRA > $O/game.log 2>&1 &)
p=""; for i in $(seq 1 60); do p=$(gpid); [ -n "$p" ] && break; sleep 2; done
[ -z "$p" ] && { echo "no game process"; exit 1; }
echo "pid $p"
if [ "$MOD" != vanilla ]; then for k in $(seq 1 20); do r=$(python3 /mnt/dow2bench/dev/obs_1107/rt/kick2.py $p "$G/Profiler.dll"); echo "$r"; echo "$r" | grep -q "flag 01" && break; sleep 1; done; fi
O=$O bash $T/drive.sh >> $O/drive.log 2>&1 & DRV=$!
t0=$(date +%s)
while [ ! -f $T/stop$STAND ] && [ $(( $(date +%s) - t0 )) -lt $(( MAXMIN * 60 )) ]; do
  sleep 3; [ -z "$(gpid)" ] && { echo "$(date +%T) game process GONE"; break; }
done
DISPLAY=$DISPLAY import -window root $O/last.png 2>/dev/null
echo "$(date +%T) $NAME done after $(( $(date +%s) - t0 )) s"
