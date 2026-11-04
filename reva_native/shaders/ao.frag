#version 450
// Ambient occlusion and contact shadows from the depth image, at half size, on the PC. The occlusion is the Alchemy estimator: how much of what's nearby rises above this point's plane. The contact shadow is a short march toward the sun, for the small gaps a shadow map's texels can't see.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(set = 1, binding = 0) uniform sampler2D uD;
layout(push_constant) uniform PC { mat4 m; vec4 p; } pc;  // m[0]: this view's rectangle in the image, x y w h. p: x the radius, y occlusion on, z contact on
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
// ⛔ a sample's depth comes from one texel, so its position has to come from that texel's middle too: otherwise on a steep face it lands off the plane, and a flat wall shadows itself in stripes
vec2 snap(vec2 uv) { return (floor(uv * V.cl3.xy) + 0.5) / V.cl3.xy; }
float depthAt(vec2 uv) { return texture(uD, pc.m[0].xy + uv * pc.m[0].zw).r; }
vec3 worldAt(vec2 uv, float z) { vec4 q = V.inv * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, z, 1.0); return q.xyz / q.w; }
void main() {
    vec2 uv0 = snap(vUV);
    float z = depthAt(uv0);
    if (z >= 0.99999) { o = vec4(1.0); return; }
    vec3 P = worldAt(uv0, z);
    vec3 N = cross(dFdx(P), dFdy(P));
    float nl = dot(N, N);
    if (nl < 1e-12) { o = vec4(1.0); return; }
    N /= sqrt(nl);
    if (dot(N, V.eye.xyz - P) < 0.0) N = -N;
    float dist = dot(P - V.eye.xyz, V.fwd.xyz);
    float a0 = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;
    float ao = 1.0, contact = 1.0;
    if (pc.p.y > 0.5) {
        float rad = pc.p.x, sr = rad * abs(V.vp[1][1]) * 0.5 / max(dist, 0.5), sx = sr * V.cl3.z;
        float sum = 0.0;
        for (int k = 0; k < 10; k++) {
            float r = (float(k) + 0.5) / 10.0, a = a0 + float(k) * 2.3999632;
            vec2 uv = snap(uv0 + vec2(cos(a) * r * sx, sin(a) * r * sr));
            vec3 v = worldAt(uv, depthAt(uv)) - P;
            float vv = dot(v, v);
            if (vv < rad * rad * 4.0) sum += max(0.0, dot(v, N) - 0.006 - 0.002 * dist) / (vv + 0.02);
        }
        ao = clamp(1.0 - sum * 1.1 / 10.0 * rad, 0.0, 1.0);
    }
    if (pc.p.z > 0.5 && V.sunDir.w > 0.0 && dot(N, V.sunDir.xyz) > 0.0) {
        vec3 R = P + N * 0.02 * max(dist * 0.05, 1.0);
        float len = 0.7, step = len / 10.0, t = step * (fract(a0 * 7.13) * 0.8 + 0.2);
        for (int k = 0; k < 10; k++, t += step) {
            vec3 Q = R + V.sunDir.xyz * t;
            vec4 c = V.vp * vec4(Q, 1.0);
            vec2 uv = snap(vec2(c.x / c.w * 0.5 + 0.5, 0.5 - c.y / c.w * 0.5));
            if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
            float zs = depthAt(uv);
            if (zs >= c.z / c.w) continue;
            float back = dot(Q - worldAt(uv, zs), -V.fwd.xyz) * -1.0;   // how far the ray is behind what the depth image holds
            if (back > 0.02 + 0.004 * dist && back < 0.35) { contact = 0.35 + 0.65 * t / len; break; }
        }
    }
    o = vec4(ao, contact, 0.0, 1.0);
}
