#version 450
// The occlusion's grain smoothed, without crossing an edge: five by five, each neighbour counted by how close its depth is to this pixel's.
layout(set = 0, binding = 0) uniform sampler2D uA;
layout(set = 0, binding = 1) uniform sampler2D uD;
layout(push_constant) uniform PC { vec4 a; } pc;  // xy: one texel
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
void main() {
    float z0 = texture(uD, vUV).r, far = 1.0 / max(1.0 - z0, 1e-5);
    vec2 sum = vec2(0.0); float w = 0.0;
    for (int y = -2; y <= 2; y++) for (int x = -2; x <= 2; x++) {
        vec2 uv = vUV + vec2(float(x), float(y)) * pc.a.xy;
        float k = 1.0 / (1.0 + 4000.0 * abs(texture(uD, uv).r - z0) * far);
        sum += texture(uA, uv).rg * k; w += k;
    }
    o = vec4(sum / w, 0.0, 1.0);
}
