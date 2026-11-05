#!/bin/bash
# mk_old22.sh - cuts the UDP delivery of Relay 22 (git a1e0075, relay/relay.c) into old22_tunnel.h for du_harness -DOLD22,
# so the tests can run the old client against a new gateway (and show the old behaviour). Needs the repo's git.
set -e
cd "$(dirname "$0")"
git show a1e0075:relay/relay.c > /tmp/old22_relay.$$.c
awk '/^enum \{ U_SYN = 1/{p=1} /^static volatile LONG g_tunSeed;/{p=0} p' /tmp/old22_relay.$$.c > old22_tunnel.h
grep '^static void u_hdr' /tmp/old22_relay.$$.c >> old22_tunnel.h
awk '/^\/\/ copies of the newest unacknowledged packets below number/{p=1} /^static int tunnel_port\(/{p=0} p' /tmp/old22_relay.$$.c >> old22_tunnel.h
sed -i 's/select(0, /select(FD_SETSIZE, /' old22_tunnel.h
rm -f /tmp/old22_relay.$$.c
echo "old22_tunnel.h: $(wc -l < old22_tunnel.h) lines"
