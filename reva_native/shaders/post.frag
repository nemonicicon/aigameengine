#version 450
// From the HDR target to the glass: the bloom added, the exposure, a filmic curve, sRGB, then a scene's LUT. Only a machine with the HDR setting runs this. The Pi's picture never comes here.
// Film comes through here too, on colours already as they'll be shown: the knee on brightness, its grade and vignette, and a lens's fisheye and a phone's frame.
layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uBloom;
layout(set = 0, binding = 2) uniform sampler3D uLut;
layout(set = 0, binding = 3) uniform sampler2D uAO;   // r occlusion, g contact shadow
layout(set = 0, binding = 4) uniform sampler2D uVol;  // what the air adds
layout(push_constant) uniform PC { vec4 a; vec4 b; vec4 c; vec4 d; } pc;  // a: exposure, bloom, the LUT's size (0 none), tonemap on. b: x the knee, y occlusion or contact on, z haze on, w the colours are already as they'll be shown. c: film's grade, its vignette, the picture's width over its height. d: the fisheye, the half-angle its corners cover, a phone's frame (1 upright, 2 wide), its clock
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
// Stephen Hill's fit of the ACES curve, which keeps a bright colour from going flat the way a curve on each channel alone does.
const mat3 ACES_IN = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
const mat3 ACES_OUT = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
vec3 aces(vec3 c) {
    c = ACES_IN * c;
    c = (c * (c + 0.0245786) - 0.000090537) / (c * (0.983729 * c + 0.4329510) + 0.238081);
    return clamp(ACES_OUT * c, 0.0, 1.0);
}
vec3 srgb(vec3 c) { c = clamp(c, 0.0, 1.0); return mix(12.92 * c, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c)); }
// the fisheye, as a projection and not a polynomial: the radius on the frame is the angle off the lens's axis, so the source is sampled at the tangent of it, which is where a straight lens put that ray. The corner maps to the corner, and its strength blends the whole thing back to none
vec2 warp(vec2 t, float aa) {
    if (pc.d.x <= 0.0) return t;
    vec2 q = t * 2.0 - 1.0; q.x *= aa;
    float rmax = sqrt(1.0 + aa * aa), rn = length(q) / rmax;
    if (rn < 1e-5) return t;
    float src = tan(rn * pc.d.y) / tan(pc.d.y);
    q *= mix(rn, src, pc.d.x) / rn;
    q.x /= aa; return q * 0.5 + 0.5;
}
float hash2(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
// seven-segment digits, for the phone's timer
float seg(vec2 p, vec2 a, vec2 b, float w) { vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0); return step(length(pa - ba * h), w); }
float digit(vec2 p, int d) {
    if (p.x < -0.1 || p.x > 0.6 || p.y < -0.1 || p.y > 1.1) return 0.0;
    float on = 0.0, w = 0.055;
    bool A = (d != 1 && d != 4), B = (d != 5 && d != 6), C = (d != 2), D = (d != 1 && d != 4 && d != 7), E = (d == 0 || d == 2 || d == 6 || d == 8), F = (d != 1 && d != 2 && d != 3 && d != 7), G = (d != 0 && d != 1 && d != 7);
    if (A) on = max(on, seg(p, vec2(0.05, 1.0), vec2(0.45, 1.0), w));
    if (B) on = max(on, seg(p, vec2(0.5, 0.95), vec2(0.5, 0.55), w));
    if (C) on = max(on, seg(p, vec2(0.5, 0.45), vec2(0.5, 0.05), w));
    if (D) on = max(on, seg(p, vec2(0.05, 0.0), vec2(0.45, 0.0), w));
    if (E) on = max(on, seg(p, vec2(0.0, 0.45), vec2(0.0, 0.05), w));
    if (F) on = max(on, seg(p, vec2(0.0, 0.95), vec2(0.0, 0.55), w));
    if (G) on = max(on, seg(p, vec2(0.05, 0.5), vec2(0.45, 0.5), w));
    return on;
}
void main() {
    // y up from here for the lens and the frame, as their numbers were worked out
    vec2 su = vUV, fu = vec2(vUV.x, 1.0 - vUV.y); float fasp = pc.c.z;
    if (pc.d.x > 0.0 || pc.d.z > 0.5) {
        float wf = 1.0;
        if (pc.d.z > 0.5) { fasp = pc.d.z > 1.5 ? (16.0 / 9.0) : (9.0 / 16.0); wf = clamp(fasp / pc.c.z, 0.05, 1.0); float dx = (fu.x - 0.5) / wf; if (abs(dx) > 0.5) { o = vec4(0.0, 0.0, 0.0, 1.0); return; } fu.x = dx + 0.5; }
        vec2 fw = warp(fu, fasp);
        su = vec2(0.5 + (fw.x - 0.5) * wf, 1.0 - fw.y);
    }
    vec4 sc = texture(uScene, su); vec3 c = sc.rgb;
    if (pc.b.y > 0.5) { vec2 k = texture(uAO, su).rg; c *= k.r * k.g; }
    if (pc.b.z > 0.5) c += texture(uVol, su).rgb;
    c += texture(uBloom, su).rgb * pc.a.y;
    c *= pc.a.x;
    if (pc.a.w > 0.5) c = srgb(aces(c * 2.6));   // 2.6 keeps the picture's average brightness where the Pi's is, measured on two scenes: the curve on its own pulls it down
    else if (pc.b.w < 0.5) {   // the old knee, for the A/B
        c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
        float kn = pc.b.x; vec3 ov = max(c - vec3(kn), vec3(0.0));
        c = min(c, vec3(kn)) + (0.96 - kn) * (vec3(1.0) - exp(-ov / (0.96 - kn)));
    }
    else if (pc.b.x > 0.0) { float l = luma(c), kn = pc.b.x, ov = max(l - kn, 0.0); c *= (min(l, kn) + (0.96 - kn) * (1.0 - exp(-ov / (0.96 - kn)))) / max(l, 1e-4); }   // film's: the knee on brightness, so a bright colour keeps its colour
    if (pc.c.x > 0.0) { float l0 = luma(c); vec3 s = clamp(c, 0.0, 1.0); s = s * s * (3.0 - 2.0 * s); c = mix(c, s, 0.16 * pc.c.x); c += vec3(-0.012, -0.002, 0.020) * pc.c.x * (1.0 - smoothstep(0.0, 0.45, l0)); c *= mix(vec3(1.0), vec3(1.03, 1.005, 0.965), pc.c.x * smoothstep(0.45, 1.0, l0)); float l1 = luma(c); c = mix(vec3(l1), c, 1.0 + 0.07 * pc.c.x); }   // film's gentle S
    if (pc.c.y > 0.0) { vec2 dv = (fu - 0.5) * vec2(fasp, 1.0) / sqrt(fasp * fasp + 1.0) * 2.0; c *= 1.0 - pc.c.y * 0.32 * smoothstep(0.55, 1.25, length(dv)); }   // and the corners falling away
    if (pc.a.z > 0.0) c = texture(uLut, c * (pc.a.z - 1.0) / pc.a.z + 0.5 / pc.a.z).rgb;
    // a phone's frame goes on last, because a camera's own frame isn't part of the scene's exposure: its lens falling off, sensor noise, the red dot and the running timer, top left of its screen
    if (pc.d.z > 0.5) {
        float r = length((fu - 0.5) * vec2(1.10, 1.0)), T = pc.d.w;
        c *= mix(1.0, 0.84, smoothstep(0.42, 0.80, r));
        c += (hash2(fu * vec2(720.0, 1280.0) + vec2(T * 37.0, T * 29.0)) - 0.5) * 0.022;
        float s2 = pc.d.z > 1.5 ? 1.0 : (9.0 / 16.0);
        vec2 q = vec2((fu.x - 0.055) / s2, fu.y - 0.945);
        if (step(0.35, fract(T)) > 0.5 && length(q) < 0.013) c = mix(c, vec3(0.95, 0.12, 0.10), 0.95);
        float sec = floor(mod(T, 60.0)), mn = floor(mod(T / 60.0, 10.0));
        vec2 tp = vec2((fu.x - 0.085) / (0.030 * s2), (fu.y - 0.935) / 0.024);
        float ink = digit(tp, int(mn));
        ink = max(ink, step(length(vec2(tp.x - 0.85, tp.y - 0.30)), 0.07));
        ink = max(ink, step(length(vec2(tp.x - 0.85, tp.y - 0.72)), 0.07));
        ink = max(ink, digit(tp - vec2(1.15, 0.0), int(floor(sec / 10.0))));
        ink = max(ink, digit(tp - vec2(1.85, 0.0), int(mod(sec, 10.0))));
        c = mix(c, vec3(1.0), clamp(ink, 0.0, 1.0) * 0.92);
    }
    o = vec4(mix(c, sc.rgb, clamp(sc.a - 1.0, 0.0, 1.0)), 1.0);   // what says it's over 1 is finished already: the toon look stands off all of this
}
