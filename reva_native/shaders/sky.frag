#version 450
precision mediump float;
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec3 vC;
layout(location = 1) in float vT;
layout(location = 0) out vec4 o;
void main() {
    vec2 d = gl_PointCoord - 0.5;
    float g = exp(-dot(d, d) * 11.0);
    vec3 c = vC * vT * g;
    o = vec4(LINEAR == 2 ? pow(max(c, vec3(0.0)), vec3(2.2)) : c, g * 0.85);
}
