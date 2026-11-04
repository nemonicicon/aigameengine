// What every world shader sees. The one view block, in one place.
layout(std140, set = 0, binding = 0) uniform View {
    mat4 vp;      // camera, already in Vulkan's clip depth
    mat4 sunVP;   // the sun's view-projection, the same way
    vec4 eye;
    vec4 sunDir;  // xyz toward the sun, w intensity; w 0 means no sun
    vec4 sunCol;
    vec4 sky;
    vec4 fog;     // rgb, density
    vec4 misc;    // x tone knee, y shadow map on, z half a sun-map texel, w the tick clock
    vec4 misc2;   // x how big a point is, for a 720-line picture being 1
    vec4 ground;  // the bottom of the sky's hemisphere
    vec4 fwd;     // the lens's forward, xyz
    vec4 split;   // how far along the lens each sun cascade ends
    vec4 cl;      // lights: x the first cell's word in the storage buffer, 0 for no lights. y cells across, z down, w slices
    vec4 cl2;     // x slices per unit of log depth, y the nearest slice's depth, zw this view's top left in pixels
    vec4 cl3;     // xy this view's size in pixels, z the screen's height over its width in this view, w the first vec4 of the shadowed lights' matrices
    mat4 casc[4]; // the sun's cascades
    mat4 inv;     // back from the picture to the world
    vec4 probe;   // baked light: xyz the grid's corner, w 1 if there is one
    vec4 probeS;  // xyz what turns a world position into the grid's 0..1
    vec4 skyZ;    // the sky as it looks: overhead
    vec4 skyH;    // at the horizon
    vec4 skyG;    // below it
    vec4 skyS;    // the sun's colour, and w how strong
    mat4 shv;     // the lens of the floor's shadow map
} V;
