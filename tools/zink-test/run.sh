#!/bin/sh
# Build and run zink-test against a Mesa built with Zink (default: dist/mesa-zink) on
# Hadron's KosmicKrisp. usage: tools/zink-test/run.sh [mesa prefix] [window [seconds]]
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MESA="${1:-$ROOT/dist/mesa-zink}"
[ -n "$1" ] || MESA="$ROOT/dist/mesa-zink"
OUT="${TMPDIR:-/tmp}/zink-test"
if [ "$2" = window ]; then
    # In a window, for a few seconds: tools/zink-test/run.sh "" window [seconds]
    OUT="${TMPDIR:-/tmp}/zink-window"
    cc -O1 -fobjc-arc -o "$OUT" "$ROOT/tools/zink-test/zink-window.m" -I"$MESA/include" -L"$MESA/lib" -lEGL \
        -framework Cocoa -framework QuartzCore -Wl,-rpath,"$MESA/lib" || exit 1
    set -- "$OUT" ${3:+"$3"}
else
    cc -O1 -o "$OUT" "$ROOT/tools/zink-test/zink-test.c" -I"$MESA/include" -L"$MESA/lib" -lEGL -Wl,-rpath,"$MESA/lib" || exit 1
    set -- "$OUT"
fi
exec env VK_DRIVER_FILES="${VK_DRIVER_FILES:-$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}" \
    MESA_KK_EXPERIMENTAL="${MESA_KK_EXPERIMENTAL:-dgc,xfb,sparse}" \
    MESA_LOADER_DRIVER_OVERRIDE=zink LIBGL_DRIVERS_PATH="$MESA/lib/dri" \
    perl -e 'alarm 120; exec @ARGV' "$@"
