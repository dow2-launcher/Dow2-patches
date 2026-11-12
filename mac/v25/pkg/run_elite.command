#!/bin/bash
# Dawn of War II: Retribution, native Mac, Elite mod (-modname Elite), with crossplay_fix v19 (joins Windows players' lobbies; v7: Windows math; v8: Windows random floats; v9: Windows pathfinder heap, terrain height, Lua numbers; v10: Windows rounding of unit turns and the cell line walk; v11: projectiles and entity facings; v12: collision queries and meshes as SimEngine.dll, doCollision, CRC trace; v13: weapons aim / scatter / hit point, range checks, more sums in Windows order; v14: weapon burst / reload counters as Windows; v15: where shells hit the terrain as Windows; v16: melee special attack chance, accuracy power, sync-kill transform as Windows; v17: knockback fly target and durations as Windows; v18: walkable-surface height as SimEngine.dll; v19: weapon target choice among equal priorities as Windows).
# Steam must be running and signed in. Elite must be installed first (Elite Launcher for macOS).   ./run_elite.command nomath  - the same without the math part (for comparison).
MODE=elite
SRC="$(cd "$(dirname "$0")" && pwd)"; HOME_DIR="$HOME/dow2mac"
# always work from ~/dow2mac, wherever the archive was unpacked
if [ "$SRC" != "$HOME_DIR" ]; then
  mkdir -p "$HOME_DIR" && cp -f "$SRC"/crossplay_fix.dylib "$SRC"/*.command "$SRC"/README*.txt "$HOME_DIR"/ && chmod +x "$HOME_DIR"/*.command
  exec "$HOME_DIR/$(basename "$0")" "$@"
fi
DIR="$HOME_DIR"
GAME="$HOME/Library/Application Support/Steam/steamapps/common/Dawn of War II - Retribution"
[ -d "$GAME" ] || { echo "Game not found in $GAME - set GAME at the top of this script"; read -r; exit 1; }
APP=$(find "$GAME" -maxdepth 2 -name "*.app" -type d | head -1)
[ -n "$APP" ] || { echo "Dawn of War II app not found in $GAME"; read -r; exit 1; }
EXTRA=()
if [ "$MODE" = elite ]; then
  DATA=$(dirname "$(find "$GAME" -maxdepth 4 -iname "DOW2.module" | head -1)")
  [ -d "$DATA" ] || { echo "game data folder not found"; read -r; exit 1; }
  if [ ! -f "$DATA/Elite.module" ]; then
    echo "Elite is not installed in $DATA - install it with the Elite Launcher first (https://rts.fintebtc.de/elite/), then run this again."; read -r; exit 1
  fi
  EXTRA=(-modname Elite)
fi
echo "game app: $APP   mode: $MODE"
unset DOW2_NOMATHFIX
[ "$1" = nomath ] && { export DOW2_NOMATHFIX=1; shift; echo "math part OFF (comparison run)"; }
xattr -d com.apple.quarantine "$DIR/crossplay_fix.dylib" 2>/dev/null; codesign -f -s - "$DIR/crossplay_fix.dylib" 2>/dev/null
rm -f /tmp/elitefix.log /tmp/dow2_net.log /tmp/dow2_rng.log /tmp/dow2_crc.log
STAMP=$(date +%Y%m%d_%H%M%S); touch "/tmp/dow2_start_$STAMP"
export SteamAppId=56400 SteamGameId=56400
# without steam_appid.txt the game restarts itself through Steam, and the restarted copy loses DYLD_INSERT_LIBRARIES (the patch)
echo 56400 > "$APP/Contents/MacOS/steam_appid.txt"
cd "$APP/Contents/MacOS"
( sleep 25; echo "--- patch log after start:"; cat /tmp/elitefix.log 2>/dev/null || echo "NO PATCH LOG - the patch did not load" ) &
DYLD_INSERT_LIBRARIES="$DIR/crossplay_fix.dylib" "./Dawn of War II - Retribution" "${EXTRA[@]}" "$@"
# everything to send back goes to ~/dow2mac/logs_<date>
OUT="$DIR/logs_${MODE}_$STAMP"; mkdir -p "$OUT"
cp /tmp/elitefix.log /tmp/dow2_net.log "$OUT"/ 2>/dev/null
gzip -c /tmp/dow2_rng.log > "$OUT/dow2_rng.log.gz" 2>/dev/null
gzip -c /tmp/dow2_crc.log > "$OUT/dow2_crc.log.gz" 2>/dev/null
find "$HOME/Library/Application Support/Feral Interactive" -iname "*syncerror*" -newer "/tmp/dow2_start_$STAMP" -exec cp {} "$OUT"/ \; 2>/dev/null
rm -f "/tmp/dow2_start_$STAMP"
echo "--- patch log:"; cat /tmp/elitefix.log 2>/dev/null
echo "--- logs to send: $OUT"; ls -la "$OUT"
( cd "$DIR" && zip -qr "logs_${MODE}_$STAMP.zip" "logs_${MODE}_$STAMP" ) && echo "--- the same as one file: $DIR/logs_${MODE}_$STAMP.zip"
