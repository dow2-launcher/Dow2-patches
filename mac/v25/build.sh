#!/bin/bash
# build.sh - crossplay_fix.dylib v25 (with the random-generator and CRC traces) with osxcross on rts (x86_64 macOS)
set -e
cd "$(dirname "$0")"
R=root@rts.fintebtc.de; D=/tmp/dow2mac_v25build
SRC="crossplay_fix.c mathfix.c msmath.c rngtrace.c randfix.c heapfix.c terrainfix.c luanumfix.c pathfix.c projfix.c crctrace.c combatfix.c aimfix.c rangefix.c sumfix.c weaponfix.c terrhitfix.c meleefix.c knockfix.c walkfix.c sortfix.c gsortfix.c costfix.c fowcellfix.c homingfix.c holdfix.c recipfix.c protfix.c artfix.c cdownfix.c rowfix.c"
ssh $R "D=$D; rm -rf \"\${D:?}\" && mkdir -p \"\$D/lift\""
scp -q $SRC *.h *.inc $R:$D/
scp -q lift/*.h lift/lift_inv.c lift/lift_fi.c lift/lift_tr.c lift/lift_aim.c lift/lift_obb.c lift/lift_th.c $R:$D/lift/
ssh $R "cd $D && PATH=/mnt/ssd/opt/osx/osxcross/target/bin:\$PATH x86_64-apple-darwin23.5-clang -dynamiclib -O2 -fno-fast-math -ffp-contract=off \
  -mmacosx-version-min=10.9 -DRNGTRACE -o crossplay_fix.dylib $SRC -Wl,-undefined,dynamic_lookup 2>&1 | grep -v '^\$' | head -30"
scp -q $R:$D/crossplay_fix.dylib .
ssh $R "D=$D; rm -rf \"\${D:?}\""
ls -la crossplay_fix.dylib
