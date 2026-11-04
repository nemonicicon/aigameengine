#version 450
// The world's one fragment shader, kept to the budget. On the tube a pixel's cost is the biggest lever in the frame (engine plan), so this is a texture, the sky, the sun and
// a shadow lookup, and nothing else until a setting asks for it. TAPS is the shadow's tap count: 1 on the Pi (the sampler's own bilinear compare), 4 on the PC.
// ⛔ No local array walked in a loop on V3D: it goes to scratch memory (59 ms against 5 for one full-screen pass, measured).
#extension GL_GOOGLE_include_directive : enable
precision mediump float;
#include "view.glsl"
layout(constant_id = 0) const int TAPS = 1;
layout(constant_id = 1) const int CUTOUT = 0;
layout(constant_id = 3) const int LINEAR = 0;   // 0 lighting on gamma-encoded values, as the Pi does. 1 this shader decodes and encodes, on an 8-bit target. 2 the sampler decodes, and the target holds the light. 3 the sampler decodes, and this shader encodes for an 8-bit picture drawn off-screen
layout(location = 0) in vec3 vN;
layout(location = 1) in vec2 vT;
layout(location = 2) in vec3 vW;
layout(location = 3) in vec4 vS;
layout(constant_id = 4) const int HEMI = 0;     // the sky as a hemisphere, with a ground under it
layout(constant_id = 5) const int LIGHTS = 0;   // point and spot lights, clustered
layout(constant_id = 6) const int CASC = 1;     // sun cascades. 1 is the one map
layout(constant_id = 7) const int PROBES = 0;   // baked light from a grid over the set
layout(location = 0) out vec4 o;
layout(set = 0, binding = 1) uniform sampler2DShadow uSh;
layout(std430, set = 0, binding = 2) readonly buffer LV { vec4 v[]; } LVB;
layout(std430, set = 0, binding = 3) readonly buffer LU { uint u[]; } LUB;
layout(set = 0, binding = 4) uniform sampler2DShadow uAtlas;   // the lights' own shadow maps, 4 x 4 tiles
layout(set = 0, binding = 5) uniform sampler3D uProbe;
layout(set = 1, binding = 0) uniform sampler2DArray uT;   // a page of textures, or one alone
layout(location = 4) flat in vec4 vTint;    // w: how much of the texture to take over the tint
layout(location = 5) flat in vec3 vExtra;   // x the mesh's own alpha, y it glows, z its layer of the page
#if defined(MOUTH_HOLE) || defined(TOON) || defined(REFL) || defined(GLOSS)
layout(push_constant) uniform Face { vec4 p; vec4 s; vec4 t; vec4 i; vec4 a; } M;   // the lip line and its tilt; the mouth's half-width, the face's height, how far open, 1 for a room; the toon's cut, its rim, the key from the lens, a room's gradient; the ink
#endif
#ifdef MOUTH_HOLE
layout(location = 6) in vec3 vFace;
#endif
// the PC's sun: which cascade this pixel is in, and a soft lookup turned a little per pixel so its steps become grain and not bands
float sunCascades(float nl) {
    float d = dot(vW - V.eye.xyz, V.fwd.xyz);
    int i = d < V.split.x ? 0 : d < V.split.y ? 1 : d < V.split.z ? 2 : d < V.split.w ? 3 : 4;
    if (i > 3) return 1.0;
    vec4 q = V.casc[i] * vec4(vW, 1.0);
    vec3 s = q.xyz / q.w;
    float b = (0.0003 + 0.0010 * (1.0 - nl)) * (1.0 + float(i) * 1.5);
    vec2 tile = vec2(float(i & 1), float(i >> 1)) * 0.5;
    vec2 uv = vec2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float e = V.misc.z * 5.0;
    float a = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;
    vec2 r0 = vec2(cos(a), sin(a)) * e, r1 = vec2(-r0.y, r0.x);
    float sum = 0.0;
    for (int k = 0; k < 8; k++) {
        float g = float(k) * 2.3999632, m = sqrt((float(k) + 0.5) / 8.0);
        vec2 o2 = (r0 * cos(g) + r1 * sin(g)) * m;
        vec2 c = clamp(uv * 0.5 + o2, vec2(V.misc.z * 2.0), vec2(0.5 - V.misc.z * 2.0)) + tile;
        sum += texture(uSh, vec3(c, s.z - b));
    }
    return sum / 8.0;
}
// one tile of the atlas, four taps
float lightShadow(int tile, vec3 W) {
    uint m = uint(V.cl3.w) + uint(tile) * 4u;
    vec4 q = mat4(LVB.v[m], LVB.v[m + 1u], LVB.v[m + 2u], LVB.v[m + 3u]) * vec4(W, 1.0);
    vec3 s = q.xyz / q.w;
    if (s.z >= 1.0 || q.w <= 0.0) return 1.0;
    vec2 uv = clamp(vec2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5), vec2(0.003), vec2(0.997)), a = (uv + vec2(float(tile % 4), float(tile / 4))) * 0.25;
    float e = 0.00018, z = s.z - 0.0012;
    return 0.25 * (texture(uAtlas, vec3(a + vec2(-e, -e), z)) + texture(uAtlas, vec3(a + vec2(e, -e), z)) + texture(uAtlas, vec3(a + vec2(-e, e), z)) + texture(uAtlas, vec3(a + vec2(e, e), z)));
}
// the lights near this pixel, from the cell it's in
vec3 lightsHere(vec3 N) {
    vec2 sc = (gl_FragCoord.xy - V.cl2.zw) / V.cl3.xy;
    float vz = dot(vW - V.eye.xyz, V.fwd.xyz);
    uint cx = uint(clamp(sc.x * V.cl.y, 0.0, V.cl.y - 1.0)), cy = uint(clamp(sc.y * V.cl.z, 0.0, V.cl.z - 1.0));
    uint sl = uint(clamp(log(max(vz, 0.01) / V.cl2.y) * V.cl2.x, 0.0, V.cl.w - 1.0));
    uint e = LUB.u[uint(V.cl.x) + cx + uint(V.cl.y) * (cy + uint(V.cl.z) * sl)];
    uint off = e >> 8, n = e & 255u;
    vec3 li = vec3(0.0);
    for (uint i = 0u; i < n; i++) {
        uint r = LUB.u[off + i];
        vec4 a = LVB.v[r], b = LVB.v[r + 1u], c = LVB.v[r + 2u], g = LVB.v[r + 3u];
        vec3 D = a.xyz - vW; float dd = dot(D, D);
        vec3 L = D * inversesqrt(dd + 0.001);
        float f = max(0.0, dot(N, L)) / (1.0 + dd * a.w);
        if (c.w > -1.5) f *= smoothstep(c.w, b.w, dot(-L, c.xyz));
        if (g.x >= 0.0 && f > 0.0) {
            int tile = int(g.x);
            if (g.y > 0.5) { vec3 d3 = vW - a.xyz, ad = abs(d3); tile += ad.x > ad.y && ad.x > ad.z ? (d3.x > 0.0 ? 0 : 1) : ad.y > ad.z ? (d3.y > 0.0 ? 2 : 3) : (d3.z > 0.0 ? 4 : 5); }
            f *= lightShadow(tile, vW + N * 0.03);
        }
        li += b.rgb * f;
    }
    return li;
}
void main() {
    vec4 t = texture(uT, vec3(vT, vExtra.z));
    if (CUTOUT == 1 && vTint.w >= 0.5 && t.a < 0.5) discard;
    vec3 tx = t.rgb, tn = vTint.rgb;
#ifdef TOON
    // the show's palette is hand-tuned as it's stored and against a light capped at 1, so it's worked out there on every machine, and what tonemaps the rest stands off it
    if (LINEAR == 2) tx = pow(tx, vec3(1.0 / 2.2));
#else
    if (LINEAR == 1) tx = pow(tx, vec3(2.2));
    if (LINEAR != 0) tn = pow(tn, vec3(2.2));
#endif
    vec3 base = mix(tn, tx, vTint.w);
#ifdef MOUTH_HOLE
    // A lip line modelled shut has nowhere to open, so a hole's cut between the lips, by where the skin started out: their own lips then part by themselves, because the lower face is skinned to the jaw. Only an open mouth draws through this
    if (vFace.z > -0.12 && abs(vFace.y) < 1.6 && abs(vFace.x) < 1.4) {
        float aH = M.s.z * 0.22, aW = 0.78 * mix(0.52, 1.0, clamp(M.s.z * 1.25, 0.0, 1.0));
        float ax = vFace.y / max(1e-4, aW), ay = (vFace.x + aH * 0.92) / max(1e-4, aH * 1.06), rr = sqrt(ax * ax + ay * ay);
        if (rr < 1.0) discard;
        base *= mix(0.72, 1.0, smoothstep(1.0, 1.18, rr));
    }
#endif
    vec3 N = normalize(vN);
    vec3 lit;
    vec3 amb = HEMI == 1 ? mix(V.ground.rgb, V.sky.rgb, N.y * 0.5 + 0.5) : V.sky.rgb;
    vec3 extra = vec3(0.0);
    if (LIGHTS == 1 && V.cl.x > 0.0) extra = lightsHere(N);
    if (PROBES == 1 && V.probe.w > 0.5) { vec4 pr = texture(uProbe, (vW + N * 0.5 - V.probe.xyz) * V.probeS.xyz); amb *= pr.r; extra += V.sunCol.rgb * V.sunDir.w * pr.b * 0.2; }
#ifdef TOON
    // the cel ramp: one key light and one shadow map, cut into two tones, lit and a cool heavy shadow, over a pixel's smoothstep so the edge doesn't crawl
    {
        const float room = M.s.w, cel = M.t.x, rim = M.t.y, keyCam = M.t.z, roomG = M.t.w;
        float sh = 1.0;
        if (V.sunDir.w > 0.0 && V.misc.y > 0.5) {
            float nl = max(0.0, dot(N, V.sunDir.xyz));
            if (CASC > 1) sh = sunCascades(nl);
            else { vec3 s = vS.xyz / vS.w; vec3 c = vec3(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5, s.z - 0.0012 - 0.0035 * (1.0 - nl)); if (c.x > 0.0 && c.x < 1.0 && c.y > 0.0 && c.y < 1.0 && c.z < 1.0) sh = texture(uSh, c); }
        }
        // a body's key comes from above and to the right of the lens, as a drawn show keys its characters, and the room's shadows still reach it, as a dim and not a cut
        vec3 Lk = V.sunDir.xyz; float shk = sh;
        if (keyCam > 0.5 && room < 0.5) { vec3 Vk = normalize(V.eye.xyz - vW), Rk = cross(vec3(0.0, 1.0, 0.0), Vk); float rl = length(Rk); Rk = rl > 1e-4 ? Rk / rl : vec3(1.0, 0.0, 0.0); Lk = normalize(Vk * 0.75 + vec3(0.0, 0.80, 0.0) + Rk * 0.30); shk = 1.0; }
        float kd = room > 0.5 ? (0.5 + 0.5 * N.y) * sh : (V.sunDir.w > 0.0 ? dot(N, Lk) * shk : dot(N, vec3(0.3965, 0.7930, 0.5898)));
        float lv = smoothstep(cel - 0.02, cel + 0.02, kd);
        vec3 full = V.sunDir.w > 0.0 ? (V.sky.rgb + V.sunCol.rgb * V.sunDir.w * 0.9) : vec3(0.80);
        full *= min(1.0, 1.0 / max(dot(full, vec3(0.299, 0.587, 0.114)), 1e-3));   // the light's capped at 1, its hue kept: nothing brightens past the palette
        // the darker the colour, the gentler the cut, or a dark skin's shadow goes black
        vec3 sm = mix(vec3(0.74, 0.70, 0.84), vec3(0.50, 0.47, 0.66), smoothstep(0.08, 0.40, dot(base, vec3(0.299, 0.587, 0.114))));
        lit = base * full * mix(sm, vec3(1.0), lv) + base * extra * 0.25;
        if (rim > 0.0 && room < 0.5) { float fr = 1.0 - max(dot(N, normalize(V.eye.xyz - vW)), 0.0); lit += base * full * smoothstep(0.60, 0.68, fr) * (0.35 + 0.65 * lv) * rim; }
        if (keyCam > 0.5 && room < 0.5) lit *= mix(0.80, 1.0, sh);
        if (room > 0.5 && roomG > 0.0) lit *= mix(1.0 - 0.16 * roomG, 1.0 + 0.03 * roomG, clamp(vW.y / 5.2, 0.0, 1.0));   // a room lit from its ceiling
    }
#else
    if (V.sunDir.w > 0.0) {
        float nl = max(0.0, dot(N, V.sunDir.xyz));
        float sh = 1.0;
        if (CASC > 1) { if (V.misc.y > 0.5 && nl > 0.0) sh = sunCascades(nl); }
        else if (V.misc.y > 0.5 && nl > 0.0) {
            vec3 s = vS.xyz / vS.w;
            vec3 c = vec3(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5, s.z - 0.0012 - 0.0035 * (1.0 - nl));
            if (c.x > 0.0 && c.x < 1.0 && c.y > 0.0 && c.y < 1.0 && c.z < 1.0) {
                if (TAPS == 1) sh = texture(uSh, c);
                else {
                    float e = V.misc.z;
                    sh = 0.25 * (texture(uSh, c + vec3(-e, -e, 0.0)) + texture(uSh, c + vec3(e, -e, 0.0)) + texture(uSh, c + vec3(-e, e, 0.0)) + texture(uSh, c + vec3(e, e, 0.0)));
                }
            }
        }
        lit = base * (amb + V.sunCol.rgb * V.sunDir.w * nl * sh + extra);
    } else {
        float l = 0.3 + 0.7 * max(0.0, dot(N, vec3(0.3965, 0.7930, 0.5898)));
        float f = l * 0.42 + 0.13;
        lit = base * ((LINEAR != 0 ? f * f : f) + extra);
    }
#endif
    if (vExtra.y > 0.5) lit = base;   // a glowing part is its own colour, unlit
    if (V.fog.w > 0.0) lit = mix(V.fog.rgb, lit, exp(-distance(vW, V.eye.xyz) * V.fog.w));
#ifndef TOON
    if (LINEAR == 1 || LINEAR == 3) lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2));
    if (LINEAR != 2)
#endif
    {
        float kn = V.misc.x;
        vec3 ov = max(lit - vec3(kn), vec3(0.0));
        lit = min(lit, vec3(kn)) + (0.96 - kn) * (vec3(1.0) - exp(-ov / (0.96 - kn)));
    }
#ifdef REFL
    lit *= exp(-max(M.a.y - vW.y, 0.0) * M.a.z);   // a polished floor shows what's near it: a reflection fades with its depth under the floor
#endif
#ifdef GLOSS
    { float ct = clamp(dot(N, normalize(V.eye.xyz - vW)), 0.0, 1.0); o = vec4(lit, min(M.a.z, M.a.y + (1.0 - M.a.y) * pow(1.0 - ct, 5.0))); }   // how much of what's under the floor shows from here: more at a grazing angle. The blend reads it
#elif defined(TOON) && !defined(TOON_BLEND)
    o = vec4(lit, 2.0);   // over 1: a pixel the post pass leaves as it is
#else
    o = vec4(lit, mix(1.0, t.a, vTint.w) * vExtra.x);   // the blend pipelines read this, so sheer cloth is sheer; the others ignore it
#endif
}
