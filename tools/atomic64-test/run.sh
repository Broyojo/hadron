#!/bin/sh
# Build and run atomic64-test against KosmicKrisp (dist/mesa, or VK_DRIVER_FILES).
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/tools/atomic64-test"
mkdir -p "$OUT"
cd "$(dirname "$0")"
glslangValidator -V --vn atomic64_test_cs -o "$OUT/atomic64-test-cs.h" atomic64-test.comp >/dev/null
glslangValidator -V --vn atomic64_image_cs -o "$OUT/atomic64-image-cs.h" atomic64-image.comp >/dev/null
clang -O1 -g -I/opt/homebrew/include -I"$OUT" -o "$OUT/atomic64-test" atomic64-test.c -L/opt/homebrew/lib -lvulkan
# stopped after two minutes: a lock that never frees would otherwise keep the GPU busy
VK_DRIVER_FILES="${VK_DRIVER_FILES:-$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}" \
    perl -e 'alarm 120; exec @ARGV' "$OUT/atomic64-test"
