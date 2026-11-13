#version 450
// A band of the swarm: lit by the red dwarf from inside and by the hole's disk from outside. View distance goes in alpha, for the lens pass.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { vec4 h; } P;
layout(location = 0) in vec3 vW;
layout(location = 1) in vec3 vB;
layout(location = 2) in vec3 vN;
layout(location = 3) in float vS;
layout(location = 4) in vec4 vR;
layout(location = 0) out vec4 o;
float h1(vec3 q) { return fract(sin(dot(q, vec3(12.9898, 78.233, 37.719))) * 43758.5453); }
void main() {
    float circ = 6.2831853 * vR.y, u = mod(vB.x, circ), v = vB.y, av = abs(v), kind = vB.z;
    if (vR.w > 0.0 && mod(u - vR.z, circ) < vR.w) discard;
    float sty = floor(vS + 0.001), seed = fract(vS);
    float L = 3.4, ru = fract(u / L);
    float rail = step(0.80, av), rib = step(av, 0.07), rung = step(ru, 0.09);
    float bay = mod(floor(u / (L * 5.0)), 2.0);
    if (kind < 1.5) {
        if (sty > 0.5 && sty < 1.5 && rail < 0.5 && rung < 0.5) discard;
        if (sty > 1.5 && rail < 0.5 && rib < 0.5 && rung < 0.5 && bay < 0.5) discard;
    }
    float pan = 0.75 + 0.5 * h1(vec3(floor(u / L), floor(v * 2.0), seed * 91.0));
    vec3 alb = vec3(0.30, 0.30, 0.32) * (rail > 0.5 ? 1.25 : (rib > 0.5 || rung > 0.5) ? 1.1 : 0.6 * pan);
    vec3 N = normalize(vN), Vv = normalize(V.eye.xyz - vW), col;
    const vec3 star = vec3(1.0, 0.72, 0.48);   // a 3200 K blackbody
    if (kind > 0.5 && kind < 1.5) {
        float d = length(vW), irr = 1.0 / (d * d);
        col = alb * star * irr * 2600.0;
        float sp = pow(max(dot(N, normalize(normalize(-vW) + Vv)), 0.0), 24.0);
        col += star * sp * irr * 420.0 * (rail > 0.5 ? 1.0 : 0.4);
    } else {
        vec3 Lh = P.h.xyz - vW; float dl = length(Lh); Lh /= dl; float ndl = max(dot(N, Lh), 0.0);
        vec3 dk = vec3(0.80, 0.88, 1.0) * 2600.0 / (dl * dl);
        float sp = pow(max(dot(N, normalize(Lh + Vv)), 0.0), 60.0);
        col = alb * ndl * dk + sp * dk * 0.30 * (rail > 0.5 ? 1.2 : 0.6);
        if (kind > 1.5) { float d = length(vW); col += alb * star * 40.0 / (d * d) * 40.0; }
    }
    o = vec4(col * 0.25, clamp(length(vW - V.eye.xyz) / 1500.0, 0.0, 0.99));
}
