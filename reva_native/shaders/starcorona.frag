#version 450
// The corona, thrillingly diffuse, in streamers. The chromosphere's ring and its spicules, a hair's height above the limb. Added.
#extension GL_GOOGLE_include_directive : enable
#include "star.glsl"
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec2 vC;
layout(location = 0) out vec4 o;
void main() {
    const float uTime = P.s.w, uTemp = P.t.x, uGrade = P.t.y;
    float rho = length(vC); if (rho < 0.97) discard;
    float a = atan(vC.y, vC.x), x = max(rho - 1.0, 0.0);
    float str = 0.55 + 0.45 * vn(vec3(a * 3.0, x * 2.5 - uTime * 0.04, 1.7));
    float cor = exp(-x * 4.2) * 0.55 * str * smoothstep(0.97, 1.0, rho);
    float ring = exp(-x * 90.0) * 0.9;
    float spi = step(0.62, h1(vec3(floor(a * 260.0), 3.0, 0.0))) * (1.0 - smoothstep(0.0, 0.035, x)) * 0.8;
    vec3 col = bb(uTemp * uGrade * 1.15) * cor + vec3(1.0, 0.38, 0.42) * (ring + spi * 0.7);
    if (LINEAR == 2) col = pow(max(col, vec3(0.0)), vec3(2.2));
    o = vec4(col, 1.0);
}
