#version 450
// Each pixel adds up the density along its own ray, in steps sized to the plume where they are, so it reads as a volume from any side, and a lens looking down it sees it gather into its vanishing point. Added, after everything solid.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(push_constant) uniform PC { vec4 a; vec4 u; vec4 v; vec4 w; vec4 r; vec4 c; } P;
layout(location = 0) in vec3 vW;
layout(location = 0) out vec4 o;
void main() {
    const vec3 uA = P.a.xyz, uU = P.u.xyz, uR = P.r.xyz; const float uL = P.a.w, uK = P.u.w, uF = P.v.w, uT = P.w.w;
    vec3 D = vW - V.eye.xyz; float tM = length(D); D /= tM;
    vec3 ob = V.eye.xyz - uA; float du = dot(D, uU), ou = dot(ob, uU);
    vec3 dp = D - du * uU, op = ob - ou * uU;
    float a = dot(dp, dp), b = 2.0 * dot(dp, op), c = dot(op, op) - uR.z * uR.z;
    float t0 = 0.0, t1 = tM;
    if (a > 1e-9) { float q = b * b - 4.0 * a * c; if (q <= 0.0) discard; q = sqrt(q); t0 = max(t0, (-b - q) / (2.0 * a)); t1 = min(t1, (-b + q) / (2.0 * a)); }
    else if (c > 0.0) discard;
    if (abs(du) > 1e-6) { float ta = -ou / du, tb = (uL - ou) / du; t0 = max(t0, min(ta, tb)); t1 = min(t1, max(ta, tb)); }
    else if (ou < 0.0 || ou > uL) discard;
    if (t1 <= t0) discard;
    float t = t0, acc = 0.0;
    for (int i = 0; i < 128; i++) { if (t >= t1) break;
        vec3 q = ob + D * t; float s = dot(q, uU); float r = mix(uR.x, uR.y, clamp(s / uL, 0.0, 1.0)); float d = length(q - s * uU);
        float h = min(max(0.4 * r, 0.75 * (d - 2.6 * r)), t1 - t);
        q = ob + D * (t + 0.5 * h); s = dot(q, uU); vec3 rr = q - s * uU; float x = clamp(s / uL, 0.0, 1.0); r = mix(uR.x, uR.y, x);
        acc += exp(-dot(rr, rr) / (r * r)) * pow(uR.x / r, uT) * pow(1.0 - x, uF) * smoothstep(0.0, 0.004, x) * h;
        t += h; }
    o = vec4(P.c.rgb * (1.0 - exp(-uK * acc)), 1.0);
}
