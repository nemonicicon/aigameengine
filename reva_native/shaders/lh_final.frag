#version 450
// The menu scene's picture and its two blooms: a filmic curve, the corners falling away, and a touch of grain so a black this deep doesn't band.
layout(set = 0, binding = 0) uniform sampler2D tb;
layout(set = 0, binding = 1) uniform sampler2D bl;
layout(set = 0, binding = 3) uniform sampler2D bl2;
layout(push_constant) uniform PC { vec4 a; } pc;   // the fade, the clock, the picture's size
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
float hs(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    vec3 c = texture(tb, vUV).rgb * 4.0 + texture(bl, vUV).rgb * 4.0 * 0.7 + texture(bl2, vUV).rgb * 4.0 * 0.5;
    c *= pc.a.x * 0.9;
    c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
    vec2 q = vUV - 0.5; c *= 1.0 - 0.45 * pow(length(q) * 1.25, 2.4);
    c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / 2.2));
    c += (hs(vUV * pc.a.zw + fract(pc.a.y * 3.1) * 91.0) - 0.5) * (1.6 / 255.0);
    o = vec4(c, 1.0);
}
