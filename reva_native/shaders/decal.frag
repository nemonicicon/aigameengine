#version 450
// A projected decal (queue step 24). The box says where it could be; the scene's depth, taken when everything solid was down, says what's really there, and a pixel of the scene
// that lies inside the box takes the mark, whatever it's a pixel of: a floor, a step, a wall, a prop. So a splat thrown at a stair folds over it.
// Two ways onto the picture. A STAIN multiplies what's under it, so it's lit and shadowed exactly as that is: a mark, a splat, a scorch, a wet patch, a crack.
// PAINT is laid over it and lit by the sun and the sky here, with the sun's shadow: a stripe, a picture.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(constant_id = 1) const int PAINT = 0;
layout(constant_id = 3) const int LINEAR = 0;
layout(constant_id = 4) const int HEMI = 0;
layout(constant_id = 6) const int CASC = 1;
layout(set = 0, binding = 1) uniform sampler2DShadow uSh;
layout(std430, set = 0, binding = 2) readonly buffer LV { vec4 v[]; } LVB;
layout(set = 0, binding = 9) uniform sampler2D uSceneZ;
layout(set = 1, binding = 0) uniform sampler2DArray uT;   // a picture, for the kind that has one: on its page, as a mesh's is
layout(location = 0) flat in int vAt;
layout(location = 0) out vec4 o;
float h21(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }
float vnoise(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y); }
float fbm(vec2 p) { float v = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { v += a * vnoise(p); p *= 2.03; a *= 0.5; } return v; }
float sunShadow(vec3 W, float nl) {
    if (V.misc.y < 0.5) return 1.0;
    if (CASC > 1) {
        float d = dot(W - V.eye.xyz, V.fwd.xyz);
        int i = d < V.split.x ? 0 : d < V.split.y ? 1 : d < V.split.z ? 2 : d < V.split.w ? 3 : 4;
        if (i > 3) return 1.0;
        vec4 q = V.casc[i] * vec4(W, 1.0);
        vec3 s = q.xyz / q.w;
        float b = (0.0006 + 0.0012 * (1.0 - nl)) * (1.0 + float(i) * 1.5), e = V.misc.z * 3.0, sum = 0.0;
        vec2 tile = vec2(float(i & 1), float(i >> 1)) * 0.5, uv = vec2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
        for (int k = 0; k < 4; k++) { vec2 c = clamp(uv * 0.5 + vec2(float(k & 1) - 0.5, float(k >> 1) - 0.5) * e, vec2(V.misc.z * 2.0), vec2(0.5 - V.misc.z * 2.0)) + tile; sum += texture(uSh, vec3(c, s.z - b)); }
        return sum * 0.25;
    }
    vec4 q = V.sunVP * vec4(W, 1.0);
    vec3 s = q.xyz / q.w, c = vec3(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5, s.z - 0.0016 - 0.0035 * (1.0 - nl));
    return (c.x > 0.0 && c.x < 1.0 && c.y > 0.0 && c.y < 1.0 && c.z < 1.0) ? texture(uSh, c) : 1.0;
}
void main() {
    float z = texelFetch(uSceneZ, ivec2(gl_FragCoord.xy), 0).r;
    if (z >= 1.0) discard;                                    // the sky takes no mark
    vec2 sc = (gl_FragCoord.xy - V.cl2.zw) / V.cl3.xy;
    vec4 q = V.inv * vec4(sc.x * 2.0 - 1.0, 1.0 - sc.y * 2.0, z, 1.0);
    vec3 W = q.xyz / q.w;
    mat4 Mi = mat4(LVB.v[vAt + 4], LVB.v[vAt + 5], LVB.v[vAt + 6], LVB.v[vAt + 7]);
    vec3 L = (Mi * vec4(W, 1.0)).xyz;                        // where that is in the box: -0.5 to 0.5 each way, y along the throw
    if (abs(L.x) > 0.5 || abs(L.y) > 0.5 || abs(L.z) > 0.5) discard;
    vec4 col = LVB.v[vAt + 8], prm = LVB.v[vAt + 9];         // its colour and strength; its kind, how far it's faded, its own dice, its picture's layer
    // which way the surface faces, from the depth's own slope: a mark thrown at a floor doesn't run down the wall beside it
    vec3 N = normalize(cross(dFdx(W), dFdy(W)));
    if (dot(N, V.eye.xyz - W) < 0.0) N = -N;
    vec3 ay = normalize(LVB.v[vAt + 1].xyz);
    float a = smoothstep(0.18, 0.45, abs(dot(N, ay))) * smoothstep(0.5, 0.36, abs(L.y));
    const int kind = int(prm.x + 0.5);
    const float seed = prm.z, r = length(L.xz) * 2.0, ang = atan(L.z, L.x);
    vec3 c = col.rgb;
    if (kind == 0) a *= smoothstep(1.0, 0.25, r);                                                             // a soft round mark
    else if (kind == 1) {                                                                                     // a splat: a ragged blob, and drops thrown out past it
        float edge = 0.50 + 0.26 * vnoise(vec2(ang * 2.6 + seed * 7.0, seed)) + 0.14 * vnoise(vec2(ang * 7.0 - seed * 3.0, seed + 4.0));
        float drops = smoothstep(0.80, 0.86, vnoise(L.xz * 17.0 + seed * 11.0)) * smoothstep(1.0, 0.82, r);
        a *= max(smoothstep(edge, edge - 0.07, r), drops); }
    else if (kind == 2) {                                                                                     // a scorch: black at the middle, ragged and thin at its edge
        float n = fbm(L.xz * 7.0 + seed * 5.0);
        a *= smoothstep(1.0, 0.12, r + (n - 0.5) * 0.55) * (0.72 + 0.28 * n); }
    else if (kind == 3) {                                                                                     // wet: an even dark patch with a soft uneven edge
        float edge = 0.62 + 0.30 * vnoise(vec2(ang * 1.7 + seed * 5.0, seed + 2.0));
        a *= smoothstep(edge, edge - 0.16, r); }
    else if (kind == 4) {                                                                                     // a painted stripe: the whole box, worn
        float fw = max(fwidth(L.x), fwidth(L.z)) * 1.5 + 1e-4;
        a *= smoothstep(0.5, 0.5 - fw, max(abs(L.x), abs(L.z))) * (0.80 + 0.20 * smoothstep(0.30, 0.42, fbm(W.xz * 3.1 + seed))); }
    else if (kind == 5) {                                                                                     // a crack: thin lines where a noise crosses its middle
        float n = abs(fbm(L.xz * 4.2 + seed * 9.0) - 0.5), n2 = abs(fbm(L.zx * 6.5 - seed * 4.0) - 0.5);
        a *= max(smoothstep(0.016, 0.0, n), 0.7 * smoothstep(0.010, 0.0, n2)) * smoothstep(1.0, 0.45, r); }
    else { vec4 t = texture(uT, vec3(L.x + 0.5, 0.5 - L.z, prm.w)); c *= t.rgb; a *= t.a; }                    // a picture
    a *= col.a * prm.y;
    if (a < 0.003) discard;
    if (LINEAR == 2) c = pow(max(c, vec3(0.0)), vec3(2.2));
    if (PAINT == 0) { o = vec4(mix(vec3(1.0), c, a), 1.0); return; }                                           // a stain: what's under it, times this
    vec3 Ls = normalize(V.sunDir.xyz);
    float nl = max(dot(N, Ls), 0.0);
    vec3 amb = HEMI == 1 ? mix(V.ground.rgb, V.sky.rgb, N.y * 0.5 + 0.5) : V.sky.rgb;
    vec3 lit = c * (amb + V.sunCol.rgb * V.sunDir.w * nl * (nl > 0.0 && V.sunDir.w > 0.0 ? sunShadow(W, nl) : 0.0));
    o = vec4(lit, a);
}
