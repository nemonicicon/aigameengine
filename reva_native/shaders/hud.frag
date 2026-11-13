#version 450
precision mediump float;
layout(location = 0) out vec4 o;
layout(push_constant) uniform PC { vec4 scr; vec4 col; } pc;
void main() { o = pc.col; }
