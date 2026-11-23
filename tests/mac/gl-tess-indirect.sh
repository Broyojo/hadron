#!/bin/sh
# Builds and runs gl-tess-indirect.c: a tessellation draw made with glDrawArraysIndirect against the
# same draw made with glDrawArrays, through Zink, with vertex and control shaders that hold N floats
# live (default 64). Mesa 0068 is why they agree on M1 and M2 (docs/findings.md #73).
# Usage: tests/mac/gl-tess-indirect.sh [N] [ICD json] [Zink prefix]
#   (defaults: 64, the installed KosmicKrisp, dist/mesa-zink)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/gl-tess-indirect"
MESA="${3:-$ROOT/dist/mesa-zink}"
mkdir -p "$OUT"
cc -O1 -Wall "$ROOT/tests/mac/gl-tess-indirect.c" -I"$MESA/include" -L"$MESA/lib" -lEGL \
    -Wl,-rpath,"$MESA/lib" -o "$OUT/gl-tess-indirect"
exec env VK_DRIVER_FILES="${2:-$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}" \
    MESA_KK_EXPERIMENTAL="${MESA_KK_EXPERIMENTAL:-dgc,xfb,sparse}" MESA_LOADER_DRIVER_OVERRIDE=zink \
    MESA_SHADER_CACHE_DISABLE=true perl -e 'alarm 120; exec @ARGV' "$OUT/gl-tess-indirect" "${1:-64}"
