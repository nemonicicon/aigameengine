#version 450
// A glowing volume along a line: the box round it, from the set's numbers, with no vertex data of its own. Its back faces are what's drawn, so a lens inside still sees it.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { vec4 a; vec4 u; vec4 v; vec4 w; vec4 r; vec4 c; } P;   // a: where it starts, length. u: along it, its strength. v: across, how it fades. w: across, how it thins. r: its radius at each end, the box's half-width. c: colour
layout(location = 0) out vec3 vW;
void main() {
    const int T[6] = int[6](0, 1, 2, 0, 2, 3);
    const vec2 UV[4] = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    const int fc = gl_VertexIndex / 6, j = gl_VertexIndex % 6, ax = fc / 2, sg = (fc % 2) == 0 ? -1 : 1;
    const vec2 k = UV[T[sg > 0 ? j : 5 - j]];
    vec3 p; p[ax] = float(sg); p[(ax + 1) % 3] = k.x; p[(ax + 2) % 3] = k.y;
    vW = P.a.xyz + P.u.xyz * ((p.z * 0.5 + 0.5) * P.a.w) + (P.v.xyz * p.x + P.w.xyz * p.y) * P.r.z;
    gl_Position = V.vp * vec4(vW, 1.0);
}
