#!/bin/sh
# Builds and runs vk-ubo-range-past-end.c: a uniform buffer descriptor whose range runs past the
# end of its buffer, as DOOM (2016) writes them.
# Usage: tests/mac/vk-ubo-range-past-end.sh [ICD json]   (default: the installed KosmicKrisp)
# For a KosmicKrisp that is built but not installed, pass
# build/mesa/src/kosmickrisp/vulkan/kosmickrisp_mesa_devenv_icd.aarch64.json.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/vk-ubo-range-past-end"
BREW=$(brew --prefix)
mkdir -p "$OUT"

cat > "$OUT/vk-ubo-range-past-end.comp" <<'GLSL'
#version 450
layout(local_size_x = 1) in;
layout(binding = 0) uniform A { uvec4 a[4096]; };
layout(binding = 1) buffer Out { uvec4 result[4]; };
layout(push_constant) uniform Index { uint index[4]; };
void main()
{
    for (int i = 0; i < 4; i++)
        result[i] = a[index[i]];
}
GLSL
glslc -mfmt=c "$OUT/vk-ubo-range-past-end.comp" -o "$OUT/vk-ubo-range-past-end.comp.inc"
clang -O1 -Wall "$ROOT/tests/mac/vk-ubo-range-past-end.c" -I"$OUT" -I"$BREW/include" -L"$BREW/lib" \
    -lvulkan -Wl,-rpath,"$BREW/lib" -o "$OUT/vk-ubo-range-past-end"
if [ $# -gt 0 ]; then
    VK_DRIVER_FILES="$1" "$OUT/vk-ubo-range-past-end"
else
    VK_DRIVER_FILES="$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json" \
        "$OUT/vk-ubo-range-past-end"
fi
