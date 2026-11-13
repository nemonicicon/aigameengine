#version 450
// The last lighthouse's swarm: orbital bands, each turning on its orbit. The band is symmetric, so its turn is an arc offset, and the fragment shader cuts the gaps and the truss by position along the ring.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { vec4 h; } P;   // xyz the black hole, w the clock
layout(location = 0) in vec3 aP;
layout(location = 1) in vec3 aB;   // arc position, across -1..1, surface 0 outer, 1 inner, 2 edge wall
layout(location = 2) in vec3 aN;
layout(location = 3) in float aS;  // style, with a per-ring seed in the fraction
layout(location = 4) in vec4 aR;   // angular speed, ring radius, gap start, gap length
layout(location = 0) out vec3 vW;
layout(location = 1) out vec3 vB;
layout(location = 2) out vec3 vN;
layout(location = 3) out float vS;
layout(location = 4) out vec4 vR;
void main() { vW = aP; vB = vec3(aB.x + aR.x * aR.y * P.h.w, aB.y, aB.z); vN = aN; vS = aS; vR = aR; gl_Position = V.vp * vec4(aP, 1.0); }
