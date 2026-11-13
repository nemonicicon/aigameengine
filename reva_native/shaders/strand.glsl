// (11-06) HAIR ON THE GPU (engine/strands.inc; gfx_vk.c's strandsRun): what each of its passes sees.
// A lock is np points in a row, root first, and its points follow the one before it's lock's: point p of lock s is s*np+j. Three
// floats a point, the processor's own layout, so the processor's way (REVA_STRANDGPU=0) and this one read the same buffers the same way.
layout(std430, set = 0, binding = 0) buffer SX { float X[]; };          // where each point is
layout(std430, set = 0, binding = 1) buffer SXP { float XP[]; };        // and where it was a substep ago
layout(std430, set = 0, binding = 2) buffer SV { float Vv[]; };         // its speed
layout(std430, set = 0, binding = 3) readonly buffer ST0 { float T0[]; };   // the haircut's place for it, last frame
layout(std430, set = 0, binding = 4) readonly buffer ST1 { float T1[]; };   // and this frame
layout(std430, set = 0, binding = 5) readonly buffer SHW { float HW[]; };   // the haircut's half-width across the lock there
layout(std430, set = 0, binding = 6) readonly buffer SNR { float NR[]; };   // and its normal
layout(std430, set = 0, binding = 7) readonly buffer SBODY { vec4 BODY[]; };    // her body, four a point: last frame's place and whether it was there; its normal and whether it's part of
                                                                                // her head standing out of her skull (an ear, her nose, her lips); this frame's place; its normal
layout(std430, set = 0, binding = 8) readonly buffer SGRID { int GRID[]; };     // the grid of her body this frame: HS_GRID heads, then each point's next
layout(std430, set = 0, binding = 9) readonly buffer SLOCK { vec4 LOCKD[]; };   // a point: its segment's length, how far along its lock from the root, how far out in her head's ellipsoid at rest
layout(std430, set = 0, binding = 10) readonly buffer SHAIR { vec4 HAIRD[]; };  // a hair: across its lock (-1 to 1), through it, how far down it (a share), its true half-width in metres
layout(std430, set = 0, binding = 11) buffer SRIBP { float RIBP[]; };   // the locks' ribbons: two corners a point, for the shadow and for REVA_HAIRS=0
layout(std430, set = 0, binding = 12) buffer SRIBN { float RIBN[]; };
layout(std430, set = 0, binding = 13) buffer SWNT { float WNT[]; };     // a point: its across, its normal, its way, nine floats, for the hairs along it
layout(std430, set = 0, binding = 14) buffer SHP { float HP[]; };       // the hairs: two corners a drawn point, both its middle
layout(std430, set = 0, binding = 15) buffer SHN { float HN[]; };       // and its way, as long as its true half-width
layout(std430, set = 0, binding = 16) buffer SVG { int VG[]; };         // hair against hair: a cell's speeds summed in fixed point, and how many
layout(std430, set = 0, binding = 17) buffer SVOX { uint VOX[]; };          // her body as a closest-point grid: a cell's nearest point (its distance over the reach, then its number), or none
layout(std430, set = 0, binding = 18) readonly buffer SPRM { float PRM[]; };    // the frame's numbers: engine/strands.inc's hsGpuFrame says which is which
layout(push_constant) uniform PC { int k; int mode; int pad0; int pad1; } pc;   // the substep, and what the pass does
#define GET(B, p) vec3(B[(p) * 3], B[(p) * 3 + 1], B[(p) * 3 + 2])
#define PUT(B, p, v) { vec3 v_ = (v); B[(p) * 3] = v_.x; B[(p) * 3 + 1] = v_.y; B[(p) * 3 + 2] = v_.z; }
