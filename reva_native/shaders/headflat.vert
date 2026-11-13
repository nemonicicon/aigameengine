#version 450
// The inside of a head with no rig of its own: a cavity and two dental arches behind the hole.
layout(location = 0) in vec3 aPos;
layout(push_constant) uniform PC { mat4 mvp; vec4 col; } pc;
void main() { gl_Position = pc.mvp * vec4(aPos, 1.0); }
