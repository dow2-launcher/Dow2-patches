#!/bin/bash
# build_ci.sh - crossplay_fix.dylib (x86_64 macOS) + player zip, locally or in GitHub Actions.
# Same compiler family and flags as build.sh (osxcross on rts = clang 19): CC defaults to clang.
#   CC=/opt/homebrew/opt/llvm@19/bin/clang ./build_ci.sh   -> out/crossplay_fix.dylib, out/dow2mac_crossplay_<ver>.zip
set -e
cd "$(dirname "$0")"
VER="$(basename "$PWD")"
CC="${CC:-clang}"
SDK="${SDKROOT:-$(xcrun --show-sdk-path 2>/dev/null || true)}"
SRC="crossplay_fix.c mathfix.c msmath.c rngtrace.c randfix.c heapfix.c terrainfix.c luanumfix.c pathfix.c projfix.c crctrace.c combatfix.c aimfix.c rangefix.c sumfix.c weaponfix.c terrhitfix.c meleefix.c knockfix.c walkfix.c sortfix.c gsortfix.c costfix.c fowcellfix.c homingfix.c holdfix.c recipfix.c protfix.c artfix.c cdownfix.c rowfix.c"
rm -rf out && mkdir -p out/dow2mac
"$CC" -target x86_64-apple-macos10.9 ${SDK:+-isysroot "$SDK"} -dynamiclib -O2 -fno-fast-math -ffp-contract=off \
  -DRNGTRACE -o out/dow2mac/crossplay_fix.dylib $SRC -Wl,-undefined,dynamic_lookup
cp README_RU.txt pkg/run_elite.command pkg/run_vanilla.command out/dow2mac/
chmod +x out/dow2mac/*.command
(cd out && zip -qr "dow2mac_crossplay_${VER}.zip" dow2mac && cp dow2mac/crossplay_fix.dylib . && shasum -a 256 crossplay_fix.dylib "dow2mac_crossplay_${VER}.zip" | tee SHA256SUMS.txt)
