#version 450
// A loop of hot gas: three strands wound round an arch, a pulse running along them, brightest where they're seen side on. Added.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(constant_id = 3) const int LINEAR = 0;
layout(push_constant) uniform PC { vec4 c; vec4 s; vec4 t; vec4 k; vec4 r; vec4 u; } P;
layout(location = 0) in vec3 vN;
layout(location = 1) in vec3 vW;
layout(location = 2) in vec2 vS;
layout(location = 0) out vec4 o;
void main() {
    float f = pow(abs(dot(normalize(vN), normalize(V.eye.xyz - vW))), 1.4);
    float pulse = 0.55 + 0.45 * sin(vS.x * 26.0 - P.s.w * 2.4 + P.r.y + vS.y * 2.1);
    vec3 col = mix(vec3(1.0, 0.36, 0.42), vec3(1.0, 0.72, 0.36), 0.5 + 0.5 * sin(vS.x * 3.14159)) * pulse * f * 0.85;
    if (LINEAR == 2) col = pow(max(col, vec3(0.0)), vec3(2.2));
    o = vec4(col, 1.0);
}
