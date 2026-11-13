#version 450
precision mediump float;
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec3 vC;
layout(location = 0) out vec4 o;
void main() { o = vec4(LINEAR == 2 ? pow(vC, vec3(2.2)) : vC, 0.95); }
