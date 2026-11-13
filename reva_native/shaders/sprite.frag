#version 450
precision mediump float;
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vC;
layout(location = 0) out vec4 o;
layout(set = 1, binding = 0) uniform sampler2D tex;
void main() { o = texture(tex, vUV) * vC; }
