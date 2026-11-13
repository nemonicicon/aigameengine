#version 450
// Glowing points in the world: stars, nebulae, lamps. x hue, y size: over 4 is a big dim slow blob, under it a twinkling dot. Sizes are for a 720-line picture, scaled to the raster.
#extension GL_GOOGLE_include_directive : enable
#include "view.glsl"
layout(location = 0) in vec3 aP;
layout(location = 1) in vec2 aW;
layout(location = 0) out vec3 vC;
layout(location = 1) out float vT;
void main() {
    float hue = aW.x * 3.0;
    vec3 c = 0.55 + 0.45 * cos(vec3(hue, hue + 2.1, hue + 4.2));
    gl_Position = V.vp * vec4(aP, 1.0);
    if (aW.y > 4.0) {
        vC = c * 0.40;
        vT = 0.5 + 0.5 * sin(V.misc.w * 0.05 + aW.x * 4.0);
        gl_PointSize = (46.0 + (aW.y - 5.0) * 22.0) * V.misc2.x;
        return;
    }
    vC = mix(vec3(0.82, 0.93, 0.88), c, 0.45);
    vT = 0.25 + 0.75 * pow(abs(sin(V.misc.w * aW.y * 0.35 + aW.x)), 2.0);
    gl_PointSize = (4.0 + aW.y * 3.0) * V.misc2.x;
}
