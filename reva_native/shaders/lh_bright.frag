#version 450
// What's bright enough in the last lighthouse's picture to bloom.
layout(set = 0, binding = 0) uniform sampler2D tx;
layout(push_constant) uniform PC { vec4 a; } pc;   // xy one texel of the source
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
void main() { vec2 px = pc.a.xy; vec3 c = texture(tx, vUV + px * vec2(-1.0, -1.0)).rgb + texture(tx, vUV + px * vec2(1.0, -1.0)).rgb + texture(tx, vUV + px * vec2(-1.0, 1.0)).rgb + texture(tx, vUV + px * vec2(1.0, 1.0)).rgb;
    c = c * 0.25 * 4.0; c = max(c - vec3(0.55), vec3(0.0)); o = vec4(c * 0.25, 1.0); }
