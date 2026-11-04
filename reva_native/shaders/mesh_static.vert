#version 450
// A mesh with no bones: props, the street. Its model matrix already includes the bind matrix. SHADOW=1 is the sun's depth pass.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
#include "inst.glsl"
layout(constant_id = 2) const int SHADOW = 0;
layout(location = 0) in vec3 aP;
layout(location = 1) in vec3 aN;
layout(location = 4) in vec2 aT;
layout(location = 11) in vec3 iN0;   // what turns its normals: its own rows, unless it's stretched
layout(location = 12) in vec3 iN1;
layout(location = 13) in vec3 iN2;
layout(location = 0) out vec3 vN;
layout(location = 1) out vec2 vT;
layout(location = 2) out vec3 vW;
layout(location = 3) out vec4 vS;
#ifdef INK
layout(push_constant) uniform Face { vec4 p; vec4 s; vec4 t; vec4 i; vec4 a; } M;
#endif
void main() {
    vec4 p4 = vec4(aP, 1.0);
    vec4 w = vec4(dot(iR0, p4), dot(iR1, p4), dot(iR2, p4), 1.0);
    gl_Position = (SHADOW == 1 ? V.sunVP : V.vp) * w;
    if (SHADOW == 1) return;
    vN = vec3(dot(iN0, aN), dot(iN1, aN), dot(iN2, aN));
    vT = aT;
    vW = w.xyz;
    vS = V.sunVP * w;
    vTint = iTint;
    vExtra = iExtra;
#ifdef INK
    { vec2 dd = (V.vp * vec4(normalize(vN), 0.0)).xy; float L = length(dd); if (L > 1e-6) gl_Position.xy += dd / L * M.i.w * 2.0 * vec2(M.a.x, 1.0) * gl_Position.w; }
#endif
}
