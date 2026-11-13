#version 450
// Anti-aliasing over time (queue step 25). Each frame is drawn a part of a pixel off the last, and this adds it to the frames before: where each pixel was in the last
// picture comes from its depth and the two lenses, what the history has there is held to the colours round the pixel now (so nothing that's gone leaves a ghost), and
// nine parts of that go with one of this frame. Edges settle and fine detail stops crawling. It's the lens's move that's followed, so a body moving on its own is held by
// the clamp alone. Off by default, in games and in film: it's a softness over time, and not everyone wants one.
layout(set = 0, binding = 0) uniform sampler2D uC;
layout(set = 0, binding = 1) uniform sampler2D uH;
layout(set = 0, binding = 3) uniform sampler2D uZ;
layout(push_constant) uniform PC { mat4 m; vec4 a; } pc;   // m: this frame's picture to the last one's. a: a texel, whether there's a history to use, how much of it
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
void main() {
    vec4 c = texture(uC, vUV);
    vec3 mn = c.rgb, mx = c.rgb;
    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) { if (x == 0 && y == 0) continue; vec3 n = texture(uC, vUV + vec2(float(x), float(y)) * pc.a.xy).rgb; mn = min(mn, n); mx = max(mx, n); }
    float z = texture(uZ, vUV).r;
    vec4 q = pc.m * vec4(vUV.x * 2.0 - 1.0, 1.0 - vUV.y * 2.0, z, 1.0);
    vec2 was = vec2(q.x / q.w * 0.5 + 0.5, 0.5 - q.y / q.w * 0.5);
    float ok = pc.a.z * step(0.0, q.w) * step(0.0, was.x) * step(was.x, 1.0) * step(0.0, was.y) * step(was.y, 1.0);
    vec3 h = clamp(texture(uH, was).rgb, mn, mx);
    o = vec4(mix(c.rgb, h, pc.a.w * ok), c.a);
}
