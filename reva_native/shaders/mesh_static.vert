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
#ifdef STRAND
const float HAIRPX = 1.0;   // (11-06) a hair's drawn width, at the least: about a pixel, or it falls between them
#endif
#ifdef INK
layout(push_constant) uniform Face { vec4 p; vec4 s; vec4 t; vec4 i; vec4 a; } M;
#endif
#ifdef FUR
layout(push_constant) uniform Face { vec4 p; vec4 s; vec4 t; vec4 i; vec4 a; vec4 m; vec4 m2; vec4 m3; } M;   // m.w how long the fur is, in metres; m2.z how far up it this shell is, 0 at the skin to 1 at the tips; m3.xyz what's pushing it over, in the world
layout(location = 14) in vec3 aRest;   // where this vertex was at rest: its own position, or for cloth that hangs, where it was before the simulation took it
layout(location = 6) out vec3 vRest;
layout(location = 7) out vec3 vFurT;   // which way the strand runs here
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
#ifdef STRAND
    // (11-06) A HAIR, ONE OF A HUNDRED THOUSAND (engine/strands.inc). Its point is the hair's middle here, both corners alike; its normal is the hair's own way, as long
    // as its true half-width; its uv says which side of the hair this corner is (the sign), how light the hair is, and how far along it this is. A hair is far thinner
    // than a pixel, so it's drawn across its way as the lens sees it, about a pixel wide, and as see-through as the share of that pixel it really covers, which goes to
    // the pixels as the length of vN
    {
        vec3 tw = vec3(dot(iR0.xyz, aN), dot(iR1.xyz, aN), dot(iR2.xyz, aN));
        float hw = length(tw); vec3 T = hw > 1e-12 ? tw / hw : vec3(0.0, -1.0, 0.0);
        vec4 c0 = V.vp * w;
        vN = T;
        if (c0.w > 1e-4) {
            vec2 px = max(V.cl3.xy, vec2(1.0)) * 0.5;   // from the view's -1 to 1 to its pixels
            vec4 c1 = V.vp * vec4(w.xyz + T * 0.01, 1.0);
            vec3 B = cross(T, V.eye.xyz - w.xyz); float bl = length(B); B = bl > 1e-9 ? B / bl : vec3(1.0, 0.0, 0.0);
            vec4 c2 = V.vp * vec4(w.xyz + B * hw, 1.0);
            vec2 s0 = c0.xy / c0.w * px, s1 = c1.w > 1e-4 ? c1.xy / c1.w * px : s0 + vec2(0.0, 1.0), s2 = c2.w > 1e-4 ? c2.xy / c2.w * px : s0;
            vec2 dir = s1 - s0; float dl = length(dir); dir = dl > 1e-6 ? dir / dl : vec2(0.0, 1.0);
            float truePx = 2.0 * length(s2 - s0), drawPx = max(truePx, HAIRPX), side = aT.x < 0.0 ? -1.0 : 1.0;
            gl_Position = c0; gl_Position.xy += vec2(-dir.y, dir.x) * (side * 0.5 * drawPx) / px * c0.w;
            vN = T * clamp(truePx / drawPx, 0.0, 1.0);
        }
        vT = vec2(abs(aT.x), aT.y);
    }
#endif
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
        vRest = aRest * (M.m3.w > 0.0 ? M.m3.w : length(vec3(iR0.x, iR1.x, iR2.x)));   // where on the body at rest, in the world's units: a strand keeps its place there. m3.w: the size said outright, for cloth whose positions are the world's already
    }
#endif
#ifdef INK
    { vec2 dd = (V.vp * vec4(normalize(vN), 0.0)).xy; float L = length(dd); if (L > 1e-6) gl_Position.xy += dd / L * M.i.w * 2.0 * vec2(M.a.x, 1.0) * gl_Position.w; }
#endif
}
