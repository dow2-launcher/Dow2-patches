#!/bin/bash
# build.sh - du_harness (the tunnel of Relay.dll 23, du_tunnel.h) and du_harness_old22 (Relay 22) for Linux, 32-bit like the DLL
set -e
cd "$(dirname "$0")"
[ -f old22_tunnel.h ] || bash mk_old22.sh
gcc -m32 -O2 -Wall -Wno-unused-function -Wno-format -o du_harness du_harness.c -lm
gcc -m32 -O2 -Wall -Wno-unused-function -Wno-format -DOLD22 -o du_harness_old22 du_harness.c -lm
echo built
