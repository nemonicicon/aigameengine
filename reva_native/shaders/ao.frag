#version 450
// Ambient occlusion and contact shadows from the depth image, at half size, on the PC. The occlusion is the Alchemy estimator: how much of what's nearby rises above this point's plane. The contact shadow is a short march toward the sun, for the small gaps a shadow map's texels can't see.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(set = 1, binding = 0) uniform sampler2D uD;
layout(push_constant) uniform PC { mat4 m; vec4 p; } pc;  // m[0]: this view's rectangle in the image, x y w h. p: x the radius, y occlusion on, z contact on
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
// ⛔ a sample's depth comes from one texel, so its position has to come from that texel's middle too: otherwise on a steep face it lands off the plane, and a flat wall shadows itself in stripes
vec2 snap(vec2 uv) { return (floor(uv * V.cl3.xy) + 0.5) / V.cl3.xy; }
float depthAt(vec2 uv) { return texture(uD, pc.m[0].xy + uv * pc.m[0].zw).r; }
vec3 worldAt(vec2 uv, float z) { vec4 q = V.inv * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, z, 1.0); return q.xyz / q.w; }
void main() {
    vec2 uv0 = snap(vUV);
    float z = depthAt(uv0);
    if (z >= 0.99999) { o = vec4(1.0); return; }
    vec3 P = worldAt(uv0, z);
    vec3 N = cross(dFdx(P), dFdy(P));
    float nl = dot(N, N);
    if (nl < 1e-12) { o = vec4(1.0); return; }
    N /= sqrt(nl);
    if (dot(N, V.eye.xyz - P) < 0.0) N = -N;
    float dist = dot(P - V.eye.xyz, V.fwd.xyz);
    // Which way this pixel's ring of samples is turned: one of sixteen turns, laid out so that any four-by-four block of pixels holds every one of them once.
    // The smoothing pass adds up exactly such a block, so on a smooth surface every pixel ends with the same 160 samples and no grain is left over.
    // ⛔ It was a noise with no pattern, smoothed over five by five: what's left of that is blotches the size of the five by five, a grid of squares on skin
    const int TURN[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    ivec2 ip = ivec2(gl_FragCoord.xy) & 3;
    int turn = TURN[ip.y * 4 + ip.x];
    float a0 = (float(turn) + 0.5) / 16.0 * 6.2831853;
    float ao = 1.0, contact = 1.0;
    // ⛔ A CLOSE-UP. Both of these are sized in the world, for a game seen from a few metres. On a face that fills the picture through a long lens, the
    // occlusion's radius was six times the picture's height and its ten samples were noise, and the march's steps were 4 cm long on a neck: a grid of little
    // squares over the skin, and a ragged dark patch under the chin (found on REVA's bare neck, 11-04, which a choker had always covered). So each is held to a
    // part of the picture's height, and everything of it that's a length shrinks with it. Where it isn't held, k is 1 and nothing has changed
    float perUnit = abs(V.vp[1][1]) * 0.5 / max(dist, 0.5);          // a world unit here, in picture heights
    float kA = min(1.0, 0.5 / (pc.p.x * perUnit));
    // ⛔ AND UP CLOSE A TRIANGLE IS MANY PIXELS WIDE. The slope read off the depth image at one pixel is then one triangle's own, a degree or two off its
    // neighbours' on a rounded body, and over a sample's reach that tilt is millimetres: each triangle got its own false shade, and the body's mesh showed
    // through as a grid (the grain's fix above didn't touch it, which is how it was told apart). So up close the slope is taken across 8 texels a side,
    // on whichever side has no edge in it, and what rises less than a few degrees above the surface isn't counted
    if (kA < 1.0) {
        vec2 tx = vec2(8.0 / V.cl3.x, 0.0), ty = vec2(0.0, 8.0 / V.cl3.y);
        float zl = depthAt(uv0 - tx), zr = depthAt(uv0 + tx), zd = depthAt(uv0 - ty), zu = depthAt(uv0 + ty);
        vec3 dx = abs(zr - z) < abs(zl - z) ? worldAt(uv0 + tx, zr) - P : P - worldAt(uv0 - tx, zl);
        vec3 dy = abs(zu - z) < abs(zd - z) ? worldAt(uv0 + ty, zu) - P : P - worldAt(uv0 - ty, zd);
        vec3 n2 = cross(dx, dy); float l2 = dot(n2, n2);
        if (l2 > 1e-12) { n2 /= sqrt(l2); if (dot(n2, V.eye.xyz - P) < 0.0) n2 = -n2; N = normalize(mix(N, n2, clamp((1.0 - kA) * 4.0, 0.0, 1.0))); }
    }
    if (pc.p.y > 0.5) {
        float k = kA;
        float rad = pc.p.x * k, sr = rad * perUnit, sx = sr * V.cl3.z;
        float sum = 0.0;
        for (int i = 0; i < 10; i++) {
            float r = (float(i) + 0.5) / 10.0, a = a0 + float(i) * 2.3999632;
            vec2 uv = snap(uv0 + vec2(cos(a) * r * sx, sin(a) * r * sr));
            vec3 v = worldAt(uv, depthAt(uv)) - P;
            float vv = dot(v, v);
            if (vv < rad * rad * 4.0) sum += max(0.0, dot(v, N) - (0.006 + 0.002 * dist) * k - 0.10 * (1.0 - k) * sqrt(vv)) / (vv + 0.02 * k * k);
        }
        ao = clamp(1.0 - sum * 1.1 / 10.0 * rad, 0.0, 1.0);
        // On the tightest close-ups it fades out altogether. A face filling the picture still showed its triangles faintly after all of the above: the
        // depth image has no surface normals in it, only slopes, and that's as far as slopes go. What a close-up needs from shade under a chin or beside
        // a nose it gets from the contact shadow below and from the body's own shading. (Not built: a normals image for this pass to read)
        ao = mix(1.0, ao, smoothstep(0.10, 0.30, kA));
    }
    if (pc.p.z > 0.5 && V.sunDir.w > 0.0 && dot(N, V.sunDir.xyz) > 0.0) {
        float k = min(1.0, 0.33 / (0.7 * perUnit));
        vec3 R = P + N * 0.02 * max(dist * 0.05, 1.0) * k;
        float len = 0.7 * k, step = len / 10.0, t = step * ((float((turn * 7 + 3) & 15) + 0.5) / 16.0 * 0.8 + 0.2);   // sixteen starts, one to a pixel of the block, the same way
        for (int i = 0; i < 10; i++, t += step) {
            vec3 Q = R + V.sunDir.xyz * t;
            vec4 c = V.vp * vec4(Q, 1.0);
            vec2 uv = snap(vec2(c.x / c.w * 0.5 + 0.5, 0.5 - c.y / c.w * 0.5));
            if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
            float zs = depthAt(uv);
            if (zs >= c.z / c.w) continue;
            float back = dot(Q - worldAt(uv, zs), -V.fwd.xyz) * -1.0;   // how far the ray is behind what the depth image holds
            if (back > (0.02 + 0.004 * dist) * k && back < 0.35 * k) { contact = 0.35 + 0.65 * t / len; break; }
        }
    }
    o = vec4(ao, contact, 0.0, 1.0);
}
