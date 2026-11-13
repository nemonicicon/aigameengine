#version 450
// Deep space, from the set's lines: a star field on the cube's six faces, a milky band, and up to eight named stars. Infinitely far, drawn first behind everything.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
layout(std430, set = 0, binding = 2) readonly buffer LV { vec4 v[]; } LVB;
layout(push_constant) uniform PC { vec4 a; } P;   // a.x where this frame's numbers start in the ring: 8 stars (direction, size), 8 colours, the milky way, then how many stars and how big a pixel is
float uPix;
float h1(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float vn(vec3 p) { vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(h1(i), h1(i + vec3(1, 0, 0)), f.x), mix(h1(i + vec3(0, 1, 0)), h1(i + vec3(1, 1, 0)), f.x), f.y), mix(mix(h1(i + vec3(0, 0, 1)), h1(i + vec3(1, 0, 1)), f.x), mix(h1(i + vec3(0, 1, 1)), h1(i + vec3(1, 1, 1)), f.x), f.y), f.z); }
vec3 h3(vec3 p) { return fract(sin(vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)), dot(p, vec3(113.5, 271.9, 124.6)))) * 43758.5453); }
vec3 scol(float t) { vec3 r = vec3(1.0, 0.42, 0.22), w = vec3(1.0, 0.96, 0.90), b = vec3(0.40, 0.56, 1.0); return t < 0.5 ? mix(r, w, t * 2.0) : mix(w, b, (t - 0.5) * 2.0); }
// one chance of a star per cell, kept off the cell's edges so it never needs its neighbours; most faint, a few bright, every colour from red to blue
vec3 field(vec3 d, float N, float dens, float seed) {
    vec3 a = abs(d); vec2 uv; float face;
    if (a.x >= a.y && a.x >= a.z) { uv = d.yz / a.x; face = d.x > 0.0 ? 0.0 : 1.0; }
    else if (a.y >= a.z) { uv = d.xz / a.y; face = d.y > 0.0 ? 2.0 : 3.0; }
    else { uv = d.xy / a.z; face = d.z > 0.0 ? 4.0 : 5.0; }
    vec2 g = (uv * 0.5 + 0.5) * N; vec2 cell = floor(g); vec2 f = g - cell;
    vec3 r = h3(vec3(cell, face * 13.0 + seed));
    if (r.x > dens) return vec3(0.0);
    vec2 sp = 0.2 + 0.6 * r.yz;
    float dist = length(f - sp) * 2.0 / N / (1.0 + dot(uv, uv));
    float b = pow(h1(vec3(cell.yx, face + seed * 1.7)), 5.0);
    float rad = uPix * (0.6 + 1.7 * b);
    float k = 1.0 - smoothstep(rad * 0.35, rad, dist);
    return scol(h1(vec3(cell, face + seed + 7.7))) * k * (0.32 + 1.9 * b);
}
void main() {
    const int at = int(P.a.x);
    const vec4 milky = LVB.v[at + 16], misc = LVB.v[at + 17];
    uPix = misc.y;
    vec4 q = V.inv * vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, 1.0, 1.0);
    vec3 d = normalize(q.xyz / q.w - V.eye.xyz);
    // (11-10) the sky turned (E->skyTurn): misc.z is the yaw and misc.w the pitch (nose down for more than 0) of what the lens rides in, and the ray is carried out of it into the sky's own frame. Both 0: the sky as it always was
    if (misc.z != 0.0 || misc.w != 0.0) { float cy = cos(misc.z), sy = sin(misc.z), cp = cos(misc.w), sp = sin(misc.w);
        d = vec3(cy * d.x + sy * sp * d.y + sy * cp * d.z, cp * d.y - sp * d.z, -sy * d.x + cy * sp * d.y + cy * cp * d.z); }
    vec3 c = field(d, 240.0, 0.075, 1.0) + field(d, 96.0, 0.05, 5.0);
    float mm = dot(d, milky.xyz), band = exp(-mm * mm / 0.03) * milky.w;
    float gr = (0.45 + 0.55 * vn(d * 7.0)) * (0.55 + 0.45 * vn(d * 19.0 + 3.1));
    c += vec3(0.60, 0.64, 0.78) * band * gr * 0.30;
    c += field(d, 420.0, 0.22, 9.0) * band * gr * 0.55;
    const int n = int(misc.x);
    for (int i = 0; i < n && i < 8; i++) {
        const vec4 s = LVB.v[at + i], sc = LVB.v[at + 8 + i];
        float ang = acos(clamp(dot(d, s.xyz), -1.0, 1.0));
        float core = 1.0 - smoothstep(s.w * 0.55, s.w, ang), halo = sc.a * exp(-ang / (s.w * 3.5));
        c += sc.rgb * (core * 1.6 + halo * 0.8);
    }
    if (LINEAR != 0) c = pow(max(c, vec3(0.0)), vec3(2.2));
    o = vec4(c, 1.0);
}
