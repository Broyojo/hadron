#version 450
layout(push_constant) uniform PC { int mode; } pc;
void main() {
   vec2 p[3] = vec2[](vec2(-1, -1), vec2(3, -1), vec2(-1, 3));
   vec2 v = p[gl_VertexIndex];
   if (pc.mode == 1) {
      int i = gl_InstanceIndex;
      vec2 o = vec2(i % 100, (i / 100) % 50) / vec2(50, 25) - 1.0;
      gl_Position = vec4(o + (v + 1.0) * 0.004, 0.5, 1.0);
      return;
   }
   gl_Position = vec4(v, float(gl_InstanceIndex) / 64.0, 1.0);
}
