#version 450
// A screen hanging in the world: one quad, its size and place in the model matrix. Row 0 of a picture drawn off-screen is its top, the same as everything else here.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { mat4 model; vec4 tint; } pc;
layout(location = 0) out vec2 vUV;
const vec2 Q[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
void main() {
    vec2 p = Q[gl_VertexIndex];
    vUV = vec2(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5);
    gl_Position = V.vp * pc.model * vec4(p, 0.0, 1.0);
}
