#version 450
// The world's one fragment shader, kept to the budget. On the tube a pixel's cost is the biggest lever in the frame (engine plan), so this is a texture, the sky, the sun and
// a shadow lookup, and nothing else until a setting asks for it. TAPS is the shadow's tap count: 1 on the Pi (the sampler's own bilinear compare), 4 on the PC.
// ⛔ No local array walked in a loop on V3D: it goes to scratch memory (59 ms against 5 for one full-screen pass, measured).
#extension GL_GOOGLE_include_directive : enable
precision mediump float;
#ifdef EARLY_Z
// (11-07) tested against the depth before it's shaded. The Pi's driver (Mesa 25.0.7's V3D compiler) does that by itself only for a shader that names no storage buffer, and
// this one names the clustered lights' two whether or not lights are on, so it shaded every pixel first and then threw away those behind. Never for a pipeline that discards
// (CUTOUT, the mouth's hole, fur, a hair): tested early, a pixel it throws away would still have written its depth
layout(early_fragment_tests) in;
#endif
#include "view.glsl"
layout(constant_id = 0) const int TAPS = 1;
layout(constant_id = 1) const int CUTOUT = 0;
layout(constant_id = 3) const int LINEAR = 0;   // 0 lighting on gamma-encoded values, as the Pi does. 1 this shader decodes and encodes, on an 8-bit target. 2 the sampler decodes, and the target holds the light. 3 the sampler decodes, and this shader encodes for an 8-bit picture drawn off-screen
layout(location = 0) in vec3 vN;
layout(location = 1) in vec2 vT;
layout(location = 2) in vec3 vW;
layout(location = 3) in vec4 vS;
layout(constant_id = 4) const int HEMI = 0;     // the sky as a hemisphere, with a ground under it
layout(constant_id = 5) const int LIGHTS = 0;   // point and spot lights, clustered
layout(constant_id = 6) const int CASC = 1;     // sun cascades. 1 is the one map
layout(constant_id = 7) const int PROBES = 0;   // baked light from a grid over the set
// what a surface is made of (queue step 22). All 0, it's its colour under the light, as it always was
layout(constant_id = 8) const int SPEC = 0;      // highlights from the sun and the lights, by how rough and how much a metal it is
layout(constant_id = 9) const int REFLECT = 0;   // what it reflects from around it: 1 the sky and the ground, 2 the set's baked surroundings as well
layout(constant_id = 10) const int NORMALS = 0;  // its normal map
layout(constant_id = 11) const int DETAIL = 0;   // fine grain up close
layout(constant_id = 12) const int PARALLAX = 0; // its normal map's height shifts its texture with the angle of view
// a body's surfaces, by kind (queue step 23). Realistic bodies only: the toon pass is its own look
layout(constant_id = 13) const int SKIN = 0;     // light wraps round skin, further in red
layout(constant_id = 14) const int HAIR = 0;     // hair's long highlight, along the strand
layout(constant_id = 15) const int EYES = 0;     // an eye is wet: a sharp highlight and a catch light
layout(constant_id = 16) const int SHEEN = 0;    // cloth's soft glow at its edges
layout(location = 0) out vec4 o;
layout(set = 0, binding = 1) uniform sampler2DShadow uSh;
layout(std430, set = 0, binding = 2) readonly buffer LV { vec4 v[]; } LVB;
layout(std430, set = 0, binding = 3) readonly buffer LU { uint u[]; } LUB;
layout(set = 0, binding = 4) uniform sampler2DShadow uAtlas;   // the lights' own shadow maps, 4 x 4 tiles
layout(set = 0, binding = 5) uniform sampler3D uProbe;
layout(set = 0, binding = 6) uniform samplerCube uEnv;    // the set's surroundings: red how open the sky is that way, green whether the sun reaches what's there
layout(set = 0, binding = 7) uniform sampler3D uSkin;     // skin, worked out ahead: across, the angle to the light; down, how tightly the surface curves. One slice deep, and square roots of what leaves the skin
layout(set = 1, binding = 0) uniform sampler2DArray uT;   // a page of textures, or one alone
layout(set = 2, binding = 0) uniform sampler2DArray uN;   // the mesh's normal map, on its page. Its alpha is its height
layout(location = 4) flat in vec4 vTint;    // w: how much of the texture to take over the tint
layout(location = 5) flat in vec3 vExtra;   // x the mesh's own alpha, y it glows, z its layer of the page
// the lip line and its tilt; the mouth's half-width, the face's height, how far open, 1 for a room; the toon's cut, its rim, the key from the lens, a room's gradient; the ink.
// Then what the mesh is made of: m is how rough, how much a metal, how much fine detail, how deep; m2.x is its normal map's layer, under 0 for none, and m2.y its kind:
// 1 skin, 2 hair, 3 an eye, 4 cloth, 5 its texture's alpha says, pixel by pixel. m3 is its eyes' glow, a colour and how much
layout(push_constant) uniform Face { vec4 p; vec4 s; vec4 t; vec4 i; vec4 a; vec4 m; vec4 m2; vec4 m3; } M;
#ifdef MOUTH_HOLE
layout(location = 6) in vec3 vFace;
#endif
#ifdef FUR
layout(location = 6) in vec3 vRest;   // a shell of fur (queue step 23): where on the body at rest this pixel is, in the world's units
layout(location = 7) in vec3 vFurT;   // and which way its strand runs
#endif
// the PC's sun: which cascade this pixel is in, and a soft lookup turned a little per pixel so its steps become grain and not bands
float sunCascades(float nl) {
    float d = dot(vW - V.eye.xyz, V.fwd.xyz);
    int i = d < V.split.x ? 0 : d < V.split.y ? 1 : d < V.split.z ? 2 : d < V.split.w ? 3 : 4;
    if (i > 3) return 1.0;
    vec4 q = V.casc[i] * vec4(vW, 1.0);
    vec3 s = q.xyz / q.w;
    float b = (0.0003 + 0.0010 * (1.0 - nl)) * (1.0 + float(i) * 1.5);
    vec2 tile = vec2(float(i & 1), float(i >> 1)) * 0.5;
    vec2 uv = vec2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    float e = V.misc.z * 5.0;
    float a = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;
    vec2 r0 = vec2(cos(a), sin(a)) * e, r1 = vec2(-r0.y, r0.x);
    float sum = 0.0;
    for (int k = 0; k < 8; k++) {
        float g = float(k) * 2.3999632, m = sqrt((float(k) + 0.5) / 8.0);
        vec2 o2 = (r0 * cos(g) + r1 * sin(g)) * m;
        vec2 c = clamp(uv * 0.5 + o2, vec2(V.misc.z * 2.0), vec2(0.5 - V.misc.z * 2.0)) + tile;
        sum += texture(uSh, vec3(c, s.z - b));
    }
    return sum / 8.0;
}
// one tile of the atlas, four taps
float lightShadow(int tile, vec3 W) {
    uint m = uint(V.cl3.w) + uint(tile) * 4u;
    vec4 q = mat4(LVB.v[m], LVB.v[m + 1u], LVB.v[m + 2u], LVB.v[m + 3u]) * vec4(W, 1.0);
    vec3 s = q.xyz / q.w;
    if (s.z >= 1.0 || q.w <= 0.0) return 1.0;
    vec2 uv = clamp(vec2(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5), vec2(0.003), vec2(0.997)), a = (uv + vec2(float(tile % 4), float(tile / 4))) * 0.25;
    float e = 0.00018, z = s.z - 0.0012;
    return 0.25 * (texture(uAtlas, vec3(a + vec2(-e, -e), z)) + texture(uAtlas, vec3(a + vec2(e, -e), z)) + texture(uAtlas, vec3(a + vec2(-e, e), z)) + texture(uAtlas, vec3(a + vec2(e, e), z)));
}
// a highlight: how much of a light a surface throws back at the eye, by how rough it is. In the light's own units, the way the diffuse is: no pi in it
vec3 ggx(vec3 N, vec3 Vd, vec3 L, float rough, vec3 F0) {
    vec3 H = normalize(Vd + L);
    float a = rough * rough, a2 = a * a;
    float NdH = max(dot(N, H), 0.0), NdL = max(dot(N, L), 0.0), NdV = max(dot(N, Vd), 1e-4), VdH = max(dot(Vd, H), 0.0);
    float d = NdH * NdH * (a2 - 1.0) + 1.0, D = a2 / (d * d);
    float k = (rough + 1.0) * (rough + 1.0) * 0.125, G = 1.0 / (4.0 * (NdV * (1.0 - k) + k) * (NdL * (1.0 - k) + k));
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdH, 5.0);
    return F * (D * G * NdL);
}
// what's around, in a direction: the sky above and the ground below, and where the set's surroundings are baked, only as much sky as is open that way. A rough surface sees a blur of it
vec3 envAt(vec3 R, float rough, vec3 base) {
    vec3 sky = V.sky.rgb, gnd = HEMI == 1 ? V.ground.rgb : V.sky.rgb * 0.35;
    float w = 0.04 + rough * 0.9;
    vec3 c = mix(gnd, sky, smoothstep(-w, w, R.y));
    if (REFLECT == 2 && V.misc2.y > 0.5) {
        vec4 e = textureLod(uEnv, R, rough * V.misc2.z);
        vec3 there = gnd * 0.8 + V.sunCol.rgb * V.sunDir.w * 0.22 * e.g;   // something's in the way: it's about the ground's colour, brighter where the sun reaches it
        c = mix(there, c, e.r);
    }
    return c;
}
// how much of that a surface throws back, by its angle and how rough (Karis's fit, for a machine with no table)
vec3 envBRDF(vec3 F0, float rough, float NdV) {
    vec4 r = rough * vec4(-1.0, -0.0275, -0.572, 0.022) + vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * NdV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}
float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float grain(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f); return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x), mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y); }
#ifndef TOON
// what kind of surface a pixel of a one-mesh person is, from its texture's alpha: read from the one texel, never filtered, because half way between two kinds would be a third
int alphaKind(vec2 uv) {
    float a = texelFetch(uT, ivec3(ivec2(fract(uv) * vec2(textureSize(uT, 0).xy)), int(vExtra.z + 0.5)), 0).a;
    return a > 0.95 ? 0 : a > 0.85 ? 1 : a > 0.75 ? 2 : a > 0.65 ? 3 : a > 0.55 ? 4 : 0;
}
// skin: light that goes into flesh comes out a little further on, and further in red than in blue. How much leaves it at an angle to the light, on a surface curved this tightly,
// is worked out ahead from skin's measured profile (gfx_vk.c, skinLutMake): on a flat cheek it's the plain falloff, and round a nose or a finger the edge of the light goes soft and red
vec3 skinLit(float ndl, float curv) { vec3 q = textureLod(uSkin, vec3((ndl * 0.5 + 0.5) * (63.0 / 64.0) + 0.5 / 64.0, clamp(curv, 0.0, 1.0) * (31.0 / 32.0) + 0.5 / 32.0, 0.5), 0.0).rgb; return q * q; }
#endif
// the lights near this pixel, from the cell it's in. With highlights on, sp gets what they throw back at the eye. skinC is how tightly skin curves here, and under 0 where it isn't skin
vec3 lightsHere(vec3 N, vec3 Vd, float rough, vec3 F0, float skinC, out vec3 sp) {
    sp = vec3(0.0);
    vec2 sc = (gl_FragCoord.xy - V.cl2.zw) / V.cl3.xy;
    float vz = dot(vW - V.eye.xyz, V.fwd.xyz);
    uint cx = uint(clamp(sc.x * V.cl.y, 0.0, V.cl.y - 1.0)), cy = uint(clamp(sc.y * V.cl.z, 0.0, V.cl.z - 1.0));
    uint sl = uint(clamp(log(max(vz, 0.01) / V.cl2.y) * V.cl2.x, 0.0, V.cl.w - 1.0));
    uint e = LUB.u[uint(V.cl.x) + cx + uint(V.cl.y) * (cy + uint(V.cl.z) * sl)];
    uint off = e >> 8, n = e & 255u;
    vec3 li = vec3(0.0);
    for (uint i = 0u; i < n; i++) {
        uint r = LUB.u[off + i];
        vec4 a = LVB.v[r], b = LVB.v[r + 1u], c = LVB.v[r + 2u], g = LVB.v[r + 3u];
        vec3 D = a.xyz - vW; float dd = dot(D, D);
        float win = 1.0;   // (11-11) a light with a reach (g.z) fades smoothly to nothing there
        if (g.z > 0.0) { float q = dd / (g.z * g.z); win = clamp(1.0 - q * q, 0.0, 1.0); win *= win; if (win <= 0.0) continue; }
        vec3 L = D * inversesqrt(dd + 0.001);
        float f = max(0.0, dot(N, L)) / (1.0 + dd * a.w) * win;
        if (c.w > -1.5) f *= smoothstep(c.w, b.w, dot(-L, c.xyz));
#ifndef TOON
        if (SKIN == 1 && skinC >= 0.0) {   // skin takes a light a little past its edge: how much light is here, then what the skin does with it
            float at = win / (1.0 + dd * a.w);
            if (c.w > -1.5) at *= smoothstep(c.w, b.w, dot(-L, c.xyz));
            if (g.x >= 0.0 && at > 0.0) {
                int tile = int(g.x);
                if (g.y > 0.5) { vec3 d3 = vW - a.xyz, ad = abs(d3); tile += ad.x > ad.y && ad.x > ad.z ? (d3.x > 0.0 ? 0 : 1) : ad.y > ad.z ? (d3.y > 0.0 ? 2 : 3) : (d3.z > 0.0 ? 4 : 5); }
                at *= lightShadow(tile, vW + N * 0.03);
            }
            li += b.rgb * skinLit(dot(N, L), skinC) * at;
            if (SPEC == 1 && f > 0.0) sp += b.rgb * at * ggx(N, Vd, L, rough, F0);
            continue;
        }
#endif
        if (g.x >= 0.0 && f > 0.0) {
            int tile = int(g.x);
            if (g.y > 0.5) { vec3 d3 = vW - a.xyz, ad = abs(d3); tile += ad.x > ad.y && ad.x > ad.z ? (d3.x > 0.0 ? 0 : 1) : ad.y > ad.z ? (d3.y > 0.0 ? 2 : 3) : (d3.z > 0.0 ? 4 : 5); }
            f *= lightShadow(tile, vW + N * 0.03);
        }
        li += b.rgb * f;
        if (SPEC == 1 && f > 0.0) sp += b.rgb * (f / max(dot(N, L), 1e-4)) * ggx(N, Vd, L, rough, F0);
    }
    return li;
}
#ifdef GODOT_HOOK
// (11-12) A SURFACE'S OWN SHADER (the Godot proof of concept's step 12: their Hedge Maze draws its hedges and ground with shaders of its own). A game's
// surface code, translated at its cook from the shader its maker wrote (skate_src/godot/godot_shader.py writes godot_hook.glsl and builds this file round
// it), says what each pixel is made of: its colour, its normal, how rough, how much a metal, how much of the room it sees, what it gives off. Everything
// else is this shader's own, so the surface is lit as the world is. Its textures are set 3's (gfx_shd.inc).
// ⛔ GODOT_HOOK_ABI is what a cooked shader was built against, and gfx.h's GFX_SHADER_ABI is the engine's: change both with anything a cooked shader
// shares with this file (its sets and bindings, the push constants, what comes in from the vertex shader), or old cooks draw wrong instead of being refused
#define GODOT_HOOK_ABI 1
struct GdSurf { vec3 albedo; vec3 normal; float rough, metal, spec, ao; vec3 emit; };
#include "godot_hook.glsl"
#endif
void main() {
#ifdef FUR
    // A strand of fur. The body at rest is cut into cells, across whichever way the surface faces most, with one strand to a cell: each its own length and a little off its cell's
    // middle, thinner toward its tip. A pixel that's off its strand, or above its tip, isn't there. Far off, the cells double in size rather than fall under a pixel, where they'd only sparkle
    float furH = M.m2.z;   // how far up the fur this shell is
    {
        vec3 an = abs(normalize(cross(dFdx(vRest), dFdy(vRest))));
        vec2 q = an.y > an.x && an.y > an.z ? vRest.xz : an.x > an.z ? vRest.zy : vRest.xy;
        float cell = mix(0.0075, 0.0016, clamp(M.m.z, 0.0, 1.0));
        cell *= exp2(max(0.0, ceil(log2(max(length(fwidth(vW)) * 1.3 / cell, 1e-6)))));
        vec2 g = q / cell, id = floor(g);
        float tall = 0.40 + 0.60 * fract(sin(dot(id, vec2(127.1, 311.7))) * 43758.5453);
        vec2 c2 = fract(g) - 0.5 - (vec2(fract(sin(dot(id + 17.3, vec2(127.1, 311.7))) * 43758.5453), fract(sin(dot(id + 41.7, vec2(127.1, 311.7))) * 43758.5453)) - 0.5) * 0.45;
        // how curly: a straight hair stands on its cell, and wool winds round as it rises, each strand from its own start, and stays thick to its tip, so it reads as tufts
        float curl = clamp(M.m2.w, 0.0, 1.0), turn = furH * (2.0 + 9.0 * curl) + tall * 39.0;
        c2 -= vec2(cos(turn), sin(turn)) * (0.30 * curl * furH);
        if (furH > tall || length(c2) * 2.0 > 0.95 * (1.0 - (0.72 - 0.40 * curl) * furH / tall)) discard;
    }
#endif
    vec2 uv = vT;
    vec3 N = normalize(vN);
#ifdef STRAND
    // (11-06) a hair (engine/strands.inc, mesh_static.vert's STRAND): its own way, how much of this pixel it covers, and it faces the lens across that way
    float hairCov = length(vN); vec3 hairT = hairCov > 1e-6 ? vN / hairCov : vec3(0.0, -1.0, 0.0); hairCov = min(hairCov, 1.0);
    { vec3 Ve = normalize(V.eye.xyz - vW), Nc = Ve - hairT * dot(Ve, hairT); float nc = length(Nc); N = nc > 1e-4 ? Nc / nc : Ve; }
#endif
#ifndef TOON
    // a normal map needs the surface's own axes: worked out here from how its position and its texture change across the pixel, so no mesh has to carry tangents
    mat3 TBN = mat3(1.0); bool mapped = false;
    if ((NORMALS == 1 || PARALLAX == 1) && M.m2.x >= 0.0) {
        vec3 dp1 = dFdx(vW), dp2 = dFdy(vW); vec2 du1 = dFdx(vT), du2 = dFdy(vT);
        vec3 dp2p = cross(dp2, N), dp1p = cross(N, dp1);
        vec3 T = dp2p * du1.x + dp1p * du2.x, B = dp2p * du1.y + dp1p * du2.y;
        float im = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-12));
        TBN = mat3(T * im, B * im, N); mapped = true;
        if (PARALLAX == 1 && M.m.w > 0.0) {   // its height, read once: the texture slides the way the eye's looking, further where it's deep
            vec3 Vt = normalize(V.eye.xyz - vW) * TBN;
            uv = vT + Vt.xy / max(Vt.z, 0.3) * (texture(uN, vec3(vT, M.m2.x)).a - 0.5) * M.m.w;
        }
    }
#endif
    vec4 t = texture(uT, vec3(uv, vExtra.z));
    if (CUTOUT == 1 && vTint.w >= 0.5 && t.a < 0.5) discard;
    // (11-11) THE INSIDE OF A MOUTH, and anything whose material row has flag 8 (GFX_OCCLUSION, vExtra.y 2): its texture's alpha, from a half up, is how
    // much of the room each point of it sees (rays cast against her head at the mouths her clips hold), and its colour is the
    // paint as bought. That share comes off the light from the room, the hemisphere's and what the surface reflects of it, and never off the paint: paint
    // darkened under a reflection left at full strength is what metal looks like (her side teeth read as braces and her tongue as a pale tray, chorus 3b)
    const bool aoA = vExtra.y > 1.5;
    const float ao = aoA ? clamp((t.a * 255.0 - 128.0) / 127.0, 0.0, 1.0) : 1.0;
#ifndef TOON
    if (NORMALS == 1 && mapped) { vec3 nm = texture(uN, vec3(uv, M.m2.x)).xyz * 2.0 - 1.0; N = normalize(TBN * nm); }
#endif
    vec3 tx = t.rgb, tn = vTint.rgb;
#ifdef TOON
    // the show's palette is hand-tuned as it's stored and against a light capped at 1, so it's worked out there on every machine, and what tonemaps the rest stands off it
    if (LINEAR == 2) tx = pow(tx, vec3(1.0 / 2.2));
#else
    if (LINEAR == 1) tx = pow(tx, vec3(2.2));
    if (LINEAR != 0) tn = pow(tn, vec3(2.2));
#endif
    vec3 base = mix(tn, tx, vTint.w);
#ifdef GODOT_HOOK
    GdSurf gd = gdSurface(uv, N);   // (11-12) the surface's own code: its colour is in light's own units, and the Pi's road lights what's stored
    base = LINEAR != 0 ? max(gd.albedo, vec3(0.0)) : pow(max(gd.albedo, vec3(0.0)), vec3(1.0 / 2.2));
    N = normalize(gd.normal);
#endif
#ifdef STRAND
    base *= vT.x * mix(0.55, 1.0, smoothstep(0.0, 0.22, vT.y));   // this hair's own shade, and darker toward its root, as her hair's picture was
#endif
#ifdef FUR
    base *= mix(0.50, 1.0, furH) * (1.0 + 0.16 * furH * furH);   // down among the roots the strands shade one another, and the tips are a little bleached
#endif
#ifdef MOUTH_HOLE
    // A lip line modelled shut has nowhere to open, so a hole's cut between the lips, by where the skin started out: their own lips then part by themselves, because the lower face is skinned to the jaw. Only an open mouth draws through this
    if (vFace.z > -0.12 && abs(vFace.y) < 1.6 && abs(vFace.x) < 1.4) {
        float aH = M.s.z * 0.22, aW = 0.78 * mix(0.52, 1.0, clamp(M.s.z * 1.25, 0.0, 1.0));
        float ax = vFace.y / max(1e-4, aW), ay = (vFace.x + aH * 0.92) / max(1e-4, aH * 1.06), rr = sqrt(ax * ax + ay * ay);
        if (rr < 1.0) discard;
        base *= mix(0.72, 1.0, smoothstep(1.0, 1.18, rr));
    }
#endif
    // (11-09) KALEIDOSCOPE (a pack's HairKaleido node, engine/body.inc vkMesh): the colour's hue turned about the grey axis by a phase that runs with
    // time (M.m2.z, 0 none) and by where the pixel is, so bands of every colour flow through her hair
    if (M.m2.z != 0.0) { float ka = M.m2.z + dot(vW, vec3(4.0, 6.5, 3.0)), kc = cos(ka), ks = sin(ka); vec3 kk = vec3(0.57735027);
        base = max(base * kc + cross(kk, base) * ks + kk * dot(kk, base) * (1.0 - kc), vec3(0.0)); }
    vec3 lit;
    vec3 amb = HEMI == 1 ? mix(V.ground.rgb, V.sky.rgb, N.y * 0.5 + 0.5) : V.sky.rgb;
    // ...and what a hollow gives back of that light, by its own colour (Jimenez, Wu, Pesce & Jarabo 2016, "Practical Real-Time Strategies for Accurate
    // Indirect Occlusion": their fit to ray-traced bounces), so a tooth's cavity stays a dimmer ivory and a tongue's goes a deeper red, where a share alone greys both
    vec3 aoD = vec3(ao);
    if (aoA) { vec3 ka = 2.0404 * base - 0.3324, kb = -4.7951 * base + 0.6417, kc = 2.7552 * base + 0.6903; aoD = max(vec3(ao), ((ao * ka + kb) * ao + kc) * ao); }
    amb *= aoD;
#ifdef GODOT_HOOK
    amb *= gd.ao;
#endif
    vec3 extra = vec3(0.0), spec = vec3(0.0);
    // what it's made of: how rough, how much a metal. A metal has no colour of its own under the light: what it reflects is tinted by it
    float rough = 1.0, metal = 0.0, NdV = 1.0; vec3 F0 = vec3(0.04), Vd = vec3(0.0, 1.0, 0.0);
#ifndef TOON
    if (DETAIL == 1 && M.m.z > 0.0) {   // fine grain up close, laid on the world so its size doesn't depend on a mesh's UVs
        float k = M.m.z * clamp(1.0 - distance(vW, V.eye.xyz) / 16.0, 0.0, 1.0);
        if (k > 0.0) { vec3 an = abs(normalize(vN)); vec2 p = (an.y > an.x && an.y > an.z ? vW.xz : an.x > an.z ? vW.zy : vW.xy) * 7.0; base *= 1.0 + (grain(p) * 0.6 + grain(p * 3.1) * 0.4 - 0.5) * 0.45 * k; }
    }
    // which kind of surface this pixel is: the mesh's own, or painted into its texture's alpha, which is how one mesh is a whole person
    int kind = 0; float skinC = -1.0, curv0 = 0.0;
    // how tightly the surface curves, in the table's units: how fast its normal turns over a millimetre (a club unit is 571 of them), against the table's tightest, a 3 mm radius.
    // Worked out for every pixel, skin or not, because a rate of change asked for inside a per-pixel choice isn't defined
    if (SKIN == 1) curv0 = clamp(length(fwidth(normalize(vN))) / max(length(fwidth(vW)), 1e-6) * 0.00547, 0.0, 1.0);
    if (SKIN == 1 || HAIR == 1 || EYES == 1 || SHEEN == 1) {
        kind = int(M.m2.y + 0.5);
        if (kind == 5) kind = alphaKind(uv);
    }
    if (SKIN == 1 && kind == 1) skinC = curv0;
#ifdef FUR
    if (HAIR == 1) kind = 2;   // fur takes the light as hair does, each strand standing on the skin
#endif
#ifdef STRAND
    if (HAIR == 1) kind = 2;   // a hair takes the light along its own way
#endif
    const bool mats = SPEC == 1 || REFLECT != 0 || SKIN == 1 || HAIR == 1 || EYES == 1 || SHEEN == 1;
    if (mats) { rough = clamp(M.m.x, 0.045, 1.0); metal = clamp(M.m.y, 0.0, 1.0); F0 = mix(vec3(0.04), base, metal); Vd = normalize(V.eye.xyz - vW); NdV = max(dot(N, Vd), 1e-4); }
    if (EYES == 1 && kind == 3) { rough = 0.07; metal = 0.0; F0 = vec3(0.04); }      // wet
    if (SHEEN == 1 && kind == 4) rough = max(rough, 0.78);                          // cloth scatters: no hard highlight
#ifdef GODOT_HOOK
    if (mats) { rough = clamp(gd.rough, 0.045, 1.0); metal = clamp(gd.metal, 0.0, 1.0); F0 = mix(vec3(0.08 * gd.spec), base, metal); }   // Godot's SPECULAR 0.5 is F0 0.04
#endif
#else
    const int kind = 0; const bool mats = false; const float skinC = -1.0;
#endif
    if (LIGHTS == 1 && V.cl.x > 0.0) extra = lightsHere(N, Vd, rough, F0, skinC, spec);
    if (PROBES == 1 && V.probe.w > 0.5) { vec4 pr = texture(uProbe, (vW + N * 0.5 - V.probe.xyz) * V.probeS.xyz); amb *= pr.r; extra += V.sunCol.rgb * V.sunDir.w * pr.b * 0.2; }
#ifdef TOON
    // the cel ramp: one key light and one shadow map, cut into two tones, lit and a cool heavy shadow, over a pixel's smoothstep so the edge doesn't crawl
    {
        const float room = M.s.w, cel = M.t.x, rim = M.t.y, keyCam = M.t.z, roomG = M.t.w;
        float sh = 1.0;
        if (V.sunDir.w > 0.0 && V.misc.y > 0.5) {
            float nl = max(0.0, dot(N, V.sunDir.xyz));
            if (CASC > 1) sh = sunCascades(nl);
            else { vec3 s = vS.xyz / vS.w; vec3 c = vec3(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5, s.z - 0.0012 - 0.0035 * (1.0 - nl)); if (c.x > 0.0 && c.x < 1.0 && c.y > 0.0 && c.y < 1.0 && c.z < 1.0) sh = texture(uSh, c); }
        }
        // a body's key comes from above and to the right of the lens, as a drawn show keys its characters, and the room's shadows still reach it, as a dim and not a cut
        vec3 Lk = V.sunDir.xyz; float shk = sh;
        if (keyCam > 0.5 && room < 0.5) { vec3 Vk = normalize(V.eye.xyz - vW), Rk = cross(vec3(0.0, 1.0, 0.0), Vk); float rl = length(Rk); Rk = rl > 1e-4 ? Rk / rl : vec3(1.0, 0.0, 0.0); Lk = normalize(Vk * 0.75 + vec3(0.0, 0.80, 0.0) + Rk * 0.30); shk = 1.0; }
        float kd = room > 0.5 ? (0.5 + 0.5 * N.y) * sh : (V.sunDir.w > 0.0 ? dot(N, Lk) * shk : dot(N, vec3(0.3965, 0.7930, 0.5898)));
        float lv = smoothstep(cel - 0.02, cel + 0.02, kd);
        vec3 full = V.sunDir.w > 0.0 ? (V.sky.rgb + V.sunCol.rgb * V.sunDir.w * 0.9) : vec3(0.80);
        full *= min(1.0, 1.0 / max(dot(full, vec3(0.299, 0.587, 0.114)), 1e-3));   // the light's capped at 1, its hue kept: nothing brightens past the palette
        // the darker the colour, the gentler the cut, or a dark skin's shadow goes black
        vec3 sm = mix(vec3(0.74, 0.70, 0.84), vec3(0.50, 0.47, 0.66), smoothstep(0.08, 0.40, dot(base, vec3(0.299, 0.587, 0.114))));
        lit = base * full * mix(sm, vec3(1.0), lv) + base * extra * 0.25;
        if (rim > 0.0 && room < 0.5) { float fr = 1.0 - max(dot(N, normalize(V.eye.xyz - vW)), 0.0); lit += base * full * smoothstep(0.60, 0.68, fr) * (0.35 + 0.65 * lv) * rim; }
        if (keyCam > 0.5 && room < 0.5) lit *= mix(0.80, 1.0, sh);
        if (room > 0.5 && roomG > 0.0) lit *= mix(1.0 - 0.16 * roomG, 1.0 + 0.03 * roomG, clamp(vW.y / 5.2, 0.0, 1.0));   // a room lit from its ceiling
    }
#else
    float shS = 1.0, nlS = 0.0;
    if (V.sunDir.w > 0.0) {
        float nl = max(0.0, dot(N, V.sunDir.xyz));
#ifdef STRAND
        { float tl = dot(hairT, V.sunDir.xyz); vec3 Cm = M.m3.xyz, Nv = vW - vec3(Cm.x, min(vW.y, Cm.y), Cm.z); float nvl = length(Nv); Nv = nvl > 1e-5 ? Nv / nvl : N;
          float w = clamp(dot(Nv, V.sunDir.xyz) * 0.5 + 0.5, 0.0, 1.0);
          nl = sqrt(max(1.0 - tl * tl, 0.0)) * w * w; }   // a hair takes the light across its own way (Kajiya and Kay), and less the further round the hair's mass from the light it is:
                                                         // the mass faces out from her head's middle above it, and from the line straight down through it below
#endif
        float sh = 1.0;
        if (CASC > 1) { if (V.misc.y > 0.5 && nl > 0.0) sh = sunCascades(nl); }
        else if (V.misc.y > 0.5 && nl > 0.0) {
            vec3 s = vS.xyz / vS.w;
            vec3 c = vec3(s.x * 0.5 + 0.5, 0.5 - s.y * 0.5, s.z - 0.0012 - 0.0035 * (1.0 - nl));
            if (c.x > 0.0 && c.x < 1.0 && c.y > 0.0 && c.y < 1.0 && c.z < 1.0) {
                if (TAPS == 1) sh = texture(uSh, c);
                else {
                    float e = V.misc.z;
                    sh = 0.25 * (texture(uSh, c + vec3(-e, -e, 0.0)) + texture(uSh, c + vec3(e, -e, 0.0)) + texture(uSh, c + vec3(-e, e, 0.0)) + texture(uSh, c + vec3(e, e, 0.0)));
                }
            }
        }
        shS = sh; nlS = nl;
        vec3 dnl = vec3(nl);
        if (skinC >= 0.0) dnl = skinLit(dot(N, V.sunDir.xyz), skinC);
        lit = base * (amb + V.sunCol.rgb * V.sunDir.w * dnl * sh + extra);
    } else {
        float l = 0.3 + 0.7 * max(0.0, dot(N, vec3(0.3965, 0.7930, 0.5898)));
        float f = l * 0.42 + 0.13;
        lit = base * ((LINEAR != 0 ? f * f : f) * aoD + extra);
        if (skinC >= 0.0) { vec3 l3 = 0.3 + 0.7 * skinLit(dot(N, vec3(0.3965, 0.7930, 0.5898)), skinC), f3 = l3 * 0.42 + 0.13; lit = base * ((LINEAR != 0 ? f3 * f3 : f3) * aoD + extra); }
    }
    // what the surface throws back: from the sun, or with no sun from the light every unlit scene has, up and to one side
    if (mats) {
        vec3 Ld = V.sunDir.w > 0.0 ? V.sunDir.xyz : vec3(0.3965, 0.7930, 0.5898);
        float nl = V.sunDir.w > 0.0 ? nlS : max(0.0, dot(N, Ld)), sh = shS;
        vec3 sunL = V.sunDir.w > 0.0 ? V.sunCol.rgb * V.sunDir.w * sh : vec3(LINEAR != 0 ? 0.30 : 0.55);
        lit *= 1.0 - metal;
        if (HAIR == 1 && kind == 2) {
            // hair: the highlight runs across the strands, so it's a band. The strand's way is down the surface, and a second, softer band takes the hair's colour
            vec3 T = cross(N, cross(vec3(0.0, 1.0, 0.0), N)); float tl = length(T); T = tl > 1e-4 ? T / tl : vec3(0.0, 1.0, 0.0);
#ifdef FUR
            T = normalize(vFurT);   // a strand of fur runs its own way: along its coat's lay, and wherever it's pushed
#endif
#ifdef STRAND
            T = hairT;   // and a hair its own
#endif
            vec3 H = normalize(Ld + Vd); float th = dot(T, H), s1 = sqrt(max(1.0 - th * th, 0.0)), th2 = dot(normalize(T + N * 0.12), H), s2 = sqrt(max(1.0 - th2 * th2, 0.0));
            spec += sunL * (pow(s1, 90.0) * 0.30 + pow(s2, 18.0) * 0.22 * base * 3.0) * max(nl, 0.15);
        }
        else if (EYES == 1 && kind == 3) { if (nl > 0.0 && sh > 0.0) spec += sunL * ggx(N, Vd, Ld, rough, F0); }
        else if (SPEC == 1 && nl > 0.0 && sh > 0.0) spec += sunL * ggx(N, Vd, Ld, rough, F0);
        if (EYES == 1 && kind == 3) {
            // an eye is wet, so what it throws back of the sun and the lights is tiny and far brighter than anything round it. It's held to what reads as a point of light, or the bloom spreads it over the face
            spec = min(spec, vec3(1.6));
            // and an eye always has a light in it: from above the lens and to its side, a small crisp point, so nobody on screen looks dead
            vec3 Rk = cross(vec3(0.0, 1.0, 0.0), Vd); float rl = length(Rk); Rk = rl > 1e-4 ? Rk / rl : vec3(1.0, 0.0, 0.0);
            vec3 Hc = normalize(Vd + normalize(Vd + vec3(0.0, 0.55, 0.0) + Rk * 0.38));
            spec += vec3(1.15) * smoothstep(0.9860, 0.9935, dot(N, Hc));
        }
        if (SHEEN == 1 && kind == 4) lit += (amb + sunL * max(nl, 0.2)) * mix(vec3(1.0), base, 0.5) * pow(1.0 - NdV, 4.0) * 0.35;   // cloth: its fibres catch the light at the edge
        if (REFLECT != 0 && kind != 2) { float open = (PROBES == 1 && V.probe.w > 0.5) ? texture(uProbe, (vW + N * 0.5 - V.probe.xyz) * V.probeS.xyz).r : 1.0;
            if (aoA) open *= clamp(pow(NdV + ao, exp2(-16.0 * rough - 1.0)) - 1.0 + ao, 0.0, 1.0);   // (11-11) a hollow reflects its own dark walls, not the room (Lagarde & de Rousiers 2014, "Moving Frostbite to PBR", 4.10.2)
            spec += envAt(reflect(-Vd, N), rough, base) * envBRDF(F0, rough, NdV) * open; }
        lit += spec;
    }
    // eyes that glow (a game's E->bodyEyes, film's eyes=), on every machine and whatever the settings. An eye that's a mesh of its own comes here unlit in the glow's colour already.
    // One painted on a person who's a single mesh is found here, by its texture's alpha. Up to 1 the eye goes over to the colour, and past 1 it's brighter than the colour
    if (M.m3.a > 0.0 && int(M.m2.y + 0.5) == 5 && alphaKind(uv) == 3) { vec3 gc = LINEAR != 0 ? pow(M.m3.rgb, vec3(2.2)) : M.m3.rgb; lit = mix(lit, gc, min(M.m3.a, 1.0)) + gc * max(M.m3.a - 1.0, 0.0); }
#endif
#ifdef GODOT_HOOK
    lit += LINEAR != 0 ? gd.emit : pow(max(gd.emit, vec3(0.0)), vec3(1.0 / 2.2));
#endif
    if (vExtra.y > 0.5 && vExtra.y < 1.5) lit = base;   // a glowing part is its own colour, unlit (2 is GFX_OCCLUSION's, above)
    if (V.fog.w > 0.0) lit = mix(V.fog.rgb, lit, exp(-distance(vW, V.eye.xyz) * V.fog.w));
#ifndef TOON
    if (LINEAR == 1 || LINEAR == 3) lit = pow(max(lit, vec3(0.0)), vec3(1.0 / 2.2));
    if (LINEAR != 2)
#endif
    {
        float kn = V.misc.x;
        vec3 ov = max(lit - vec3(kn), vec3(0.0));
        lit = min(lit, vec3(kn)) + (0.96 - kn) * (vec3(1.0) - exp(-ov / (0.96 - kn)));
    }
#ifdef REFL
    lit *= exp(-max(M.a.y - vW.y, 0.0) * M.a.z);   // a polished floor shows what's near it: a reflection fades with its depth under the floor
#endif
#ifdef GLOSS
    { float ct = clamp(dot(N, normalize(V.eye.xyz - vW)), 0.0, 1.0); o = vec4(lit, min(M.a.z, M.a.y + (1.0 - M.a.y) * pow(1.0 - ct, 5.0))); }   // how much of what's under the floor shows from here: more at a grazing angle. The blend reads it
#elif defined(TOON) && !defined(TOON_BLEND)
    o = vec4(lit, 2.0);   // over 1: a pixel the post pass leaves as it is
#else
    o = vec4(lit, (aoA ? 1.0 : mix(1.0, t.a, vTint.w)) * vExtra.x);   // the blend pipelines read this, so sheer cloth is sheer; the others ignore it. A GFX_OCCLUSION mesh's alpha is its share of the room
#ifdef STRAND
    o.a *= hairCov;   // a hair covers this much of the pixel
#endif
#endif
}
