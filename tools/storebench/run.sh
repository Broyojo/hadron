#!/bin/sh
# Build and run storebench against KosmicKrisp (dist/mesa, or VK_DRIVER_FILES).
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/tools/storebench"
mkdir -p "$OUT"
cd "$(dirname "$0")"
glslangValidator -V --vn storebench_cs -o "$OUT/storebench-cs.h" storebench.comp >/dev/null
clang -O1 -g -I/opt/homebrew/include -I"$OUT" -o "$OUT/storebench" storebench.c -L/opt/homebrew/lib -lvulkan
VK_DRIVER_FILES="${VK_DRIVER_FILES:-$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}" \
    MESA_KK_EXPERIMENTAL=sparse "$OUT/storebench"
