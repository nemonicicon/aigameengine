#version 450
// The menu scene's red dwarf.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(location = 0) in vec3 aP;
layout(location = 0) out vec3 vW;
void main() { vW = aP; gl_Position = V.vp * vec4(aP, 1.0); }
