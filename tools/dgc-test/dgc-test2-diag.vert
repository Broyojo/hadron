#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
layout(buffer_reference, std430) readonly buffer Obj { vec4 rect; vec4 color; float z; };
layout(push_constant) uniform PC { uvec2 obj; };
layout(location = 0) out vec4 c;
const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
void main()
{
    int column = gl_VertexIndex / 6;
    vec2 t = corners[gl_VertexIndex % 6];
    vec4 rect = vec4((16 * column + 2) / 32.0 - 1, 2 / 32.0 - 1, (16 * column + 14) / 32.0 - 1, 62 / 32.0 - 1);
    gl_Position = vec4(mix(rect.xy, rect.zw, t), 0.5, 1.0);
    c = (obj.x | obj.y) != 0 ? Obj(obj).color : vec4(1);
}
