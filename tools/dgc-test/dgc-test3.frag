#version 450
layout(push_constant) uniform PC { vec4 color; };
layout(location = 0) out vec4 o;
void main() { o = color; }
