#version 450
// A skinned mesh. Bones arrive as three vec4 rows each, already multiplied by the mesh's bind matrix, somewhere in this frame's ring: each copy says where its own start. SHADOW=1 is the sun's depth pass.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
#include "inst.glsl"
layout(constant_id = 2) const int SHADOW = 0;
layout(location = 0) in vec3 aP;
layout(location = 1) in vec3 aN;
layout(location = 2) in uvec4 aJ;
layout(location = 3) in vec4 aW;
layout(location = 4) in vec2 aT;
layout(location = 10) in uint iBone;
layout(std430, set = 0, binding = 2) readonly buffer Ring { vec4 b[]; } B;
layout(location = 0) out vec3 vN;
layout(location = 1) out vec2 vT;
layout(location = 2) out vec3 vW;
layout(location = 3) out vec4 vS;
#ifdef MOUTH_HOLE
layout(push_constant) uniform Mouth { vec4 p; vec4 s; } M;   // the lip line at rest and its tilt; the mouth's half-width, the face's height, how far open
layout(location = 6) out vec3 vFace;
#endif
#ifdef INK
layout(push_constant) uniform Face { vec4 p; vec4 s; vec4 t; vec4 i; vec4 a; } M;   // i.w how wide the line is, as a share of the picture's height, a.x the picture's height over its width
#endif
void main() {
#ifdef MOUTH_HOLE
    { vec2 md = aP.xy - M.p.xy; float ct = cos(M.p.w), st = sin(M.p.w); vec2 mr = vec2(md.x * ct + md.y * st, -md.x * st + md.y * ct); vFace = vec3(mr.y / max(1e-5, M.s.x), mr.x / max(1e-5, M.s.x), (aP.z - M.p.z) / max(1e-5, M.s.y)); }
#endif
    ivec4 i = ivec4(aJ) * 3 + int(iBone);
    vec4 a = B.b[i.x] * aW.x + B.b[i.y] * aW.y + B.b[i.z] * aW.z + B.b[i.w] * aW.w;
    vec4 b = B.b[i.x + 1] * aW.x + B.b[i.y + 1] * aW.y + B.b[i.z + 1] * aW.z + B.b[i.w + 1] * aW.w;
    vec4 c = B.b[i.x + 2] * aW.x + B.b[i.y + 2] * aW.y + B.b[i.z + 2] * aW.z + B.b[i.w + 2] * aW.w;
    vec4 p4 = vec4(aP, 1.0), s4 = vec4(dot(a, p4), dot(b, p4), dot(c, p4), 1.0);
    vec4 w = vec4(dot(iR0, s4), dot(iR1, s4), dot(iR2, s4), 1.0);
    gl_Position = (SHADOW == 1 ? V.sunVP : V.vp) * w;
    if (SHADOW == 1) return;
    vec3 n = vec3(dot(a.xyz, aN), dot(b.xyz, aN), dot(c.xyz, aN));
    vN = vec3(dot(iR0.xyz, n), dot(iR1.xyz, n), dot(iR2.xyz, n));
    vT = aT;
    vW = w.xyz;
    vS = V.sunVP * w;
    vTint = iTint;
    vExtra = iExtra;
#ifdef INK
    // the toon pass's second draw: every vertex pushed out along its normal's direction on screen, front faces culled, and the back faces left showing round the silhouette are the line. In clip space, so its width holds at any distance
    { vec2 dd = (V.vp * vec4(normalize(vN), 0.0)).xy; float L = length(dd); if (L > 1e-6) gl_Position.xy += dd / L * M.i.w * 2.0 * vec2(M.a.x, 1.0) * gl_Position.w; }
#endif
}
