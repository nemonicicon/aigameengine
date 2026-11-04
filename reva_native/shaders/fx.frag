#version 450
// Flat colour, or SOFT: fading from the middle by its uv. A pixel here costs next to nothing, which is the point: a floor's pattern is worked out per tile on the CPU, not per pixel.
precision mediump float;
layout(constant_id = 1) const int SOFT = 0;
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec4 vC;
layout(location = 1) in vec2 vU;
layout(location = 0) out vec4 o;
void main() {
    float a = vC.a;
    if (SOFT == 1) a *= exp(-dot(vU, vU) * 3.2);
    o = vec4(LINEAR == 2 ? pow(max(vC.rgb, vec3(0.0)), vec3(2.2)) : vC.rgb, a);
}
