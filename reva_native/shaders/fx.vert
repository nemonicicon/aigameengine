#version 450
// Effect triangles already in the world: a tile, a beam, a soft blob on the ground.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(location = 0) in vec3 aP;
layout(location = 1) in vec4 aC;
layout(location = 2) in vec2 aU;
layout(location = 0) out vec4 vC;
layout(location = 1) out vec2 vU;
layout(location = 2) out vec3 vW;   // where it is, for a particle that fades out against the scene behind it (step 24)
void main() { vC = aC; vU = aU; vW = aP; gl_Position = V.vp * vec4(aP, 1.0); }
