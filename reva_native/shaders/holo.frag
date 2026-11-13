#version 450
// Her hologram on the dance floor: a solid made of light. Her own texture under a purple cast, drifting scanlines, a hot rim. A depth prepass goes first so she occludes herself and alpha can breathe.
#extension GL_GOOGLE_include_directive : enable
precision mediump float;
layout(constant_id = 3) const int LINEAR = 0;
#include "view.glsl"
layout(location = 0) in vec3 vN;
layout(location = 1) in vec2 vT;
layout(location = 2) in vec3 vW;
layout(location = 0) out vec4 o;
layout(location = 5) flat in vec3 vExtra;   // z: its layer of the texture page
layout(set = 1, binding = 0) uniform sampler2DArray uT;
void main() {
    vec3 N = normalize(vN);
    float fr = pow(1.0 - abs(dot(N, normalize(V.eye.xyz - vW))), 1.5);
    float l = 0.30 + 0.70 * max(0.0, dot(N, vec3(0.3965, 0.7930, 0.5898)));
    float scan = 0.86 + 0.14 * sin(vW.y * 22.0 - V.misc.w * 5.0);
    vec3 rim = mix(vec3(0.48, 0.17, 0.90), vec3(1.0, 0.60, 1.0), fr);
    vec3 tx = texture(uT, vec3(vT, vExtra.z)).rgb;
    if (LINEAR == 2) tx = pow(tx, vec3(1.0 / 2.2));   // the sampler decoded it, and this look is all in gamma
    vec3 c = tx * vec3(0.88, 0.72, 1.14) * (0.44 + 0.80 * l) * scan + rim * fr * 0.80;
    o = vec4(LINEAR == 2 ? pow(max(c, vec3(0.0)), vec3(2.2)) : c, 0.68 + 0.26 * fr);
}
