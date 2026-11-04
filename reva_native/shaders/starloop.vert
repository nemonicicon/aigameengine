#version 450
// A star's loops, turning with the surface under them.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { vec4 c; vec4 s; vec4 t; vec4 k; vec4 r; vec4 u; } P;
layout(location = 0) in vec3 aP;
layout(location = 1) in vec3 aN;
layout(location = 2) in vec2 aS;
layout(location = 0) out vec3 vN;
layout(location = 1) out vec3 vW;
layout(location = 2) out vec2 vS;
void main() { float ca = cos(P.r.x), sa = sin(P.r.x);
    vec3 q = vec3(ca * aP.x - sa * aP.z, aP.y, sa * aP.x + ca * aP.z);
    vN = vec3(ca * aN.x - sa * aN.z, aN.y, sa * aN.x + ca * aN.z);
    vW = P.c.xyz + q * P.c.w; vS = aS; gl_Position = V.vp * vec4(vW, 1.0); }
