#!/bin/bash
# Dawn of War II: Retribution (Steam, native Mac version, vanilla) with crossplay_fix.dylib: play with Windows players.
# Steam must be running and signed in. Extra arguments go to the game (e.g. ./run_vanilla.command -sync_high).
DIR="$(cd "$(dirname "$0")" && pwd)"
GAME="${GAME:-$HOME/Library/Application Support/Steam/steamapps/common/Dawn of War II - Retribution}"
[ -d "$GAME" ] || { echo "Game not found in $GAME - run with GAME=/path/to/the/game/folder"; read -r; exit 1; }
APP=$(find "$GAME" -maxdepth 2 -name "*.app" -type d | head -1)
[ -n "$APP" ] || { echo "Dawn of War II app not found in $GAME"; read -r; exit 1; }
echo "game app: $APP"
# a downloaded dylib is quarantined and unsigned: macOS would refuse to load it
xattr -d com.apple.quarantine "$DIR/crossplay_fix.dylib" 2>/dev/null; codesign -f -s - "$DIR/crossplay_fix.dylib" 2>/dev/null
rm -f /tmp/crossplay_fix.log
export SteamAppId=56400 SteamGameId=56400
# without steam_appid.txt the game restarts itself through Steam, and the restarted copy loses DYLD_INSERT_LIBRARIES (the patch)
echo 56400 > "$APP/Contents/MacOS/steam_appid.txt"
cd "$APP/Contents/MacOS"
( sleep 25; echo "--- patch log after start:"; cat /tmp/crossplay_fix.log 2>/dev/null || echo "NO PATCH LOG - the patch did not load" ) &
DYLD_INSERT_LIBRARIES="$DIR/crossplay_fix.dylib" "./Dawn of War II - Retribution" "$@"
echo "--- patch log:"; cat /tmp/crossplay_fix.log 2>/dev/null
