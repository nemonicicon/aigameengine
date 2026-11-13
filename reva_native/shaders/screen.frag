#version 450
// A screen emits: no normal, no light. A dark border and a faint scanline comb, so it reads as a screen hanging on a wall and not a floating rectangle of face.
layout(constant_id = 3) const int LINEAR = 0;
layout(set = 1, binding = 0) uniform sampler2D uT;
layout(push_constant) uniform PC { mat4 model; vec4 tint; } pc;  // tint.x: how bright
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
void main() {
    vec2 b = abs(vUV - 0.5) * 2.0;
    float inner = step(max(b.x, b.y), 0.92);
    vec3 c = texture(uT, vUV).rgb * pc.tint.x;
    c *= 0.85 + 0.15 * sin((1.0 - vUV.y) * 160.0);
    c = mix(vec3(0.05, 0.05, 0.06), c, inner);
    o = vec4(LINEAR == 2 ? pow(max(c, vec3(0.0)), vec3(2.2)) : c, 1.0);
}
