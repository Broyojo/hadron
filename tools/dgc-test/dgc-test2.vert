#version 450
#extension GL_EXT_buffer_reference : require
layout(buffer_reference, std430) readonly buffer Obj { vec4 rect; vec4 color; float z; };
layout(push_constant) uniform PC { Obj obj; };
layout(location = 0) out vec4 c;
const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1), vec2(0, 1), vec2(1, 0), vec2(1, 1));
void main()
{
    vec2 t = corners[gl_VertexIndex % 6];
    gl_Position = vec4(mix(obj.rect.xy, obj.rect.zw, t), obj.z, 1.0);
    c = obj.color;
}
