#version 450
// The dance floor's shadow: one quad lying just over the floor, as wide as the floor is.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { mat4 model; vec4 rect; } pc;  // rect: x min, x max, z min, z max
layout(location = 0) out vec3 vP;
const vec2 Q[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
void main() {
    vec2 q = Q[gl_VertexIndex];
    vP = vec3(mix(pc.rect.x, pc.rect.y, q.x), 0.006, mix(pc.rect.z, pc.rect.w, q.y));
    gl_Position = V.vp * vec4(vP, 1.0);
}
