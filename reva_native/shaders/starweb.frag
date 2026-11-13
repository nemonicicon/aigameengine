#version 450
// A soft glowing point or thread, its middle going to white: Wan-To's pieces and what holds them.
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec4 vC;
layout(location = 1) in vec2 vU;
layout(location = 0) out vec4 o;
void main() {
    float d = length(vU), a = exp(-d * d * 3.5) * (1.0 - smoothstep(0.85, 1.0, d));
    vec3 col = mix(vC.rgb, vec3(1.0), exp(-d * d * 20.0) * 0.7);
    o = vec4(LINEAR == 2 ? pow(col, vec3(2.2)) : col, vC.a * a);
}
