#!/bin/sh
# Builds and runs vk-polygon-points.c: VK_POLYGON_MODE_POINT with culling, from a vertex shader
# and from a geometry shader.
# Usage: tests/mac/vk-polygon-points.sh [ICD json]   (default: the installed KosmicKrisp)
# For a KosmicKrisp that is built but not installed, pass
# build/mesa/src/kosmickrisp/vulkan/kosmickrisp_mesa_devenv_icd.aarch64.json.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/vk-polygon-points"
BREW=$(brew --prefix)
mkdir -p "$OUT"

cat > "$OUT/vk-polygon-points-strip.vert" <<'GLSL'
#version 450
void main()
{
    gl_Position = vec4((gl_VertexIndex & 2) != 0 ? 0.5 : -0.5, (gl_VertexIndex & 1) != 0 ? 0.5 : -0.5,
                       0.5, 1.0);
    gl_PointSize = 3.0;
}
GLSL
cat > "$OUT/vk-polygon-points-point.vert" <<'GLSL'
#version 450
void main()
{
    gl_Position = vec4(0.0, 0.0, 0.5, 1.0);
}
GLSL
cat > "$OUT/vk-polygon-points.geom" <<'GLSL'
#version 450
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
void main()
{
    for (int i = 0; i < 4; i++) {
        gl_Position = vec4((i & 2) != 0 ? 0.5 : -0.5, (i & 1) != 0 ? 0.5 : -0.5, 0.5, 1.0);
        gl_PointSize = 3.0;
        EmitVertex();
    }
    EndPrimitive();
}
GLSL
cat > "$OUT/vk-polygon-points.frag" <<'GLSL'
#version 450
layout(location = 0) out vec4 color;
void main()
{
    color = vec4(1.0, 0.0, 0.0, 1.0);
}
GLSL
for s in vk-polygon-points-strip.vert vk-polygon-points-point.vert vk-polygon-points.geom \
         vk-polygon-points.frag; do
    glslc -mfmt=c "$OUT/$s" -o "$OUT/$s.inc"
done
clang -O1 -Wall "$ROOT/tests/mac/vk-polygon-points.c" -I"$OUT" -I"$BREW/include" -L"$BREW/lib" \
    -lvulkan -Wl,-rpath,"$BREW/lib" -o "$OUT/vk-polygon-points"
if [ $# -gt 0 ]; then
    VK_DRIVER_FILES="$1" "$OUT/vk-polygon-points"
else
    VK_DRIVER_FILES="$ROOT/dist/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json" \
        "$OUT/vk-polygon-points"
fi
