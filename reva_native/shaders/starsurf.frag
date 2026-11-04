#version 450
// A star's surface: boiling granules at its temperature, sunspots a couple of thousand Kelvin cooler, a darker and redder limb, and the poles turning slower than the equator.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
#include "star.glsl"
layout(constant_id = 3) const int LINEAR = 0;
layout(std430, set = 0, binding = 2) readonly buffer LV { vec4 v[]; } LVB;
layout(location = 0) in vec3 vQ;
layout(location = 1) in vec3 vW;
layout(location = 0) out vec4 o;
float fil(vec3 p, vec3 S) { vec3 up = abs(S.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0); vec3 e1 = normalize(cross(S, up)), e2 = cross(S, e1);
    vec3 t = p - S * dot(p, S); return 0.5 + 0.5 * sin(44.0 * atan(dot(t, e2), dot(t, e1) + 1e-6)); }
void main() {
    const float uTime = P.s.w, uTemp = P.t.x, uGrade = P.t.y, uExpo = P.t.z, uRot = P.t.w;
    vec3 q = normalize(vQ);
    if (P.k.y > 0.0) { float d = mod(atan(q.x, q.z) - P.k.x + 12.5663706, 6.2831853); if (d < P.k.y) discard; }
    float ang = uTime * uRot * (1.0 - 0.3 * q.y * q.y), ca = cos(ang), sa = sin(ang);
    vec3 p = vec3(ca * q.x - sa * q.z, q.y, sa * q.x + ca * q.z);
    vec2 w = worley(p * 40.0, uTime), w2 = worley(p * 115.0 + vec3(3.1), uTime * 1.4);
    float lane = smoothstep(0.015, 0.13, w.y - w.x), lane2 = smoothstep(0.02, 0.12, w2.y - w2.x), body = pow(1.0 - smoothstep(0.0, 0.8, w.x), 1.5);
    float sg = vn(p * 6.0 + vec3(0.0, uTime * 0.03, 0.0));
    float T = uTemp * (0.91 + 0.17 * body + 0.06 * (sg - 0.5)) - 1000.0 * (1.0 - lane) - 170.0 * (1.0 - lane2);
    const int at = int(P.k.w), ns = int(P.k.z);
    for (int i = 0; i < 16 && i < ns; i++) { vec4 S = LVB.v[at + i];
        float d = acos(clamp(dot(p, S.xyz), -1.0, 1.0)), z = S.w;
        float um = 1.0 - smoothstep(0.30 * z, 0.42 * z, d), pen = 1.0 - smoothstep(0.78 * z, 1.0 * z, d);
        T -= 2000.0 * um + (650.0 + 550.0 * fil(p, S.xyz)) * (pen - um); }
    float mu = clamp(dot(normalize(V.eye.xyz - vW), q), 0.0, 1.0);
    float I = 0.30 + 0.70 * pow(mu, 0.5);
    vec3 col = bb(T * (0.86 + 0.14 * mu) * uGrade) * I * uExpo;
    if (LINEAR == 2) col = pow(max(col, vec3(0.0)), vec3(2.2));
    o = vec4(col, 1.0);
}
