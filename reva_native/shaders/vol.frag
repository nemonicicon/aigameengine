#version 450
// Volumetric haze and light shafts, on the PC: a march through the air from the lens to whatever's there, asking the sun's shadow map at each step. Half size, jittered per pixel, and the bloom's blur on the way to the glass hides the grain.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(constant_id = 6) const int CASC = 1;
layout(set = 0, binding = 1) uniform sampler2DShadow uSh;
layout(set = 1, binding = 0) uniform sampler2D uD;
layout(push_constant) uniform PC { mat4 m; vec4 p; } pc;  // m[0]: this view's rectangle in the image. p: x density, y how far it marches
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
float sunAt(vec3 W) {
    if (V.misc.y < 0.5) return 1.0;
    vec4 q; vec2 tile = vec2(0.0); float scale = 1.0;
    if (CASC > 1) {
        float d = dot(W - V.eye.xyz, V.fwd.xyz);
        int i = d < V.split.x ? 0 : d < V.split.y ? 1 : d < V.split.z ? 2 : d < V.split.w ? 3 : 4;
        if (i > 3) return 1.0;
        q = V.casc[i] * vec4(W, 1.0); tile = vec2(float(i & 1), float(i >> 1)) * 0.5; scale = 0.5;
    } else q = V.sunVP * vec4(W, 1.0);
    vec3 s = q.xyz / q.w;
    vec2 uv = vec2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || s.z > 1.0) return 1.0;
    return texture(uSh, vec3(uv * scale + tile, s.z - 0.0008));
}
void main() {
    float z = texture(uD, pc.m[0].xy + vUV * pc.m[0].zw).r;
    vec4 q = V.inv * vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, z, 1.0);
    vec3 P = q.xyz / q.w, rd = P - V.eye.xyz;
    float len = length(rd); rd /= max(len, 1e-4); len = min(len, pc.p.y);
    float j = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float mu = V.sunDir.w > 0.0 ? dot(rd, V.sunDir.xyz) : 0.0, g = 0.55, phase = (1.0 - g * g) / (12.566 * pow(1.0 + g * g - 2.0 * g * mu, 1.5));
    const int N = 18;
    float step = len / float(N), sun = 0.0, air = 0.0;
    for (int i = 0; i < N; i++) {
        vec3 W = V.eye.xyz + rd * (float(i) + j) * step;
        float h = exp(-max(W.y, 0.0) * 0.07) * exp(-pc.p.x * float(i) * step);
        air += h;
        sun += h * sunAt(W);
    }
    vec3 c = (V.sunCol.rgb * V.sunDir.w * sun * phase * 3.0 + V.sky.rgb * air * 0.25) * pc.p.x * step;
    o = vec4(c, 1.0);
}
