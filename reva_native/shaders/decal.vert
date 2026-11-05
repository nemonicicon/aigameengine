#version 450
// A projected decal's box (queue step 24): a cube from -0.5 to 0.5 under the decal's matrix, with no vertex data of its own. Its back faces are what's drawn, with no depth test,
// so a lens inside the box still sees it; the fragment shader finds what's really there from the scene's depth. One call draws every decal: each is ten vec4s in the frame's buffer.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(std430, set = 0, binding = 2) readonly buffer LV { vec4 v[]; } LVB;
layout(push_constant) uniform PC { vec4 a; } P;   // x: the first decal's place in the buffer, in vec4s
layout(location = 0) flat out int vAt;
void main() {
    const int T[6] = int[6](0, 1, 2, 0, 2, 3);
    const vec2 UV[4] = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    const int fc = gl_VertexIndex / 6, j = gl_VertexIndex % 6, ax = fc / 2, sg = (fc % 2) == 0 ? -1 : 1;
    const vec2 k = UV[T[sg > 0 ? j : 5 - j]];
    vec3 p; p[ax] = float(sg); p[(ax + 1) % 3] = k.x; p[(ax + 2) % 3] = k.y;
    int at = int(P.a.x) + gl_InstanceIndex * 10;
    mat4 M = mat4(LVB.v[at], LVB.v[at + 1], LVB.v[at + 2], LVB.v[at + 3]);
    vAt = at;
    gl_Position = V.vp * (M * vec4(p * 0.5, 1.0));
}
