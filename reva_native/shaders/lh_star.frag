#version 450
// The red dwarf: limb-darkened, boiling, spotted. View distance goes in alpha, for the lens pass.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { vec4 h; } P;
layout(location = 0) in vec3 vW;
layout(location = 0) out vec4 o;
float h3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float n3(vec3 p) { vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(h3(i), h3(i + vec3(1, 0, 0)), f.x), mix(h3(i + vec3(0, 1, 0)), h3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(h3(i + vec3(0, 0, 1)), h3(i + vec3(1, 0, 1)), f.x), mix(h3(i + vec3(0, 1, 1)), h3(i + vec3(1, 1, 1)), f.x), f.y), f.z); }
void main() {
    const float t = P.h.w;
    vec3 N = normalize(vW); float mu = max(dot(N, normalize(V.eye.xyz - vW)), 0.0);
    float g = n3(N * 9.0 + vec3(t * 0.15, 0.0, t * 0.11)) * 0.6 + n3(N * 23.0 - vec3(0.0, t * 0.3, 0.0)) * 0.4;
    float spot = smoothstep(0.62, 0.7, n3(N * 3.0 + vec3(7.1)));
    vec3 c = mix(vec3(1.0, 0.60, 0.36), vec3(1.0, 0.80, 0.58), g) * (0.35 + 0.65 * pow(mu, 0.55)) * (1.0 - 0.7 * spot) * 5.0;
    o = vec4(c * 0.25, clamp(length(vW - V.eye.xyz) / 1500.0, 0.0, 0.99));
}
