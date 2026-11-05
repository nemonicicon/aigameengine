#version 450
// The lens's own blur (queue step 25), between the scene and the post pass, on the scene's own light. Two jobs, a pass each:
// MODE 1, DEPTH OF FIELD: what's nearer or further than the lens is focused goes soft, by how far off it is. Each pixel gathers from a disc, and a point only spreads as far as
//   its own softness, so a sharp subject isn't smeared by the soft wall behind it, and something soft in front of it does spread over it.
// MODE 2, MOTION BLUR: where a point of the scene was in the last frame's picture is worked out from its depth and the two lenses, and the picture's smeared along the way it
//   came, for as much of the frame as the shutter's open. It's the lens's own move that's followed: a body crossing a still lens stays sharp (not built).
layout(constant_id = 1) const int MODE = 1;
layout(set = 0, binding = 0) uniform sampler2D uC;
layout(set = 0, binding = 3) uniform sampler2D uZ;
// DoF: m takes the picture back to the world. a: the eye, and how far away it's focused. b: the lens's forward, and how soft. c: a texel, the most blur in pixels, the picture's height in pixels
// blur: m takes this frame's picture to the last one's. a: x the shutter, y the longest smear as a share of the picture. c: as above
layout(push_constant) uniform PC { mat4 m; vec4 a; vec4 b; vec4 c; vec4 d; } pc;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
float noise(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
float away(vec2 uv) { float z = texture(uZ, uv).r; vec4 q = pc.m * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, z, 1.0); return dot(q.xyz / q.w - pc.a.xyz, pc.b.xyz); }
float soft(float d) { return clamp(pc.b.w * abs(d - pc.a.w) / max(d, 0.05) * 0.012 * pc.c.w, 0.0, pc.c.z); }   // a point's blur, as a radius in pixels
void main() {
    vec4 c0 = texture(uC, vUV);
    if (MODE == 1) {
        float d0 = away(vUV), r0 = soft(d0), R = r0;
        // something soft in front, near enough to reach this pixel: the disc is as wide as its blur
        for (int i = 0; i < 8; i++) { float a = float(i) * 0.7853982; vec2 q = vUV + vec2(cos(a), sin(a)) * pc.c.xy * pc.c.z * 0.6; float d = away(q); if (d < d0) R = max(R, min(soft(d), pc.c.z)); }
        if (R < 0.6) { o = c0; return; }
        vec3 sum = c0.rgb; float n = 1.0; float turn = noise(gl_FragCoord.xy) * 6.2831853;
        for (int i = 0; i < 40; i++) {
            float r = sqrt((float(i) + 0.5) / 40.0) * R, a = float(i) * 2.3999632 + turn;
            vec2 q = vUV + vec2(cos(a), sin(a)) * r * pc.c.xy;
            float d = away(q), s = soft(d);
            if (d > d0 + 0.05) s = min(s, r0);                  // it's behind this pixel: it can't spread further over it than this pixel's own blur
            float w = smoothstep(r - 1.0, r + 1.0, s);
            sum += texture(uC, q).rgb * w; n += w;
        }
        o = vec4(sum / n, c0.a); return;
    }
    float z = texture(uZ, vUV).r;
    vec4 q = pc.m * vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, z, 1.0);
    vec2 was = vec2(q.x / q.w * 0.5 + 0.5, 0.5 - q.y / q.w * 0.5), v = (vUV - was) * pc.a.x;
    if (q.w <= 0.0) v = vec2(0.0);
    float l = length(v); if (l > pc.a.y) v *= pc.a.y / l;
    if (l * pc.c.w < 0.75) { o = c0; return; }
    vec3 sum = vec3(0.0); float j = noise(gl_FragCoord.xy) - 0.5;
    for (int i = 0; i < 12; i++) sum += texture(uC, vUV + v * ((float(i) + 0.5 + j) / 12.0 - 0.5)).rgb;
    o = vec4(sum / 12.0, c0.a);
}
