#!/bin/sh
# Builds and runs vk-depth-bounds-msaa.c: the depth bounds test on a multisampled depth buffer.
# Usage: tests/mac/vk-depth-bounds-msaa.sh [ICD json]   (default: the installed KosmicKrisp)
# For a KosmicKrisp that is built but not installed, pass
# build/mesa/src/kosmickrisp/vulkan/kosmickrisp_mesa_devenv_icd.aarch64.json.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/vk-depth-bounds-msaa"
BREW=$(brew --prefix)
mkdir -p "$OUT"

cat > "$OUT/vk-depth-bounds-msaa.vert" <<'EOF'
#version 450
void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.5, 1.0);
}
EOF
cat > "$OUT/vk-depth-bounds-msaa-depth.frag" <<'EOF'
#version 450
layout(location = 0) out vec4 color;
void main()
{
    gl_FragDepth = 0.1 + 0.25 * float(gl_SampleID);
    color = vec4(0.0);
}
EOF
cat > "$OUT/vk-depth-bounds-msaa-red.frag" <<'EOF'
#version 450
layout(location = 0) out vec4 color;
void main()
{
    color = vec4(1.0, 0.0, 0.0, 1.0);
}
EOF
cat > "$OUT/vk-depth-bounds-msaa-red-mask.frag" <<'EOF'
#version 450
layout(location = 0) out vec4 color;
void main()
{
    gl_SampleMask[0] = 0x5;
    color = vec4(1.0, 0.0, 0.0, 1.0);
}
EOF
for s in vk-depth-bounds-msaa.vert vk-depth-bounds-msaa-depth.frag vk-depth-bounds-msaa-red.frag \
         vk-depth-bounds-msaa-red-mask.frag; do
    glslc -mfmt=c "$OUT/$s" -o "$OUT/$s.inc"
done
clang -O1 -Wall "$ROOT/tests/mac/vk-depth-bounds-msaa.c" -I"$OUT" -I"$BREW/include" -L"$BREW/lib" \
    -lvulkan -Wl,-rpath,"$BREW/lib" -o "$OUT/vk-depth-bounds-msaa"
if [ $# -gt 0 ]; then
    VK_DRIVER_FILES="$1" "$OUT/vk-depth-bounds-msaa"
else
    VK_DRIVER_FILES="$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json" \
        "$OUT/vk-depth-bounds-msaa"
fi
