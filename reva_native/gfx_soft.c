/* gfx_soft.c -- THE ENGINE'S OWN RASTERIZER (queue step 27, 11-05): the engine with no driver at all.
 * Vulkan draws the packet on a GPU (gfx_vk.c), and the no-display build only hashes it. This draws it on the processor, into ordinary memory, at 320 x 240: a budget
 * picture, not Vulkan's and not matched to it pixel for pixel. Build with -DREVA_SOFT and this file in gfx_vk.c's place (driver.py buildsoft): nothing links Vulkan.
 *
 * ⭐ THE SAME PIXELS ON EVERY MACHINE. That's the other half of what it's for. The sim's hash is the same on a PC and a Pi, and so is this picture, so a frame's hash
 * is a receipt that crosses machines, which a GPU's pixels never are (anim_bench/hashcheck.py --soft). What keeps it so:
 *   - on floats, only + - * / and sqrt, each of which IEEE 754 rounds one way, and never contracted into one another (-ffp-contract=off, as every build line has);
 *   - exp, sin, cos and pow are the club's own (club_math.c), never the C library's, and what the pixel loops need of them is tables made from those;
 *   - a triangle's coverage is whole-number arithmetic on positions snapped to a sixteenth of a pixel, with the top-left rule, so no pixel hangs on a rounding;
 *   - colours are mixed as whole numbers;
 *   - a float that isn't a number never reaches a float-to-int conversion, which is where machines differ: f2i and the checks beside it turn it into 0 first;
 *   - one thread, the packet's own order, and nothing here reads a clock for anything it draws.
 * ⛔ Anything added here keeps to that list, or the receipt parts on the first machine that rounds another way.
 *
 * WHAT IT DRAWS: meshes with and without bones (skinned here), a texture with its smaller copies chosen a triangle at a time, a cutout's holes, blended and glowing
 * meshes, a hologram, water as a tinted sheet, the sun, the sky's light and the strongest lamps near each mesh worked out at each vertex, fog, the toon pass's two
 * tones, a sky, particles and effect triangles, smoke, flat decals, lines, glowing points, the HUD, text and sprites, split screen.
 * WHAT IT DOESN'T (it's the budget version): shadows, mirrors and what a glossy floor reflects, pictures drawn off-screen and the screens that show them, beams, the
 * star, an open mouth's hole, fur, materials, the toon pass's line, the post pass and the lens, the lighthouse door's own passes, a frame between two ticks. */
#include "gfx.h"
#include "club_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) && !defined(REVA_SDL)
#define REVA_SDL 1
#endif
#ifndef REVA_SDL
#error "gfx_soft.c is on the SDL road only (build_linux.sh soft, driver.py buildsoft): the Pi's direct road hands the engine the display's own size, and this picture has one of its own"
#endif
#include <SDL2/SDL.h>

static int g_W = 320, g_H = 240;            /* the picture. REVA_SOFTRES=WxH tries another; the receipt's is this one */
static uint32_t *g_fb;                      /* 0x00RRGGBB a pixel, the top row first */
static float *g_zb;                         /* depth, 0 at the lens's near plane to 1 */
static GfxConfig g_cfg;
static int g_texCap = 512, g_smooth = 0, g_maxLights = 4, g_say = 0;
static struct { float cel, rim, keyCam, roomG, ink; } g_toon = {0.f, 0.f, 0.f, 0.f, 0.f};
static long g_nTri, g_nTriDrawn, g_nPix; static int g_nCalls;
static unsigned char *g_who; static int g_whoKind; static long g_kindHid[8], g_kindBl[8];   /* (11-07) REVA_SOFTSAY=2: the kind of thing that wrote each pixel last, and whose were drawn over */

static inline int f2i(float v, int hi) { return v > 0.f ? (v < (float)hi ? (int)v : hi) : 0; }   /* a float as a whole number from 0 to hi; what isn't a number is 0 */
#define PX(r, g, b, a) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* ---- geometry: a buffer is a copy in ordinary memory ------------------------------------------------------------------------------------------------ */
typedef struct { unsigned char *p; long size; float box[6]; long boxN; } SBuf;   /* box: a positions buffer's bounds, for the count they were worked out over */
static SBuf *g_buf; static unsigned g_nBuf;
static SBuf *bufOf(unsigned name) {
    if (name >= g_nBuf) { unsigned n = name + 1024; g_buf = realloc(g_buf, n * sizeof(SBuf)); memset(g_buf + g_nBuf, 0, (n - g_nBuf) * sizeof(SBuf)); g_nBuf = n; }
    return &g_buf[name];
}
void gfxBufferData(unsigned name, long size, const void *data, int dynamic) {
    (void)dynamic; if (!name || size < 0) return; SBuf *b = bufOf(name);
    if (b->size != size || !b->p) { free(b->p); b->p = size ? malloc((size_t)size) : NULL; b->size = b->p ? size : 0; }
    if (b->p) { if (data) memcpy(b->p, data, (size_t)size); else memset(b->p, 0, (size_t)size); }
    b->boxN = -1;
}
void gfxBufferSub(unsigned name, long off, long size, const void *data) {
    if (!name || name >= g_nBuf || !data) return;
    SBuf *b = &g_buf[name];
    if (!b->p || off < 0 || size <= 0 || off + size > b->size) return;
    memcpy(b->p + off, data, (size_t)size); b->boxN = -1;
}
void gfxBufferFree(unsigned name) { if (!name || name >= g_nBuf) return; free(g_buf[name].p); memset(&g_buf[name], 0, sizeof(SBuf)); }
/* a stream of a mesh: its bytes if the buffer is there and long enough, or NULL. Nothing a pack says is trusted to be the right length */
static const void *stream(unsigned name, size_t need) { if (!name || name >= g_nBuf) return NULL; const SBuf *b = &g_buf[name]; return (b->p && (size_t)b->size >= need) ? b->p : NULL; }

/* ---- textures: a few sizes of each, none bigger than the cap, because a 320 x 240 picture never shows more ---------------------------------------------
 * A mesh's texture wraps, so every size kept is a power of two each way: one that isn't is shrunk to the next power down. A sprite sheet is kept exactly as it came
 * (gfxTextureNear), pixel for pixel, and clamps. Shrinking weighs each colour by its alpha, so a cutout's edge isn't darkened by the clear pixels beside it. */
#define LV_MAX 14
typedef struct { uint32_t *lv[LV_MAX]; int n, w, h; unsigned char holes, kind; } STex;   /* kind: 0 a mesh's, 1 a sprite sheet, 2 a sheet of text or smoke (smooth) */
static STex *g_tex; static unsigned g_nTex;
static struct { unsigned name; int w, h; uint32_t *px; } g_raw;   /* the last small texture as it came, in case the next call says it's a sprite sheet */
static long g_texBytes;
static void texDrop(STex *t) { for (int i = 0; i < t->n; i++) { g_texBytes -= (long)(t->w >> i ? t->w >> i : 1) * (t->h >> i ? t->h >> i : 1) * 4; free(t->lv[i]); } memset(t, 0, sizeof *t); }
static uint32_t *texBox(const unsigned char *s, int w, int h, int tw, int th) {
    uint32_t *o = malloc((size_t)tw * th * 4); if (!o) return NULL;
    for (int y = 0; y < th; y++) { int y0 = (int)((long)y * h / th), y1 = (int)((long)(y + 1) * h / th); if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < tw; x++) { int x0 = (int)((long)x * w / tw), x1 = (int)((long)(x + 1) * w / tw); if (x1 <= x0) x1 = x0 + 1;
            uint32_t sa = 0, sr = 0, sg = 0, sb = 0, pr = 0, pg = 0, pb = 0, n = 0;
            for (int j = y0; j < y1; j++) { const unsigned char *p = s + ((size_t)j * w + x0) * 4; for (int i = x0; i < x1; i++, p += 4) { const uint32_t a = p[3]; sa += a; sr += p[0] * a; sg += p[1] * a; sb += p[2] * a; pr += p[0]; pg += p[1]; pb += p[2]; n++; } }
            if (sa) o[(size_t)y * tw + x] = PX((sr + sa / 2) / sa, (sg + sa / 2) / sa, (sb + sa / 2) / sa, (sa + n / 2) / n);
            else o[(size_t)y * tw + x] = PX((pr + n / 2) / n, (pg + n / 2) / n, (pb + n / 2) / n, 0); } }
    return o;
}
static uint32_t *texHalf(const uint32_t *s, int w, int h, int tw, int th) {
    uint32_t *o = malloc((size_t)tw * th * 4); if (!o) return NULL;
    for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) { uint32_t sa = 0, sr = 0, sg = 0, sb = 0, pr = 0, pg = 0, pb = 0, n = 0;
        for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) { int xx = x * 2 + i, yy = y * 2 + j; if (xx >= w) xx = w - 1; if (yy >= h) yy = h - 1; const uint32_t c = s[(size_t)yy * w + xx], a = c >> 24, r = (c >> 16) & 255u, g = (c >> 8) & 255u, b = c & 255u;
            sa += a; sr += r * a; sg += g * a; sb += b * a; pr += r; pg += g; pb += b; n++; }
        if (sa) o[(size_t)y * tw + x] = PX((sr + sa / 2) / sa, (sg + sa / 2) / sa, (sb + sa / 2) / sa, (sa + n / 2) / n);
        else o[(size_t)y * tw + x] = PX((pr + n / 2) / n, (pg + n / 2) / n, (pb + n / 2) / n, 0); }
    return o;
}
void gfxTexture(unsigned name, int w, int h, const void *rgba) {
    if (!name || !rgba || w < 1 || h < 1 || w > 32768 || h > 32768) return;
    if (name >= g_nTex) { unsigned n = name + 1024; g_tex = realloc(g_tex, n * sizeof(STex)); memset(g_tex + g_nTex, 0, (n - g_nTex) * sizeof(STex)); g_nTex = n; }
    STex *t = &g_tex[name]; texDrop(t); const unsigned char *p = rgba;
    for (long i = 0, n = (long)w * h; i < n; i++) if (p[i * 4 + 3] < 128) { t->holes = 1; break; }   /* a texture with holes in it is a cutout wherever it's used whole: the same rule as the Vulkan renderer's */
    int tw = 1, th = 1; while (tw * 2 <= w && tw * 2 <= g_texCap) tw *= 2; while (th * 2 <= h && th * 2 <= g_texCap) th *= 2;
    t->lv[0] = texBox(p, w, h, tw, th); if (!t->lv[0]) return; t->w = tw; t->h = th; t->n = 1; g_texBytes += (long)tw * th * 4;
    while ((tw > 1 || th > 1) && t->n < LV_MAX) { const int nw = tw > 1 ? tw / 2 : 1, nh = th > 1 ? th / 2 : 1; t->lv[t->n] = texHalf(t->lv[t->n - 1], tw, th, nw, nh); if (!t->lv[t->n]) break; t->n++; tw = nw; th = nh; g_texBytes += (long)tw * th * 4; }
    free(g_raw.px); g_raw.px = NULL; g_raw.name = 0;
    if ((long)w * h <= 1024L * 1024L) { g_raw.px = texBox(p, w, h, w, h); g_raw.name = name; g_raw.w = w; g_raw.h = h; }
}
void gfxTextureNear(unsigned name) {
    if (!name || name >= g_nTex || !g_tex[name].n) return;
    STex *t = &g_tex[name];
    if (g_raw.name == name && g_raw.px) { const unsigned char holes = t->holes; texDrop(t); t->lv[0] = g_raw.px; t->w = g_raw.w; t->h = g_raw.h; t->n = 1; t->holes = holes; g_texBytes += (long)t->w * t->h * 4; g_raw.px = NULL; g_raw.name = 0; }
    t->kind = 1;
}
int gfxShaderNew(const uint32_t *spv, int words, int kind) { (void)spv; (void)words; (void)kind; return 0; }
void gfxSkyShader(int shader) { (void)shader; }   /* (11-12) a surface's own shader: none on the processor, so its meshes are drawn plainly */
void gfxShaderTexture(int shader, int binding, int w, int h, int layers, const void *rgba, int flags) { (void)shader; (void)binding; (void)w; (void)h; (void)layers; (void)rgba; (void)flags; }
void gfxShaderDrop(int shader) { (void)shader; }
/* (11-12) part of a texture again: the processor's copies are its texture made to powers of two and halved, so a part can't be put back without the
 * whole; a game's painted picture stays as it was first given here (said once) */
void gfxTextureSub(unsigned name, int x, int y, int w, int h, const void *rgba) { (void)name; (void)x; (void)y; (void)w; (void)h; (void)rgba; static int said; if (!said) { said = 1; printf("[soft] a picture painted part by part isn't redrawn on the processor\n"); fflush(stdout); } }
void gfxTextureHud(unsigned name) { if (name && name < g_nTex && g_tex[name].n) g_tex[name].kind = 2; }
void gfxTextureFree(unsigned name) { if (!name || name >= g_nTex) return; texDrop(&g_tex[name]); if (g_raw.name == name) { free(g_raw.px); g_raw.px = NULL; g_raw.name = 0; } }
static const STex *texOf(unsigned name) { return (name && name < g_nTex && g_tex[name].n) ? &g_tex[name] : NULL; }

/* ---- what a budget version doesn't have: the calls are answered, and nothing's drawn for them ------------------------------------------------------------ */
void gfxMemSay(void) { long b = 0; for (unsigned i = 0; i < g_nBuf; i++) b += g_buf[i].size; printf("[mem] the rasterizer holds %.1f MB of geometry and %.1f MB of textures (none bigger than %d a side)\n", (double)b / 1048576.0, (double)g_texBytes / 1048576.0, g_texCap); fflush(stdout); }
void gfxUploadAsync(int on) { (void)on; }
void gfxTargetMake(unsigned tex, int w, int h, int nearest) { (void)tex; (void)w; (void)h; (void)nearest; }   /* a picture drawn off-screen: its texture stays empty, and a mesh that wears it shows its tint */
void gfxFeed(unsigned tex, const float *vp, const float *eye) { (void)tex; (void)vp; (void)eye; }
void gfxScreen(unsigned tex, const float *model, float amp) { (void)tex; (void)model; (void)amp; }
void gfxHead(const GfxHead *h) { (void)h; }
void gfxFloorShadow(unsigned tex, const float *vp, const float *rect) { (void)tex; (void)vp; (void)rect; }
int gfxHasBC(void) { return 0; }      /* a cooked texture comes as plain pixels then: the engine decodes its top level (engine/load.inc) */
int gfxHasBC7(void) { return 0; }
int gfxCompute(void) { return 0; }   /* (11-06) no compute here: the engine simulates its hair itself */
int gfxXrOn(void) { return 0; }      /* (11-11) a headset is the Vulkan renderer's */
int gfxXrFrame(GfxXrEye eye[2], GfxXrHand hand[2]) { (void)eye; (void)hand; return 0; }
void gfxXrEyeSize(int *w, int *h) { *w = *h = 0; }
void gfxXrBuzz(int hand, float amount, float seconds) { (void)hand; (void)amount; (void)seconds; }
int gfxXrQuit(void) { return 0; }
float gfxXrScale(void) { return 0.f; }
int gfxBufferRead(unsigned name, void *data, long size) { (void)name; (void)data; (void)size; return 0; }
void gfxBufferGpu(unsigned name, long size, const void *data) { gfxBufferData(name, size, data, 0); }
void gfxTextureBC(unsigned name, int w, int h, int levels, int fmt, const void *data, int holes) { (void)name; (void)w; (void)h; (void)levels; (void)fmt; (void)data; (void)holes; }
void gfxTexturePage(unsigned name) { (void)name; }
void gfxPagesBuild(void) {}
void gfxEnvCube(const unsigned char *rgba, int size, const float *at) { (void)rgba; (void)size; (void)at; }
void gfxProbes(const unsigned char *rgba, int w, int h, int d, const float *geo) { (void)rgba; (void)w; (void)h; (void)d; (void)geo; }
void gfxLut(const unsigned char *rgba, int n) { (void)rgba; (void)n; }
void gfxConfig(const GfxConfig *c) { g_cfg = *c; }
void gfxTube(const float *p) { (void)p; }
float gfxShownAspect(void) { return 0.f; }
void gfxShotSize(int *w, int *h) { *w = g_W; *h = g_H; }
int gfxResize(int *W, int *H) { (void)W; (void)H; return 0; }
/* (11-12) its picture is its own size, scaled to the window each frame (SDL_BlitScaled): nothing to make again; gfxPointer is beside present(), below.
 * A shot is the picture, and so is everything the engine lays out on */
int gfxPointer(float u, float v, float *px, float *py);
double gfxWaited(void) { return 0.0; }
double gfxIdleWaited(void) { return 0.0; }
void gfxDrain(void) { }
int gfxDutyOwn(void) { return 0; }
int gfxLines(void) { return -1; }
unsigned gfxVblankSeq(void) { return 0u; }
double gfxVblankAt(void) { return 0.0; }
double gfxFieldRate(void) { return 0.0; }
void gfxBackBetween(void) {}
void gfxBackBegin(void) {}
void gfxToonSet(float cel, float rim, float keyFromLens, float roomGradient, float ink, const float *inkRGB) { (void)inkRGB; g_toon.cel = cel; g_toon.rim = rim; g_toon.keyCam = keyFromLens; g_toon.roomG = roomGradient; g_toon.ink = ink; }
void gfxBackStats(int *calls, int *binds, int *pages, int *stretched) { *calls = g_nCalls; *binds = 0; *pages = 0; *stretched = 0; }

/* ---- tables, made once from the club's own math, so a pixel loop calls nothing ------------------------------------------------------------------------- */
static float g_expLut[1026];      /* exp(-x), x from 0 to 8 in steps of 1/128 */
static int g_softLut[258];        /* an effect's fade from its middle: exp(-r2 * 3.2) * 256, r2 from 0 to 2 in steps of 1/128 */
static int g_dotLut[66];          /* a glowing point's fall-off: exp(-d2 * 11) * 256, d2 from 0 to 0.5 in steps of 1/128 */
static void lutsMake(void) {
    for (int i = 0; i < 1026; i++) g_expLut[i] = (float)club_exp(-(double)i / 128.0);
    for (int i = 0; i < 258; i++) g_softLut[i] = (int)(club_exp(-(double)i / 128.0 * 3.2) * 256.0 + 0.5);
    for (int i = 0; i < 66; i++) g_dotLut[i] = (int)(club_exp(-(double)i / 128.0 * 11.0) * 256.0 + 0.5);
}
static inline float expNeg(float x) { if (!(x > 0.f)) return 1.f; if (x >= 8.f) return 0.f; const float f = x * 128.f; const int i = (int)f; return g_expLut[i] + (g_expLut[i + 1] - g_expLut[i]) * (f - (float)i); }

/* ---- a triangle ------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct { float x, y, z, w, u, v, c[4]; } SV;                 /* a vertex in clip space (GL's: z from -w to w), its texture coordinate, and four numbers that ride with it */
typedef struct { int X, Y; float z, q, uq, vq, cq[4]; } SP;         /* on the picture: sixteenths of a pixel, depth 0 to 1, 1 over w, and the rest over w, so they can be stepped straight across */
enum { BL_NONE, BL_ALPHA, BL_ADD };
enum { TM_FLAT, TM_TEX, TM_MIX, TM_ALPHA };                          /* a pixel's own colour: the tint; the texture; the two mixed; white, with the texture's alpha */
enum { CM_NONE, CM_FOG, CM_ALPHA };                                  /* what a vertex's fourth number is: nothing; how much of the pixel is left after fog; its alpha */
typedef struct {
    const STex *tex; int texMode, blend, ztest, zwrite, cut, soft, cmode, nocolor;
    int tint[3], useTex, alpha;          /* the tint 0..255; how much of the texture 0..256; the draw's own alpha 0..256 */
    int fog[3];
    int x0, y0, x1, y1;                  /* the part of the picture it may touch */
} Paint;
#define GUARD 64.f   /* how far outside the picture, in pictures, a triangle may reach before it's cut: its corners then fit a whole number with room to spare */
static void tri(const SP *A, const SP *B, const SP *C, const Paint *P) {
    int64_t area = (int64_t)(B->X - A->X) * (C->Y - A->Y) - (int64_t)(C->X - A->X) * (B->Y - A->Y);
    if (area == 0) return;
    if (area < 0) { const SP *t = B; B = C; C = t; area = -area; }      /* either way round: nothing's culled, as on the Vulkan renderer */
    int minX = A->X < B->X ? A->X : B->X, maxX = A->X > B->X ? A->X : B->X, minY = A->Y < B->Y ? A->Y : B->Y, maxY = A->Y > B->Y ? A->Y : B->Y;
    if (C->X < minX) minX = C->X;
    if (C->X > maxX) maxX = C->X;
    if (C->Y < minY) minY = C->Y;
    if (C->Y > maxY) maxY = C->Y;
    int x0 = (minX + 7) >> 4, x1 = (maxX + 8) >> 4, y0 = (minY + 7) >> 4, y1 = (maxY + 8) >> 4;   /* the pixels whose middles can be inside */
    if (x0 < P->x0) x0 = P->x0;
    if (x1 > P->x1) x1 = P->x1;
    if (y0 < P->y0) y0 = P->y0;
    if (y1 > P->y1) y1 = P->y1;
    if (x0 >= x1 || y0 >= y1) return;
    /* an edge's number at a point is not negative inside. A pixel exactly on an edge belongs to the triangle whose top or left edge it is, and to no other */
    const int64_t dxAB = B->X - A->X, dyAB = B->Y - A->Y, dxBC = C->X - B->X, dyBC = C->Y - B->Y, dxCA = A->X - C->X, dyCA = A->Y - C->Y;
    const int64_t px = (int64_t)x0 * 16 + 8, py = (int64_t)y0 * 16 + 8;
    int64_t rAB = dxAB * (py - A->Y) - dyAB * (px - A->X), rBC = dxBC * (py - B->Y) - dyBC * (px - B->X), rCA = dxCA * (py - C->Y) - dyCA * (px - C->X);
    if (!(dyAB < 0 || (dyAB == 0 && dxAB > 0))) rAB -= 1;
    if (!(dyBC < 0 || (dyBC == 0 && dxBC > 0))) rBC -= 1;
    if (!(dyCA < 0 || (dyCA == 0 && dxCA > 0))) rCA -= 1;
    /* how each number changes a pixel across and a pixel down */
    const float ax = (float)A->X * 0.0625f, ay = (float)A->Y * 0.0625f, bx = (float)B->X * 0.0625f - ax, by = (float)B->Y * 0.0625f - ay, cx = (float)C->X * 0.0625f - ax, cy = (float)C->Y * 0.0625f - ay;
    const float ia = 1.f / ((float)area * (1.f / 256.f)), sx = (float)x0 + 0.5f - ax, sy = (float)y0 + 0.5f - ay;
    const float *fa = &A->z, *fb2 = &B->z, *fc = &C->z; float gx[8], gy[8], g0[8];   /* depth, 1 over w, and the six that ride with them: eight floats in a row in an SP */
    for (int k = 0; k < 8; k++) { const float db = fb2[k] - fa[k], dc = fc[k] - fa[k]; gx[k] = (db * cy - dc * by) * ia; gy[k] = (dc * bx - db * cx) * ia; g0[k] = fa[k] + gx[k] * sx + gy[k] * sy; }
    /* which size of the texture: the one nearest a texel to a pixel, for the whole triangle */
    const STex *T = P->tex; const uint32_t *tp = NULL; int tw = 1, th = 1, wrap = 0; float ub = 0.f, vb = 0.f, twf = 1.f, thf = 1.f;
    if (T && P->texMode != TM_FLAT) { int lv = 0;
        const float uA = A->uq / A->q, vA = A->vq / A->q, uB = B->uq / B->q, vB = B->vq / B->q, uC = C->uq / C->q, vC = C->vq / C->q;
        if (T->kind != 1 && T->n > 1) { float ta = (uB - uA) * (vC - vA) - (uC - uA) * (vB - vA); if (ta < 0.f) ta = -ta;
            float ratio = ta * (float)T->w * (float)T->h * ia; while (ratio > 2.25f && lv < T->n - 1) { ratio *= 0.25f; lv++; } }
        tw = T->w >> lv; th = T->h >> lv; if (tw < 1) tw = 1;
        if (th < 1) th = 1;
        tp = T->lv[lv]; twf = (float)tw; thf = (float)th;
        if (T->kind == 0) { wrap = 1; float m = uA < uB ? uA : uB; if (uC < m) m = uC;
            ub = floorf(m) - 1.f; m = vA < vB ? vA : vB; if (vC < m) m = vC;
            vb = floorf(m) - 1.f; } }
    const int texMode = tp ? P->texMode : TM_FLAT, blend = P->blend, cmode = P->cmode, soft = P->soft, smooth = g_smooth || (T && T->kind == 2);
    const int flatR = P->texMode == TM_FLAT ? P->tint[0] : P->texMode == TM_MIX ? (P->tint[0] * (256 - P->useTex) + 255 * P->useTex) >> 8 : 255;   /* with no texture to read, a texture is white */
    const int flatG = P->texMode == TM_FLAT ? P->tint[1] : P->texMode == TM_MIX ? (P->tint[1] * (256 - P->useTex) + 255 * P->useTex) >> 8 : 255;
    const int flatB = P->texMode == TM_FLAT ? P->tint[2] : P->texMode == TM_MIX ? (P->tint[2] * (256 - P->useTex) + 255 * P->useTex) >> 8 : 255;
    const int64_t stAB = -dyAB * 16, stBC = -dyBC * 16, stCA = -dyCA * 16;
    g_nTriDrawn++;
    for (int y = y0; y < y1; y++) { const float j = (float)(y - y0);
        int64_t eAB = rAB, eBC = rBC, eCA = rCA; rAB += dxAB * 16; rBC += dxBC * 16; rCA += dxCA * 16;
        float z = g0[0] + gy[0] * j, q = g0[1] + gy[1] * j, uq = g0[2] + gy[2] * j, vq = g0[3] + gy[3] * j, c0 = g0[4] + gy[4] * j, c1 = g0[5] + gy[5] * j, c2 = g0[6] + gy[6] * j, c3 = g0[7] + gy[7] * j;
        uint32_t *fb = g_fb + (size_t)y * g_W; float *zb = g_zb + (size_t)y * g_W;
        for (int x = x0; x < x1; x++, eAB += stAB, eBC += stBC, eCA += stCA, z += gx[0], q += gx[1], uq += gx[2], vq += gx[3], c0 += gx[4], c1 += gx[5], c2 += gx[6], c3 += gx[7]) {
            if ((eAB | eBC | eCA) < 0) continue;
            if (P->ztest && !(z <= zb[x])) continue;
            if (!(q > 1e-20f)) continue;
            const float w = 1.f / q;
            int tr = flatR, tg = flatG, tb = flatB, ta = 255;
            float fu = 0.f, fv = 0.f;
            if (texMode != TM_FLAT || soft) { fu = uq * w; fv = vq * w; }
            if (texMode != TM_FLAT) {
                float uu = fu - ub, vv = fv - vb; uint32_t t;
                if (wrap) { if (!(uu >= 0.f && uu < 8192.f)) uu = 0.f; if (!(vv >= 0.f && vv < 8192.f)) vv = 0.f; }
                else { if (!(uu > 0.f)) uu = 0.f; else if (uu > 1.f) uu = 1.f; if (!(vv > 0.f)) vv = 0.f; else if (vv > 1.f) vv = 1.f; }
                if (!smooth) { int tx = (int)(uu * twf), ty = (int)(vv * thf);
                    if (wrap) { tx &= tw - 1; ty &= th - 1; } else { if (tx >= tw) tx = tw - 1; if (ty >= th) ty = th - 1; }
                    t = tp[(size_t)ty * tw + tx]; }
                else {   /* four texels, mixed by how far between them the pixel falls: whole numbers */
                    int ux = (int)(uu * twf * 256.f) - 128, vy = (int)(vv * thf * 256.f) - 128; if (!wrap) { if (ux < 0) ux = 0; if (vy < 0) vy = 0; }
                    int tx = ux >> 8, ty = vy >> 8, tx1 = tx + 1, ty1 = ty + 1; const int kx = ux & 255, ky = vy & 255;
                    if (wrap) { tx &= tw - 1; ty &= th - 1; tx1 &= tw - 1; ty1 &= th - 1; } else { if (tx >= tw) tx = tw - 1; if (ty >= th) ty = th - 1; if (tx1 >= tw) tx1 = tw - 1; if (ty1 >= th) ty1 = th - 1; }
                    const uint32_t p00 = tp[(size_t)ty * tw + tx], p10 = tp[(size_t)ty * tw + tx1], p01 = tp[(size_t)ty1 * tw + tx], p11 = tp[(size_t)ty1 * tw + tx1];
                    const uint32_t w00 = (uint32_t)((256 - kx) * (256 - ky)), w10 = (uint32_t)(kx * (256 - ky)), w01 = (uint32_t)((256 - kx) * ky), w11 = (uint32_t)(kx * ky);
#define CH(s) ((((p00 >> s) & 255u) * w00 + ((p10 >> s) & 255u) * w10 + ((p01 >> s) & 255u) * w01 + ((p11 >> s) & 255u) * w11 + 32768u) >> 16)
                    t = (CH(24) << 24) | (CH(16) << 16) | (CH(8) << 8) | CH(0);
#undef CH
                }
                ta = (int)(t >> 24);
                if (texMode == TM_TEX) { tr = (int)((t >> 16) & 255u); tg = (int)((t >> 8) & 255u); tb = (int)(t & 255u); }
                else if (texMode == TM_MIX) { const int ut = P->useTex; tr = (P->tint[0] * (256 - ut) + (int)((t >> 16) & 255u) * ut) >> 8; tg = (P->tint[1] * (256 - ut) + (int)((t >> 8) & 255u) * ut) >> 8; tb = (P->tint[2] * (256 - ut) + (int)(t & 255u) * ut) >> 8; ta = (255 * (256 - ut) + ta * ut) >> 8; }
                else { tr = tg = tb = 255; }
                if (P->cut && ta < 128) continue;
            }
            if (P->nocolor) { if (P->zwrite) zb[x] = z; continue; }
            int r = (tr * f2i(c0 * w * 256.f, 1023)) >> 8, g = (tg * f2i(c1 * w * 256.f, 1023)) >> 8, b = (tb * f2i(c2 * w * 256.f, 1023)) >> 8;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            int a = P->alpha;
            if (cmode == CM_FOG) { const int f = f2i(c3 * w * 256.f, 256); r = (r * f + P->fog[0] * (256 - f)) >> 8; g = (g * f + P->fog[1] * (256 - f)) >> 8; b = (b * f + P->fog[2] * (256 - f)) >> 8; }
            else if (cmode == CM_ALPHA) a = (a * f2i(c3 * w * 256.f, 256)) >> 8;
            if (blend == BL_NONE) { fb[x] = PX(r, g, b, 0); if (P->zwrite) zb[x] = z; g_nPix++;
                if (g_who) { unsigned char *o9 = &g_who[&fb[x] - g_fb]; if (*o9) g_kindHid[*o9 - 1]++; *o9 = (unsigned char)(g_whoKind + 1); }   /* (11-07) REVA_SOFTSAY=2 */
                continue; }
            a = (a * (ta + (ta >> 7))) >> 8;
            if (soft) a = (a * g_softLut[f2i((fu * fu + fv * fv) * 128.f, 256)]) >> 8;
            if (a <= 0) continue;
            const uint32_t d = fb[x]; const int dr = (int)((d >> 16) & 255u), dg = (int)((d >> 8) & 255u), db = (int)(d & 255u);
            if (blend == BL_ALPHA) { r = (r * a + dr * (256 - a)) >> 8; g = (g * a + dg * (256 - a)) >> 8; b = (b * a + db * (256 - a)) >> 8; }
            else { r = dr + ((r * a) >> 8); g = dg + ((g * a) >> 8); b = db + ((b * a) >> 8); if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255; }
            fb[x] = PX(r, g, b, 0); if (P->zwrite) zb[x] = z;
            g_nPix++; if (g_who) g_kindBl[g_whoKind]++;
        } }
}

/* a vertex onto the picture. vr: the view's left, top, width and height */
static inline void project(SP *o, const SV *v, const float *vr) {
    const float q = 1.f / v->w;
    o->X = (int)((double)((v->x * q * 0.5f + 0.5f) * vr[2] + vr[0]) * 16.0 + 1048576.5) - 1048576;
    o->Y = (int)((double)((0.5f - v->y * q * 0.5f) * vr[3] + vr[1]) * 16.0 + 1048576.5) - 1048576;
    o->z = v->z * q * 0.5f + 0.5f; o->q = q; o->uq = v->u * q; o->vq = v->v * q; o->cq[0] = v->c[0] * q; o->cq[1] = v->c[1] * q; o->cq[2] = v->c[2] * q; o->cq[3] = v->c[3] * q;
}
/* which sides of the room it can be drawn in a vertex is outside of: bit 0 nearer than the near plane (or behind the lens), 1 to 4 past the guard band, 6 not a number at all */
static inline int codeOf(const SV *v) {
    int c = 0;
    if (!(v->x - v->x == 0.f && v->y - v->y == 0.f && v->z - v->z == 0.f && v->w - v->w == 0.f)) return 64;
    if (v->z < -v->w || v->w < 1e-6f) c |= 1;
    if (v->x < -GUARD * v->w) c |= 2;
    if (v->x > GUARD * v->w) c |= 4;
    if (v->y < -GUARD * v->w) c |= 8;
    if (v->y > GUARD * v->w) c |= 16;
    return c;
}
/* and which sides of the picture itself: three corners all off the same side is a triangle nobody sees */
static inline int offOf(const SV *v) { return (v->x < -v->w ? 1 : 0) | (v->x > v->w ? 2 : 0) | (v->y < -v->w ? 4 : 0) | (v->y > v->w ? 8 : 0) | (v->z < -v->w ? 16 : 0) | (v->z > v->w ? 32 : 0); }
static inline float planeD(const SV *v, int k) { return k == 0 ? v->z + v->w : k == 1 ? v->w - 1e-6f : k == 2 ? v->x + GUARD * v->w : k == 3 ? GUARD * v->w - v->x : k == 4 ? v->y + GUARD * v->w : GUARD * v->w - v->y; }
/* a triangle that crosses the near plane or the guard band, cut to what's inside and drawn as a fan */
static void triClip(const SV *a, const SV *b, const SV *c, const float *vr, const Paint *P) {
    SV buf[2][12]; int n = 3, cur = 0; buf[0][0] = *a; buf[0][1] = *b; buf[0][2] = *c;
    for (int k = 0; k < 6 && n >= 3; k++) { const SV *in = buf[cur]; SV *out = buf[cur ^ 1]; int m = 0;
        for (int i = 0; i < n; i++) { const SV *p = &in[i], *q = &in[(i + 1) % n]; const float dp = planeD(p, k), dq = planeD(q, k);
            if (dp >= 0.f) out[m++] = *p;
            if ((dp >= 0.f) != (dq >= 0.f)) { const float t = dp / (dp - dq); SV *o = &out[m++]; const float *pf = &p->x, *qf = &q->x; float *of = &o->x; for (int f = 0; f < 10; f++) of[f] = pf[f] + (qf[f] - pf[f]) * t; } }
        n = m; cur ^= 1; }
    if (n < 3) return;
    SP sp[12]; for (int i = 0; i < n; i++) { if (!(buf[cur][i].w > 0.f)) return; project(&sp[i], &buf[cur][i], vr); }
    for (int i = 1; i + 1 < n; i++) tri(&sp[0], &sp[i], &sp[i + 1], P);
}
/* three vertices already in clip space, however they lie */
static void triAny(const SV *a, const SV *b, const SV *c, const float *vr, const Paint *P) {
    const int ca = codeOf(a), cb = codeOf(b), cc = codeOf(c);
    g_nTri++;
    if ((ca | cb | cc) & 64) return;
    if (offOf(a) & offOf(b) & offOf(c)) return;
    if (ca | cb | cc) { triClip(a, b, c, vr, P); return; }
    SP sa, sb, sc; project(&sa, a, vr); project(&sb, b, vr); project(&sc, c, vr); tri(&sa, &sb, &sc, P);
}

/* ---- the frame's own numbers ---------------------------------------------------------------------------------------------------------------------------- */
typedef struct { float pos[3], col[3], falloff, dir[3], cosO, cosI; } SLight;
typedef struct {
    const float *vp, *eye; float vr[4]; int x0, y0, x1, y1;      /* the lens and its part of the picture */
    float sun[3], sunC[3], amb[3]; int sunOn;
    float fogD; int fog[3];
    float knee; int kneeOn; float kneeLut[258];                  /* the highlight shoulder, by table: a light's strength from 0 to 4 */
    float toonFull[3];
    const GfxLight *lights; int nLights;
    float tsec;
} Scene;
static Scene g_sc;
static inline float kneeOf(float v) { if (!g_sc.kneeOn) return v; if (!(v > 0.f)) return 0.f; if (v >= 4.f) return g_sc.kneeLut[256]; const float f = v * 64.f; const int i = (int)f; return g_sc.kneeLut[i] + (g_sc.kneeLut[i + 1] - g_sc.kneeLut[i]) * (f - (float)i); }
static void mul44(float *r, const float *a, const float *b) { for (int c = 0; c < 4; c++) for (int i = 0; i < 4; i++) r[c * 4 + i] = a[0 * 4 + i] * b[c * 4 + 0] + a[1 * 4 + i] * b[c * 4 + 1] + a[2 * 4 + i] * b[c * 4 + 2] + a[3 * 4 + i] * b[c * 4 + 3]; }
static int inv44(const float *m, float *o) {
    float a[4][8]; for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { a[r][c] = m[c * 4 + r]; a[r][4 + c] = r == c ? 1.f : 0.f; }
    for (int c = 0; c < 4; c++) { int p = c; float best = a[c][c] < 0.f ? -a[c][c] : a[c][c];
        for (int r = c + 1; r < 4; r++) { const float v = a[r][c] < 0.f ? -a[r][c] : a[r][c]; if (v > best) { best = v; p = r; } }
        if (!(best > 1e-20f)) return 0;
        if (p != c) for (int k = 0; k < 8; k++) { const float t = a[c][k]; a[c][k] = a[p][k]; a[p][k] = t; }
        const float d = 1.f / a[c][c]; for (int k = 0; k < 8; k++) a[c][k] *= d;
        for (int r = 0; r < 4; r++) if (r != c) { const float f = a[r][c]; if (f != 0.f) for (int k = 0; k < 8; k++) a[r][k] -= f * a[c][k]; } }
    for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) o[c * 4 + r] = a[r][4 + c];
    return 1;
}

/* ---- a mesh ------------------------------------------------------------------------------------------------------------------------------------------ */
static SV *g_sv; static SP *g_sp; static unsigned char *g_code, *g_off; static unsigned g_svCap;
enum { LOOK_PLAIN, LOOK_HOLO, LOOK_WATER };
/* how: 0 solid, 1 blended, 2 a hologram's depth alone, 3 a hologram's light */
static void meshDraw(const GfxPacket *pk, const GfxDraw *d, int how) {
    const unsigned nv = d->nVerts; if (!nv || nv > (1u << 24)) return;
    const float *pos = d->posOn ? ((size_t)d->posOff + (size_t)nv * 12 <= pk->memUsed ? (const float *)(pk->mem + d->posOff) : NULL) : (const float *)stream(d->vb[0], (size_t)nv * 12);
    if (!pos) return;
    const float *nrm = (const float *)stream(d->vb[1], (size_t)nv * 12), *uv = (const float *)stream(d->vb[5], (size_t)nv * 8);
    const unsigned char *jo = (const unsigned char *)stream(d->vb[2], (size_t)nv * 4), *we = (const unsigned char *)stream(d->vb[3], (size_t)nv * 4);
    const uint32_t *idx = NULL; if (d->nIndex) { idx = (const uint32_t *)stream(d->vb[4], (size_t)d->nIndex * 4); if (!idx) return; }
    const unsigned bones = d->bones; const float *pal = NULL;
    if (bones) { if (!jo || !we || (size_t)d->palOff + (size_t)bones * 48 > pk->memUsed) return; pal = (const float *)(pk->mem + d->palOff); }
    const float *M = d->model; float mvp[16]; mul44(mvp, g_sc.vp, M);
    /* a mesh with no bones that's wholly off the picture is left before a vertex of it is touched: its box's corners, all off one side */
    if (!bones && !d->posOn) { SBuf *b = &g_buf[d->vb[0]];
        if (b->boxN != (long)nv) { float lo[3] = {pos[0], pos[1], pos[2]}, hi[3] = {pos[0], pos[1], pos[2]}; int bad = 0;
            for (unsigned i = 0; i < nv; i++) for (int k = 0; k < 3; k++) { const float v = pos[i * 3 + k]; if (!(v - v == 0.f)) bad = 1; else { if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; } }
            if (bad) { lo[0] = lo[1] = lo[2] = -1e18f; hi[0] = hi[1] = hi[2] = 1e18f; }
            memcpy(b->box, lo, 12); memcpy(b->box + 3, hi, 12); b->boxN = (long)nv; }
        if (b->box[0] > -1e17f) { int all = 63; for (int c = 0; c < 8 && all; c++) { SV t; const float x = b->box[(c & 1) ? 3 : 0], y = b->box[(c & 2) ? 4 : 1], z = b->box[(c & 4) ? 5 : 2];
                t.x = mvp[0] * x + mvp[4] * y + mvp[8] * z + mvp[12]; t.y = mvp[1] * x + mvp[5] * y + mvp[9] * z + mvp[13]; t.z = mvp[2] * x + mvp[6] * y + mvp[10] * z + mvp[14]; t.w = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15]; all &= offOf(&t); }
            if (all) return; } }
    if (nv > g_svCap) { g_svCap = nv + 4096; free(g_sv); free(g_sp); free(g_code); free(g_off); g_sv = malloc(sizeof(SV) * g_svCap); g_sp = malloc(sizeof(SP) * g_svCap); g_code = malloc(g_svCap); g_off = malloc(g_svCap); if (!g_sv || !g_sp || !g_code || !g_off) { g_svCap = 0; return; } }
    const int look = (d->flags & GFX_WATER) ? LOOK_WATER : how >= 2 ? LOOK_HOLO : LOOK_PLAIN;
    const int glow = (d->flags & GFX_GLOW) != 0, toon = (d->flags & GFX_TOON) != 0 && look == LOOK_PLAIN, room = (d->flags & GFX_TOONROOM) != 0;
    const int fogOn = g_sc.fogD > 0.f && look != LOOK_WATER;
    /* the lamps that reach this mesh most, by where it stands: a handful, because every vertex asks each of them */
    SLight L[8]; int nL = 0;
    if (g_maxLights > 0 && g_sc.nLights > 0 && !glow && look == LOOK_PLAIN) { float best[8]; const float cx = M[12], cy = M[13] + (bones ? 1.5f : 0.f), cz = M[14];
        for (int i = 0; i < g_sc.nLights; i++) { const GfxLight *g = &g_sc.lights[i]; const float dx = g->pos[0] - cx, dy = g->pos[1] - cy, dz = g->pos[2] - cz; float dd = dx * dx + dy * dy + dz * dz;
            float m = g->col[0] > g->col[1] ? g->col[0] : g->col[1]; if (g->col[2] > m) m = g->col[2];
            const float s = m / (1.f + dd * g->falloff); if (!(s > 0.02f)) continue;
            int at = nL < g_maxLights ? nL : -1; if (at < 0) { int lo = 0; for (int k = 1; k < nL; k++) if (best[k] < best[lo]) lo = k; if (best[lo] < s) at = lo; }
            if (at < 0) continue;
            SLight *o = &L[at]; memcpy(o->pos, g->pos, 12); memcpy(o->col, g->col, 12); o->falloff = g->falloff; memcpy(o->dir, g->dir, 12); o->cosI = g->cosInner;
            o->cosO = (g->dir[0] * g->dir[0] + g->dir[1] * g->dir[1] + g->dir[2] * g->dir[2] > 0.01f) ? g->cosOuter : -2.f; best[at] = s; if (at == nL) nL++; } }
    const int needW = fogOn || nL > 0 || look != LOOK_PLAIN;
    /* the toon pass's key: from the sun, or from above and to the right of the lens for a body, as a drawn show keys its characters. Worked out once for the mesh, from where it stands */
    float key[3] = {g_sc.sun[0], g_sc.sun[1], g_sc.sun[2]};
    if (toon && !room && g_toon.keyCam > 0.5f) { float vx = g_sc.eye[0] - M[12], vy = g_sc.eye[1] - M[13], vz = g_sc.eye[2] - M[14]; float l = sqrtf(vx * vx + vy * vy + vz * vz); if (l > 1e-6f) { vx /= l; vy /= l; vz /= l; }
        float rx = vz, rz = -vx; l = sqrtf(rx * rx + rz * rz); if (l > 1e-4f) { rx /= l; rz /= l; } else { rx = 1.f; rz = 0.f; }
        key[0] = vx * 0.75f + rx * 0.30f; key[1] = vy * 0.75f + 0.80f; key[2] = vz * 0.75f + rz * 0.30f; l = sqrtf(key[0] * key[0] + key[1] * key[1] + key[2] * key[2]); if (l > 1e-6f) { key[0] /= l; key[1] /= l; key[2] /= l; } }
    else if (toon && !g_sc.sunOn) { key[0] = 0.3965f; key[1] = 0.7930f; key[2] = 0.5898f; }
    for (unsigned i = 0; i < nv; i++) {
        float px = pos[i * 3], py = pos[i * 3 + 1], pz = pos[i * 3 + 2], nx = 0.f, ny = 1.f, nz = 0.f;
        if (nrm) { nx = nrm[i * 3]; ny = nrm[i * 3 + 1]; nz = nrm[i * 3 + 2]; }
        if (bones) {   /* its bones, mixed by its weights: three rows, as the packet carries them */
            const unsigned char *J = jo + i * 4, *Wt = we + i * 4; float a[4] = {0.f, 0.f, 0.f, 0.f}, b[4] = {0.f, 0.f, 0.f, 0.f}, c[4] = {0.f, 0.f, 0.f, 0.f};
            for (int k = 0; k < 4; k++) { if (!Wt[k]) continue; const float w = (float)Wt[k] * (1.f / 255.f); const float *B = pal + (size_t)(J[k] < bones ? J[k] : 0) * 12;
                a[0] += B[0] * w; a[1] += B[1] * w; a[2] += B[2] * w; a[3] += B[3] * w; b[0] += B[4] * w; b[1] += B[5] * w; b[2] += B[6] * w; b[3] += B[7] * w; c[0] += B[8] * w; c[1] += B[9] * w; c[2] += B[10] * w; c[3] += B[11] * w; }
            const float sx = a[0] * px + a[1] * py + a[2] * pz + a[3], sy = b[0] * px + b[1] * py + b[2] * pz + b[3], sz = c[0] * px + c[1] * py + c[2] * pz + c[3];
            const float tx = a[0] * nx + a[1] * ny + a[2] * nz, ty = b[0] * nx + b[1] * ny + b[2] * nz, tz = c[0] * nx + c[1] * ny + c[2] * nz;
            px = sx; py = sy; pz = sz; nx = tx; ny = ty; nz = tz; }
        SV *o = &g_sv[i];
        o->x = mvp[0] * px + mvp[4] * py + mvp[8] * pz + mvp[12]; o->y = mvp[1] * px + mvp[5] * py + mvp[9] * pz + mvp[13]; o->z = mvp[2] * px + mvp[6] * py + mvp[10] * pz + mvp[14]; o->w = mvp[3] * px + mvp[7] * py + mvp[11] * pz + mvp[15];
        const int code = codeOf(o); g_code[i] = (unsigned char)code; g_off[i] = (unsigned char)offOf(o);
        if (uv) { o->u = uv[i * 2]; o->v = uv[i * 2 + 1]; } else { o->u = o->v = 0.f; }
        /* the light on it, in the world */
        float wnx = M[0] * nx + M[4] * ny + M[8] * nz, wny = M[1] * nx + M[5] * ny + M[9] * nz, wnz = M[2] * nx + M[6] * ny + M[10] * nz;
        { const float l = sqrtf(wnx * wnx + wny * wny + wnz * wnz); if (l > 1e-12f) { const float il = 1.f / l; wnx *= il; wny *= il; wnz *= il; } else { wnx = 0.f; wny = 1.f; wnz = 0.f; } }
        float wx = 0.f, wy = 0.f, wz = 0.f, dist = 0.f, ex = 0.f, ey = 0.f, ez = 0.f;
        if (needW) { wx = M[0] * px + M[4] * py + M[8] * pz + M[12]; wy = M[1] * px + M[5] * py + M[9] * pz + M[13]; wz = M[2] * px + M[6] * py + M[10] * pz + M[14];
            ex = g_sc.eye[0] - wx; ey = g_sc.eye[1] - wy; ez = g_sc.eye[2] - wz; dist = sqrtf(ex * ex + ey * ey + ez * ez); }
        float lr, lg, lb, c3 = 1.f;
        if (look == LOOK_HOLO) {   /* a body made of light: her own colours under a purple cast, brighter at her edge */
            const float l = 0.30f + 0.70f * (wnx * 0.3965f + wny * 0.7930f + wnz * 0.5898f > 0.f ? wnx * 0.3965f + wny * 0.7930f + wnz * 0.5898f : 0.f);
            float fr = dist > 1e-6f ? (wnx * ex + wny * ey + wnz * ez) / dist : 1.f; if (fr < 0.f) fr = -fr;
            fr = 1.f - fr; if (fr < 0.f) fr = 0.f;
            const float k = 0.44f + 0.80f * l; lr = 0.88f * k + 0.9f * fr * 0.80f; lg = 0.72f * k + 0.4f * fr * 0.80f; lb = 1.14f * k + fr * 0.80f; c3 = 0.68f + 0.26f * fr; }
        else if (look == LOOK_WATER) {   /* a pool: its own colour looking down, the sky at a glancing angle */
            float nd = dist > 1e-6f ? (wnx * ex + wny * ey + wnz * ez) / dist : 1.f; if (nd < 0.f) { nd = -nd; wnx = -wnx; wny = -wny; wnz = -wnz; }
            if (nd > 1.f) nd = 1.f;
            const float k1 = 1.f - nd, F = 0.02f + 0.98f * k1 * k1 * k1 * k1 * k1; float ns = wnx * g_sc.sun[0] + wny * g_sc.sun[1] + wnz * g_sc.sun[2]; if (!(ns > 0.f)) ns = 0.f;
            const float deep = 0.42f + 0.58f * ns; static const float HZ[3] = {1.85f, 1.80f, 1.70f}, HA[3] = {0.10f, 0.11f, 0.12f}; float o3[3];
            for (int k = 0; k < 3; k++) o3[k] = d->tint[k] * deep * (1.f - F) + (g_sc.amb[k] * HZ[k] + HA[k]) * F;
            lr = o3[0]; lg = o3[1]; lb = o3[2]; c3 = 0.58f + 0.39f * F; }
        else if (glow) { lr = lg = lb = 1.f; }
        else if (toon) {   /* two tones: lit, and a cool heavy shadow. The cut's made at the vertex, which at this size is nearly at the pixel */
            const float kd = room ? 0.5f + 0.5f * wny : wnx * key[0] + wny * key[1] + wnz * key[2];
            if (kd >= g_toon.cel) { lr = g_sc.toonFull[0]; lg = g_sc.toonFull[1]; lb = g_sc.toonFull[2]; } else { lr = g_sc.toonFull[0] * 0.62f; lg = g_sc.toonFull[1] * 0.585f; lb = g_sc.toonFull[2] * 0.75f; } }
        else if (g_sc.sunOn) { float nl = wnx * g_sc.sun[0] + wny * g_sc.sun[1] + wnz * g_sc.sun[2]; if (!(nl > 0.f)) nl = 0.f;
            lr = g_sc.amb[0] + g_sc.sunC[0] * nl; lg = g_sc.amb[1] + g_sc.sunC[1] * nl; lb = g_sc.amb[2] + g_sc.sunC[2] * nl; }
        else { float l = wnx * 0.3965f + wny * 0.7930f + wnz * 0.5898f; if (!(l > 0.f)) l = 0.f;   /* no sun: the light every unlit scene has, from above and to one side */
            lr = lg = lb = (0.3f + 0.7f * l) * 0.42f + 0.13f; }
        for (int k = 0; k < nL; k++) { const SLight *g = &L[k]; const float dx = g->pos[0] - wx, dy = g->pos[1] - wy, dz = g->pos[2] - wz, dd = dx * dx + dy * dy + dz * dz, il = 1.f / sqrtf(dd + 0.001f);
            float f = (wnx * dx + wny * dy + wnz * dz) * il; if (!(f > 0.f)) continue;
            f /= 1.f + dd * g->falloff;
            if (g->cosO > -1.5f) { float t = (-(dx * g->dir[0] + dy * g->dir[1] + dz * g->dir[2]) * il - g->cosO) / (g->cosI - g->cosO); if (!(t > 0.f)) continue;
                if (t > 1.f) t = 1.f;
                f *= t * t * (3.f - 2.f * t); }
            if (toon) f *= 0.25f;
            lr += g->col[0] * f; lg += g->col[1] * f; lb += g->col[2] * f; }
        if (look == LOOK_PLAIN && !toon) { lr = kneeOf(lr); lg = kneeOf(lg); lb = kneeOf(lb); }
        if (fogOn) c3 = expNeg(dist * g_sc.fogD);
        o->c[0] = lr; o->c[1] = lg; o->c[2] = lb; o->c[3] = c3;
        if (!code) project(&g_sp[i], o, g_sc.vr);
    }
    Paint P; memset(&P, 0, sizeof P); P.x0 = g_sc.x0; P.y0 = g_sc.y0; P.x1 = g_sc.x1; P.y1 = g_sc.y1;
    const STex *T = texOf(d->tex); const int ut = f2i(d->useTex * 256.f + 0.5f, 256);
    P.tex = (T && ut > 0 && look != LOOK_WATER) ? T : NULL; P.useTex = ut;
    P.texMode = look == LOOK_WATER ? TM_FLAT : ut >= 255 ? TM_TEX : ut <= 0 ? TM_FLAT : TM_MIX;
    for (int k = 0; k < 3; k++) { P.tint[k] = look == LOOK_WATER ? 255 : f2i(d->tint[k] * 255.f + 0.5f, 255); P.fog[k] = g_sc.fog[k]; }
    P.cut = P.tex && ((d->flags & GFX_CUTOUT) || (T->holes && d->useTex >= 0.5f));
    P.alpha = f2i(d->alpha * 256.f + 0.5f, 256); P.ztest = 1;
    if (how == 0) { P.blend = BL_NONE; P.zwrite = 1; P.cmode = fogOn ? CM_FOG : CM_NONE; }
    else if (how == 2) { P.blend = BL_NONE; P.zwrite = 1; P.nocolor = 1; P.cut = 0; }
    else { P.blend = BL_ALPHA; P.zwrite = 0; P.cut = 0; P.cmode = look != LOOK_PLAIN ? CM_ALPHA : fogOn ? CM_FOG : CM_NONE; }
    const unsigned nt = (d->nIndex ? d->nIndex : nv) / 3; g_nCalls++;
    for (unsigned t = 0; t < nt; t++) { unsigned a = t * 3, b = a + 1, c = a + 2; if (idx) { a = idx[a]; b = idx[b]; c = idx[c]; if (a >= nv || b >= nv || c >= nv) continue; }
        g_nTri++;
        if (g_off[a] & g_off[b] & g_off[c]) continue;
        const int cd = g_code[a] | g_code[b] | g_code[c]; if (cd & 64) continue;
        if (cd) triClip(&g_sv[a], &g_sv[b], &g_sv[c], g_sc.vr, &P); else tri(&g_sp[a], &g_sp[b], &g_sp[c], &P); }
}

/* ---- what's laid over the world: effect triangles, smoke, glowing points, lines, flat decals ------------------------------------------------------------ */
static void fxTris(const float *v, int n, int how, unsigned tex) {
    Paint P; memset(&P, 0, sizeof P); P.x0 = g_sc.x0; P.y0 = g_sc.y0; P.x1 = g_sc.x1; P.y1 = g_sc.y1; P.ztest = 1; P.alpha = 256; P.cmode = CM_ALPHA; P.tint[0] = P.tint[1] = P.tint[2] = 255;
    P.blend = (how == GFX_FX_ADD || how == GFX_FX_GLOW || how == GFX_FX_GLOWZ) ? BL_ADD : BL_ALPHA;
    P.soft = (how == GFX_FX_SOFT || how == GFX_FX_SOFTZ || how == GFX_FX_GLOW || how == GFX_FX_GLOWZ);
    if (how == GFX_FX_PUFF) { P.tex = texOf(tex); if (!P.tex) return; P.texMode = TM_ALPHA; }
    const float *m = g_sc.vp;
    for (int i = 0; i + 2 < n; i += 3) { SV s[3];
        for (int k = 0; k < 3; k++) { const float *p = v + (size_t)(i + k) * 9; SV *o = &s[k];
            o->x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]; o->y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]; o->z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]; o->w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
            o->c[0] = p[3]; o->c[1] = p[4]; o->c[2] = p[5]; o->c[3] = p[6]; o->u = p[7]; o->v = p[8]; }
        triAny(&s[0], &s[1], &s[2], g_sc.vr, &P); }
}
/* a glowing point: a small soft dot, added. Sizes are for a 720-line picture */
static void skyPoints(const float *p, int n) {
    const float *m = g_sc.vp; const float k = g_sc.vr[3] / 720.f;
    for (int i = 0; i < n; i++, p += 5) { SV s; s.x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]; s.y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]; s.z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]; s.w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
        s.u = s.v = 0.f; s.c[0] = s.c[1] = s.c[2] = s.c[3] = 0.f;
        if (codeOf(&s) || offOf(&s)) continue;
        SP q; project(&q, &s, g_sc.vr);
        const float hue = p[3] * 3.f; float c[3] = {0.55f + 0.45f * cosf(hue), 0.55f + 0.45f * cosf(hue + 2.1f), 0.55f + 0.45f * cosf(hue + 4.2f)}, tw, size;
        if (p[4] > 4.f) { for (int j = 0; j < 3; j++) c[j] *= 0.40f; tw = 0.5f + 0.5f * sinf(g_sc.tsec * 0.05f + p[3] * 4.f); size = (46.f + (p[4] - 5.f) * 22.f) * k; }
        else { static const float W3[3] = {0.82f, 0.93f, 0.88f}; for (int j = 0; j < 3; j++) c[j] = W3[j] * 0.55f + c[j] * 0.45f; float sn = sinf(g_sc.tsec * p[4] * 0.35f + p[3]); if (sn < 0.f) sn = -sn; tw = 0.25f + 0.75f * sn * sn; size = (4.f + p[4] * 3.f) * k; }
        if (!(size > 0.f)) continue;
        if (size > 64.f) size = 64.f;
        const int ci[3] = {f2i(c[0] * tw * 255.f, 255), f2i(c[1] * tw * 255.f, 255), f2i(c[2] * tw * 255.f, 255)};
        const int cx = q.X >> 4, cy = q.Y >> 4, r = (int)(size * 0.5f) + 1; const float inv = 1.f / size;
        for (int y = cy - r; y <= cy + r; y++) { if (y < g_sc.y0 || y >= g_sc.y1) continue;
            for (int x = cx - r; x <= cx + r; x++) { if (x < g_sc.x0 || x >= g_sc.x1) continue;
                const float dx = ((float)x + 0.5f - (float)q.X * 0.0625f) * inv, dy = ((float)y + 0.5f - (float)q.Y * 0.0625f) * inv, d2 = dx * dx + dy * dy; if (d2 >= 0.5f) continue;
                if (!(q.z <= g_zb[(size_t)y * g_W + x])) continue;
                const int g = g_dotLut[f2i(d2 * 128.f, 64)], a = (g * g * 218) >> 16;   /* its colour falls off, and so does how much of it is added: 0.85 at the middle */
                const uint32_t d = g_fb[(size_t)y * g_W + x]; int rr = (int)((d >> 16) & 255u) + ((ci[0] * a) >> 8), gg = (int)((d >> 8) & 255u) + ((ci[1] * a) >> 8), bb = (int)(d & 255u) + ((ci[2] * a) >> 8);
                g_fb[(size_t)y * g_W + x] = PX(rr > 255 ? 255 : rr, gg > 255 ? 255 : gg, bb > 255 ? 255 : bb, 0); } } }
}
/* a line: added to what's there, a pixel wide, with the scene in front of it unless it reads through */
static void lineDraw(const float *a, const float *b, int over) {
    const float *m = g_sc.vp; SV s[2]; const float *p[2] = {a, b};
    for (int k = 0; k < 2; k++) { s[k].x = m[0] * p[k][0] + m[4] * p[k][1] + m[8] * p[k][2] + m[12]; s[k].y = m[1] * p[k][0] + m[5] * p[k][1] + m[9] * p[k][2] + m[13]; s[k].z = m[2] * p[k][0] + m[6] * p[k][1] + m[10] * p[k][2] + m[14]; s[k].w = m[3] * p[k][0] + m[7] * p[k][1] + m[11] * p[k][2] + m[15];
        s[k].u = s[k].v = 0.f; s[k].c[0] = p[k][3]; s[k].c[1] = p[k][4]; s[k].c[2] = p[k][5]; s[k].c[3] = 1.f; if (codeOf(&s[k]) & 64) return; }
    for (int k = 0; k < 6; k++) { const float d0 = planeD(&s[0], k), d1 = planeD(&s[1], k); if (d0 < 0.f && d1 < 0.f) return;   /* cut to the room it can be drawn in */
        if ((d0 < 0.f) != (d1 < 0.f)) { const float t = d0 / (d0 - d1); SV c; const float *pf = &s[0].x, *qf = &s[1].x; float *of = &c.x; for (int f = 0; f < 10; f++) of[f] = pf[f] + (qf[f] - pf[f]) * t; if (d0 < 0.f) s[0] = c; else s[1] = c; } }
    if (!(s[0].w > 0.f) || !(s[1].w > 0.f)) return;
    SP q0, q1; project(&q0, &s[0], g_sc.vr); project(&q1, &s[1], g_sc.vr);
    const int dx = q1.X - q0.X, dy = q1.Y - q0.Y, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy; int steps = ((adx > ady ? adx : ady) + 15) >> 4; if (steps < 1) steps = 1;
    if (steps > 4096) return;
    for (int i = 0; i <= steps; i++) { const int x = (q0.X + (int)((int64_t)dx * i / steps)) >> 4, y = (q0.Y + (int)((int64_t)dy * i / steps)) >> 4; if (x < g_sc.x0 || x >= g_sc.x1 || y < g_sc.y0 || y >= g_sc.y1) continue;
        const float t = (float)i / (float)steps, z = q0.z + (q1.z - q0.z) * t; if (over != 1 && !(z <= g_zb[(size_t)y * g_W + x])) continue;
        const float q = q0.q + (q1.q - q0.q) * t; if (!(q > 1e-20f)) continue;
        const float w = 1.f / q; const int r = f2i((q0.cq[0] + (q1.cq[0] - q0.cq[0]) * t) * w * 242.f, 255), g = f2i((q0.cq[1] + (q1.cq[1] - q0.cq[1]) * t) * w * 242.f, 255), bl = f2i((q0.cq[2] + (q1.cq[2] - q0.cq[2]) * t) * w * 242.f, 255);
        const uint32_t d = over == 2 ? 0u : g_fb[(size_t)y * g_W + x];   /* (11-12) a solid line is its own colour; the others are added */
        const int rr = (int)((d >> 16) & 255u) + r, gg = (int)((d >> 8) & 255u) + g, bb = (int)(d & 255u) + bl;
        g_fb[(size_t)y * g_W + x] = PX(rr > 255 ? 255 : rr, gg > 255 ? 255 : gg, bb > 255 ? 255 : bb, 0); }
}
/* a decal, laid flat across the middle of its box, as every machine draws it that doesn't read the scene's depth mid-frame: a stain is a soft dark mark, paint is its colour */
static void decalsDraw(const GfxPacket *pk) {
    static const float Q[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < pk->nDecal; i++) { const GfxDecal *d = &pk->decal[i]; const int k = (int)(d->kind + 0.5f), cls = k == 4 ? 1 : 0; if (cls != pass || k >= 6) continue;
        const float *m = d->m; float ly = sqrtf(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]), v[6 * 9]; if (!(ly > 1e-6f)) ly = 1.f;
        for (int c = 0; c < 6; c++) { const float u = Q[c][0], w = Q[c][1]; float *p = v + c * 9; for (int a = 0; a < 3; a++) p[a] = m[12 + a] + m[a] * 0.5f * u + m[8 + a] * 0.5f * w + m[4 + a] / ly * 0.03f;
            if (cls) { p[3] = d->col[0]; p[4] = d->col[1]; p[5] = d->col[2]; p[6] = d->col[3] * d->fade; }
            else { const float av = (d->col[0] + d->col[1] + d->col[2]) / 3.f; p[3] = d->col[0] * 0.3f; p[4] = d->col[1] * 0.3f; p[5] = d->col[2] * 0.3f; p[6] = d->col[3] * d->fade * (1.f - 0.9f * (av > 1.f ? 1.f : av)); }
            p[7] = u; p[8] = w; }
        fxTris(v, 6, cls ? GFX_FX_ALPHA : GFX_FX_SOFT, 0); }
}
static void fxDraw(const GfxPacket *pk, int under, int vi) {
    for (int i = 0; i < pk->nFx; i++) { const GfxFx *x = &pk->fx[i]; if (x->under != under || (x->view >= 0 && x->view != vi) || x->n <= 0) continue;
        if (x->how == GFX_FX_WEB || x->how == GFX_FX_WEBX) continue;   /* the star's own */
        if (x->how == GFX_FX_SKY) { const float *p = x->vb ? (const float *)stream(x->vb, (size_t)x->n * 20) : ((size_t)x->off + (size_t)x->n * 20 <= pk->memUsed ? (const float *)(pk->mem + x->off) : NULL); if (p) skyPoints(p, x->n); continue; }
        if (x->vb || (size_t)x->off + (size_t)x->n * 36 > pk->memUsed) continue;
        fxTris((const float *)(pk->mem + x->off), x->n, x->how, x->tex); }
}
/* the day's sky, behind everything: from the horizon's colour up to the sky's own overhead, the ground's below, and a glow round the sun. One square root a pixel */
static void skyDraw(const GfxView *v) {
    float inv[16]; if (!inv44(g_sc.vp, inv)) return;
    static int lut[513][3]; float zen[3], hz[3], gnd[3];
    static const float ZK[3] = {1.35f, 1.45f, 1.75f}, GK[3] = {0.55f, 0.58f, 0.50f};
    for (int k = 0; k < 3; k++) { zen[k] = v->sky[k] * ZK[k]; hz[k] = v->clear[k]; gnd[k] = v->sky[k] * GK[k]; }
    for (int i = 0; i <= 512; i++) { const float dy = (float)(i - 256) / 256.f, t = dy > 0.f ? dy : 0.f, up = t > 0.f ? powf(t, 0.55f) : 0.f;
        float g = (dy + 0.10f) / 0.115f; g = g < 0.f ? 0.f : g > 1.f ? 1.f : g; g = g * g * (3.f - 2.f * g);
        for (int k = 0; k < 3; k++) lut[i][k] = f2i((gnd[k] + ((hz[k] + (zen[k] - hz[k]) * up) - gnd[k]) * g) * 255.f + 0.5f, 255); }
    /* the lens's rays through the picture's corners: a pixel's ray is those mixed, which is exact */
    float cr[4][3]; const float ex = g_sc.eye[0], ey = g_sc.eye[1], ez = g_sc.eye[2];
    for (int c = 0; c < 4; c++) { const float nx = (c & 1) ? 1.f : -1.f, ny = (c & 2) ? -1.f : 1.f; float q[4];
        for (int r = 0; r < 4; r++) q[r] = inv[r] * nx + inv[4 + r] * ny + inv[8 + r] * 1.f + inv[12 + r];
        if (!(q[3] > 1e-20f || q[3] < -1e-20f)) return;
        cr[c][0] = q[0] / q[3] - ex; cr[c][1] = q[1] / q[3] - ey; cr[c][2] = q[2] / q[3] - ez; }
    const float si = v->sunI > 0.f ? v->sunI : 0.15f; const int sun = v->sunI > 0.f; static int glow[258]; int sc[3];
    if (sun) { for (int i = 0; i <= 256; i++) { const float sd = (float)i / 256.f, s2 = sd * sd, s4 = s2 * s2, s26 = powf(sd, 26.f); glow[i] = f2i((s26 * 0.42f + s4 * 0.10f) * si * 256.f, 1024); }
        for (int k = 0; k < 3; k++) sc[k] = f2i(v->sunCol[k] * 255.f, 255); }
    const int w = g_sc.x1 - g_sc.x0, h = g_sc.y1 - g_sc.y0;
    for (int y = g_sc.y0; y < g_sc.y1; y++) { const float fy = ((float)(y - g_sc.y0) + 0.5f) / (float)h; float l[3], r[3];
        for (int k = 0; k < 3; k++) { l[k] = cr[0][k] + (cr[2][k] - cr[0][k]) * fy; r[k] = cr[1][k] + (cr[3][k] - cr[1][k]) * fy; }
        uint32_t *fb = g_fb + (size_t)y * g_W;
        for (int x = g_sc.x0; x < g_sc.x1; x++) { const float fx = ((float)(x - g_sc.x0) + 0.5f) / (float)w, dx = l[0] + (r[0] - l[0]) * fx, dy = l[1] + (r[1] - l[1]) * fx, dz = l[2] + (r[2] - l[2]) * fx;
            const float len = sqrtf(dx * dx + dy * dy + dz * dz); if (!(len > 1e-20f)) continue;
            const int i = f2i((dy / len + 1.f) * 256.f, 512); int rr = lut[i][0], gg = lut[i][1], bb = lut[i][2];
            if (sun) { const float sd = (dx * g_sc.sun[0] + dy * g_sc.sun[1] + dz * g_sc.sun[2]) / len; int a = glow[f2i(sd * 256.f, 256)]; if (sd > 0.99965f) a += 2304;   /* the disc itself */
                if (a) { rr += (sc[0] * a) >> 8; gg += (sc[1] * a) >> 8; bb += (sc[2] * a) >> 8; if (rr > 255) rr = 255; if (gg > 255) gg = 255; if (bb > 255) bb = 255; } }
            fb[x] = PX(rr, gg, bb, 0); } }
}
/* the HUD: triangles on a canvas whose top left is 0,0, in the order they were asked for */
static void hudDraw(const GfxPacket *pk) {
    const float full[4] = {0.f, 0.f, (float)g_W, (float)g_H};
    for (int i = 0; i < pk->nHud; i++) { const GfxHudRec *h = &pk->hud[i]; if (h->n <= 0 || !(h->scr[0] > 0.f) || !(h->scr[1] > 0.f)) continue;
        Paint P; memset(&P, 0, sizeof P); P.x1 = g_W; P.y1 = g_H; P.blend = BL_ALPHA; P.alpha = 256;
        const float kx = full[2] / h->scr[0], ky = full[3] / h->scr[1]; const int stride = h->tex ? 8 : 2;
        if ((size_t)h->off + (size_t)h->n * stride * 4 > pk->memUsed) continue;
        if (h->tex) { P.tex = texOf(h->tex); if (!P.tex || !P.tex->kind) continue; P.texMode = TM_TEX; P.cmode = CM_ALPHA; }
        else { P.texMode = TM_FLAT; for (int k = 0; k < 3; k++) P.tint[k] = f2i(h->col[k] * 255.f + 0.5f, 255); P.alpha = f2i(h->col[3] * 256.f + 0.5f, 256); if (P.alpha <= 0) continue; }
        const float *v = (const float *)(pk->mem + h->off);
        for (int t = 0; t + 2 < h->n; t += 3) { SP s[3]; int bad = 0;
            for (int k = 0; k < 3; k++) { const float *p = v + (size_t)(t + k) * stride; const float x = p[0] * kx, y = p[1] * ky; if (!(x > -30000.f && x < 30000.f && y > -30000.f && y < 30000.f)) { bad = 1; break; }
                s[k].X = (int)((double)x * 16.0 + 1048576.5) - 1048576; s[k].Y = (int)((double)y * 16.0 + 1048576.5) - 1048576; s[k].z = 0.f; s[k].q = 1.f;
                if (h->tex) { s[k].uq = p[2]; s[k].vq = p[3]; s[k].cq[0] = p[4]; s[k].cq[1] = p[5]; s[k].cq[2] = p[6]; s[k].cq[3] = p[7]; } else { s[k].uq = s[k].vq = 0.f; s[k].cq[0] = s[k].cq[1] = s[k].cq[2] = s[k].cq[3] = 1.f; } }
            if (!bad) tri(&s[0], &s[1], &s[2], &P); } }
}

/* ---- the frame ------------------------------------------------------------------------------------------------------------------------------------------ */
static void present(void);
static void sceneSet(const GfxView *v, const float *vp, const float *eye, int rx, int ry, int rw, int rh) {
    g_sc.vp = vp; g_sc.eye = eye; g_sc.vr[0] = (float)rx; g_sc.vr[1] = (float)ry; g_sc.vr[2] = (float)rw; g_sc.vr[3] = (float)rh;
    g_sc.x0 = rx < 0 ? 0 : rx; g_sc.y0 = ry < 0 ? 0 : ry; g_sc.x1 = rx + rw > g_W ? g_W : rx + rw; g_sc.y1 = ry + rh > g_H ? g_H : ry + rh;
    (void)v;
}
void gfxBackEnd(const GfxPacket *pk, const GfxView *v) {
    if (!g_fb) return;
    g_nTri = g_nTriDrawn = g_nPix = 0; g_nCalls = 0;
    if (g_say == 2) { if (!g_who) g_who = (unsigned char *)malloc((size_t)g_W * g_H); if (g_who) memset(g_who, 0, (size_t)g_W * g_H); memset(g_kindHid, 0, sizeof g_kindHid); memset(g_kindBl, 0, sizeof g_kindBl); }
    const Uint64 t0 = g_say ? SDL_GetPerformanceCounter() : 0;   /* for what REVA_SOFTSAY prints, and nothing that's drawn */
    /* the light, the fog and the shoulder, once for the frame */
    g_sc.sunOn = v->sunI > 0.f; g_sc.tsec = v->tsec;
    { float l = sqrtf(v->sunDir[0] * v->sunDir[0] + v->sunDir[1] * v->sunDir[1] + v->sunDir[2] * v->sunDir[2]); if (!(l > 1e-6f)) l = 1.f; for (int k = 0; k < 3; k++) { g_sc.sun[k] = v->sunDir[k] / l; g_sc.sunC[k] = v->sunCol[k] * v->sunI; g_sc.amb[k] = v->sky[k]; } }
    g_sc.fogD = v->fogD > 0.f ? v->fogD : 0.f; for (int k = 0; k < 3; k++) g_sc.fog[k] = f2i(v->fog[k] * 255.f + 0.5f, 255);
    g_sc.lights = v->lights; g_sc.nLights = (v->lights && v->nLights > 0) ? (v->nLights > GFX_MAX_LIGHTS ? GFX_MAX_LIGHTS : v->nLights) : 0;
    { const float kn = v->knee; g_sc.kneeOn = kn > 0.f && kn < 0.95f; g_sc.knee = kn;   /* over the knee a light is folded toward 0.96 and never reaches it; with none it's cut at 1 by the pixel */
      if (g_sc.kneeOn) for (int i = 0; i <= 257; i++) { const float x = (float)i / 64.f; g_sc.kneeLut[i] = x <= kn ? x : kn + (0.96f - kn) * (1.f - expf(-(x - kn) / (0.96f - kn))); } }
    { float f[3], lum; for (int k = 0; k < 3; k++) f[k] = g_sc.sunOn ? v->sky[k] + v->sunCol[k] * v->sunI * 0.9f : 0.80f;   /* the toon pass's light, capped at 1 with its hue kept */
      lum = f[0] * 0.299f + f[1] * 0.587f + f[2] * 0.114f; if (lum > 1.f) for (int k = 0; k < 3; k++) f[k] /= lum;
      memcpy(g_sc.toonFull, f, 12); }
    const int nv = v->nViews > 0 ? (v->nViews > 16 ? 16 : v->nViews) : 1;
    { const uint32_t c = (v->bars || v->space) ? 0u : PX(f2i(v->clear[0] * 255.f + 0.5f, 255),   /* (11-10) a set that says `space` is black here: it came out the day's horizon colour, white at noon */ f2i(v->clear[1] * 255.f + 0.5f, 255), f2i(v->clear[2] * 255.f + 0.5f, 255), 0);
      const size_t n = (size_t)g_W * g_H; for (size_t i = 0; i < n; i++) { g_fb[i] = c; g_zb[i] = 1.f; } }
    int lh = 0;
    for (int vi = 0; vi < nv && !lh; vi++) {
        if (v->nViews > 0) sceneSet(v, v->views[vi].vp, v->views[vi].eye, v->views[vi].x, v->views[vi].y, v->views[vi].w, v->views[vi].h); else sceneSet(v, v->vp, v->eye, 0, 0, g_W, g_H);
        if (g_sc.x0 >= g_sc.x1 || g_sc.y0 >= g_sc.y1) continue;
        if (v->bars) { const uint32_t c = PX(f2i(v->clear[0] * 255.f + 0.5f, 255), f2i(v->clear[1] * 255.f + 0.5f, 255), f2i(v->clear[2] * 255.f + 0.5f, 255), 0); for (int y = g_sc.y0; y < g_sc.y1; y++) for (int x = g_sc.x0; x < g_sc.x1; x++) g_fb[(size_t)y * g_W + x] = c; }
        if (v->skyDome) skyDraw(v);
        fxDraw(pk, 1, vi);
        /* everything solid, in the packet's order; then what's laid over it, as the Vulkan renderer orders them */
        for (int i = 0; i < pk->nDraw; i++) { const GfxDraw *d = &pk->draws[i]; if (d->flags & (GFX_SHADOW | GFX_MIRRORED | GFX_BLEND)) continue;
            if ((d->flags & GFX_HOLO) && d->bones) continue;
            if (v->wall && (d->id >> 28) != GFX_KIND_HERO && (d->id >> 28) != GFX_KIND_CAST) continue;
            g_whoKind = (int)(d->id >> 28) & 7; meshDraw(pk, d, 0); }
        decalsDraw(pk);
        for (int i = 0; i < pk->nDraw; i++) { const GfxDraw *d = &pk->draws[i]; if ((d->flags & (GFX_SHADOW | GFX_MIRRORED)) || !(d->flags & GFX_BLEND)) continue;
            if ((d->flags & GFX_HOLO) && d->bones) continue;
            if (v->wall && (d->id >> 28) != GFX_KIND_HERO && (d->id >> 28) != GFX_KIND_CAST) continue;
            g_whoKind = (int)(d->id >> 28) & 7; meshDraw(pk, d, 1); }
        for (int i = 0; i < pk->nLine; i++) { const GfxLines *l = &pk->lines[i]; if (l->n < 2 || (size_t)l->off + (size_t)l->n * 24 > pk->memUsed) continue;
            const float *p = (const float *)(pk->mem + l->off); for (int k = 0; k + 1 < l->n; k += 2) lineDraw(p + (size_t)k * 6, p + (size_t)(k + 1) * 6, l->over); }
        fxDraw(pk, 0, vi);
        for (int how = 2; how <= 3; how++) for (int i = 0; i < pk->nDraw; i++) { const GfxDraw *d = &pk->draws[i]; if ((d->flags & (GFX_SHADOW | GFX_MIRRORED)) || !(d->flags & GFX_HOLO) || !d->bones) continue;
            if (v->wall && (d->id >> 28) != GFX_KIND_HERO && (d->id >> 28) != GFX_KIND_CAST) continue;
            meshDraw(pk, d, how); }
    }
    hudDraw(pk);
    if (g_say) { static int n; static double acc; acc += (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        if (++n >= 120) { printf("[soft] %d meshes, %ld triangles asked for, %ld drawn, %ld pixels written; %.2f ms a frame over the last %d\n", g_nCalls, g_nTri, g_nTriDrawn, g_nPix, acc / n, n);
            if (g_who) { static const char *const K[8] = {"other", "hero", "crowd", "prop", "cast", "actor", "object", "far"}; long vis[9] = {0}; for (size_t i = 0, m = (size_t)g_W * g_H; i < m; i++) vis[g_who[i]]++;
                printf("[soft] the last frame's pixels by kind, seen / drawn over / blended (of %d):", g_W * g_H); for (int k = 0; k < 8; k++) if (vis[k + 1] || g_kindHid[k] || g_kindBl[k]) printf(" %s %ld/%ld/%ld", K[k], vis[k + 1], g_kindHid[k], g_kindBl[k]); printf("; the clear %ld\n", vis[0]); }
            fflush(stdout); n = 0; acc = 0.0; } }
    present();
}
/* the last frame, RGBA with the bottom row first */
int gfxReadLag(void *rgba, int w, int h) { (void)rgba; (void)w; (void)h; return 0; }   /* (11-12) the processor draws one frame at a time: nothing to overlap */
int gfxRead(void *rgba, int w, int h) {
    if (!g_fb || w != g_W || h != g_H) return 0;
    unsigned char *o = rgba;
    for (int y = h - 1; y >= 0; y--) { const uint32_t *r = g_fb + (size_t)y * w; for (int x = 0; x < w; x++) { const uint32_t c = r[x]; o[0] = (unsigned char)(c >> 16); o[1] = (unsigned char)(c >> 8); o[2] = (unsigned char)c; o[3] = 255; o += 4; } }
    return 1;
}
static void softStart(int *W, int *H) {
    const char *e = getenv("REVA_SOFTRES"); int w = 0, h = 0;
    if (e && sscanf(e, "%dx%d", &w, &h) == 2 && w >= 64 && h >= 48 && w <= 4096 && h <= 4096) { g_W = w; g_H = h; }
    e = getenv("REVA_SOFTTEX"); if (e && atoi(e) >= 16) { int c = 16; while (c * 2 <= atoi(e) && c < 4096) c *= 2; g_texCap = c; }
    e = getenv("REVA_SOFTSMOOTH"); g_smooth = (e && e[0] == '1');
    e = getenv("REVA_SOFTLIGHTS"); if (e) { g_maxLights = atoi(e); if (g_maxLights < 0) g_maxLights = 0; if (g_maxLights > 8) g_maxLights = 8; }
    e = getenv("REVA_SOFTSAY"); g_say = (e && e[0] == '1') ? 1 : (e && e[0] == '2') ? 2 : 0;   /* 2: and the pixels by kind of thing (11-07) */
    g_fb = malloc((size_t)g_W * g_H * 4); g_zb = malloc((size_t)g_W * g_H * sizeof(float));
    if (!g_fb || !g_zb) { printf("[gfx] no memory for a %d x %d picture\n", g_W, g_H); exit(1); }
    memset(g_fb, 0, (size_t)g_W * g_H * 4);
    lutsMake(); *W = g_W; *H = g_H;
    printf("[gfx] the engine's own rasterizer: %d x %d on the processor, no driver. Textures to %d a side, %s, %d lamps a mesh\n", g_W, g_H, g_texCap, g_smooth ? "smoothed" : "nearest", g_maxLights); fflush(stdout);
}

/* ---- the SDL road: the picture onto a window's own memory, a whole number of times bigger, with black round it ------------------------------------------- */
static SDL_Window *g_win; static SDL_Surface *g_src;
void gfxInit(void *sdlWindow, int *W, int *H) {
    softStart(W, H);
    const char *e = getenv("REVA_NOWINDOW"); g_win = (e && e[0] == '1') ? NULL : (SDL_Window *)sdlWindow;
    if (g_win) { g_src = SDL_CreateRGBSurfaceWithFormatFrom(g_fb, g_W, g_H, 32, g_W * 4, SDL_PIXELFORMAT_RGB888); if (!g_src) { printf("[gfx] no surface for the picture: %s\n", SDL_GetError()); g_win = NULL; } }
    if (!g_win) { printf("[gfx] no window: the picture is drawn and kept, for a shot or a hash\n"); fflush(stdout); }
}
/* the Pi's direct road isn't built for this: it hands the engine the display's own size, and this picture has one of its own. Under a desktop it runs as it does here */
void gfxInitKms(int a, uint32_t b, uint32_t c, void *d, int e, int f, void (*g)(unsigned, unsigned)) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; printf("[gfx] the rasterizer has no direct road: run it under a desktop\n"); exit(1); }
void gfxKmsRemode(void *drmMode, int W, int H) { (void)drmMode; (void)W; (void)H; }
int gfxPointer(float u, float v, float *px, float *py) {   /* (11-12) as present() lays the picture in the window: a whole-number scale in the middle, or fitted */
    if (!g_win) return 0;
    int ww = 0, wh = 0; SDL_GetWindowSize(g_win, &ww, &wh); if (ww < 1 || wh < 1) return 0;
    int k = ww / g_W; if (wh / g_H < k) k = wh / g_H;
    int dw, dh; if (k >= 1) { dw = g_W * k; dh = g_H * k; } else { dw = ww; dh = ww * g_H / g_W; if (dh > wh) { dh = wh; dw = wh * g_W / g_H; } }
    const float x0 = (float)(ww - dw) * 0.5f, y0 = (float)(wh - dh) * 0.5f, gx = u * (float)ww, gy = v * (float)wh;
    if (gx < x0 || gy < y0 || gx > x0 + (float)dw || gy > y0 + (float)dh) return 0;
    *px = (gx - x0) / (float)dw * (float)g_W; *py = (gy - y0) / (float)dh * (float)g_H; return 1; }
static void present(void) {
    if (!g_win) return;
    SDL_Surface *ws = SDL_GetWindowSurface(g_win); if (!ws) return;
    int k = ws->w / g_W; if (ws->h / g_H < k) k = ws->h / g_H;
    SDL_Rect dst; if (k >= 1) { dst.w = g_W * k; dst.h = g_H * k; } else { dst.w = ws->w; dst.h = ws->w * g_H / g_W; if (dst.h > ws->h) { dst.h = ws->h; dst.w = ws->h * g_W / g_H; } }
    dst.x = (ws->w - dst.w) / 2; dst.y = (ws->h - dst.h) / 2;
    static int lw, lh; if (lw != ws->w || lh != ws->h) { SDL_FillRect(ws, NULL, 0); lw = ws->w; lh = ws->h; }
    if (SDL_BlitScaled(g_src, NULL, ws, &dst) || SDL_UpdateWindowSurface(g_win)) { static int said; if (!said) { said = 1; printf("[gfx] the picture didn't reach the window: %s\n", SDL_GetError()); fflush(stdout); } }
    else { static int said; if (!said) { said = 1; printf("[gfx] the picture is on a %d x %d window, %d x %d of it, %d times its size\n", ws->w, ws->h, dst.w, dst.h, k >= 1 ? k : 0); fflush(stdout); } }
}
