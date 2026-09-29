#!/usr/bin/env bash
# Build hadron-steam.exe, the stand-in for Steam's Windows process (native arm64).
source "$(dirname "$0")/env.sh"
out="$DIST/lib/wine/aarch64-windows/hadron-steam.exe"
"$ROOT/toolchains/llvm-mingw/bin/aarch64-w64-mingw32-clang" -O2 -municode -o "$out" \
    "$ROOT/launcher/hadron-steam.c" -ladvapi32
log "built $out"
