/* gfx.h -- the renderer's whole face (engine plan, Phase 2). One way: the game hands it a frame as data and nothing comes back, so the sim can't tell whether anything was drawn.
 * The frame is a packet (gfx_packet.c), plain data in ordinary memory: Vulkan 1.3 on every machine draws it in gfx_vk.c, and the no-display build only hashes it. Geometry and textures are numbers (gfxBufferNew, gfxTextureNew) the engine fills once
 * (gfxBufferData, gfxTexture) and names in its draws. Since step 18 of the plan (11-04) this is the engine's only renderer: every draw site writes the packet.
 * ⭐ REVA_SOFT (queue step 27, 11-05): the engine's own rasterizer on the processor, gfx_soft.c, in gfx_vk.c's place, for a machine with no driver at all and for a
 * picture that's the same on every machine. It answers every call below. So to the rest of the engine a renderer is linked, which is what REVA_VK asks everywhere;
 * a line that means Vulkan itself also asks that REVA_SOFT isn't set. */
#ifndef GFX_H
#define GFX_H
#if defined(REVA_SOFT) && !defined(REVA_VK)
#define REVA_VK 1
#endif
#include <stdint.h>
#include <stddef.h>

/* a light beyond the sun. falloff: how fast it dims with distance, 1/(1+falloff*d*d). A spot has a direction of length 1 and the cosines of its edges; a point light's dir is all 0 */
typedef struct { float pos[3], col[3], falloff, dir[3], cosOuter, cosInner; int shadow; } GfxLight;
#define GFX_MAX_LIGHTS 256
typedef struct {
    float vp[16], sunVP[16];        /* column-major, GL's clip conventions: the renderer converts */
    float eye[3];
    float sunDir[3], sunI;          /* toward the sun; sunI 0 means no sun */
    float sunCol[3], sky[3];
    float fog[3], fogD;             /* colour, density; 0 is off */
    float clear[3];
    float knee;                     /* where the highlight shoulder starts */
    int shadowOn;
    float tsec;                     /* the tick clock, for what twinkles */
    long tick;                      /* the sim tick this frame shows */
    unsigned cuts;                  /* how many times the lens has jumped: anything that blends across frames doesn't blend across a change here */
    int reset;                      /* nothing before this frame carries over */
    int nViews;                     /* split screen: 0 is one picture through vp. Otherwise this many, of the same frame's data */
    struct { float vp[16], eye[3]; int x, y, w, h; } views[16];
    float exposure;                 /* the scene's, on the HDR road. 0 is the default */
    float dayExposure;              /* and what the hour of the day adds to it. Worked out from the sun, so the packet's hash already has it */
    float ground[3];                /* the bottom of the sky's hemisphere; all 0 works one out from the sky */
    const GfxLight *lights; int nLights;
    float haze;                     /* the scene's air, for volumetric light. 0 is none */
    int skyDome;                    /* this frame has a sky to draw behind everything. Worked out from what's loaded, so the hash doesn't carry it */
    const float *space;             /* deep space behind everything, 72 floats: 8 stars (direction, size), 8 colours, the milky way, how many stars and how big a pixel is. NULL for none */
    float filmGrade, filmVig;       /* film's own look, on the post pass */
    float filmBloom;                /* and how much of what's saturated blooms there */
    int filmPostOn;                 /* film's post pass, this frame. Off, the picture goes straight through */
    float filmTone;                 /* with it off, a knee in the mesh's own shader, channel by channel. 0 is none */
    float fish, fishTh;             /* a lens's fisheye, and the half-angle its corners cover */
    float feed, feedT;              /* a phone's frame round the picture (1 upright, 2 wide), and its clock */
    int wall;                       /* the views are the lab's wall: the bodies alone in each */
    float gloss[4];                 /* the glossy floor: its height, 1 over how far a reflection fades, how much it shows looking straight down, the most it ever shows */
    /* (step 25) the lens, and film's own look, this frame. All 0 is none of it, and a frame with none hashes as it did */
    float focus, dof;               /* depth of field: how far away the lens is focused, in world units, and how soft what's nearer or further goes. Either 0: everything's sharp */
    float shutter;                  /* motion blur: how much of the frame's time the shutter's open, 0 none to 1 */
    float sepia, flash, burn, grain;/* film's: how far to sepia, white over the picture, a burn eating in from the edge, grain */
    float weave[2];                 /* and the gate's weave: the whole picture off its place by this share of its width and its height */
    int bars;                       /* the views don't cover the picture (a game's fair shape on a wider screen): what's outside them is black, and each is cleared to the scene's colour */
} GfxView;

enum { GFX_SHADOW = 1,              /* for the sun's depth pass, not the picture */
       GFX_CUTOUT = 2,              /* alpha-tested */
       GFX_BLEND = 4,               /* alpha-blended, after everything opaque */
       GFX_HOLO = 8,                /* a body made of light: a depth prepass, then blended, after everything else */
       GFX_GLOW = 16,               /* unlit, at its own colour */
       GFX_TOON = 32,               /* the toon pass: the cel ramp, and a line unless it's GFX_NOINK */
       GFX_TOONROOM = 64,           /* lit as a room's lit, from above */
       GFX_NOINK = 128,              /* a toon mesh with no line round it */
       GFX_MIRRORED = 256,          /* this copy is the mesh seen in a mirror, so its winding goes the other way */
       GFX_MIRCULL = 512,           /* and its backs don't show there: the reflected copy of a mirror's own wall lies in its plane, facing away */
       GFX_REFL = 1024,             /* seen in the glossy floor: it fades with its depth under the floor */
       GFX_GLOSS = 2048,            /* the glossy floor: down after everything that reflects in it, over what's under it */
       GFX_WATER = 4096 };          /* a pool's surface: it reads the sky and the sun, and blends */
enum { GFX_FX_ALPHA = 0,            /* effect triangles: alpha-blended */
       GFX_FX_ADD = 1,              /* added */
       GFX_FX_SOFT = 2 };           /* alpha-blended, and fading from the middle by their uv */

typedef struct {
    unsigned id;                    /* stable from frame to frame: kind << 28 | owner << 12 | mesh (kinds below) */
    unsigned vb[6];                 /* GL buffer names: positions, normals, joints, weights, indices, UVs. 0 for one it hasn't got */
    unsigned nIndex, nVerts;        /* nIndex 0 draws nVerts unindexed */
    unsigned tex;                   /* GL texture name, 0 for none */
    const float *model, *bind;      /* 16 floats each */
    const float *pal;               /* bones x 16 floats; NULL for a static mesh */
    unsigned bones;
    float tint[3], useTex;          /* useTex 1 is the texture, 0 the tint, between is a mix */
    int flags;
    float alpha;                    /* a sheer mesh's own alpha; 0 is the default, solid */
    unsigned palShared;             /* not 0: pal is a pose shared by many bodies, and this with the pointer names it for this frame, so its bones go in the packet once */
    const float *pos;               /* not NULL: this copy's own positions this frame, nVerts x 3 floats: a face that's moved. They go in the packet */
    const float *mouth;             /* not NULL: a mouth that's open cuts a hole between the lips: the lip line x y z and its tilt, the mouth's half-width and the face's height, how far open, and one more for luck. 8 floats */
    float mat[4];                   /* (step 22) what it's made of: how rough, 0 a mirror to 1 chalk; how much a metal, 0 or 1 and little between; how much fine detail it has up close, 0 none; how deep its surface is for parallax, in its texture's widths, 0 flat. All 0 is nothing said, and the renderer takes rough 0.85 */
    unsigned ntex;                  /* its normal map's texture name, 0 for none. Its alpha is its height, for parallax */
    float kind;                     /* (step 23) what kind of surface, for a body: 0 plain, 1 skin, 2 hair, 3 an eye, 4 cloth, 5 its texture's alpha says pixel by pixel (over 0.95 plain, about 0.9 skin, 0.8 hair, 0.7 an eye, 0.6 cloth), which is how one mesh can be a whole person */
    float eye[4];                   /* its eyes glow this colour, by this much: 0 is no glow. On fur (kind 6) the first three are which way it's pushed, in the world, and the fourth is 0 */
    unsigned rest;                  /* (step 23) for a mesh whose drawn positions aren't its own (cloth that hangs: they're the simulation's, in the world), the buffer of where they were at rest. Fur keeps each strand's place by it. 0: its own positions */
    float restScale;                /* and the world's units to one of that buffer's. 0: the renderer works it out from the mesh's matrices */
} GfxMesh;

enum { GFX_KIND_HERO = 1, GFX_KIND_CROWD = 2, GFX_KIND_PROP = 3, GFX_KIND_CAST = 4, GFX_KIND_ACTOR = 5, GFX_KIND_OBJECT = 6, GFX_KIND_FAR = 7 };

/* start-up. The SDL road passes its window, and W and H come back as the drawable's. The direct road passes the display it already holds through gfxInitKms. */
void gfxInit(void *sdlWindow, int *W, int *H);
void gfxInitKms(int drmFd, uint32_t crtc, uint32_t connector, void *drmMode, int W, int H, void (*onFlip)(unsigned sec, unsigned usec));
void gfxKmsRemode(void *drmMode, int W, int H);    /* the direct road, between frames: new scanout buffers and depth, and the next frame sets the mode */
/* what this machine draws with: one table, chosen at setup and never moved by a timer (engine/gfxset.inc). What the machine can't do comes back off, and it says so */
typedef struct {
    int shadowTaps, sunMap;
    int aniso;                      /* anisotropic filtering's level, 0 off */
    int msaa;                       /* 1, 2, 4 or 8 */
    int linear;                     /* 0 lighting on gamma-encoded values. 1 the shader decodes and encodes, on an 8-bit target. 2 sRGB textures, and the light in the target */
    int hdr;                        /* a 16-bit float target and a pass to the glass */
    int tonemap;                    /* with hdr: 1 a filmic curve, 0 the old knee */
    int bloom;                      /* with hdr: bloom from what's over white */
    int grade;                      /* with hdr: a scene's LUT */
    int hemi;                       /* the sky as a hemisphere, with a ground under it */
    int lights;                     /* point and spot lights, clustered: how many at once, 0 none */
    int cascades;                   /* sun cascades: 1 is the one map, up to 4 */
    int ao;                         /* with hdr: ambient occlusion from the depth image */
    int contact;                    /* with hdr: contact shadows from the depth image */
    int volumetric;                 /* with hdr: light shafts through a scene's haze */
    int shadowTiles;                /* the atlas for lights' own shadows: how many of its 16 tiles to use, 0 none. A spot takes one, a lamp six */
    int probes;                     /* baked light from a set's probe grid */
    int sky;                        /* 0 the horizon's flat colour, 1 a gradient and the sun, 2 clouds as well */
    int screens;                    /* pictures drawn off-screen, and screens in the world that show them */
    int merge;                      /* every copy of a mesh in one call. Off, each draw is its own call, in the packet's order */
    int pages;                      /* a game's textures moved onto shared pages once it's loaded. Off, each stays on its own */
    int mirrors;                    /* mirrors and the glossy floor show what's in them. Off, a mirror's a dark opening and the floor's matte */
    /* materials (queue step 22). All off, a surface is its colour under the light, as it always was */
    int spec;                       /* highlights from the sun and the lights, by how rough and how much a metal each surface is (GGX) */
    int reflect;                    /* what a surface reflects from around it: 1 the sky and the ground, 2 the set's own baked surroundings where it has them */
    int normals;                    /* normal maps: a surface's small bumps catch the light */
    int detail;                     /* fine grain on a surface up close, where its material says so */
    int parallax;                   /* a normal map's height shifts the texture with the angle of view, where its material says how deep */
    /* bodies (queue step 23), by a mesh's kind. Realistic bodies only: the toon pass is its own look */
    int skin;                       /* light wraps round skin and goes red at its edge, as it does through flesh */
    int hair;                       /* hair's long highlight, along the strand */
    int eyes;                       /* an eye is wet: a sharp highlight, a catch light from the lens's side, and what's around it */
    int sheen;                      /* cloth's soft edge glow, and no hard highlight */
    int fur;                        /* (step 23) how many shells a furry mesh is drawn in, over its skin: 0 none, 16 a good coat, to 48 */
    /* effects and the world (queue step 24) */
    int particles;                  /* how many particles at once, from every emitter: the oldest makes way */
    int softfx;                     /* a particle fades out where it meets a surface: the scene's depth is read mid-frame */
    int decals;                     /* decals projected onto whatever's there, by the scene's depth. Off, a decal's a flat mark */
    int lod;                        /* how far detail lasts: 0 the shortest, 2 as authored, 4 twice as far. Film meshes are the top detail */
    int refract;                    /* water bends what's under it, and breaks at its shore */
    int foliage;                    /* how much of a set's grass and scattered trees is drawn, in a hundred */
    int beamlights;                 /* a set's beams are lights too: 0 a glow in the air only, 1 they light what they point at, 2 and cast shadows */
    /* the display (queue step 25) */
    int dof;                        /* with hdr: depth of field, where a frame's lens asks for it (GfxView.focus and dof) */
    int mblur;                      /* with hdr: motion blur from the lens's own move, where a frame's shutter asks. Off in games: blur on a flat panel isn't for everyone */
    int taa;                        /* with hdr: the picture put together over frames, each drawn a part of a pixel off the last. Off by default */
    int tube;                       /* with hdr: the flat-panel look, the picture as a tube shows it. 0 off, 1 a 480-line interlaced set, 2 a 240-line one */
    int hdrout;                     /* with hdr and tonemap: the display's own HDR, where it says it has HDR10. 0 off, 1 on with a peak of 1000 nits, a bigger number that peak. What it can't do comes back 0 */
    int interp;                     /* on a display faster than the rules' 60 a second: the pictures between two ticks are worked out from the two packets */
    int resW, resH;                 /* the picture's own size, where it isn't the glass's: 0 is the glass's. Fullscreen, it's drawn at this and shown at the panel's size */
    int aspect;                     /* the picture's shape, width over height in thousandths (1333 is 4:3): 0 is the glass's. A picture narrower than its glass has black either side */
} GfxConfig;
/* a set's baked surroundings, for reflections: six faces of size x size RGBA texels (+x -x +y -y +z -z), and where it was taken from. NULL for none */
void gfxEnvCube(const unsigned char *rgba, int size, const float *at);
void gfxConfig(const GfxConfig *c);
void gfxTube(const float *p);        /* the flat-panel look's numbers, 11 of them in data/tube.txt's order; NULL is the engine's own */
float gfxShownAspect(void);          /* how wide the picture is shown for its height, when its pixels aren't square (a 640 x 240 tube picture is shown 4:3): 0 when they are */
void gfxShotSize(int *w, int *h);    /* the size gfxRead gives: the glass's. That's the picture's unless a presenter stands between them (step 25), when a shot shows what the glass does */
void gfxLut(const unsigned char *rgba, int size);   /* a scene's colour grade: size cubed RGBA texels, or NULL for none */
void gfxProbes(const unsigned char *rgba, int w, int h, int d, const float *geo);   /* a set's baked light: w x h x d RGBA texels, and its corner and spacing, 6 floats. NULL for none */
/* pictures drawn off-screen, and screens in the world that show them. A target is named by a texture number; nearest keeps its pixels hard. They're dressing, so the packet's hash leaves them out */
void gfxTargetMake(unsigned tex, int w, int h, int nearest);
void gfxFeed(unsigned tex, const float *vp, const float *eye);   /* this frame, the world is also drawn into this target through this lens */
void gfxScreen(unsigned tex, const float *model, float amp);     /* this frame, a quad from -1 to 1 under this matrix shows this target */
typedef struct {
    unsigned target, vbo, ebo, tex, morph[8], nIndex;
    float mvp[16], model[16], weights[8];
    float open, wide, seal, lipY, falloff, lipHalf, drop, pivotY, pivotZ, angle, mouthW, round, spread, press, mouthDark, aperture, edge, edgeSharp, lift, minY, height, centerX, halfWidth, deform;
    unsigned ivbo, iebo; int flatN; struct { int first, count; float col[3], mvp[16]; } flat[3];   /* the inside of a head with no rig of its own */
} GfxHead;
void gfxHead(const GfxHead *h);                                    /* this frame, Holly's head is drawn into its target */
void gfxFloorShadow(unsigned tex, const float *vp, const float *rect);   /* this frame, the bodies' shapes through this lens darken a floor: rect is x min, x max, z min, z max */

/* resources: a buffer or a texture is a number the engine hands around, never 0. The renderer's tables are as long as the highest number, so a dropped one comes
 * back, oldest first and only once a good few are waiting (step 13), or a world that streams would make the tables longer for as long as it ran (gfx_packet.c) */
unsigned gfxBufferNew(void);
unsigned gfxTextureNew(void);
void gfxBuffersNew(int n, unsigned *names);
void gfxTexturesNew(int n, unsigned *names);
void gfxBufferDrop(unsigned name);                 /* its number goes back to the pool, and its memory once no frame in flight can be using it */
void gfxTextureDrop(unsigned name);
void gfxBuffersDrop(int n, const unsigned *names);
void gfxTexturesDrop(int n, const unsigned *names);
void gfxBufferData(unsigned name, long size, const void *data, int dynamic);   /* dynamic: written again this session, maybe every frame */
void gfxBufferSub(unsigned name, long off, long size, const void *data);
void gfxTexture(unsigned name, int w, int h, const void *rgba);
void gfxBufferFree(unsigned name);   /* the renderer's half of a drop: the memory, not the number */
void gfxTextureFree(unsigned name);
void gfxUploadAsync(int on);         /* while on, a new texture's copy goes in the next frame's commands and nothing waits for the GPU */
void gfxTexturePage(unsigned name);                             /* this texture is only ever on meshes: it can share a page */
void gfxPagesBuild(void);                                       /* between frames, after loading: what can share a page moves onto one. Nothing to do costs nothing */
int gfxHasBC(void);                                             /* can this GPU sample BC1 and BC3 */
int gfxHasBC7(void);                                            /* and BC7: a PC's cook. The Pi can't */
void gfxTextureNear(unsigned name);                             /* this texture is for sprites: no filtering, no wrapping */
void gfxTextureHud(unsigned name);                              /* this texture is a sheet of text: drawn in 2D as sprites are, but smooth, with its mips */
void gfxTextureBC(unsigned name, int w, int h, int levels, int fmt, const void *data, int holes);   /* a cooked texture: fmt 1, 3 or 7, levels largest first */

/* a frame: Begin, any number of the rest, End. All of it goes into the packet. ⛔ A dynamic buffer is read when the packet is drawn, so a name written twice in a frame draws the last write. */
void gfxFrameBegin(void);
void gfxMesh(const GfxMesh *m);
void gfxBeam(const float *d);                       /* a glowing volume along a line, 24 floats: where it starts and its length, along it and its strength, across and how it fades, across and how it thins, its radius at each end and the box's half-width, nothing, its colour, nothing. Added, after everything solid */
/* (step 24) a mark projected onto whatever's there. m: the box it's thrown through, a cube from -0.5 to 0.5 under this matrix, its y along the throw and pointing back out of the surface.
 * col: its colour and how strong. kind: 0 a soft round mark, 1 a splat, 2 a scorch, 3 a wet patch, 4 a painted stripe (the whole box), 5 a crack, 6 a picture (tex). 0, 1, 2, 3 and 5 stain
 * what's under them, so they're lit and shadowed as it is; 4 and 6 are paint, lit by the sun and the sky. fade: 1 fresh to 0 gone. seed: its own dice, so two splats aren't one shape.
 * Where the machine doesn't read the scene's depth mid-frame (the `decals` setting) it's drawn as a flat mark across the box's middle, so the packet's the same on every machine */
typedef struct { float m[16]; float col[4]; float kind, fade, seed; unsigned tex; } GfxDecal;
#define GFX_MAX_DECALS 256
void gfxDecal(const GfxDecal *d);
void gfxLines3D(const float *xyzrgb, int nVerts);   /* additive, depth-tested */
void gfxLines3DOver(const float *xyzrgb, int nVerts);   /* the same, and they read through what's in front */
void gfxFxTex(const float *xyzrgbauv, int nVerts, unsigned tex);   /* soft sprites in the world, 9 floats a vertex, alpha from a texture made by gfxTextureHud: smoke */
void gfxToonSet(float cel, float rim, float keyFromLens, float roomGradient, float ink, const float *inkRGB);   /* the toon pass's numbers */
void gfxMemSay(void);   /* [mem]: what the renderer's holding, for reading a long session */
void gfxFxTrisView(const float *xyzrgbauv, int nVerts, int how, int under, int view);   /* for one picture of a split screen: what faces the lens has to be made for each */
void gfxFxTris(const float *xyzrgbauv, int nVerts, int how, int under);   /* triangles already in the world, 9 floats a vertex. under: before the bodies, which then cover them. Never write depth */
void gfxSkyPoints(unsigned buffer, const float *xyzHueSize, int nPoints, int under);   /* glowing points, 5 floats each, added: from a static buffer, or inline when that's 0 */
void gfxHud(const float *xy, int nVerts, float canvasW, float canvasH, float r, float g, float b, float a);   /* triangles on a canvas, alpha-blended */
void gfxSprites(unsigned tex, const float *xyuvrgba, int nVerts, float canvasW, float canvasH);   /* textured triangles on a canvas, 8 floats a vertex, in order with the HUD's */
void gfxFrameEnd(const GfxView *v);
/* (step 25) THE PICTURES BETWEEN TWO TICKS, for a display that shows more than the rules' 60 a second. The engine says each refresh whether it's on and how far this
 * refresh lies from the last tick toward the next, 0 to 1 (gfxBetweenSet). On a refresh where a tick ran, gfxFrameEnd keeps the packet and draws the picture that far
 * between the tick before and this one. On a refresh where none ran the engine draws nothing and calls gfxBetween, which does the same from the two it has. A body's
 * place and bones, a mesh's own positions, colours and the lens are mixed; particles, lines, the HUD and decals are the later tick's. Across a cut nothing's mixed.
 * What's shown is one tick behind what the rules have done: that's what it costs. The packet's hash is of what the game asked for, and doesn't know any of this */
void gfxBetweenSet(int on, float t);
void gfxBetweenSay(int *paired, int *draws);       /* of the last mixed picture's draws, how many found themselves in the tick before */
void gfxBetween(void);
void gfxBackBetween(void);                          /* between gfx_packet.c and the renderer: the frame just begun has no packet of its own */
typedef struct { int draws, sunDraws; long indices, sunIndices; } GfxStats;
void gfxStats(GfxStats *s);                         /* what the last frame asked for */
void gfxBackStats(int *calls, int *binds, int *pages, int *stretched);   /* what the last frame cost the renderer: draw calls, texture binds, how many pages there are, and how many of its meshes were stretched */
double gfxWaited(void);                             /* ms spent blocked on the GPU or the display since the last call */
int gfxRead(void *rgba, int w, int h);              /* the last frame, RGBA with the bottom row first, when REVA_SHOT asked at start-up */

/* the packet, between gfx_packet.c and whatever draws it. Offsets are into mem. Bones are 3 vec4 rows each, multiplied by the mesh's bind. */
#define GFX_MAX_DRAWS 16384
#define GFX_MAX_HUD 4096
typedef struct { unsigned id; unsigned vb[6]; unsigned nIndex, nVerts, tex; float model[16]; unsigned bones, palOff; float tint[3], useTex; int flags; float alpha; unsigned posOn, posOff; float mouth[8]; float mat[4]; unsigned ntex; float kind, eye[4]; unsigned rest; float restScale; } GfxDraw;   /* mat, ntex and kind are what it's made of: content, like a texture's pixels, so the hash leaves them out. So does it leave out eye, which is picture */
typedef struct { unsigned off; int n; int over; } GfxLines;
typedef struct { unsigned off; int n; float scr[2], col[4]; unsigned tex; } GfxHudRec;   /* tex 0: flat colour, 2 floats a vertex. Otherwise sprites, 8 floats a vertex */
enum { GFX_FX_SKY = 3, GFX_FX_PUFF = 4, GFX_FX_WEB = 5, GFX_FX_WEBX = 6,
       GFX_FX_GLOW = 7,             /* (step 24) added, and fading from the middle by their uv: fire, sparks */
       GFX_FX_SOFTZ = 8,            /* GFX_FX_SOFT that also fades out where it meets a surface, where the machine reads the scene's depth (softfx); elsewhere it's GFX_FX_SOFT */
       GFX_FX_GLOWZ = 9 };          /* and GFX_FX_GLOW that does */
typedef struct { unsigned off; int n; int how, under; unsigned vb; int view; unsigned tex; } GfxFx;   /* view: one picture of a split screen's, or -1 for all of them */
/* a star you can see the inside of: the buffers its parts are in, and its numbers. c: centre, radius. s: stretch, clock. t: temperature, its grade, exposure, how fast it turns. cut: the wedge's start and width, how many spots. rt, up: across and up the lens */
typedef struct { int on; unsigned sphVB, sphIB, sphN, faceVB, faceN, quadVB, loopVB; int nLoop, loopOff[25]; float loopAng[24], loopPh[24]; float c[4], s[4], t[4], cut[3], rt[3], up[3], spot[16][4]; } GfxStar;
void gfxStar(const GfxStar *s);
typedef struct { GfxDraw *draws; int nDraw; GfxLines lines[64]; int nLine; GfxHudRec hud[GFX_MAX_HUD]; int nHud; GfxFx fx[64]; int nFx; float beam[64][24]; int nBeam; GfxStar star; unsigned char *mem; size_t memUsed; GfxDecal decal[GFX_MAX_DECALS]; int nDecal; } GfxPacket;
void gfxBackBegin(void);
void gfxBackEnd(const GfxPacket *p, const GfxView *v);

#endif
