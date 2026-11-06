#!/bin/bash
# wine_smoke.sh <dow2-ladder gameserver dir> <dir with Relay.dll> [secs] - two model players through the REAL Relay.dll under
# Wine (wine_smoke.c), as a native room of our lobby server: TCP connect to the node's battle port -> Relay.dll's UDP tunnel ->
# udp_gateway.py -> battle_relay.py, in the same two network namespaces as storm_test.py (netem 15 ms +- 3, 1 % loss).
# Root needed. Its own WINEPREFIX (default ~/.du_wine_smoke) - no stand is touched. Prints both players' lines, the Relay.log
# "udp:" lines and the gateway's close lines.
set -u
GS=$(readlink -f "$1"); DLLDIR=$(readlink -f "$2"); SECS=${3:-30}
WINE=${WINE:-/mnt/dow2bench/wine/wine-11.20-staging-amd64-wow64/bin/wine}
export WINEPREFIX=${WINEPREFIX:-$HOME/.du_wine_smoke} WINEDEBUG=-all
cd "$(dirname "$0")"; HERE=$(pwd); OUT=$(mktemp -d)
i686-w64-mingw32-gcc -O2 -o "$OUT/wine_smoke.exe" wine_smoke.c -lws2_32 || exit 1
[ -d "$WINEPREFIX" ] || "$WINE" wineboot -i >/dev/null 2>&1
for p in 1 2; do mkdir -p "$OUT/p$p"; cp "$DLLDIR/Relay.dll" "$OUT/wine_smoke.exe" "$OUT/p$p/"; printf '[Relay]\r\nNodes=10.77.0.1\r\n' > "$OUT/p$p/Relay.ini"; done
python3 -c "import sys, argparse; sys.path.insert(0, '$HERE'); import storm_test as s; s.net_up(argparse.Namespace(delay=15, jitter=3, loss=1))" || exit 1
ip netns exec du_gw env RELAY_PEERS= RELAY_LOG="$OUT/battle.jsonl" python3 "$GS/battle_relay.py" 47100 127.0.0.1 > "$OUT/battle.out" 2>&1 & B=$!
ip netns exec du_gw env UDPGW_TRUSTED= python3 "$GS/udp_gateway.py" 47100 127.0.0.1:47100 > "$OUT/gateway.out" 2>&1 & G=$!
sleep 1; SID=$((RANDOM * 7 + 1000))
(cd "$OUT/p1" && ip netns exec du_cl "$WINE" wine_smoke.exe 10.77.0.1 $SID 1 "$SECS" > "$OUT/p1.txt" 2>&1) & P1=$!
sleep 3
(cd "$OUT/p2" && ip netns exec du_cl "$WINE" wine_smoke.exe 10.77.0.1 $SID 2 $((SECS - 3)) > "$OUT/p2.txt" 2>&1) & P2=$!
wait $P1 $P2; sleep 2
kill $G $B 2>/dev/null; wait 2>/dev/null
"${WINE%/wine}/wineserver" -k 2>/dev/null
python3 -c "import sys; sys.path.insert(0, '$HERE'); import storm_test as s; s.net_down()"
echo "== out: $OUT"; cat "$OUT/p1.txt" "$OUT/p2.txt"
for p in 1 2; do grep -a "Relay 23 start\|relay:\|udp:" "$OUT/p$p/Relay.log" | grep -v "UDP ping\|does not answer\|answers a ping" ; done
grep "ev=close\|ev=storm\|ev=dead" "$OUT/gateway.out"; grep "ev=join\|session_end" "$OUT/battle.out"
