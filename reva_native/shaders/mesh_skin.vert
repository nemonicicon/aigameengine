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
#ifdef FUR
layout(push_constant) uniform Face { vec4 p; vec4 s; vec4 t; vec4 i; vec4 a; vec4 m; vec4 m2; vec4 m3; } M;   // m.w how long the fur is, in metres; m2.z how far up it this shell is, 0 at the skin to 1 at the tips; m3.xyz what's pushing it over, in the world
layout(location = 14) in vec3 aRest;   // where this vertex was at rest: its own position, or for cloth that hangs, where it was before the simulation took it
layout(location = 6) out vec3 vRest;
layout(location = 7) out vec3 vFurT;   // which way the strand runs here
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
#ifdef FUR
    // a shell of fur: the same skin, this far out along a strand. A strand stands along the normal and leans the way it's pushed, more toward its tip, and never gets longer for leaning.
    // It doesn't all move as one: the push comes and goes a little across the body
    {
        float t = M.m2.z, len = M.m.w * 1.75;   // metres to the world's units
        float gust = 1.0 + 0.22 * sin(V.misc.w * 2.7 + dot(w.xyz, vec3(1.9, 0.7, 1.3)));
        float limp = clamp(len / 0.07, 0.25, 1.4);   // a short coat is stiff, and a long one goes where it's pushed (0.07: four centimetres)
        // the coat's lay. Hair doesn't stand on end: it lies along the body, back toward the tail and down, from the root. A short coat lies nearly flat, a long one stands off more,
        // and wool hardly lies at all. Then what's pushing it bends it further, more toward the tip
        vec3 nw = normalize(vN);
        float lay = clamp(1.0 - M.m.w / 0.10, 0.35, 0.90) * (1.0 - 0.75 * clamp(M.m2.w, 0.0, 1.0));
        vec3 comb = M.m3.w > 0.0 ? vec3(0.0, -1.0, 0.0) : -normalize(vec3(iR0.z, iR1.z, iR2.z)) * 0.8 + vec3(0.0, -0.6, 0.0);   // on a garment that hangs it lies downwards; on a body, back and down
        comb -= nw * dot(comb, nw);
        float cl = length(comb); comb = cl > 1e-4 ? comb / cl : vec3(0.0);
        vec3 dir = normalize(nw * (1.0 - 0.85 * lay) + comb * lay + M.m3.xyz * (t * gust * limp));
        vFurT = dir;
        w.xyz += dir * (len * t);
        gl_Position = V.vp * w; vW = w.xyz; vS = V.sunVP * w;
        vRest = aRest * (M.m3.w > 0.0 ? M.m3.w : (length(vec3(a.x, b.x, c.x)) * length(vec3(iR0.x, iR1.x, iR2.x))));   // where on the body at rest, in the world's units: a strand keeps its place there. m3.w: the size said outright, for cloth whose positions are the world's already
    }
#endif
#ifdef INK
    // the toon pass's second draw: every vertex pushed out along its normal's direction on screen, front faces culled, and the back faces left showing round the silhouette are the line. In clip space, so its width holds at any distance
    { vec2 dd = (V.vp * vec4(normalize(vN), 0.0)).xy; float L = length(dd); if (L > 1e-6) gl_Position.xy += dd / L * M.i.w * 2.0 * vec2(M.a.x, 1.0) * gl_Position.w; }
#endif
}
