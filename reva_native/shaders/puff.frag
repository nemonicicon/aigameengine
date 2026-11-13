#version 450
// A puff of smoke: its shape from a soft texture, its colour from the light it's in.
layout(constant_id = 3) const int LINEAR = 0;
layout(location = 0) in vec4 vC;
layout(location = 1) in vec2 vU;
layout(location = 0) out vec4 o;
layout(set = 1, binding = 0) uniform sampler2D tex;
void main() {
    float a = texture(tex, vU).a * vC.a; if (a < 0.002) discard;
    o = vec4(LINEAR == 2 ? pow(max(vC.rgb, vec3(0.0)), vec3(2.2)) : vC.rgb, a);
}
