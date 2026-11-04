#version 450
// 2D on a virtual canvas, top left at 0,0: the HUD's coordinates, whatever the framebuffer's shape.
layout(location = 0) in vec2 aP;
layout(push_constant) uniform PC { vec4 scr; vec4 col; } pc;
void main() { gl_Position = vec4(aP.x / pc.scr.x * 2.0 - 1.0, 1.0 - aP.y / pc.scr.y * 2.0, 0.0, 1.0); }
