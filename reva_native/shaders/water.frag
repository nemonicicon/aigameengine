#version 450
// A pool's surface: what's under it looking straight down, the sky at a glancing angle, which is why a ripple shows at all. A two-centimetre wave swings the reflected ray far enough to change the colour completely. The sky's the same description the dome has.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec3 vN;
layout(location = 2) in vec3 vW;
layout(location = 4) flat in vec4 vTint;
layout(location = 0) out vec4 o;
float h21(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }
float vnoise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y); }
float fbm(vec2 p) { float v = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { v += a * vnoise(p); p *= 2.03; a *= 0.5; } return v; }
vec3 skyColor(vec3 d, vec3 zen, vec3 hz, vec3 gnd, vec3 sunC, float si) {
    d = normalize(d);
    float t = clamp(d.y, 0.0, 1.0);
    vec3 c = mix(hz, zen, pow(t, 0.55));
    float sd = max(dot(d, normalize(V.sunDir.xyz)), 0.0);
    if (d.y > 0.01) {
        vec2 q = d.xz / max(d.y, 0.08);
        float T = V.misc.w;
        float c1 = fbm(q * 0.42 + vec2(T * 0.0075, T * 0.0030)), c2 = fbm(q * 1.05 + vec2(-T * 0.0130, T * 0.0068));
        float cl = clamp((c1 * 0.70 + c2 * 0.40 - 0.48) * 2.1, 0.0, 1.0) * smoothstep(0.015, 0.34, d.y);
        vec3 cc = mix(vec3(0.95, 0.95, 0.97), sunC * 1.06, 0.40 * pow(sd, 3.0)) * (0.72 + 0.42 * si);
        c = mix(c, cc, cl * 0.74);
    }
    c += sunC * si * (pow(sd, 1200.0) * 9.0 + pow(sd, 26.0) * 0.42 + pow(sd, 4.0) * 0.10);
    return mix(gnd, c, smoothstep(-0.10, 0.015, d.y));
}
void main() {
    vec3 N = normalize(vN), Vv = normalize(V.eye.xyz - vW);
    if (dot(N, Vv) < 0.0) N = -N;
    const vec3 sky = V.sky.rgb, sunC = V.sunCol.rgb; const float si = V.sunDir.w > 0.0 ? V.sunDir.w : 0.15;
    vec3 zen = sky * vec3(1.35, 1.45, 1.75), hz = sky * vec3(1.85, 1.80, 1.70) + vec3(0.10, 0.11, 0.12), gnd = sky * vec3(0.55, 0.58, 0.50);
    float F = 0.02 + 0.98 * pow(1.0 - clamp(dot(N, Vv), 0.0, 1.0), 5.0);
    vec3 R = reflect(-Vv, N), Ls = normalize(V.sunDir.xyz);
    vec3 refl = skyColor(R, zen, hz, gnd, sunC, si);
    vec3 deep = vTint.rgb * (0.42 + 0.58 * max(dot(N, Ls), 0.0));
    vec3 c = mix(deep, refl, F);
    c += sunC * si * pow(max(dot(R, Ls), 0.0), 300.0) * 1.8;   // a glint where the reflected ray finds the sun itself
    o = vec4(LINEAR == 2 ? pow(max(c, vec3(0.0)), vec3(2.2)) : c, mix(0.58, 0.97, F));
}
