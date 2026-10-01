#!/bin/sh
# Build and run a DGC test against KosmicKrisp (dist/mesa), with its EXT_device_generated_commands on.
#   run.sh      dgc-test: per-draw vertex/index buffers, push constants, count buffer
#   run.sh 2    dgc-test2: push address + non-indexed / CPU-index-buffer draws mid render pass, depth
#   run.sh 3    dgc-test3: two triangle strips split by primitive restart, through a geometry shader
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/tools/dgc-test"
name="dgc-test${1:-}"
mkdir -p "$OUT"
cd "$(dirname "$0")"
glslangValidator -V --target-env vulkan1.3 --vn "$(echo "$name" | tr - _)_vs" -o "$OUT/$name-vs.h" "$name.vert" >/dev/null
glslangValidator -V --target-env vulkan1.3 --vn "$(echo "$name" | tr - _)_fs" -o "$OUT/$name-fs.h" "$name.frag" >/dev/null
[ -f "$name.geom" ] && glslangValidator -V --target-env vulkan1.3 --vn "$(echo "$name" | tr - _)_gs" -o "$OUT/$name-gs.h" "$name.geom" >/dev/null
[ -f "$name-diag.vert" ] && glslangValidator -V --target-env vulkan1.3 --vn "$(echo "$name" | tr - _)_diag_vs" -o "$OUT/$name-diag-vs.h" "$name-diag.vert" >/dev/null
clang -O1 -g -I/opt/homebrew/include -I"$OUT" -o "$OUT/$name" "$name.c" -L/opt/homebrew/lib -lvulkan
VK_DRIVER_FILES="${VK_DRIVER_FILES:-$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json}" \
    MESA_KK_EXPERIMENTAL=dgc "$OUT/$name"
