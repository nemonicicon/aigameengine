#version 450
// One direction of a blur.
layout(set = 0, binding = 0) uniform sampler2D tx;
layout(push_constant) uniform PC { vec4 a; } pc;   // xy the direction, a texel long
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
void main() { vec2 dir = pc.a.xy; vec3 c = texture(tx, vUV).rgb * 0.227 + (texture(tx, vUV + dir * 1.385).rgb + texture(tx, vUV - dir * 1.385).rgb) * 0.316 + (texture(tx, vUV + dir * 3.231).rgb + texture(tx, vUV - dir * 3.231).rgb) * 0.070; o = vec4(c, 1.0); }
