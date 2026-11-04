#version 450
// The lens: every pixel's ray, bent past the black hole by a Schwarzschild-like pull, crossing the accretion disk and the pulsars' cores, and looked up in the rasterised scene when it gets out.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(std430, set = 0, binding = 2) readonly buffer LV { vec4 v[]; } LVB;
layout(set = 1, binding = 0) uniform sampler2D scn;
layout(push_constant) uniform PC { vec4 a; } P;   // a.x where this frame's numbers start in the ring
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
const float RM = 60.0, LR = 44.3;
vec3 H, dN, dU, dV; float rs, rin, rout, Tin, t;
float hs(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vn2(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hs(i), hs(i + vec2(1, 0)), f.x), mix(hs(i + vec2(0, 1)), hs(i + vec2(1, 1)), f.x), f.y); }
float fbm(vec2 p) { return vn2(p) * 0.55 + vn2(p * 2.1 + 3.7) * 0.3 + vn2(p * 4.3 - 1.3) * 0.15; }
// a place on the rasterised scene, from clip space: the scene went through a flipped viewport, so its top row is y up
vec4 scene(vec2 ndc) { return texture(scn, vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5)); }
vec3 lookup(vec3 p, vec3 v) {
    float tt = max(length(H - V.eye.xyz) - length(p - V.eye.xyz) + 15.0, 1.0); vec4 s = vec4(0.0, 0.0, 0.0, 1.0);
    for (int k = 0; k < 3; k++) { vec3 q = p + v * tt; vec4 c = V.vp * vec4(q, 1.0); if (c.w <= 0.01) return vec3(0.0);
        vec2 u = c.xy / c.w; if (abs(u.x) > 1.0 || abs(u.y) > 1.0) return vec3(0.0);
        s = scene(u); if (s.a > 0.985) break; tt = max(tt + s.a * 1500.0 - length(q - V.eye.xyz), 0.5); }
    return s.a > 0.985 ? vec3(0.0) : s.rgb * 4.0;
}
vec3 bb(float T) { T = clamp(T, 1000.0, 40000.0) / 100.0; vec3 c;
    c.r = T <= 66.0 ? 1.0 : clamp(1.292936 * pow(T - 60.0, -0.1332047), 0.0, 1.0);
    c.g = T <= 66.0 ? clamp(0.3900816 * log(T) - 0.6318414, 0.0, 1.0) : clamp(1.129891 * pow(T - 60.0, -0.0755148), 0.0, 1.0);
    c.b = T >= 66.0 ? 1.0 : (T <= 19.0 ? 0.0 : clamp(0.5432068 * log(T - 10.0) - 1.1962541, 0.0, 1.0));
    return c; }
vec4 disk(vec3 xr, float rr, vec3 v) {
    float x = (rr - rin) / (rout - rin);
    float om = 1.3 * pow(rin / rr, 1.5) * t, cu = dot(xr, dU), cv = dot(xr, dV);
    vec2 pr = vec2(cu * cos(om) - cv * sin(om), cu * sin(om) + cv * cos(om));
    float n = fbm(pr * 0.9), ring = 0.8 + 0.2 * sin(rr * 5.0 + n * 6.0);
    float Tm = pow(rin / rr, 0.75) * pow(max(1.0 - sqrt(rin / rr), 0.0), 0.25) * 1.5;
    vec3 u = normalize(cross(dN, xr)); float be = min(sqrt(0.5 * rs / max(rr - rs, 0.05)), 0.9), ga = 1.0 / sqrt(1.0 - be * be);
    float g = sqrt(max(1.0 - rs / rr, 0.0)) / (ga * (1.0 - be * dot(u, -v)));
    vec3 c = bb(Tin * Tm * g) * pow(Tm * g, 4.0) * 6.0;
    float a = smoothstep(0.0, 0.07, x) * (1.0 - smoothstep(0.55, 1.0, x)) * (0.45 + 0.55 * n) * ring;
    return vec4(c, clamp(a, 0.0, 0.95));
}
void main() {
    const int at = int(P.a.x);
    const vec4 cR = LVB.v[at], cU = LVB.v[at + 1], cF = LVB.v[at + 2], h4 = LVB.v[at + 3], k4 = LVB.v[at + 4];
    H = h4.xyz; rs = h4.w; rin = k4.x; rout = k4.y; Tin = k4.z; t = k4.w; dN = LVB.v[at + 5].xyz; dU = LVB.v[at + 6].xyz; dV = LVB.v[at + 7].xyz;
    vec2 nd = vec2(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0); vec3 d = normalize(cF.xyz + nd.x * cR.w * cR.xyz + nd.y * cU.w * cU.xyz);
    vec4 s0 = texture(scn, vUV); vec3 base = s0.rgb * 4.0; float sd = s0.a > 0.985 ? 1e9 : s0.a * 1500.0;
    vec3 eye = V.eye.xyz;
    if (cF.w < 0.5) { o = vec4(base * 0.25, 1.0); return; }
    vec3 oc = H - eye; float tc = dot(oc, d), b2 = dot(oc, oc) - tc * tc;
    if (b2 > RM * RM || tc < -RM) { o = vec4(base * 0.25, 1.0); return; }
    float t0 = max(tc - sqrt(RM * RM - b2), 0.0);
    if (sd < t0) { o = vec4(base * 0.25, 1.0); return; }
    vec3 p = eye + d * t0, v = d, acc = vec3(0.0), bg = vec3(0.0); float T = 1.0; bool fin = false;
    float jit = hs(gl_FragCoord.xy + fract(t * 7.13) * 31.0);
    for (int i = 0; i < 220; i++) {
        vec3 r = p - H; float rl = length(r);
        if (rl < rs) { fin = true; break; }
        if (i > 0 && rl > RM && dot(r, v) > 0.0) { bg = lookup(p, v); fin = true; break; }
        if (dot(p, p) < LR * LR) { vec4 c = V.vp * vec4(p, 1.0); bg = scene(c.xy / c.w).rgb * 4.0; fin = true; break; }
        float h = clamp(rl * 0.04, 0.004, 2.5); if (i == 0) h *= 0.5 + jit;
        vec3 cr = cross(r, v); float h2 = dot(cr, cr);
        vec3 v2 = normalize(v - 1.5 * rs * h2 * r / pow(rl, 5.0) * h);
        vec3 pn = p + v2 * h;
        float a0 = dot(r, dN), a1 = dot(pn - H, dN);
        if (a0 * a1 < 0.0) { vec3 x = p + (pn - p) * (a0 / (a0 - a1)); vec3 xr = x - H; float rr = length(xr);
            if (rr > rin && rr < rout) { vec4 dc = disk(xr, rr, v2); acc += T * dc.rgb * dc.a; T *= 1.0 - dc.a; } }
        for (int k = 0; k < 3; k++) { vec4 pk = LVB.v[at + 8 + k];
            vec3 w0 = p - pk.xyz; float sp = -dot(w0, v2);
            if (sp > 0.0 && sp <= h) { vec3 cl = w0 + v2 * sp; float dd = dot(cl, cl);
                acc += T * vec3(0.80, 0.88, 1.0) * (0.004 + pk.w * 14.0) * (0.0004 / (dd + 0.0004) * 3.0 + 0.02 / (1.0 + dd * 40.0)); } }
        p = pn; v = v2;
        if (T < 0.01) { fin = true; break; }
    }
    if (!fin) bg = lookup(p, v);
    o = vec4((acc + T * bg) * 0.25, 1.0);
}
