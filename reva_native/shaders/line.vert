#version 450
// Coloured lines in the world: a game's neon overlay.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(location = 0) in vec3 aP;
layout(location = 1) in vec3 aC;
layout(location = 0) out vec3 vC;
void main() { vC = aC; gl_Position = V.vp * vec4(aP, 1.0); }
