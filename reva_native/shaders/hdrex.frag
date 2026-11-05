#version 450
// HDR on the glass (queue step 25): the light a bright pixel has ABOVE what the ordinary picture can show. The post pass makes the picture it always made, white at 1, and this
// works out, from the same scene with the same bloom and exposure, how far past white each pixel went. The presenter adds it back for a display that can show it. So the
// picture below white is exactly the one every other display gets, and only what was clipped comes back: a sun's glint, a flame, a lamp's own face.
layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uBloom;
layout(set = 0, binding = 3) uniform sampler2D uAO;
layout(set = 0, binding = 4) uniform sampler2D uVol;
layout(push_constant) uniform PC { vec4 a; vec4 b; vec4 c; vec4 d; vec4 e; } pc;   // a to d are the post pass's own. e.x: the display's peak over paper white
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 o;
void main() {
    vec4 sc = texture(uScene, vUV); vec3 c = sc.rgb;
    if (pc.b.y > 0.5) { vec2 k = texture(uAO, vUV).rg; c *= k.r * k.g; }
    if (pc.b.z > 0.5) c += texture(uVol, vUV).rgb;
    c += texture(uBloom, vUV).rgb * pc.a.y;
    c *= pc.a.x * 2.6;                                          // where the post pass's curve starts from
    vec3 over = max(c - vec3(1.0), vec3(0.0));
    vec3 ex = (pc.e.x - 1.0) * (vec3(1.0) - exp(-over * 0.5));  // up to the display's peak, easing in
    o = vec4(ex * (1.0 - clamp(sc.a - 1.0, 0.0, 1.0)), 1.0);    // a toon pixel is finished as it is
}
