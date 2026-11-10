#!/bin/sh
# Builds and runs vk-depth-bounds-memory.c: memory taken by draws that write depth with the depth
# bounds test on [0, 1], where KosmicKrisp emulates the test (issue #128).
# Usage: tests/mac/vk-depth-bounds-memory.sh [ICD json]   (default: the installed KosmicKrisp)
# For a KosmicKrisp that is built but not installed, pass
# build/mesa/src/kosmickrisp/vulkan/kosmickrisp_mesa_devenv_icd.aarch64.json.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/vk-depth-bounds-memory"
BREW=$(brew --prefix)
mkdir -p "$OUT"

cat > "$OUT/vk-depth-bounds-memory.vert" <<'EOF'
#version 450
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.5, 1.0);
}
EOF
cat > "$OUT/vk-depth-bounds-memory.frag" <<'EOF'
#version 450
layout(push_constant) uniform Push { vec4 color; } push;
layout(location = 0) out vec4 color;
void main()
{
    color = push.color;
}
EOF
for s in vk-depth-bounds-memory.vert vk-depth-bounds-memory.frag; do
    glslc -mfmt=c "$OUT/$s" -o "$OUT/$s.inc"
done
clang -O1 -Wall "$ROOT/tests/mac/vk-depth-bounds-memory.c" -I"$OUT" -I"$BREW/include" -L"$BREW/lib" \
    -lvulkan -Wl,-rpath,"$BREW/lib" -o "$OUT/vk-depth-bounds-memory"
if [ $# -gt 0 ]; then
    VK_DRIVER_FILES="$1" "$OUT/vk-depth-bounds-memory"
else
    VK_DRIVER_FILES="$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json" \
        "$OUT/vk-depth-bounds-memory"
fi
