#!/bin/sh
# Build and run zink-test against a Mesa built with Zink (default: build/mesa-zink-install) on
# Hadron's KosmicKrisp. usage: tools/zink-test/run.sh [mesa prefix]
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MESA="${1:-$ROOT/build/mesa-zink-install}"
OUT="${TMPDIR:-/tmp}/zink-test"
cc -O1 -o "$OUT" "$ROOT/tools/zink-test/zink-test.c" -I"$MESA/include" -L"$MESA/lib" -lEGL -Wl,-rpath,"$MESA/lib" || exit 1
exec env VK_DRIVER_FILES="${VK_DRIVER_FILES:-$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}" \
    MESA_KK_EXPERIMENTAL="${MESA_KK_EXPERIMENTAL:-dgc,xfb,sparse}" \
    MESA_LOADER_DRIVER_OVERRIDE=zink LIBGL_DRIVERS_PATH="$MESA/lib/dri" \
    perl -e 'alarm 120; exec @ARGV' "$OUT"
