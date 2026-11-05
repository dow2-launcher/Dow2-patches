#!/bin/bash
# run_matrix.sh NEW_GAMESERVER_DIR OLD_GAMESERVER_DIR OUT_DIR [secs] - every scenario of the report (relay_udp_storm.md), one after
# the other (one netns pair at a time). NEW = dow2-ladder gameserver/ of the fix branch, OLD = of main before it.
set -u
cd "$(dirname "$0")"
NEW=$1; OLD=$2; OUT=$3; SECS=${4:-40}
mkdir -p "$OUT"
run() { local name=$1; shift; echo "== $name"; python3 storm_test.py --secs "$SECS" --out "$OUT/$name" "$@" > "$OUT/$name.json" 2> "$OUT/$name.err" || echo "  failed: $(tail -2 "$OUT/$name.err")"; }
# 1. the 4-player game as on 05.11 (old relay, old gateway, Relay 22) vs everything new; netem 15 ms +- 3, 1 % loss
run p4_old_all      --players 4 --loss 1 --ladder "$OLD" --client old22
run p4_new_all      --players 4 --loss 1 --ladder "$NEW" --client new
run p6_old_all      --players 6 --loss 1 --ladder "$OLD" --client old22
run p6_new_all      --players 6 --loss 1 --ladder "$NEW" --client new
# 2. which half does what: old relay with the new tunnel, new relay with the old tunnel
run p4_oldrelay_newtun --players 4 --loss 1 --ladder "$NEW" --relay-dir "$OLD" --client new
run p4_newrelay_oldtun --players 4 --loss 1 --ladder "$OLD" --relay-dir "$NEW" --client old22
# 3. the game floods 650 small messages/s per player (as Fov's game did): the tunnels
run flood_oldtun    --players 4 --loss 1 --flood 650 --ladder "$OLD" --relay-dir "$NEW" --client old22
run flood_newtun    --players 4 --loss 1 --flood 650 --ladder "$NEW" --client new
# 3b. the per-address limit of the gateway (rule 7): the same flood, the 4 players behind one address, limit 600 packets/s (deliberately below their ~1000/s)
run flood_addrlimit --players 4 --loss 1 --flood 650 --ladder "$NEW" --client new --addr-rate 600
# 4. a bad line: 5 % loss, 40 ms +- 10
run bad_oldtun      --players 4 --loss 5 --delay 40 --jitter 10 --ladder "$OLD" --relay-dir "$NEW" --client old22
run bad_newtun      --players 4 --loss 5 --delay 40 --jitter 10 --ladder "$NEW" --client new
# 5. faults: a client packet lost forever, all acks from the gateway lost, the gateway silent (after 8 s)
for f in eternal_seq ack_blackhole down_silence; do
  run fault_${f}_old --players 2 --fault $f --ladder "$OLD" --relay-dir "$NEW" --client old22
  run fault_${f}_new --players 2 --fault $f --ladder "$NEW" --client new
done
# 6. compatibility: new client + old gateway, old client + new gateway, new client + gateway without copies (UDPGW_VERSION=1)
run compat_newcl_oldgw --players 4 --loss 1 --ladder "$OLD" --relay-dir "$NEW" --client new
run compat_oldcl_newgw --players 4 --loss 1 --ladder "$NEW" --client old22
run compat_newcl_gwv1  --players 4 --loss 1 --ladder "$NEW" --client new --gwver 1
echo done
