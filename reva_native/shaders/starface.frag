#version 450
// The wedge cut out of a star, as a diagram: the core seething, a photon's pinball walk through the radiative zone, the convection zone's rolling cells, and ink between the layers.
#extension GL_GOOGLE_include_directive : enable
#include "star.glsl"
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec3 vQ;
layout(location = 1) in vec3 vW;
layout(location = 0) out vec4 o;
void main() {
    const float uTime = P.s.w, uExpo = P.t.z;
    float r = length(vQ), hs = length(vQ.xz), th = atan(vQ.y, hs + 1e-6);
    vec3 col;
    if (r < 0.25) {
        float n = vn(vQ * 14.0 + vec3(0.0, 0.0, uTime * 0.9)) * 0.6 + vn(vQ * 31.0 - vec3(uTime * 1.3)) * 0.4;
        col = mix(vec3(1.0, 0.94, 0.78), vec3(1.0, 1.0, 0.97), n) * (1.05 - 0.25 * r / 0.25);
    } else if (r < 0.8) {
        float u = (r - 0.25) / 0.55; col = mix(vec3(1.0, 0.86, 0.52), vec3(1.0, 0.62, 0.22), u) * (0.92 + 0.08 * vn(vQ * 9.0 + vec3(uTime * 0.2)));
        vec2 g = vec2(hs, vQ.y) * 70.0, c = floor(g); float tick = floor(uTime * 7.0);
        vec3 hp = h3(vec3(c, tick)); vec2 sp = c + 0.15 + 0.7 * hp.xy;
        float dot1 = 1.0 - smoothstep(0.10, 0.22, length(g - sp));
        col = mix(col, vec3(1.0, 1.0, 0.95), dot1 * step(0.55, hp.z));
    } else {
        float v = (r - 0.8) / 0.2, u = (th + 1.5707963) / 3.1415926 * 15.0 + 0.45 * sin(th * 5.0 + 1.3) + 0.25 * sin(th * 11.0 + uTime * 0.05), ci = floor(u), fu = fract(u);
        float sgn = mod(ci, 2.0) < 1.0 ? 1.0 : -1.0;
        float psi = sin(3.1415926 * fu) * sin(3.1415926 * v);
        float band = 0.5 + 0.5 * sin(6.2831853 * (psi * 3.0 - sgn * uTime * 0.35 + 0.15 * vn(vec3(fu * 4.0, v * 4.0, ci + uTime * 0.1))));
        float heat = 0.5 + 0.5 * sgn * cos(3.1415926 * fu);
        col = mix(vec3(0.86, 0.30, 0.08), vec3(1.0, 0.78, 0.32), heat) * (0.84 + 0.16 * band);
    }
    float ink = min(min(abs(r - 0.25), abs(r - 0.8)), abs(r - 1.0));
    col *= mix(0.25, 1.0, smoothstep(0.003, 0.008, ink));
    col *= uExpo;
    if (LINEAR == 2) col = pow(max(col, vec3(0.0)), vec3(2.2));
    o = vec4(col, 1.0);
}
