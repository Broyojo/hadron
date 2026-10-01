#!/bin/sh
# Build and run minmax-test against KosmicKrisp (dist/mesa, or VK_DRIVER_FILES).
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/tools/minmax-test"
mkdir -p "$OUT"
cd "$(dirname "$0")"
glslangValidator -V --vn minmax_test_cs -o "$OUT/minmax-test-cs.h" minmax-test.comp >/dev/null
clang -O1 -g -I/opt/homebrew/include -I"$OUT" -o "$OUT/minmax-test" minmax-test.c -L/opt/homebrew/lib -lvulkan
VK_DRIVER_FILES="${VK_DRIVER_FILES:-$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}" \
    MESA_KK_EXPERIMENTAL=sparse "$OUT/minmax-test"
