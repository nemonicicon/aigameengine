// One copy of a mesh. The renderer draws every copy of a mesh in one call (gfx_vk.c, batchesMake), and this is what each copy brings of its own.
layout(location = 5) in vec4 iR0;       // its model matrix, as rows
layout(location = 6) in vec4 iR1;
layout(location = 7) in vec4 iR2;
layout(location = 8) in vec4 iTint;     // w: how much of the texture to take over the tint
layout(location = 9) in vec3 iExtra;    // x its own alpha, y it glows, z its layer of the texture page
layout(location = 4) flat out vec4 vTint;
layout(location = 5) flat out vec3 vExtra;
