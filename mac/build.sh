#!/bin/bash
# build.sh - crossplay_fix.dylib (x86_64 macOS) + the player zip. Used by .github/workflows/mac.yml (macOS runner, clang 19).
#   CC=/opt/homebrew/opt/llvm@19/bin/clang ./build.sh   -> out/crossplay_fix.dylib, out/dow2mac_crossplay_<ver>.zip
set -e
cd "$(dirname "$0")"
VER="${VER:-v26-vanilla}"
CC="${CC:-clang}"
SDK="${SDKROOT:-$(xcrun --show-sdk-path 2>/dev/null || true)}"
SRC="crossplay_fix.c mathfix.c msmath.c randfix.c heapfix.c terrainfix.c luanumfix.c pathfix.c projfix.c combatfix.c aimfix.c rangefix.c sumfix.c weaponfix.c terrhitfix.c meleefix.c knockfix.c walkfix.c sortfix.c gsortfix.c costfix.c fowcellfix.c homingfix.c holdfix.c recipfix.c protfix.c artfix.c cdownfix.c rowfix.c"
rm -rf out && mkdir -p out/dow2mac
"$CC" -target x86_64-apple-macos10.9 ${SDK:+-isysroot "$SDK"} -dynamiclib -O2 -fno-fast-math -ffp-contract=off \
  -o out/dow2mac/crossplay_fix.dylib $SRC -Wl,-undefined,dynamic_lookup
cp run_vanilla.command out/dow2mac/ && chmod +x out/dow2mac/run_vanilla.command
cp README.md out/dow2mac/README.txt && cp ../LICENSE out/dow2mac/LICENSE.txt
(cd out && zip -qrX "dow2mac_crossplay_${VER}.zip" dow2mac && cp dow2mac/crossplay_fix.dylib . \
  && shasum -a 256 crossplay_fix.dylib "dow2mac_crossplay_${VER}.zip" | tee SHA256SUMS.txt)
