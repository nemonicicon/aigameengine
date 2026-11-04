#version 450
// The bodies' shapes, thrown along a slanted light onto the floor, fading out at the map's edge. The old road's floor did this in its own shader; here the floor is per tile, so it's a dark layer over it.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(set = 1, binding = 0) uniform sampler2D uT;
layout(location = 0) in vec3 vP;
layout(location = 0) out vec4 o;
void main() {
    vec4 q = V.shv * vec4(vP.x, 0.0, vP.z, 1.0);
    vec2 uv = vec2(q.x * 0.5 + 0.5, 0.5 - q.y * 0.5);
    vec2 b = smoothstep(vec2(0.0), vec2(0.08), uv) * smoothstep(vec2(0.0), vec2(0.08), 1.0 - uv);
    o = vec4(0.0, 0.0, 0.0, 0.60 * texture(uT, uv).r * b.x * b.y);
}
