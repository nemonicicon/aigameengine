/* gfx.h -- the renderer's whole face (engine plan, Phase 2). One way: the game hands it a frame as data and nothing comes back, so the sim can't tell whether anything was drawn.
 * The frame is a packet (gfx_packet.c), plain data in ordinary memory: Vulkan 1.3 on every machine draws it in gfx_vk.c, and the no-display build only hashes it. Geometry and textures are named by the GL names the pack loader already holds: under -DREVA_VK gl_null.c passes glBufferData and
 * glTexImage2D through to here, and every other GL call in the engine does nothing. So a game comes across by adding these calls at its draw sites, one site at a time, and
 * what hasn't come across yet simply isn't drawn. The GL build is untouched and stays for film and the old doors until each is ported. */
#ifndef GFX_H
#define GFX_H
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
} GfxConfig;
void gfxConfig(const GfxConfig *c);
void gfxLut(const unsigned char *rgba, int size);   /* a scene's colour grade: size cubed RGBA texels, or NULL for none */
void gfxProbes(const unsigned char *rgba, int w, int h, int d, const float *geo);   /* a set's baked light: w x h x d RGBA texels, and its corner and spacing, 6 floats. NULL for none */
/* pictures drawn off-screen, and screens in the world that show them. A target is named by a GL texture name; nearest keeps its pixels hard. They're dressing, so the packet's hash leaves them out */
void gfxTargetMake(unsigned glTex, int w, int h, int nearest);
void gfxFeed(unsigned glTex, const float *vp, const float *eye);   /* this frame, the world is also drawn into this target through this lens */
void gfxScreen(unsigned glTex, const float *model, float amp);     /* this frame, a quad from -1 to 1 under this matrix shows this target */
typedef struct {
    unsigned target, vbo, ebo, tex, morph[8], nIndex;
    float mvp[16], model[16], weights[8];
    float open, wide, seal, lipY, falloff, lipHalf, drop, pivotY, pivotZ, angle, mouthW, round, spread, press, mouthDark, aperture, edge, edgeSharp, lift, minY, height, centerX, halfWidth, deform;
    unsigned ivbo, iebo; int flatN; struct { int first, count; float col[3], mvp[16]; } flat[3];   /* the inside of a head with no rig of its own */
} GfxHead;
void gfxHead(const GfxHead *h);                                    /* this frame, Holly's head is drawn into its target */
void gfxFloorShadow(unsigned glTex, const float *vp, const float *rect);   /* this frame, the bodies' shapes through this lens darken a floor: rect is x min, x max, z min, z max */

/* resources, from gl_null.c */
void gfxBufferData(unsigned name, long size, const void *data, int dynamic);
void gfxBufferSub(unsigned name, long off, long size, const void *data);
void gfxTexture(unsigned name, int w, int h, const void *rgba);
void gfxBufferFree(unsigned name);   /* its memory comes back once no frame in flight can be using it */
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
void gfxLines3D(const float *xyzrgb, int nVerts);   /* additive, depth-tested */
void gfxLines3DOver(const float *xyzrgb, int nVerts);   /* the same, and they read through what's in front */
void gfxFxTex(const float *xyzrgbauv, int nVerts, unsigned tex);   /* soft sprites in the world, 9 floats a vertex, alpha from a texture made by gfxTextureHud: smoke */
void gfxToonSet(float cel, float rim, float keyFromLens, float roomGradient, float ink, const float *inkRGB);   /* the toon pass's numbers */
void gfxMemSay(void);   /* [mem]: what the renderer's holding, for reading a long session */
void gfxFxTrisView(const float *xyzrgbauv, int nVerts, int how, int under, int view);   /* for one picture of a split screen: what faces the lens has to be made for each */
void gfxFxTris(const float *xyzrgbauv, int nVerts, int how, int under);   /* triangles already in the world, 9 floats a vertex. under: before the bodies, which then cover them. Never write depth */
void gfxSkyPoints(unsigned glBuffer, const float *xyzHueSize, int nPoints, int under);   /* glowing points, 5 floats each, added: from a static GL buffer, or inline when that's 0 */
void gfxHud(const float *xy, int nVerts, float canvasW, float canvasH, float r, float g, float b, float a);   /* triangles on a canvas, alpha-blended */
void gfxSprites(unsigned tex, const float *xyuvrgba, int nVerts, float canvasW, float canvasH);   /* textured triangles on a canvas, 8 floats a vertex, in order with the HUD's */
void gfxFrameEnd(const GfxView *v);
typedef struct { int draws, sunDraws; long indices, sunIndices; } GfxStats;
void gfxStats(GfxStats *s);                         /* what the last frame asked for */
void gfxBackStats(int *calls, int *binds, int *pages, int *stretched);   /* what the last frame cost the renderer: draw calls, texture binds, how many pages there are, and how many of its meshes were stretched */
double gfxWaited(void);                             /* ms spent blocked on the GPU or the display since the last call */
int gfxRead(void *rgba, int w, int h);              /* the last frame, as glReadPixels would give it, when REVA_SHOT asked at start-up */

/* the packet, between gfx_packet.c and whatever draws it. Offsets are into mem. Bones are 3 vec4 rows each, multiplied by the mesh's bind. */
#define GFX_MAX_DRAWS 16384
#define GFX_MAX_HUD 4096
typedef struct { unsigned id; unsigned vb[6]; unsigned nIndex, nVerts, tex; float model[16]; unsigned bones, palOff; float tint[3], useTex; int flags; float alpha; unsigned posOn, posOff; float mouth[8]; } GfxDraw;
typedef struct { unsigned off; int n; int over; } GfxLines;
typedef struct { unsigned off; int n; float scr[2], col[4]; unsigned tex; } GfxHudRec;   /* tex 0: flat colour, 2 floats a vertex. Otherwise sprites, 8 floats a vertex */
enum { GFX_FX_SKY = 3, GFX_FX_PUFF = 4, GFX_FX_WEB = 5, GFX_FX_WEBX = 6 };
typedef struct { unsigned off; int n; int how, under; unsigned vb; int view; unsigned tex; } GfxFx;   /* view: one picture of a split screen's, or -1 for all of them */
/* a star you can see the inside of: the buffers its parts are in, and its numbers. c: centre, radius. s: stretch, clock. t: temperature, its grade, exposure, how fast it turns. cut: the wedge's start and width, how many spots. rt, up: across and up the lens */
typedef struct { int on; unsigned sphVB, sphIB, sphN, faceVB, faceN, quadVB, loopVB; int nLoop, loopOff[25]; float loopAng[24], loopPh[24]; float c[4], s[4], t[4], cut[3], rt[3], up[3], spot[16][4]; } GfxStar;
void gfxStar(const GfxStar *s);
typedef struct { GfxDraw *draws; int nDraw; GfxLines lines[64]; int nLine; GfxHudRec hud[GFX_MAX_HUD]; int nHud; GfxFx fx[64]; int nFx; float beam[64][24]; int nBeam; GfxStar star; unsigned char *mem; size_t memUsed; } GfxPacket;
void gfxBackBegin(void);
void gfxBackEnd(const GfxPacket *p, const GfxView *v);

#endif
