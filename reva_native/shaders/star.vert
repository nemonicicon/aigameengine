#version 450
// A star's sphere and the faces of the wedge cut out of it.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { vec4 c; vec4 s; vec4 t; vec4 k; vec4 r; vec4 u; } P;
layout(location = 0) in vec3 aP;
layout(location = 0) out vec3 vQ;
layout(location = 1) out vec3 vW;
void main() { vQ = aP; vW = P.c.xyz + aP * P.c.w * P.s.xyz; gl_Position = V.vp * vec4(vW, 1.0); }
