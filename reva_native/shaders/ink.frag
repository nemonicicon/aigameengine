#version 450
// The toon pass's line: a body's back faces, pushed out on screen. One colour, and the post pass leaves it as it is.
layout(push_constant) uniform Face { vec4 p; vec4 s; vec4 t; vec4 i; vec4 a; } M;
layout(location = 0) out vec4 o;
void main() { o = vec4(M.i.rgb, 2.0); }
