#!/usr/bin/env bash
# Build Hadron's app icon from the logo drawings in assets/logo.
#
# The icon is the "gluon H": an H whose crossbar is a gluon's coil. Below 128 pixels the coil
# closes up, so the small sizes use the same H with a plain bar (hadron-icon-small.svg).
#
# Usage: scripts/make-icon.sh [output.icns]   (default: build/package/Hadron.icns)
source "$(dirname "$0")/env.sh"

out="${1:-$BUILD/package/Hadron.icns}"
set="$(mktemp -d)/Hadron.iconset"
mkdir -p "$set" "$(dirname "$out")"
# iconutil's names: the size in points, and @2x for the same size at twice the pixels.
for spec in 16:icon_16x16 32:icon_16x16@2x 32:icon_32x32 64:icon_32x32@2x 128:icon_128x128 \
            256:icon_128x128@2x 256:icon_256x256 512:icon_256x256@2x 512:icon_512x512 1024:icon_512x512@2x; do
    px="${spec%%:*}" name="${spec#*:}"
    if (( px < 128 )); then svg="$ROOT/assets/logo/hadron-icon-small.svg"; else svg="$ROOT/assets/logo/hadron-icon.svg"; fi
    sips -s format png -Z "$px" "$svg" --out "$set/$name.png" >/dev/null
done
iconutil -c icns -o "$out" "$set"
rm -rf "$(dirname "$set")"
log "built $out"
