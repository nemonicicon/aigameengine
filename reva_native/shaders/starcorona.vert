#version 450
// A quad facing the lens, round a star.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { vec4 c; vec4 s; vec4 t; vec4 k; vec4 r; vec4 u; } P;
layout(location = 0) in vec2 aC;
layout(location = 0) out vec2 vC;
void main() { vC = aC * 2.6; vec3 w = P.c.xyz + (P.r.xyz * aC.x + P.u.xyz * aC.y) * P.c.w * 2.6; gl_Position = V.vp * vec4(w, 1.0); }
