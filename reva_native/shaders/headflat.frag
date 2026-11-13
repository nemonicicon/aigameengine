#version 450
layout(push_constant) uniform PC { mat4 mvp; vec4 col; } pc;
layout(location = 0) out vec4 o;
void main() { o = vec4(pc.col.rgb, 1.0); }
