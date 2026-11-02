#!/bin/bash
# build.sh - crossplay_fix.dylib v8 (with the random-generator trace) with osxcross on rts (x86_64 macOS)
set -e
cd "$(dirname "$0")"
R=root@rts.fintebtc.de; D=/tmp/dow2mac_v8build
ssh $R "rm -rf $D && mkdir -p $D"
scp -q crossplay_fix.c mathfix.c mathfix_tables.h msmath.c rngtrace.c randfix.c win24.h $R:$D/
ssh $R "cd $D && PATH=/mnt/ssd/opt/osx/osxcross/target/bin:\$PATH x86_64-apple-darwin23.5-clang -dynamiclib -O2 -fno-fast-math -ffp-contract=off \
  -mmacosx-version-min=10.9 -DRNGTRACE -o crossplay_fix.dylib crossplay_fix.c mathfix.c msmath.c rngtrace.c randfix.c -Wl,-undefined,dynamic_lookup 2>&1 | grep -v '^\$' | head -20"
scp -q $R:$D/crossplay_fix.dylib .
ssh $R "rm -rf $D"
ls -la crossplay_fix.dylib
