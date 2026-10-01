#version 450
layout(location = 0) out vec4 o;
layout(constant_id = 0) const int DISCARD = 0;
layout(constant_id = 1) const int ITERS = 64;
void main() {
   vec2 c = gl_FragCoord.xy * 0.001;
   float a = 0.0;
   for (int i = 0; i < ITERS; i++)
      a += sin(c.x * float(i) + a) * cos(c.y + a);
   if (DISCARD != 0 && fract(gl_FragCoord.x * 0.37) > 0.995)
      discard;
   o = vec4(a, c, 1.0);
}
