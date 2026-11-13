#version 450
// The day's sky, drawn first behind everything: the same description the old road's skyColor has. A gradient from the horizon up, the sun as a disc in a glow, a cloud deck drifting on the tick clock, and ground below the horizon. All worked out as it looks, and turned into light at the end on the HDR road, so the sun's disc is over white and blooms.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(constant_id = 1) const int MODE = 1;     // 1 the gradient and the sun, 2 clouds as well
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
float h21(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x), mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p) { float v = 0.0, a = 0.5; for (int i = 0; i < 4; i++) { v += a * vnoise(p); p *= 2.03; a *= 0.5; } return v; }
#ifdef GODOT_SKY_HOOK
// (11-12) A GAME'S OWN SKY (the Godot proof of concept's step 12: their parks' skies are Godot sky shaders). Their sky code, translated at the cook
// (skate_src/godot/godot_shader.py writes godot_hook.glsl and builds this file round it), says the sky's colour each way from the eye, in light's own
// units; the engine's sun is their first light. Its textures are set 3's (gfx_shd.inc). ⛔ GODOT_HOOK_ABI as mesh.frag's: change both with what a
// cooked sky shares with this file (the view block, what comes in from post_vert), and gfx.h's GFX_SHADER_ABI with them
#define GODOT_HOOK_ABI 1
#include "godot_hook.glsl"
#endif
void main() {
    vec4 q = V.inv * vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, 1.0, 1.0);
    vec3 d = normalize(q.xyz / q.w - V.eye.xyz);
#ifdef GODOT_SKY_HOOK
    { vec3 gc = max(gdSky(d), vec3(0.0)); o = vec4(LINEAR == 2 ? gc : pow(gc, vec3(1.0 / 2.2)), 1.0); return; }
#endif
    float t = clamp(d.y, 0.0, 1.0);
    vec3 c = mix(V.skyH.rgb, V.skyZ.rgb, pow(t, 0.55));
    float sd = max(dot(d, normalize(V.sunDir.xyz)), 0.0), si = V.skyS.w;
    if (MODE == 2 && d.y > 0.01) {
        vec2 p = d.xz / max(d.y, 0.08);
        float T = V.misc.w;
        float c1 = fbm(p * 0.42 + vec2(T * 0.0075, T * 0.0030));
        float c2 = fbm(p * 1.05 + vec2(-T * 0.0130, T * 0.0068));
        float cl = clamp((c1 * 0.70 + c2 * 0.40 - 0.48) * 2.1, 0.0, 1.0) * smoothstep(0.015, 0.34, d.y);
        vec3 cc = mix(vec3(0.95, 0.95, 0.97), V.skyS.rgb * 1.06, 0.40 * pow(sd, 3.0)) * (0.72 + 0.42 * si);
        c = mix(c, cc, cl * 0.74);
    }
    c += V.skyS.rgb * si * (pow(sd, 1200.0) * 9.0 + pow(sd, 26.0) * 0.42 + pow(sd, 4.0) * 0.10);
    c = mix(V.skyG.rgb, c, smoothstep(-0.10, 0.015, d.y));
    o = vec4(LINEAR == 2 ? pow(max(c, vec3(0.0)), vec3(2.2)) : c, 1.0);
}
