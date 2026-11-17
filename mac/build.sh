#!/bin/bash
# build.sh - the Mac add-ons (x86_64 dylibs for the native game, Feral 1.0.3): one dylib per add-on folder, into mac/out/.
#   macOS (GitHub Actions): CC=/opt/homebrew/opt/llvm@19/bin/clang mac/build.sh
#   osxcross:               CC=x86_64-apple-darwin23.5-clang TARGET="-mmacosx-version-min=10.9" mac/build.sh
# Each <addon>/<addon>.c -> out/<Name>.dylib (Name = its .ini name), the ini copied next to it, SHA256SUMS.txt.
set -e
cd "$(dirname "$0")"
CC="${CC:-clang}"
TARGET="${TARGET:--target x86_64-apple-macos10.9}"
SDK="${SDKROOT:-$(xcrun --show-sdk-path 2>/dev/null || true)}"
ADDONS="${ADDONS:-nohints reinforcefix herolabelfix}"
rm -rf out && mkdir -p out
for a in $ADDONS; do
  ini=$(ls "$a"/*.ini); name=$(basename "$ini" .ini)
  "$CC" $TARGET ${SDK:+-isysroot "$SDK"} -dynamiclib -O2 -Wall -Wno-unused-function -fno-stack-protector \
    -install_name "@rpath/$name.dylib" -o "out/$name.dylib" "$a/$a.c"
  cp "$ini" out/
done
(cd out && { shasum -a 256 ./*.dylib 2>/dev/null || sha256sum ./*.dylib; } | tee SHA256SUMS.txt)
