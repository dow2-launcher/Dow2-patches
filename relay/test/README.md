# relay/test - the UDP tunnel of Relay.dll on Linux, against the real udp_gateway.py / battle_relay.py

`du_harness.c` builds the tunnel of Relay.dll (`relay/du_tunnel.h`, the very code of the DLL, 32-bit) as a Linux program:
it takes one "game" TCP connection on 127.0.0.1:<port>, does the DU handshake with a gateway and carries the bytes.
`du_harness_old22` is the same with the delivery of Relay 22 (cut out of git a1e0075 by `mk_old22.sh`), to compare.

    bash build.sh                                   # gcc -m32 (gcc-multilib), both programs
    sudo python3 storm_test.py --ladder <dow2-ladder>/gameserver --client new --players 4 --loss 1 --secs 40 --out /tmp/x
    sudo bash run_matrix.sh <new gameserver dir> <old gameserver dir> /tmp/matrix 40      # every scenario of the report

`storm_test.py` makes two network namespaces (`du_gw` 10.77.0.1 with udp_gateway.py + battle_relay.py, `du_cl` 10.77.0.2 with
the clients and the game model) joined by a veth pair with `tc netem` (delay / jitter / loss), runs a model of N players
(hello, turns every 100 ms, the server's ticks, the game's reliable peer messages as RLink ProxyMessages 0x10 with the
game's retry / forced ack habits, optionally a flood of small messages) and prints one JSON line: per player the game's
counters (misdelivered = a proxy message for another peer, the game's "dropping PROXY message ... (we're not host)";
turn latency p50/p90/p99/max = own turn -> the tick that carries it), per client the "over" line of the tunnel, per
gateway connection its close line, and the storm / dead / addr_limit events. A fault proxy (`--fault`) can sit between
clients and gateway: `eternal_seq` (one client packet lost forever, copies included), `ack_blackhole` (no ACK / ACK2 from
the gateway after 8 s), `down_silence` (nothing from the gateway after 8 s).
The netns are removed at the end (also on errors); nothing outside them is touched. Needs root, iproute2, tc.
