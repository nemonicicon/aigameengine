/* gfx_vk.c -- the Vulkan renderer (engine plan, Phase 2). Vulkan 1.3, dynamic rendering, one forward pass, one sun shadow map, pipelines all built at start-up.
 * Two roads to the glass: on the SDL road a window and a swapchain; on the Pi's direct road three things the display card owns, imported as linear images and flipped by us through
 * KMS, which gives the kernel's flip timestamps (v3d_lab2.c and vk_lab.c measured that road against GL: anim_bench/pi/).
 * The memory is simple on purpose: buffers bump-allocate from host-visible arenas and are never freed, and each frame has a ring for what lasts one frame. */
#include "gfx.h"
#include "gfx_spv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <vulkan/vulkan.h>
#if defined(_WIN32) && !defined(REVA_SDL)
#define REVA_SDL 1
#endif
#ifdef REVA_SDL
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#endif

#define CK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS && r_ != VK_SUBOPTIMAL_KHR) { printf("[gfx] FAILED %s: %d\n", #x, r_); fflush(stdout); exit(1); } } while (0)
#define FRAMES 2
#define RING_SIZE (16u * 1024u * 1024u)
#define ARENA_SIZE (64u * 1024u * 1024u)
#define MAX_DRAWS GFX_MAX_DRAWS
#define MAX_TEX 8192
#define MAX_IMAGES 8

static VkInstance g_inst; static VkPhysicalDevice g_gpu; static VkDevice g_dev; static VkQueue g_q; static uint32_t g_qf; static VkCommandPool g_pool;
static VkPhysicalDeviceMemoryProperties g_mp; static VkDeviceSize g_uboAlign = 256;
static int g_W, g_H, g_taps = 1, g_sunSize = 1024;
static const VkFormat COLOR_FMT = VK_FORMAT_B8G8R8A8_UNORM; static VkFormat g_depthFmt;
static GfxConfig g_cfg = {1, 1024, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2048, 0, 0, 2, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
/* where the world draws. With the settings off it's the glass itself, in one pass, and none of this exists. Otherwise the world draws here, and the HUD goes on the glass after */
static const VkFormat HDR_FMT = VK_FORMAT_R16G16B16A16_SFLOAT; static VkFormat g_sceneFmt = VK_FORMAT_B8G8R8A8_UNORM; static VkSampleCountFlagBits g_samples = VK_SAMPLE_COUNT_1_BIT; static int g_split;
static int g_filmPost;   /* the post pass on colours that are already as they'll be shown: film's way. The world draws on an 8-bit target, so what's over white clips as it always has there, then the highlights roll on brightness and not channel by channel, or a bright colour goes grey */
static VkImage g_msImg, g_hdrImg; static VkImageView g_msView, g_hdrView; static VkDeviceMemory g_msMem, g_hdrMem;
#define BLOOM_N 5
static VkImage g_blImg[BLOOM_N]; static VkImageView g_blView[BLOOM_N]; static VkDeviceMemory g_blMem[BLOOM_N]; static int g_blW[BLOOM_N], g_blH[BLOOM_N];
static VkDescriptorSet g_blDs[BLOOM_N], g_hdrDs, g_postDs;
static VkImage g_lutImg; static VkImageView g_lutView; static VkDeviceMemory g_lutMem; static int g_lutSize;
#define ATLAS_SIZE 4096
static VkImage g_atlasImg; static VkImageView g_atlasView;
static VkImage g_prImg; static VkImageView g_prView; static VkDeviceMemory g_prMem; static float g_prGeo[6]; static int g_prDims[3], g_prOn;
/* what reads the depth image: half-size targets for occlusion and the air */
static const VkFormat AO_FMT = VK_FORMAT_R8G8_UNORM; static int g_readDepth;
static VkImage g_zImg, g_aoImg[2], g_volImg; static VkImageView g_zView, g_aoView[2], g_volView, g_zRead; static VkDeviceMemory g_zMem, g_aoMem[2], g_volMem; static VkDescriptorSet g_zDs, g_blurDs; static VkSampler g_smpNear;
static VkImage g_img[MAX_IMAGES]; static VkImageView g_view[MAX_IMAGES]; static uint32_t g_nImg;
static VkImage g_depth, g_sunImg; static VkImageView g_depthView, g_sunView; static VkDeviceMemory g_depthMem;
static VkDescriptorSetLayout g_dslView, g_dslTex, g_dslPost; static VkPipelineLayout g_layout, g_layoutPost; static VkDescriptorPool g_dp; static VkSampler g_smpRepeat, g_smpShadow, g_smpClamp;
static VkImage g_snapImg, g_zsImg, g_dumImg; static VkImageView g_snapView, g_zsView, g_dumView; static int g_mid;   /* (step 24) the scene's colour and depth as they stood mid-frame, what's bound where a machine doesn't take them, and whether this one does */
static VkFormat g_glassFmt = VK_FORMAT_B8G8R8A8_UNORM; __attribute__((unused)) static VkColorSpaceKHR g_glassCs = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; static int g_hdrOut, g_exFresh; __attribute__((unused)) static int g_csExt; static VkImage g_exImg; static VkImageView g_exView;   /* (step 25) the glass's own format; whether it's an HDR10 display's; and the picture of the light past white */
static int g_GW, g_GH, g_present; static float g_picShown, g_tubeP[11] = {448.f, 139.f, 0.47f, 0.25f, 1.f, 0.f, 0.30f, 0.55f, 2.4f, 0.025f, 0.f}; static VkImage g_picImg, g_glassImg[MAX_IMAGES]; static VkImageView g_picView, g_glassView[MAX_IMAGES]; static VkDescriptorSet g_picDs;   /* (step 25) the glass's size, whether the picture's its own, and the picture when it is */
__attribute__((unused)) static void pictureSize(void); __attribute__((unused)) static void pictureMake(void);   /* below, with the pass that puts the picture on the glass. The SDL road's: the direct road's picture is the glass */
enum { SRC_HDR, SRC_HIST0, SRC_HIST1, SRC_SCR0, SRC_SCR1 };   /* (step 25) where a lens's pass reads the scene from */
static VkImage g_scrImg[2], g_histImg[2]; static VkImageView g_scrView[2], g_histView[2]; static VkDescriptorSet g_srcDs[5], g_taaDs[2]; static int g_lens, g_taaAt, g_taaOK, g_prevOK; static float g_prevVP[16]; static unsigned g_prevCuts;   /* (step 25) the lens's passes: two pictures to work in, two to keep the last frame in, and the last frame's lens */
enum { P_STATIC, P_STATIC_CUT, P_SKIN, P_SKIN_CUT, P_STATIC_BLEND, P_SKIN_BLEND, P_MOUTH, P_MOUTH_CUT, P_TOON, P_TOON_CUT, P_TOON_STATIC, P_TOON_STATIC_CUT, P_TOON_MOUTH, P_TOON_MOUTH_CUT, P_TOON_BLEND, P_INK, P_INK_STATIC, P_FUR, P_FUR_STATIC, P_TOON_FUR, P_TOON_FUR_STATIC, P_M_STATIC, P_M_STATIC_CUT, P_M_TOON_STATIC, P_M_TOON_STATIC_CUT, P_R_STATIC, P_R_STATIC_CUT, P_R_SKIN, P_R_SKIN_CUT, P_TR_STATIC, P_TR_STATIC_CUT, P_TR_SKIN, P_TR_SKIN_CUT, P_M_INK, P_M_INK_STATIC, P_GLOSS, P_TOON_GLOSS, P_TOON_GLOSS_A, P_SH_STATIC, P_SH_SKIN, P_LINE, P_HUD, P_FX_ALPHA, P_FX_ADD, P_FX_SOFT, P_SKY, P_HOLO_Z, P_HOLO, P_SPRITE, P_SPACE, P_BEAM, P_SKYDOME, P_SCREEN, P_WATER, P_FX_PUFF, P_FX_WEB, P_FX_WEBX, P_STAR_SURF, P_STAR_FACE, P_STAR_CORONA, P_STAR_LOOP, P_LINE_OVER, P_T_STATIC, P_T_STATIC_CUT, P_T_SKIN, P_T_SKIN_CUT, P_HEAD, P_HEADFLAT, P_B_SKIN, P_FLOORSH, P_POST, P_BLOOM0, P_BLOOM_DOWN, P_BLOOM_UP, P_AO, P_AOBLUR, P_VOL, P_FX_GLOW, P_FX_SOFTZ, P_FX_GLOWZ, P_DECAL, P_DECAL_PAINT, P_LENS_DOF, P_LENS_MB, P_TAA, P_PRESENT, P_TUBE480, P_TUBE240, P_PRESENT_HDR, P_HDREX, P_N };
static VkPipeline g_pipe[P_N];

static uint32_t memType(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < g_mp.memoryTypeCount; i++) if ((bits & (1u << i)) && (g_mp.memoryTypes[i].propertyFlags & want) == want) return i;
    printf("[gfx] no memory type for %x\n", want); fflush(stdout); exit(1);
}
static int memTypeHas(uint32_t bits, VkMemoryPropertyFlags want) {
    for (uint32_t i = 0; i < g_mp.memoryTypeCount; i++) if ((bits & (1u << i)) && (g_mp.memoryTypes[i].propertyFlags & want) == want) return 1;
    return 0;
}
typedef struct { VkBuffer b; VkDeviceMemory m; char *map; VkDeviceSize size, used; } Arena;
#define HOST_MEM (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
/* an arena is host memory the CPU writes; `want` asks for more (the capture buffer wants it CACHED, because the CPU reads that one
 * back: uncached, a 1280x720 frame read ~50 ms), and a device without that type gets the plain kind */
static void arenaMakeAs(Arena *a, VkDeviceSize size, VkBufferUsageFlags use, VkMemoryPropertyFlags want) {
    VkBufferCreateInfo i = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; i.size = size; i.usage = use; CK(vkCreateBuffer(g_dev, &i, NULL, &a->b));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(g_dev, a->b, &mr);
    if (!memTypeHas(mr.memoryTypeBits, want)) want = HOST_MEM;
    VkMemoryAllocateInfo m = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; m.allocationSize = mr.size; m.memoryTypeIndex = memType(mr.memoryTypeBits, want);
    CK(vkAllocateMemory(g_dev, &m, NULL, &a->m)); CK(vkBindBufferMemory(g_dev, a->b, a->m, 0)); CK(vkMapMemory(g_dev, a->m, 0, VK_WHOLE_SIZE, 0, (void **)&a->map)); a->size = size; a->used = 0;
}
static void arenaMake(Arena *a, VkDeviceSize size, VkBufferUsageFlags use) { arenaMakeAs(a, size, use, HOST_MEM); }
static void capMake(int W, int H);   /* the capture buffer, below its declaration */
#define VB_USE (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)
static Arena g_arena[256]; static int g_nArena; static Arena g_zero;
/* what's been freed, and when: it can be used again once no frame in flight can still be reading it */
typedef struct { VkBuffer b; VkDeviceSize off, cap; char *p; unsigned long at; } GFree;
#define MAX_FREE 4096
static GFree g_free[MAX_FREE]; static int g_nFree; static unsigned long g_frames;
static char *arenaAlloc(VkDeviceSize n, VkBuffer *b, VkDeviceSize *off) {
    n = (n + 15) & ~(VkDeviceSize)15;
    for (int i = 0; i < g_nFree; i++) { GFree *f = &g_free[i]; if (f->cap < n || g_frames - f->at <= FRAMES + 1) continue;
        char *p = f->p; *b = f->b; *off = f->off;
        if (f->cap > n) { f->p += n; f->off += n; f->cap -= n; } else { *f = g_free[--g_nFree]; }   /* what's left over stays free */
        return p; }
    Arena *a = g_nArena ? &g_arena[g_nArena - 1] : NULL;
    if (!a || a->used + n > a->size) { if (g_nArena >= 256) { printf("[gfx] out of arenas\n"); exit(1); } a = &g_arena[g_nArena++]; arenaMake(a, n > ARENA_SIZE ? n : ARENA_SIZE, VB_USE); }
    *b = a->b; *off = a->used; char *p = a->map + a->used; a->used = (a->used + n + 15) & ~(VkDeviceSize)15; return p;
}

/* ⛔ 11-04: A LONG SESSION. A freed piece joins the free pieces either side of it. It used to stay a piece of its own, so bodies of different sizes coming and going filled this list in two minutes, then what was freed was lost for good, and the blocks grew until they ran out and the engine exited */
static void freeAdd(VkBuffer b, VkDeviceSize off, VkDeviceSize cap, char *p) {
    for (int i = 0; i < g_nFree; i++) { GFree *f = &g_free[i]; if (f->b != b) continue;
        if (f->off + f->cap == off) { f->cap += cap; f->at = g_frames;
            for (int k = 0; k < g_nFree; k++) { GFree *h = &g_free[k]; if (h->b == b && h->off == f->off + f->cap) { f->cap += h->cap; *h = g_free[--g_nFree]; break; } }
            return; }
        if (off + cap == f->off) { f->off = off; f->p = p; f->cap += cap; f->at = g_frames;
            for (int k = 0; k < g_nFree; k++) { GFree *h = &g_free[k]; if (h->b == b && h->off + h->cap == f->off) { h->cap += f->cap; h->at = g_frames; *f = g_free[--g_nFree]; break; } }
            return; } }
    if (g_nFree >= MAX_FREE) { static int said; if (!said) { said = 1; printf("[gfx] more than %d separate free pieces: one's been lost\n", MAX_FREE); fflush(stdout); } return; }
    GFree *f = &g_free[g_nFree++]; f->b = b; f->off = off; f->cap = cap; f->p = p; f->at = g_frames;
}
/* ---- buffers and textures, by GL name ---------------------------------------------------- */
typedef struct { VkBuffer b; VkDeviceSize off, cap; char *p; int dyn; unsigned long at; } GBuf;   /* at: the frame a dynamic one was given its place in */
static unsigned long g_ringStamp = 1;   /* which frame's ring is being written: a dynamic buffer's place is only good for the frame it was given in */
static GBuf *g_buf; static unsigned g_nBuf;
static GBuf *bufOf(unsigned name) {
    if (name >= g_nBuf) { unsigned n = name + 4096; g_buf = realloc(g_buf, n * sizeof(GBuf)); memset(g_buf + g_nBuf, 0, (n - g_nBuf) * sizeof(GBuf)); g_nBuf = n; }
    return &g_buf[name];
}
typedef struct { VkImage img; VkImageView view, viewA; VkDescriptorSet ds, dsA, dsNear; int holes; VkDeviceMemory mem; VkFormat fmt; int w, h, levels, want, page, layer; } GTex;   /* ds is the plain descriptor. dsA is the mesh path's, which reads every texture as an array: a page once it's on one, and layer says where */
static GTex *g_tex; static unsigned g_nTex; static VkDescriptorSet g_whiteDs;

/* each frame's short-lived things */
typedef struct {
    float model[16], tint[4], extra[3]; VkBuffer vb[5]; VkDeviceSize off[5]; VkBuffer ib; VkDeviceSize ibOff; uint32_t nIndex, nVerts, bonesOff; VkDescriptorSet tex; int skin, flags, texKey, next, kind; float mouth[8];
    VkBuffer restVb; VkDeviceSize restOff; float restScale;   /* (step 23) where its vertices were at rest, for fur: its own positions unless it said otherwise */
    float mat[12]; VkDescriptorSet ntex;   /* (step 22) what it's made of: rough, metal, detail, depth; then its normal map's layer (-1 none), its kind (step 23: skin, hair, an eye, cloth) and two spare; then its eyes' glow, rgb and how much. And its normal map's page. One mesh is one material, so a call pushes it once */
} Draw;
/* a call: every copy of one mesh, chained through Draw.next. The lists run in this order in one array */
enum { L_SUN, L_OPAQUE, L_GLOSS, L_BLEND, L_HOLO, L_N };
typedef struct { int d0, dLast, n, list, key, order; uint32_t first; } Batch;
typedef struct {
    VkCommandBuffer cb; VkFence fence; VkSemaphore acq; Arena ring; VkDescriptorSet dsView;
    Draw *draws; int nDraw; int used; Batch *bat; int start[L_N + 1]; uint32_t instAt;
} Frame;
static Frame g_fr[FRAMES]; static int g_slot, g_frameLive; static Arena g_cap;
static void capMake(int W, int H) { arenaMakeAs(&g_cap, (VkDeviceSize)W * H * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, HOST_MEM | VK_MEMORY_PROPERTY_HOST_CACHED_BIT); }
__attribute__((unused)) static VkSemaphore g_rendered[MAX_IMAGES];
static void *ringAlloc(Frame *f, VkDeviceSize n, VkDeviceSize align, uint32_t *off) {
    VkDeviceSize o = (f->ring.used + align - 1) & ~(align - 1);
    if (o + n > f->ring.size) o = 0;
    f->ring.used = o + n; *off = (uint32_t)o; return f->ring.map + o;
}

void gfxBufferData(unsigned name, long size, const void *data, int dynamic) {
    if (!g_dev || size <= 0) return;
    GBuf *g = bufOf(name);
    if (dynamic) { Frame *f = &g_fr[g_slot]; uint32_t o; char *p = ringAlloc(f, (VkDeviceSize)size, 16, &o); if (data) memcpy(p, data, (size_t)size); g->b = f->ring.b; g->off = o; g->cap = (VkDeviceSize)size; g->p = p; g->dyn = 1; g->at = g_ringStamp; return; }
    if (!g->b || g->dyn || (VkDeviceSize)size > g->cap) { if (g->b && !g->dyn && g->p && g->cap) freeAdd(g->b, g->off, (g->cap + 15) & ~(VkDeviceSize)15, g->p); g->p = arenaAlloc((VkDeviceSize)size, &g->b, &g->off); g->cap = (VkDeviceSize)size; g->dyn = 0; }
    if (data) memcpy(g->p, data, (size_t)size);
}
void gfxMemSay(void) { VkDeviceSize used = 0, size = 0, loose = 0; for (int i = 0; i < g_nArena; i++) { used += g_arena[i].used; size += g_arena[i].size; } for (int i = 0; i < g_nFree; i++) loose += g_free[i].cap;
    unsigned texLive = 0; for (unsigned i = 0; i < g_nTex; i++) if (g_tex[i].img) texLive++;
    printf("[mem] geometry: %d blocks, %.1f of %.1f MB used, %d free pieces holding %.1f MB. Textures: %u alive. Name tables: %u buffers, %u textures\n", g_nArena, (double)used / 1048576.0, (double)size / 1048576.0, g_nFree, (double)loose / 1048576.0, texLive, g_nBuf, g_nTex); fflush(stdout); }
void gfxBufferFree(unsigned name) {
    if (!g_dev || name >= g_nBuf) return;
    GBuf *g = &g_buf[name];
    if (g->b && !g->dyn && g->p && g->cap) freeAdd(g->b, g->off, (g->cap + 15) & ~(VkDeviceSize)15, g->p);
    memset(g, 0, sizeof *g);
}
void gfxBufferSub(unsigned name, long off, long size, const void *data) {
    if (!g_dev || name >= g_nBuf) return;
    GBuf *g = &g_buf[name];
    if (g->dyn && off == 0 && (VkDeviceSize)size >= g->cap) { gfxBufferData(name, size, data, 1); return; }   /* it lives in a ring that's come round since: a new place, in this frame's */
    /* ⛔ 11-05: A PART of a dynamic buffer, written in a later frame than the one that placed it, went to the old place: in a ring that had come round and been
     * given to something else. Hanging cloth writes its points into a buffer made for the most it could ever have, so it was always a part, and its skirt was
     * drawn from whatever lay there that frame: sheets the size of the room, some frames and not others. It gets a new place in this frame's ring first */
    if (g->dyn && g->at != g_ringStamp && g->cap) { Frame *f = &g_fr[g_slot]; uint32_t o; g->p = ringAlloc(f, g->cap, 16, &o); g->b = f->ring.b; g->off = o; g->at = g_ringStamp; }
    if (g->p && (VkDeviceSize)(off + size) <= g->cap) memcpy(g->p + off, data, (size_t)size);
}
static VkCommandBuffer oneBegin(void) {
    VkCommandBufferAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; a.commandPool = g_pool; a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; a.commandBufferCount = 1; VkCommandBuffer cb; CK(vkAllocateCommandBuffers(g_dev, &a, &cb));
    VkCommandBufferBeginInfo b = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb, &b); return cb;
}
static void oneEnd(VkCommandBuffer cb) {
    vkEndCommandBuffer(cb); VkSubmitInfo su = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO}; su.commandBufferCount = 1; su.pCommandBuffers = &cb;
    VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence f; CK(vkCreateFence(g_dev, &fi, NULL, &f)); CK(vkQueueSubmit(g_q, 1, &su, f)); CK(vkWaitForFences(g_dev, 1, &f, VK_TRUE, UINT64_MAX)); vkDestroyFence(g_dev, f, NULL); vkFreeCommandBuffers(g_dev, g_pool, 1, &cb);
}
static void barrierImg(VkCommandBuffer cb, VkImage img, VkImageAspectFlags asp, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags s0, VkPipelineStageFlags s1, VkAccessFlags a0, VkAccessFlags a1, uint32_t mips) {
    VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.srcAccessMask = a0; b.dstAccessMask = a1; b.oldLayout = from; b.newLayout = to; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img; b.subresourceRange.aspectMask = asp; b.subresourceRange.levelCount = mips; b.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cb, s0, s1, 0, 0, NULL, 0, NULL, 1, &b);
}
static VkDeviceMemory g_imgMem;   /* what the last imgMake bound: only something that gets remade needs it */
static VkImage imgMakeX(VkFormat f, uint32_t w, uint32_t h, uint32_t mips, VkSampleCountFlagBits samples, VkImageCreateFlags flags, VkImageUsageFlags use, VkImageAspectFlags asp, VkImageView *view) {
    VkImage img; VkImageCreateInfo i = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; i.flags = flags; i.imageType = VK_IMAGE_TYPE_2D; i.format = f; i.extent.width = w; i.extent.height = h; i.extent.depth = 1; i.mipLevels = mips; i.arrayLayers = 1; i.samples = samples; i.tiling = VK_IMAGE_TILING_OPTIMAL; i.usage = use; CK(vkCreateImage(g_dev, &i, NULL, &img));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_dev, img, &mr); VkMemoryAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize = mr.size; a.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); VkDeviceMemory m; CK(vkAllocateMemory(g_dev, &a, NULL, &m)); g_imgMem = m; CK(vkBindImageMemory(g_dev, img, m, 0));
    VkImageViewCreateInfo v = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image = img; v.viewType = VK_IMAGE_VIEW_TYPE_2D; v.format = f; v.subresourceRange.aspectMask = asp; v.subresourceRange.levelCount = mips; v.subresourceRange.layerCount = 1; CK(vkCreateImageView(g_dev, &v, NULL, view)); return img;
}
static VkImage imgMake(VkFormat f, uint32_t w, uint32_t h, uint32_t mips, VkImageUsageFlags use, VkImageAspectFlags asp, VkImageView *view) { return imgMakeX(f, w, h, mips, VK_SAMPLE_COUNT_1_BIT, 0, use, asp, view); }
static VkDescriptorSet dsTexture(VkImageView view) {
    VkDescriptorSetAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool = g_dp; a.descriptorSetCount = 1; a.pSetLayouts = &g_dslTex; VkDescriptorSet s; CK(vkAllocateDescriptorSets(g_dev, &a, &s));
    VkDescriptorImageInfo im = {g_smpRepeat, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = s; w.dstBinding = 0; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); return s;
}
/* ---- uploads that don't wait. While it's on, a new texture's bytes go in a ring and its copy goes at the front of the next frame's commands, so nothing stops for the GPU. The ring is big enough that it can't come round on a frame still in flight */
#define UP_RING (32u << 20)
#define UP_MAX 64
typedef struct { VkImage img; uint32_t mips; VkBufferImageCopy c[16]; uint32_t nc; } GUpload;
static GUpload g_upl[UP_MAX]; static int g_nUpl, g_upAsync; static Arena g_upRing;
void gfxUploadAsync(int on) { g_upAsync = on; }
static int upCan(size_t total) { return g_upAsync && g_nUpl < UP_MAX && total <= UP_RING / 8; }
static char *upAlloc(size_t total, VkDeviceSize *off) {
    if (!g_upRing.b) arenaMake(&g_upRing, UP_RING, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (g_upRing.used + total > g_upRing.size) g_upRing.used = 0;
    *off = g_upRing.used; char *p = g_upRing.map + g_upRing.used; g_upRing.used += (total + 15) & ~(size_t)15; return p;
}
/* a texture gone: its parts wait out the frames in flight */
typedef struct { VkImage img; VkImageView view, viewA; VkDescriptorSet ds, dsA; VkDeviceMemory mem; unsigned long at; } GGone;
#define MAX_GONE 512
static GGone g_gone[MAX_GONE]; static int g_nGone;
typedef struct { VkImage img; VkImageView view; VkDescriptorSet ds; VkDeviceMemory mem; int live; } GPage;
#define MAX_PAGES 128
#define PAGE_LAYERS 256
#define PAGE_BYTES (256u << 20)
static GPage g_page[MAX_PAGES];
void gfxTextureFree(unsigned name) {
    if (!g_dev || name >= g_nTex) return;
    GTex *t = &g_tex[name];
    if (t->page) { GPage *p = &g_page[t->page - 1]; if (--p->live <= 0 && g_nGone < MAX_GONE) { GGone *g = &g_gone[g_nGone++]; memset(g, 0, sizeof *g); g->img = p->img; g->view = p->view; g->ds = p->ds; g->mem = p->mem; g->at = g_frames; memset(p, 0, sizeof *p); } memset(t, 0, sizeof *t); return; }
    if (!t->img || g_nGone >= MAX_GONE) return;
    GGone *g = &g_gone[g_nGone++]; g->img = t->img; g->view = t->view; g->viewA = t->viewA; g->ds = t->ds; g->dsA = t->dsA; g->mem = t->mem; g->at = g_frames; memset(t, 0, sizeof *t);
}
/* what every texture ends with: its plain descriptor, and the mesh path's, an array of one until it's on a page */
static VkImageView viewArray(VkImage img, VkFormat f, uint32_t mips, uint32_t layers) { VkImageView o; VkImageViewCreateInfo v = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image = img; v.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; v.format = f; v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; v.subresourceRange.levelCount = mips; v.subresourceRange.layerCount = layers; CK(vkCreateImageView(g_dev, &v, NULL, &o)); return o; }
static void texFinish(GTex *t, VkFormat f, int w, int h, int levels) { t->fmt = f; t->w = w; t->h = h; t->levels = levels; t->ds = dsTexture(t->view); t->viewA = viewArray(t->img, f, (uint32_t)levels, 1); t->dsA = dsTexture(t->viewA); }
/* every texture gets a full mip chain, a box filter on the CPU: the same pixels on every machine, and a street in 480i needs them */
static void texUpload(GTex *t, int w, int h, const unsigned char *rgba) {
    uint32_t mips = 1; { int a = w, b = h; while (a > 1 || b > 1) { a = a > 1 ? a / 2 : 1; b = b > 1 ? b / 2 : 1; mips++; } }
    VkFormat fmt = g_cfg.linear == 2 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    t->img = imgMakeX(fmt, (uint32_t)w, (uint32_t)h, mips, VK_SAMPLE_COUNT_1_BIT, g_cfg.linear == 2 ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT : 0, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &t->view); t->mem = g_imgMem;
    { size_t tot = 0; int a = w, b = h; for (uint32_t l = 0; l < mips; l++) { tot += (size_t)a * b * 4; a = a > 1 ? a / 2 : 1; b = b > 1 ? b / 2 : 1; }
      if (upCan(tot) && mips <= 16) { VkDeviceSize base; char *o = upAlloc(tot, &base); GUpload *u = &g_upl[g_nUpl++]; u->img = t->img; u->mips = mips; u->nc = 0;
          unsigned char *cur = malloc((size_t)w * h * 4); memcpy(cur, rgba, (size_t)w * h * 4); size_t at = 0; a = w; b = h;
          for (uint32_t l = 0; l < mips; l++) { memcpy(o + at, cur, (size_t)a * b * 4);
              VkBufferImageCopy c = {0}; c.bufferOffset = base + at; c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.mipLevel = l; c.imageSubresource.layerCount = 1; c.imageExtent.width = (uint32_t)a; c.imageExtent.height = (uint32_t)b; c.imageExtent.depth = 1; u->c[u->nc++] = c;
              int na = a > 1 ? a / 2 : 1, nb = b > 1 ? b / 2 : 1; at += (size_t)a * b * 4;
              if (l + 1 < mips) { unsigned char *nx = malloc((size_t)na * nb * 4);
                  for (int y = 0; y < nb; y++) for (int x = 0; x < na; x++) for (int k = 0; k < 4; k++) { int x1 = x * 2 < a ? x * 2 : a - 1, x2 = x * 2 + 1 < a ? x * 2 + 1 : a - 1, y1 = y * 2 < b ? y * 2 : b - 1, y2 = y * 2 + 1 < b ? y * 2 + 1 : b - 1;
                      nx[(y * na + x) * 4 + k] = (unsigned char)((cur[(y1 * a + x1) * 4 + k] + cur[(y1 * a + x2) * 4 + k] + cur[(y2 * a + x1) * 4 + k] + cur[(y2 * a + x2) * 4 + k] + 2) / 4); }
                  free(cur); cur = nx; }
              a = na; b = nb; }
          free(cur); texFinish(t, fmt, w, h, (int)mips); return; } }
    size_t total = 0; { int a = w, b = h; for (uint32_t l = 0; l < mips; l++) { total += (size_t)a * b * 4; a = a > 1 ? a / 2 : 1; b = b > 1 ? b / 2 : 1; } }
    Arena st; arenaMake(&st, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    VkCommandBuffer cb = oneBegin(); barrierImg(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, mips);
    /* ⛔ mapped memory is uncached for the CPU: reading it back is thousands of times slower, so the chain is built in plain memory and copied across */
    unsigned char *cur = malloc((size_t)w * h * 4); memcpy(cur, rgba, (size_t)w * h * 4); size_t off = 0; int a = w, b = h;
    for (uint32_t l = 0; l < mips; l++) {
        memcpy(st.map + off, cur, (size_t)a * b * 4);
        VkBufferImageCopy c = {0}; c.bufferOffset = off; c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.mipLevel = l; c.imageSubresource.layerCount = 1; c.imageExtent.width = (uint32_t)a; c.imageExtent.height = (uint32_t)b; c.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(cb, st.b, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        int na = a > 1 ? a / 2 : 1, nb = b > 1 ? b / 2 : 1; size_t lvl = (size_t)a * b * 4;
        if (l + 1 < mips) { unsigned char *nx = malloc((size_t)na * nb * 4);
            for (int y = 0; y < nb; y++) for (int x = 0; x < na; x++) for (int k = 0; k < 4; k++) { int x1 = x * 2 < a ? x * 2 : a - 1, x2 = x * 2 + 1 < a ? x * 2 + 1 : a - 1, y1 = y * 2 < b ? y * 2 : b - 1, y2 = y * 2 + 1 < b ? y * 2 + 1 : b - 1;
                nx[(y * na + x) * 4 + k] = (unsigned char)((cur[(y1 * a + x1) * 4 + k] + cur[(y1 * a + x2) * 4 + k] + cur[(y2 * a + x1) * 4 + k] + cur[(y2 * a + x2) * 4 + k] + 2) / 4); }
            free(cur); cur = nx; }
        off += lvl; a = na; b = nb; }
    free(cur);
    barrierImg(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, mips);
    oneEnd(cb);
    vkDestroyBuffer(g_dev, st.b, NULL); vkFreeMemory(g_dev, st.m, NULL);
    texFinish(t, fmt, w, h, (int)mips);
}
void gfxTexture(unsigned name, int w, int h, const void *rgba) {
    if (!g_dev || !rgba || w < 1 || h < 1) return;
    if (name >= g_nTex) { unsigned n = name + 1024; g_tex = realloc(g_tex, n * sizeof(GTex)); memset(g_tex + g_nTex, 0, (n - g_nTex) * sizeof(GTex)); g_nTex = n; }
    texUpload(&g_tex[name], w, h, rgba);
    /* a texture with holes in it needs the cutout pipeline, and only those: discard costs early depth on this GPU, so it isn't on for everything as GL's one shader has it */
    { const unsigned char *p = rgba; int holes = 0; for (long i = 0, n = (long)w * h; i < n && !holes; i++) if (p[i * 4 + 3] < 128) holes = 1; g_tex[name].holes = holes; }
}
/* a sprite sheet: its own descriptor, on a sampler that neither filters nor wraps */
void gfxTextureNear(unsigned name) {
    static VkSampler nearest = VK_NULL_HANDLE;
    if (!g_dev || name >= g_nTex || !g_tex[name].view) return;
    if (!nearest) { VkSamplerCreateInfo sc = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sc.magFilter = VK_FILTER_NEAREST; sc.minFilter = VK_FILTER_NEAREST; sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sc.maxLod = 0.f; CK(vkCreateSampler(g_dev, &sc, NULL, &nearest)); }
    VkDescriptorSetAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool = g_dp; a.descriptorSetCount = 1; a.pSetLayouts = &g_dslTex; VkDescriptorSet d; CK(vkAllocateDescriptorSets(g_dev, &a, &d));
    VkImageView sv = g_tex[name].view;
    if (g_cfg.linear == 2) { VkImageViewCreateInfo vc = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vc.image = g_tex[name].img; vc.viewType = VK_IMAGE_VIEW_TYPE_2D; vc.format = VK_FORMAT_R8G8B8A8_UNORM; vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vc.subresourceRange.levelCount = 1; vc.subresourceRange.layerCount = 1; CK(vkCreateImageView(g_dev, &vc, NULL, &sv)); }   /* a sprite's bytes go on the glass as they are, undecoded */
    VkDescriptorImageInfo im = {nearest, sv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = d; w.dstBinding = 0; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL);
    g_tex[name].dsNear = d;
}
/* a sheet of text: its own descriptor too, smooth and with its mips, because it's drawn a good deal smaller than it's packed */
void gfxTextureHud(unsigned name) {
    static VkSampler smooth = VK_NULL_HANDLE;
    if (!g_dev || name >= g_nTex || !g_tex[name].view) return;
    if (!smooth) { VkSamplerCreateInfo sc = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sc.magFilter = VK_FILTER_LINEAR; sc.minFilter = VK_FILTER_LINEAR; sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sc.maxLod = VK_LOD_CLAMP_NONE; CK(vkCreateSampler(g_dev, &sc, NULL, &smooth)); }
    VkDescriptorSetAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool = g_dp; a.descriptorSetCount = 1; a.pSetLayouts = &g_dslTex; VkDescriptorSet d; CK(vkAllocateDescriptorSets(g_dev, &a, &d));
    VkImageView sv = g_tex[name].view;
    if (g_cfg.linear == 2) { VkImageViewCreateInfo vc = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vc.image = g_tex[name].img; vc.viewType = VK_IMAGE_VIEW_TYPE_2D; vc.format = VK_FORMAT_R8G8B8A8_UNORM; vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vc.subresourceRange.levelCount = (uint32_t)g_tex[name].levels; vc.subresourceRange.layerCount = 1; CK(vkCreateImageView(g_dev, &vc, NULL, &sv)); }
    VkDescriptorImageInfo im = {smooth, sv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = d; w.dstBinding = 0; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL);
    g_tex[name].dsNear = d;
}
/* ---- pictures drawn off-screen, and the screens that show them ------------------------------- */
#define MAX_TARGETS 8
typedef struct { unsigned name; int w, h; VkImage depth; VkImageView depthView; } GTarget;
static GTarget g_tg[MAX_TARGETS]; static int g_nTg;
static struct { unsigned tex; float vp[16], eye[3]; } g_feed; static int g_feedOn;
#define MAX_SCREENS 32
static struct { unsigned tex; float model[16], amp; } g_scr[MAX_SCREENS]; static int g_nScr;
static GfxHead g_head; static int g_headOn;
static GTarget *tgOf(unsigned name) { for (int i = 0; i < g_nTg; i++) if (g_tg[i].name == name) return &g_tg[i]; return NULL; }
void gfxTargetMake(unsigned name, int w, int h, int nearest) {
    if (!g_dev || !g_cfg.screens || !name || tgOf(name) || g_nTg >= MAX_TARGETS) return;
    if (name >= g_nTex) { unsigned n = name + 1024; g_tex = realloc(g_tex, n * sizeof(GTex)); memset(g_tex + g_nTex, 0, (n - g_nTex) * sizeof(GTex)); g_nTex = n; }
    GTex *t = &g_tex[name]; GTarget *g = &g_tg[g_nTg++]; g->name = name; g->w = w; g->h = h;
    t->img = imgMake(COLOR_FMT, (uint32_t)w, (uint32_t)h, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &t->view); t->mem = g_imgMem;
    g->depth = imgMake(g_depthFmt, (uint32_t)w, (uint32_t)h, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, &g->depthView);
    VkDescriptorSetAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool = g_dp; a.descriptorSetCount = 1; a.pSetLayouts = &g_dslTex; CK(vkAllocateDescriptorSets(g_dev, &a, &t->ds));
    VkDescriptorImageInfo im = {nearest ? g_smpNear : g_smpClamp, t->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; wr.dstSet = t->ds; wr.dstBinding = 0; wr.descriptorCount = 1; wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &wr, 0, NULL);
    /* black until something draws in it */
    VkCommandBuffer cb = oneBegin(); barrierImg(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, 1);
    VkClearColorValue black; memset(&black, 0, sizeof black); black.float32[3] = 1.f; VkImageSubresourceRange rr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; vkCmdClearColorImage(cb, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &rr);
    barrierImg(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1); oneEnd(cb);
}
void gfxFeed(unsigned name, const float *vp, const float *eye) { if (!g_cfg.screens || !tgOf(name)) return; g_feed.tex = name; memcpy(g_feed.vp, vp, 64); memcpy(g_feed.eye, eye, 12); g_feedOn = 1; }
void gfxScreen(unsigned name, const float *model, float amp) { if (!g_cfg.screens || g_nScr >= MAX_SCREENS || !name) return; g_scr[g_nScr].tex = name; memcpy(g_scr[g_nScr].model, model, 64); g_scr[g_nScr].amp = amp; g_nScr++; }
void gfxHead(const GfxHead *h) { if (!g_cfg.screens || !tgOf(h->target)) return; g_head = *h; g_headOn = 1; }
static struct { unsigned tex; float vp[16], rect[4]; } g_fs; static int g_fsOn;
static int g_between, g_nScrWas, g_fsWas;   /* (step 25) a frame with no packet of its own, between two ticks: the screens and the floor's shadow are the last tick's, and nothing's drawn off-screen again */
void gfxFloorShadow(unsigned name, const float *vp, const float *rect) { if (!g_cfg.screens || !tgOf(name)) return; g_fs.tex = name; memcpy(g_fs.vp, vp, 64); memcpy(g_fs.rect, rect, 16); g_fsOn = 1; }
int gfxHasBC(void) {
    static int has = -1;
    if (has < 0) {
        VkFormatProperties a, b; VkFormatFeatureFlags want = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        vkGetPhysicalDeviceFormatProperties(g_gpu, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, &a); vkGetPhysicalDeviceFormatProperties(g_gpu, VK_FORMAT_BC3_UNORM_BLOCK, &b);
        has = (a.optimalTilingFeatures & want) == want && (b.optimalTilingFeatures & want) == want;
        printf("[gfx] BC1 and BC3 textures: %s\n", has ? "yes" : "no, they're decoded at load"); fflush(stdout);
    }
    return has;
}
int gfxHasBC7(void) {
    static int has = -1;
    if (has < 0) { VkFormatProperties a; VkFormatFeatureFlags want = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT; vkGetPhysicalDeviceFormatProperties(g_gpu, VK_FORMAT_BC7_UNORM_BLOCK, &a); has = (a.optimalTilingFeatures & want) == want; printf("[gfx] BC7 textures: %s\n", has ? "yes" : "no, they're decoded at load"); fflush(stdout); }
    return has;
}
/* a cooked texture: every level is already there, so this is one copy per level, and nothing is made here */
void gfxTextureBC(unsigned name, int w, int h, int levels, int fmt, const void *data, int holes) {
    if (!g_dev || !data || w < 1 || h < 1 || levels < 1) return;
    if (name >= g_nTex) { unsigned n = name + 1024; g_tex = realloc(g_tex, n * sizeof(GTex)); memset(g_tex + g_nTex, 0, (n - g_nTex) * sizeof(GTex)); g_nTex = n; }
    GTex *t = &g_tex[name]; size_t block = fmt == 1 ? 8 : 16, total = 0;
    for (int l = 0, a = w, b = h; l < levels; l++, a = a > 1 ? a / 2 : 1, b = b > 1 ? b / 2 : 1) total += (size_t)((a + 3) / 4) * ((b + 3) / 4) * block;
    VkFormat vf = fmt == 7 ? (g_cfg.linear == 2 ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK) : fmt == 3 ? (g_cfg.linear == 2 ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK) : (g_cfg.linear == 2 ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    t->img = imgMake(vf, (uint32_t)w, (uint32_t)h, (uint32_t)levels, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &t->view); t->mem = g_imgMem;
    if (upCan(total) && levels <= 16) { VkDeviceSize base; char *o = upAlloc(total, &base); memcpy(o, data, total); GUpload *u = &g_upl[g_nUpl++]; u->img = t->img; u->mips = (uint32_t)levels; u->nc = 0; size_t at = 0;
        for (int l = 0, a = w, b = h; l < levels; l++, a = a > 1 ? a / 2 : 1, b = b > 1 ? b / 2 : 1) { VkBufferImageCopy c = {0}; c.bufferOffset = base + at; c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.mipLevel = (uint32_t)l; c.imageSubresource.layerCount = 1; c.imageExtent.width = (uint32_t)a; c.imageExtent.height = (uint32_t)b; c.imageExtent.depth = 1; u->c[u->nc++] = c; at += (size_t)((a + 3) / 4) * ((b + 3) / 4) * block; }
        texFinish(t, vf, w, h, levels); t->holes = holes; return; }
    Arena st; arenaMake(&st, total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    memcpy(st.map, data, total);
    VkCommandBuffer cb = oneBegin();
    barrierImg(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, (uint32_t)levels);
    size_t off = 0;
    for (int l = 0, a = w, b = h; l < levels; l++, a = a > 1 ? a / 2 : 1, b = b > 1 ? b / 2 : 1) {
        VkBufferImageCopy c = {0}; c.bufferOffset = off; c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.mipLevel = (uint32_t)l; c.imageSubresource.layerCount = 1; c.imageExtent.width = (uint32_t)a; c.imageExtent.height = (uint32_t)b; c.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(cb, st.b, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        off += (size_t)((a + 3) / 4) * ((b + 3) / 4) * block;
    }
    barrierImg(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, (uint32_t)levels);
    oneEnd(cb);
    vkDestroyBuffer(g_dev, st.b, NULL); vkFreeMemory(g_dev, st.m, NULL);
    texFinish(t, vf, w, h, levels);
    t->holes = holes;
}
/* ---- THE TEXTURE PAGES (engine plan, Phase 2) ---------------------------------------------------
 * A texture change breaks a batch, and this GPU has 16 textures a stage and no bindless. So once a game has loaded, its packs' textures move onto shared pages, one page per size and format, each texture a layer.
 * A layer and not a tile of one big image: repeat and mips work as they did, nothing bleeds, no UV moves, and the picture is the same pixel for pixel. What loads later, or streams in, stays on its own until the next load. */
static int g_pagesDue;
void gfxTexturePage(unsigned name) { if (!g_dev || !g_cfg.pages || g_upAsync || name >= g_nTex || !g_tex[name].dsA) return; g_tex[name].want = 1; g_pagesDue = 1; }
void gfxPagesBuild(void) {
    if (!g_pagesDue) return;
    g_pagesDue = 0; vkDeviceWaitIdle(g_dev); struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0); int moved = 0, made = 0;
    for (unsigned n = 0; n < g_nTex; n++) { GTex a = g_tex[n]; if (!a.want) continue;
        unsigned mem[PAGE_LAYERS]; int k = 0, slot = -1;
        /* a page is one allocation, and the textures it replaces are still here while it's filled, so it can't be huge */
        size_t bytes = 0; { int block = (a.fmt == VK_FORMAT_R8G8B8A8_UNORM || a.fmt == VK_FORMAT_R8G8B8A8_SRGB) ? 0 : (a.fmt == VK_FORMAT_BC1_RGBA_UNORM_BLOCK || a.fmt == VK_FORMAT_BC1_RGBA_SRGB_BLOCK) ? 8 : 16; for (int l = 0, x = a.w, y = a.h; l < a.levels; l++, x = x > 1 ? x / 2 : 1, y = y > 1 ? y / 2 : 1) bytes += block ? (size_t)((x + 3) / 4) * ((y + 3) / 4) * block : (size_t)x * y * 4; }
        size_t most = PAGE_BYTES / (bytes ? bytes : 1); if (most > PAGE_LAYERS) most = PAGE_LAYERS; if (most < 1) most = 1;
        for (unsigned m = n; m < g_nTex && (size_t)k < most; m++) { GTex *b = &g_tex[m]; if (b->want && b->fmt == a.fmt && b->w == a.w && b->h == a.h && b->levels == a.levels) mem[k++] = m; }
        for (int i = 0; i < MAX_PAGES && slot < 0; i++) if (!g_page[i].img) slot = i;
        if (k < 2 || slot < 0) { for (int i = 0; i < k; i++) g_tex[mem[i]].want = 0; continue; }   /* alone in its size, or there's no room: it stays as it is */
        GPage *p = &g_page[slot];
        VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = a.fmt; ii.extent.width = (uint32_t)a.w; ii.extent.height = (uint32_t)a.h; ii.extent.depth = 1; ii.mipLevels = (uint32_t)a.levels; ii.arrayLayers = (uint32_t)k; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; CK(vkCreateImage(g_dev, &ii, NULL, &p->img));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_dev, p->img, &mr); VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ma.allocationSize = mr.size; ma.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(g_dev, &ma, NULL, &p->mem) != VK_SUCCESS) { printf("[gfx] no memory for a texture page of %d: they stay on their own\n", k); fflush(stdout); vkDestroyImage(g_dev, p->img, NULL); p->img = VK_NULL_HANDLE; for (int i = 0; i < k; i++) g_tex[mem[i]].want = 0; continue; }
        CK(vkBindImageMemory(g_dev, p->img, p->mem, 0));
        p->view = viewArray(p->img, a.fmt, (uint32_t)a.levels, (uint32_t)k); p->ds = dsTexture(p->view); p->live = k;
        VkCommandBuffer cb = oneBegin();
        VkImageMemoryBarrier pb = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; pb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; pb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; pb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; pb.srcQueueFamilyIndex = pb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; pb.image = p->img; pb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; pb.subresourceRange.levelCount = (uint32_t)a.levels; pb.subresourceRange.layerCount = (uint32_t)k;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &pb);
        for (int i = 0; i < k; i++) { GTex *t = &g_tex[mem[i]];
            barrierImg(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, (uint32_t)a.levels);
            VkImageCopy c[16]; memset(c, 0, sizeof c); int nl = a.levels < 16 ? a.levels : 16;
            for (int l = 0, x = a.w, y = a.h; l < nl; l++, x = x > 1 ? x / 2 : 1, y = y > 1 ? y / 2 : 1) { c[l].srcSubresource.aspectMask = c[l].dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c[l].srcSubresource.mipLevel = c[l].dstSubresource.mipLevel = (uint32_t)l; c[l].srcSubresource.layerCount = c[l].dstSubresource.layerCount = 1; c[l].dstSubresource.baseArrayLayer = (uint32_t)i; c[l].extent.width = (uint32_t)x; c[l].extent.height = (uint32_t)y; c[l].extent.depth = 1; }
            vkCmdCopyImage(cb, t->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, p->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)nl, c); }
        pb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; pb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; pb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; pb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &pb);
        oneEnd(cb);
        for (int i = 0; i < k; i++) { GTex *t = &g_tex[mem[i]]; vkFreeDescriptorSets(g_dev, g_dp, 1, &t->ds); vkFreeDescriptorSets(g_dev, g_dp, 1, &t->dsA); vkDestroyImageView(g_dev, t->view, NULL); vkDestroyImageView(g_dev, t->viewA, NULL); vkDestroyImage(g_dev, t->img, NULL); vkFreeMemory(g_dev, t->mem, NULL);
            t->img = VK_NULL_HANDLE; t->view = t->viewA = VK_NULL_HANDLE; t->ds = VK_NULL_HANDLE; t->mem = VK_NULL_HANDLE; t->dsA = p->ds; t->page = slot + 1; t->layer = i; t->want = 0; }
        moved += k; made++; }
    struct timespec t1; clock_gettime(CLOCK_MONOTONIC, &t1);
    if (moved) { printf("[gfx] texture pages: %d textures onto %d pages, %.0f ms\n", moved, made, (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6); fflush(stdout); }
}

/* ---- pipelines --------------------------------------------------------------------------- */
static VkShaderModule shaderOf(const uint32_t *code, size_t bytes) { VkShaderModuleCreateInfo i = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; i.codeSize = bytes; i.pCode = code; VkShaderModule m; CK(vkCreateShaderModule(g_dev, &i, NULL, &m)); return m; }
enum { PF_DEPTH = 1, PF_DWRITE = 2, PF_BLEND = 4, PF_ADD = 8, PF_NOCOLOR = 16, PF_LINE = 32, PF_POINT = 64, PF_MASK0 = 128, PF_HULL = 256, PF_CULLF = 512, PF_CW = 1024, PF_DYNCULL = 2048, PF_GLOSS = 4096, PF_MASKA = 8192, PF_MUL = 16384, PF_RGB = 32768 };   /* MUL: what's there is multiplied by what's drawn (a stain). RGB: the target's alpha is left as it is */
typedef struct { const uint32_t *vs; size_t vsN; const uint32_t *fs; size_t fsN; int flags, cutout, shadow, verts, pass; } PipeSpec;   /* verts: 0 static, 1 skin, 2 line, 3 hud, 4 effect, 5 sky point, 7 none, 8 a head, 9 positions alone, 11 static and 12 skin with where each vertex was at rest (fur). pass: 0 the world, 1 the HUD, 2 to the glass, 3 bloom, 4 occlusion, 5 its blur, 6 the air, 7 a picture drawn off-screen, 8 a full-screen picture that reads the view and a texture */
typedef struct { float r[12], tint[4], extra[3]; uint32_t bone; float n[9]; } GInst;   /* one copy of a mesh: its model matrix's rows, its colour, its alpha, glow and layer, where its bones start in the ring, and what turns its normals */
static VkPipeline pipeMake(const PipeSpec *s) {
    int off7 = s->pass == 7;   /* a picture drawn off-screen is plain: 8-bit, and no light but the sun's and the sky's */
    /* 8 to 12 are a surface's material (step 22): highlights, what it reflects, normal maps, fine detail, parallax. A picture drawn off-screen has none of them */
    int spec[18] = {g_taps, s->cutout, s->shadow, (off7 && g_cfg.linear == 2) ? 3 : g_cfg.linear, g_cfg.hemi, off7 ? 0 : g_cfg.lights > 0, off7 ? 1 : g_cfg.cascades, off7 ? 0 : g_cfg.probes,
                    off7 ? 0 : g_cfg.spec, off7 ? 0 : g_cfg.reflect, off7 ? 0 : g_cfg.normals, off7 ? 0 : g_cfg.detail, off7 ? 0 : g_cfg.parallax,
                    off7 ? 0 : g_cfg.skin, off7 ? 0 : g_cfg.hair, off7 ? 0 : g_cfg.eyes, off7 ? 0 : g_cfg.sheen, (!off7 && g_mid && g_cfg.refract) ? 1 : 0};   /* 13 to 16: a body's skin, hair, eyes and cloth (step 23) */
    VkSpecializationMapEntry me[18] = {{0, 0, 4}, {1, 4, 4}, {2, 8, 4}, {3, 12, 4}, {4, 16, 4}, {5, 20, 4}, {6, 24, 4}, {7, 28, 4}, {8, 32, 4}, {9, 36, 4}, {10, 40, 4}, {11, 44, 4}, {12, 48, 4}, {13, 52, 4}, {14, 56, 4}, {15, 60, 4}, {16, 64, 4}, {17, 68, 4}}; VkSpecializationInfo si = {18, me, sizeof spec, spec};   /* 17: water reads what's under it (step 24) */
    VkPipelineShaderStageCreateInfo st[2] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = shaderOf(s->vs, s->vsN); st[0].pName = "main"; st[0].pSpecializationInfo = &si; int ns = 1;
    if (s->fs) { st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = shaderOf(s->fs, s->fsN); st[1].pName = "main"; st[1].pSpecializationInfo = &si; ns = 2; }
    VkVertexInputBindingDescription vb[9]; VkVertexInputAttributeDescription va[12]; uint32_t nb = 0, na = 0;
#define B_(st_) do { vb[nb].binding = nb; vb[nb].stride = (st_); vb[nb].inputRate = VK_VERTEX_INPUT_RATE_VERTEX; nb++; } while (0)
#define A_(loc_, bind_, fmt_, off_) do { va[na].location = (loc_); va[na].binding = (bind_); va[na].format = (fmt_); va[na].offset = (off_); na++; } while (0)
#define I_(skin_) do { uint32_t b_ = nb; vb[nb].binding = nb; vb[nb].stride = sizeof(GInst); vb[nb].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE; nb++; A_(5, b_, VK_FORMAT_R32G32B32A32_SFLOAT, 0); A_(6, b_, VK_FORMAT_R32G32B32A32_SFLOAT, 16); A_(7, b_, VK_FORMAT_R32G32B32A32_SFLOAT, 32); A_(8, b_, VK_FORMAT_R32G32B32A32_SFLOAT, 48); A_(9, b_, VK_FORMAT_R32G32B32_SFLOAT, 64); if (skin_) { A_(10, b_, VK_FORMAT_R32_UINT, 76); } else { A_(11, b_, VK_FORMAT_R32G32B32_SFLOAT, 80); A_(12, b_, VK_FORMAT_R32G32B32_SFLOAT, 92); A_(13, b_, VK_FORMAT_R32G32B32_SFLOAT, 104); } } while (0)
    if (s->verts == 0) { B_(12); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); if (!s->shadow) { B_(12); A_(1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0); B_(8); A_(4, 2, VK_FORMAT_R32G32_SFLOAT, 0); } I_(0); }
    else if (s->verts == 1) { B_(12); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); B_(12); A_(1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0); B_(4); A_(2, 2, VK_FORMAT_R8G8B8A8_UINT, 0); B_(4); A_(3, 3, VK_FORMAT_R8G8B8A8_UNORM, 0); B_(8); A_(4, 4, VK_FORMAT_R32G32_SFLOAT, 0); I_(1); }
    else if (s->verts == 11) { B_(12); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); B_(12); A_(1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0); B_(8); A_(4, 2, VK_FORMAT_R32G32_SFLOAT, 0); B_(12); A_(14, 3, VK_FORMAT_R32G32B32_SFLOAT, 0); I_(0); }
    else if (s->verts == 12) { B_(12); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); B_(12); A_(1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0); B_(4); A_(2, 2, VK_FORMAT_R8G8B8A8_UINT, 0); B_(4); A_(3, 3, VK_FORMAT_R8G8B8A8_UNORM, 0); B_(8); A_(4, 4, VK_FORMAT_R32G32_SFLOAT, 0); B_(12); A_(14, 5, VK_FORMAT_R32G32B32_SFLOAT, 0); I_(1); }
    else if (s->verts == 2) { B_(24); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); A_(1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12); }
    else if (s->verts == 4) { B_(36); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); A_(1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 12); A_(2, 0, VK_FORMAT_R32G32_SFLOAT, 28); }
    else if (s->verts == 6) { B_(32); A_(0, 0, VK_FORMAT_R32G32_SFLOAT, 0); A_(1, 0, VK_FORMAT_R32G32_SFLOAT, 8); A_(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16); }
    else if (s->verts == 5) { B_(20); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); A_(1, 0, VK_FORMAT_R32G32_SFLOAT, 12); }
    else if (s->verts == 8) { B_(32); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); A_(1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12); A_(2, 0, VK_FORMAT_R32G32_SFLOAT, 24); for (int m7 = 0; m7 < 8; m7++) { B_(12); vb[nb - 1].binding = (uint32_t)(1 + m7); va[na].location = (uint32_t)(3 + m7); va[na].binding = (uint32_t)(1 + m7); va[na].format = VK_FORMAT_R32G32B32_SFLOAT; va[na].offset = 0; na++; } }
    else if (s->verts == 9) { B_(12); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); }
    else if (s->verts == 11) { B_(56); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); A_(1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12); A_(2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24); A_(3, 0, VK_FORMAT_R32_SFLOAT, 36); A_(4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 40); }
    else if (s->verts == 12) { B_(40); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); }
    else if (s->verts == 10) { B_(32); A_(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0); A_(1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12); A_(2, 0, VK_FORMAT_R32G32_SFLOAT, 24); }
    else if (s->verts != 7) { B_(8); A_(0, 0, VK_FORMAT_R32G32_SFLOAT, 0); }
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO}; vi.vertexBindingDescriptionCount = nb; vi.pVertexBindingDescriptions = vb; vi.vertexAttributeDescriptionCount = na; vi.pVertexAttributeDescriptions = va;
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = (s->flags & PF_LINE) ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : (s->flags & PF_POINT) ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.f;
    if (s->flags & PF_CULLF) rs.cullMode = VK_CULL_MODE_FRONT_BIT;
    if (s->flags & PF_CW) rs.frontFace = VK_FRONT_FACE_CLOCKWISE;   /* a reflection flips the winding */
    if (s->flags & PF_HULL) { rs.cullMode = VK_CULL_MODE_FRONT_BIT; rs.depthBiasEnable = 1; rs.depthBiasConstantFactor = 4.f; rs.depthBiasSlopeFactor = 2.5f; }   /* where a surface and the line meet at the same depth, the line was winning and drew black blotches: it's pushed a hair back */
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = (s->pass || s->shadow) ? VK_SAMPLE_COUNT_1_BIT : g_samples;
    VkPipelineDepthStencilStateCreateInfo ds = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO}; ds.depthTestEnable = (s->flags & PF_DEPTH) ? 1 : 0; ds.depthWriteEnable = (s->flags & PF_DWRITE) ? 1 : 0; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState ba = {0}; ba.colorWriteMask = (s->flags & PF_MASKA) ? VK_COLOR_COMPONENT_A_BIT : (s->flags & PF_MASK0) ? 0 : 15;
    if (s->flags & (PF_BLEND | PF_ADD)) { ba.blendEnable = 1; ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; ba.dstColorBlendFactor = (s->flags & PF_ADD) ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.colorBlendOp = VK_BLEND_OP_ADD; ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; ba.alphaBlendOp = VK_BLEND_OP_ADD; }
    if (s->flags & PF_GLOSS) { ba.blendEnable = 1; ba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; ba.colorBlendOp = VK_BLEND_OP_ADD; ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; ba.alphaBlendOp = VK_BLEND_OP_ADD; }   /* its own colour, plus what's under it times how much a floor shows from here */
    if (s->flags & PF_MUL) { ba.blendEnable = 1; ba.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR; ba.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; ba.colorBlendOp = VK_BLEND_OP_ADD; ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.alphaBlendOp = VK_BLEND_OP_ADD; }
    if (s->flags & PF_RGB) ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = (s->flags & PF_NOCOLOR) ? 0 : 1; cb.pAttachments = &ba;
    VkDynamicState dy[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE}; VkPipelineDynamicStateCreateInfo dsi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; dsi.dynamicStateCount = (s->flags & PF_DYNCULL) ? 3 : 2; dsi.pDynamicStates = dy;
    VkFormat fmt = (s->pass == 3 || s->pass == 6) ? HDR_FMT : (s->pass == 4 || s->pass == 5) ? AO_FMT : s->pass == 10 ? g_glassFmt : (s->pass && s->pass != 9) ? COLOR_FMT : g_sceneFmt;   /* 9: a full-screen pass over the scene, into a picture like it. 10: onto the glass itself, whatever it's made of (step 25) */
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO}; if (!(s->flags & PF_NOCOLOR)) { ri.colorAttachmentCount = 1; ri.pColorAttachmentFormats = &fmt; } if (!s->pass || s->pass == 7 || (s->pass == 1 && !g_split)) ri.depthAttachmentFormat = g_depthFmt;
    VkGraphicsPipelineCreateInfo g = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; g.pNext = &ri; g.stageCount = (uint32_t)ns; g.pStages = st; g.pVertexInputState = &vi; g.pInputAssemblyState = &ia; g.pViewportState = &vp; g.pRasterizationState = &rs; g.pMultisampleState = &ms; g.pDepthStencilState = &ds; g.pColorBlendState = &cb; g.pDynamicState = &dsi; g.layout = (s->pass == 2 || s->pass == 3 || s->pass == 5 || s->pass == 9 || s->pass == 10) ? g_layoutPost : g_layout;
    VkPipeline p; CK(vkCreateGraphicsPipelines(g_dev, VK_NULL_HANDLE, 1, &g, NULL, &p)); return p;
}
#define SPV(n) spv_##n, sizeof spv_##n
static void pipesMake(void) {
    const int D = PF_DEPTH | PF_DWRITE;
    PipeSpec t[P_N] = {
        [P_STATIC] = {SPV(mesh_static_vert), SPV(mesh_frag), D, 0, 0, 0}, [P_STATIC_CUT] = {SPV(mesh_static_vert), SPV(mesh_frag), D, 1, 0, 0},
        [P_SKIN] = {SPV(mesh_skin_vert), SPV(mesh_frag), D, 0, 0, 1}, [P_SKIN_CUT] = {SPV(mesh_skin_vert), SPV(mesh_frag), D, 1, 0, 1},
        [P_STATIC_BLEND] = {SPV(mesh_static_vert), SPV(mesh_frag), PF_DEPTH | PF_BLEND, 0, 0, 0}, [P_SKIN_BLEND] = {SPV(mesh_skin_vert), SPV(mesh_frag), PF_DEPTH | PF_BLEND, 0, 0, 1},
        [P_MOUTH] = {SPV(mesh_skin_mouth_vert), SPV(mesh_mouth_frag), D, 0, 0, 1}, [P_MOUTH_CUT] = {SPV(mesh_skin_mouth_vert), SPV(mesh_mouth_frag), D, 1, 0, 1},
        [P_TOON] = {SPV(mesh_skin_vert), SPV(mesh_toon_frag), D, 0, 0, 1}, [P_TOON_CUT] = {SPV(mesh_skin_vert), SPV(mesh_toon_frag), D, 1, 0, 1}, [P_TOON_STATIC] = {SPV(mesh_static_vert), SPV(mesh_toon_frag), D, 0, 0, 0}, [P_TOON_STATIC_CUT] = {SPV(mesh_static_vert), SPV(mesh_toon_frag), D, 1, 0, 0},
        [P_TOON_MOUTH] = {SPV(mesh_skin_mouth_vert), SPV(mesh_toon_mouth_frag), D, 0, 0, 1}, [P_TOON_MOUTH_CUT] = {SPV(mesh_skin_mouth_vert), SPV(mesh_toon_mouth_frag), D, 1, 0, 1}, [P_TOON_BLEND] = {SPV(mesh_skin_vert), SPV(mesh_toon_blend_frag), PF_DEPTH | PF_BLEND, 0, 0, 1},
        [P_INK] = {SPV(mesh_skin_ink_vert), SPV(ink_frag), D | PF_HULL, 0, 0, 1}, [P_INK_STATIC] = {SPV(mesh_static_ink_vert), SPV(ink_frag), D | PF_HULL, 0, 0, 0},
        [P_FUR] = {SPV(mesh_skin_fur_vert), SPV(mesh_fur_frag), D, 0, 0, 12}, [P_FUR_STATIC] = {SPV(mesh_static_fur_vert), SPV(mesh_fur_frag), D, 0, 0, 11},   /* (step 23) a shell of fur */
        [P_TOON_FUR] = {SPV(mesh_skin_fur_vert), SPV(mesh_toon_fur_frag), D, 0, 0, 12}, [P_TOON_FUR_STATIC] = {SPV(mesh_static_fur_vert), SPV(mesh_toon_fur_frag), D, 0, 0, 11},   /* and on a cel-shaded body: flat-coloured strands, over its line */
        [P_M_STATIC] = {SPV(mesh_static_vert), SPV(mesh_frag), D | PF_CW | PF_DYNCULL, 0, 0, 0}, [P_M_STATIC_CUT] = {SPV(mesh_static_vert), SPV(mesh_frag), D | PF_CW | PF_DYNCULL, 1, 0, 0}, [P_M_TOON_STATIC] = {SPV(mesh_static_vert), SPV(mesh_toon_frag), D | PF_CW | PF_DYNCULL, 0, 0, 0}, [P_M_TOON_STATIC_CUT] = {SPV(mesh_static_vert), SPV(mesh_toon_frag), D | PF_CW | PF_DYNCULL, 1, 0, 0},
        [P_R_STATIC] = {SPV(mesh_static_vert), SPV(mesh_refl_frag), D | PF_CW | PF_DYNCULL, 0, 0, 0}, [P_R_STATIC_CUT] = {SPV(mesh_static_vert), SPV(mesh_refl_frag), D | PF_CW | PF_DYNCULL, 1, 0, 0}, [P_R_SKIN] = {SPV(mesh_skin_vert), SPV(mesh_refl_frag), D, 0, 0, 1}, [P_R_SKIN_CUT] = {SPV(mesh_skin_vert), SPV(mesh_refl_frag), D, 1, 0, 1},
        [P_TR_STATIC] = {SPV(mesh_static_vert), SPV(mesh_toon_refl_frag), D | PF_CW | PF_DYNCULL, 0, 0, 0}, [P_TR_STATIC_CUT] = {SPV(mesh_static_vert), SPV(mesh_toon_refl_frag), D | PF_CW | PF_DYNCULL, 1, 0, 0}, [P_TR_SKIN] = {SPV(mesh_skin_vert), SPV(mesh_toon_refl_frag), D, 0, 0, 1}, [P_TR_SKIN_CUT] = {SPV(mesh_skin_vert), SPV(mesh_toon_refl_frag), D, 1, 0, 1},
        [P_M_INK] = {SPV(mesh_skin_ink_vert), SPV(ink_frag), D | PF_HULL | PF_CW, 0, 0, 1}, [P_M_INK_STATIC] = {SPV(mesh_static_ink_vert), SPV(ink_frag), D | PF_HULL | PF_CW, 0, 0, 0},
        [P_GLOSS] = {SPV(mesh_static_vert), SPV(mesh_gloss_frag), D | PF_GLOSS, 0, 0, 0}, [P_TOON_GLOSS] = {SPV(mesh_static_vert), SPV(mesh_toon_gloss_frag), D | PF_GLOSS, 0, 0, 0}, [P_TOON_GLOSS_A] = {SPV(mesh_static_vert), SPV(mesh_toon_frag), PF_DEPTH | PF_MASKA, 0, 0, 0},
        [P_SH_STATIC] = {SPV(mesh_static_vert), NULL, 0, D | PF_NOCOLOR, 0, 1, 0}, [P_SH_SKIN] = {SPV(mesh_skin_vert), NULL, 0, D | PF_NOCOLOR, 0, 1, 1},
        [P_LINE] = {SPV(line_vert), SPV(line_frag), PF_DEPTH | PF_ADD | PF_LINE, 0, 0, 2}, [P_HUD] = {SPV(hud_vert), SPV(hud_frag), PF_BLEND, 0, 0, 3, 1}, [P_SPRITE] = {SPV(sprite_vert), SPV(sprite_frag), PF_BLEND, 0, 0, 6, 1},
        [P_FX_ALPHA] = {SPV(fx_vert), SPV(fx_frag), PF_DEPTH | PF_BLEND, 0, 0, 4}, [P_FX_ADD] = {SPV(fx_vert), SPV(fx_frag), PF_DEPTH | PF_ADD, 0, 0, 4}, [P_FX_SOFT] = {SPV(fx_vert), SPV(fx_frag), PF_DEPTH | PF_BLEND, 1, 0, 4}, [P_FX_GLOW] = {SPV(fx_vert), SPV(fx_frag), PF_DEPTH | PF_ADD, 1, 0, 4},
        [P_FX_SOFTZ] = {SPV(fx_vert), SPV(fx_frag), PF_DEPTH | PF_BLEND, 2, 0, 4}, [P_FX_GLOWZ] = {SPV(fx_vert), SPV(fx_frag), PF_DEPTH | PF_ADD, 2, 0, 4},   /* (step 24) and fading out where they meet what's behind them */
        [P_DECAL] = {SPV(decal_vert), SPV(decal_frag), PF_CULLF | PF_MUL | PF_RGB, 0, 0, 7, 0}, [P_DECAL_PAINT] = {SPV(decal_vert), SPV(decal_frag), PF_CULLF | PF_BLEND | PF_RGB, 1, 0, 7, 0},
        [P_LENS_DOF] = {SPV(post_vert), SPV(lens_frag), 0, 1, 0, 7, 9}, [P_LENS_MB] = {SPV(post_vert), SPV(lens_frag), 0, 2, 0, 7, 9}, [P_TAA] = {SPV(post_vert), SPV(taa_frag), 0, 0, 0, 7, 9},   /* (step 25) the lens's passes */
        [P_PRESENT] = {SPV(post_vert), SPV(present_frag), 0, 0, 0, 7, 10}, [P_TUBE480] = {SPV(post_vert), SPV(tube_frag), 0, 1, 0, 7, 10}, [P_TUBE240] = {SPV(post_vert), SPV(tube_frag), 0, 2, 0, 7, 10},   /* (step 25) the picture onto the glass */
        [P_PRESENT_HDR] = {SPV(post_vert), SPV(present_frag), 0, 2, 0, 7, 10}, [P_HDREX] = {SPV(post_vert), SPV(hdrex_frag), 0, 0, 0, 7, 3},   /* and onto an HDR10 glass, with the light past white */
        [P_SKY] = {SPV(sky_vert), SPV(sky_frag), PF_DEPTH | PF_ADD | PF_POINT, 0, 0, 5},
        [P_HOLO_Z] = {SPV(mesh_skin_vert), NULL, 0, D | PF_MASK0, 0, 0, 1}, [P_HOLO] = {SPV(mesh_skin_vert), SPV(holo_frag), PF_DEPTH | PF_BLEND, 0, 0, 1},
        [P_SKYDOME] = {SPV(post_vert), SPV(skydome_frag), 0, g_cfg.sky, 0, 7, 0},
        [P_SPACE] = {SPV(post_vert), SPV(space_frag), 0, 0, 0, 7, 0}, [P_BEAM] = {SPV(beam_vert), SPV(beam_frag), PF_DEPTH | PF_ADD | PF_CULLF, 0, 0, 7, 0},
        [P_SCREEN] = {SPV(screen_vert), SPV(screen_frag), D, 0, 0, 7, 0},
        [P_WATER] = {SPV(mesh_static_vert), SPV(water_frag), PF_DEPTH | PF_BLEND, 0, 0, 0}, [P_FX_PUFF] = {SPV(fx_vert), SPV(puff_frag), PF_DEPTH | PF_BLEND, 0, 0, 4}, [P_FX_WEB] = {SPV(fx_vert), SPV(starweb_frag), PF_DEPTH | PF_BLEND, 0, 0, 4}, [P_FX_WEBX] = {SPV(fx_vert), SPV(starweb_frag), PF_BLEND, 0, 0, 4},
        [P_STAR_SURF] = {SPV(star_vert), SPV(starsurf_frag), D, 0, 0, 9}, [P_STAR_FACE] = {SPV(star_vert), SPV(starface_frag), D, 0, 0, 9}, [P_STAR_CORONA] = {SPV(starcorona_vert), SPV(starcorona_frag), PF_DEPTH | PF_ADD, 0, 0, 3}, [P_STAR_LOOP] = {SPV(starloop_vert), SPV(starloop_frag), PF_DEPTH | PF_ADD, 0, 0, 10},
        [P_LINE_OVER] = {SPV(line_vert), SPV(line_frag), PF_ADD | PF_LINE, 0, 0, 2},
        [P_T_STATIC] = {SPV(mesh_static_vert), SPV(mesh_frag), D, 0, 0, 0, 7}, [P_T_STATIC_CUT] = {SPV(mesh_static_vert), SPV(mesh_frag), D, 1, 0, 0, 7}, [P_T_SKIN] = {SPV(mesh_skin_vert), SPV(mesh_frag), D, 0, 0, 1, 7}, [P_T_SKIN_CUT] = {SPV(mesh_skin_vert), SPV(mesh_frag), D, 1, 0, 1, 7},
        [P_HEAD] = {SPV(head_vert), SPV(head_frag), D, 0, 0, 8, 7}, [P_HEADFLAT] = {SPV(headflat_vert), SPV(headflat_frag), D, 0, 0, 9, 7},
        [P_B_SKIN] = {SPV(mesh_skin_vert), SPV(white_frag), 0, 0, 0, 1, 7}, [P_FLOORSH] = {SPV(floorshadow_vert), SPV(floorshadow_frag), PF_DEPTH | PF_BLEND, 0, 0, 7, 0},
        [P_POST] = {SPV(post_vert), SPV(post_frag), 0, 0, 0, 7, 2}, [P_BLOOM0] = {SPV(post_vert), SPV(bloom_frag), 0, 0, 0, 7, 3}, [P_BLOOM_DOWN] = {SPV(post_vert), SPV(bloom_frag), 0, 1, 0, 7, 3}, [P_BLOOM_UP] = {SPV(post_vert), SPV(bloom_frag), PF_ADD, 2, 0, 7, 3},
        [P_AO] = {SPV(post_vert), SPV(ao_frag), 0, 0, 0, 7, 4}, [P_AOBLUR] = {SPV(post_vert), SPV(aoblur_frag), 0, 0, 0, 7, 5}, [P_VOL] = {SPV(post_vert), SPV(vol_frag), 0, 0, 0, 7, 6} };
    for (int i = 0; i < P_N; i++) { if (i == P_SKYDOME && !g_cfg.sky) continue; if (i >= P_FUR && i <= P_TOON_FUR_STATIC && !g_cfg.fur) continue; if (i >= P_M_STATIC && i <= P_TOON_GLOSS_A && !g_cfg.mirrors) continue; if (i >= P_SCREEN && i <= P_FLOORSH && !g_cfg.screens) continue; if (i >= P_POST && i <= P_VOL && !g_cfg.hdr) continue; if (i >= P_FX_SOFTZ && i <= P_DECAL_PAINT && !g_mid) continue; if (i == P_LENS_DOF && !(g_lens && g_cfg.dof)) continue; if (i == P_LENS_MB && !(g_lens && g_cfg.mblur)) continue; if (i == P_TAA && !(g_lens && g_cfg.taa)) continue; if (i >= P_PRESENT && i <= P_TUBE240 && (!g_present || (i == P_PRESENT) == (g_cfg.tube != 0) || (i == P_TUBE480 && g_cfg.tube != 1) || (i == P_TUBE240 && g_cfg.tube != 2))) continue; if ((i == P_PRESENT_HDR || i == P_HDREX) && !g_hdrOut) continue; if ((i == P_AO || i == P_AOBLUR) && !g_cfg.ao && !g_cfg.contact) continue; if (i == P_VOL && !g_cfg.volumetric) continue; g_pipe[i] = pipeMake(&t[i]); }
}

/* ---- start-up ---------------------------------------------------------------------------- */
static void deviceMake(const char **instExt, uint32_t nInst, const char **devExt, uint32_t nDev) {
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3; ai.pApplicationName = "aigameengine";
    VkInstanceCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &ai; ci.enabledExtensionCount = nInst; ci.ppEnabledExtensionNames = instExt; CK(vkCreateInstance(&ci, NULL, &g_inst));
    uint32_t n = 8; VkPhysicalDevice pd[8]; vkEnumeratePhysicalDevices(g_inst, &n, pd); int best = -1;
    /* a discrete GPU or the Pi's V3D first, any other GPU next, and a software device (lavapipe) only when it's all there is: the receipt's test layer (step 19), never a player's */
    int bestS = -1; for (uint32_t i = 0; i < n; i++) { VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(pd[i], &p); int s = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 : (strstr(p.deviceName, "V3D") ? 3 : (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 0 : 1)); if (s > bestS) { best = (int)i; bestS = s; if (s == 3) break; } }
    if (best < 0) { printf("[gfx] no GPU\n"); exit(1); } g_gpu = pd[best];
    if (bestS == 0) printf("[gfx] a software device: deterministic pixels, the receipt's test layer, not a player's road\n");
    VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(g_gpu, &p); g_uboAlign = p.limits.minUniformBufferOffsetAlignment; if (g_uboAlign < 16) g_uboAlign = 16;
    printf("[gfx] %s, Vulkan %u.%u.%u, driver reports ubo align %u\n", p.deviceName, VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion), (unsigned)g_uboAlign); fflush(stdout);
    vkGetPhysicalDeviceMemoryProperties(g_gpu, &g_mp);
    uint32_t nq = 16; VkQueueFamilyProperties qp[16]; vkGetPhysicalDeviceQueueFamilyProperties(g_gpu, &nq, qp); for (uint32_t i = 0; i < nq; i++) if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { g_qf = i; break; }
    float pr = 1.f; VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = g_qf; qi.queueCount = 1; qi.pQueuePriorities = &pr;
    /* what the settings ask for, against what this machine has */
    VkPhysicalDeviceFeatures have, want; vkGetPhysicalDeviceFeatures(g_gpu, &have); memset(&want, 0, sizeof want); want.samplerAnisotropy = have.samplerAnisotropy; want.textureCompressionBC = have.textureCompressionBC;
    if (g_cfg.aniso > 0) { if (!have.samplerAnisotropy) g_cfg.aniso = 0; else if ((float)g_cfg.aniso > p.limits.maxSamplerAnisotropy) g_cfg.aniso = (int)p.limits.maxSamplerAnisotropy; }
    { VkSampleCountFlags ok = p.limits.framebufferColorSampleCounts & p.limits.framebufferDepthSampleCounts; int sc = g_cfg.msaa < 1 ? 1 : g_cfg.msaa; while (sc > 1 && !(ok & (VkSampleCountFlags)sc)) sc /= 2; g_cfg.msaa = sc; g_samples = (VkSampleCountFlagBits)sc; }
    if (g_cfg.hdr) { VkFormatProperties q; vkGetPhysicalDeviceFormatProperties(g_gpu, HDR_FMT, &q); VkFormatFeatureFlags w = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT; if ((q.optimalTilingFeatures & w) != w) g_cfg.hdr = 0; }
    if (g_cfg.hdr) { if (g_cfg.linear) g_cfg.linear = 2; } else { if (g_cfg.linear == 2) g_cfg.linear = 1; g_cfg.tonemap = g_cfg.bloom = g_cfg.grade = g_cfg.ao = g_cfg.contact = g_cfg.volumetric = 0; g_cfg.softfx = g_cfg.decals = g_cfg.refract = 0; g_cfg.dof = g_cfg.mblur = g_cfg.taa = g_cfg.hdrout = 0; }
    g_mid = g_cfg.hdr && (g_cfg.softfx || g_cfg.decals || g_cfg.refract);
    g_lens = g_cfg.hdr && (g_cfg.dof || g_cfg.mblur || g_cfg.taa);
    g_readDepth = g_cfg.ao || g_cfg.contact || g_cfg.volumetric || g_mid || g_lens;
    g_filmPost = g_cfg.hdr && !g_cfg.linear; g_sceneFmt = (g_cfg.hdr && !g_filmPost) ? HDR_FMT : COLOR_FMT; g_split = g_cfg.hdr || g_samples > VK_SAMPLE_COUNT_1_BIT;
    VkPhysicalDeviceVulkan13Features f13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES}; f13.dynamicRendering = 1;
    VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.pNext = &f13; di.pEnabledFeatures = &want; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.enabledExtensionCount = nDev; di.ppEnabledExtensionNames = devExt; CK(vkCreateDevice(g_gpu, &di, NULL, &g_dev)); vkGetDeviceQueue(g_dev, g_qf, 0, &g_q);
    VkCommandPoolCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cpi.queueFamilyIndex = g_qf; CK(vkCreateCommandPool(g_dev, &cpi, NULL, &g_pool));
    { VkFormat tr[3] = {VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT}; for (int i = 0; i < 3 && !g_depthFmt; i++) { VkFormatProperties q; vkGetPhysicalDeviceFormatProperties(g_gpu, tr[i], &q); if ((q.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) && (q.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) g_depthFmt = tr[i]; } }
}
/* ---- what's sized to the raster ------------------------------------------------------------ */
static VkDescriptorSet dsPost(VkImageView view) {
    VkDescriptorSetAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool = g_dp; a.descriptorSetCount = 1; a.pSetLayouts = &g_dslPost; VkDescriptorSet s; CK(vkAllocateDescriptorSets(g_dev, &a, &s));
    VkDescriptorImageInfo im = {g_smpClamp, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = s; w.dstBinding = 0; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); return s;
}
static void postWrite(void) {
    VkImageView aov = (g_cfg.ao || g_cfg.contact) ? g_aoView[1] : g_hdrView, volv = g_cfg.volumetric ? g_volView : g_hdrView;
    VkDescriptorImageInfo im[5] = {{g_smpClamp, g_hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {g_smpClamp, g_cfg.bloom ? g_blView[0] : g_hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {g_smpClamp, g_lutView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {g_smpClamp, aov, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {g_smpClamp, volv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
    VkWriteDescriptorSet w[5]; memset(w, 0, sizeof w);
    for (int i = 0; i < 5; i++) { w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = g_postDs; w[i].dstBinding = (uint32_t)i; w[i].descriptorCount = 1; w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &im[i]; }
    vkUpdateDescriptorSets(g_dev, 5, w, 0, NULL);
}
static void img3D(const unsigned char *rgba, int w_, int h_, int d_, VkImage *outI, VkImageView *outV, VkDeviceMemory *outM) {
    VkImageCreateInfo i = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; i.imageType = VK_IMAGE_TYPE_3D; i.format = VK_FORMAT_R8G8B8A8_UNORM; i.extent.width = (uint32_t)w_; i.extent.height = (uint32_t)h_; i.extent.depth = (uint32_t)d_; i.mipLevels = 1; i.arrayLayers = 1; i.samples = VK_SAMPLE_COUNT_1_BIT; i.tiling = VK_IMAGE_TILING_OPTIMAL; i.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; CK(vkCreateImage(g_dev, &i, NULL, outI));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_dev, *outI, &mr); VkMemoryAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize = mr.size; a.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); CK(vkAllocateMemory(g_dev, &a, NULL, outM)); CK(vkBindImageMemory(g_dev, *outI, *outM, 0));
    VkImageViewCreateInfo v = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image = *outI; v.viewType = VK_IMAGE_VIEW_TYPE_3D; v.format = VK_FORMAT_R8G8B8A8_UNORM; v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; v.subresourceRange.levelCount = 1; v.subresourceRange.layerCount = 1; CK(vkCreateImageView(g_dev, &v, NULL, outV));
    size_t bytes = (size_t)w_ * h_ * d_ * 4; Arena st; arenaMake(&st, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT); memcpy(st.map, rgba, bytes);
    VkCommandBuffer cb = oneBegin(); barrierImg(cb, *outI, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, 1);
    VkBufferImageCopy c = {0}; c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.layerCount = 1; c.imageExtent.width = (uint32_t)w_; c.imageExtent.height = (uint32_t)h_; c.imageExtent.depth = (uint32_t)d_; vkCmdCopyBufferToImage(cb, st.b, *outI, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    barrierImg(cb, *outI, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1); oneEnd(cb);
    vkDestroyBuffer(g_dev, st.b, NULL); vkFreeMemory(g_dev, st.m, NULL);
}
static void lutMake(const unsigned char *rgba, int n) { img3D(rgba, n, n, n, &g_lutImg, &g_lutView, &g_lutMem); }
static void probeBind(void) {
    for (int i = 0; i < FRAMES; i++) { VkDescriptorImageInfo im = {g_smpClamp, g_prView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = g_fr[i].dsView; w.dstBinding = 5; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); }
}
/* (step 22) a set's surroundings as a cube, for what a surface reflects: six faces of n x n, n a power of two, and every smaller level made here by halving, so a rough surface reads a blur of them */
static VkImage g_envImg; static VkImageView g_envView; static VkDeviceMemory g_envMem; static int g_envOn, g_envMips = 1;
static void imgCube(const unsigned char *rgba, int n, VkImage *outI, VkImageView *outV, VkDeviceMemory *outM, int *mipsOut) {
    int mips = 1; for (int s = n; s > 1; s >>= 1) mips++; if (mips > 12) mips = 12;
    VkImageCreateInfo i = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; i.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; i.imageType = VK_IMAGE_TYPE_2D; i.format = VK_FORMAT_R8G8B8A8_UNORM; i.extent.width = i.extent.height = (uint32_t)n; i.extent.depth = 1; i.mipLevels = (uint32_t)mips; i.arrayLayers = 6; i.samples = VK_SAMPLE_COUNT_1_BIT; i.tiling = VK_IMAGE_TILING_OPTIMAL; i.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; CK(vkCreateImage(g_dev, &i, NULL, outI));
    VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_dev, *outI, &mr); VkMemoryAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; a.allocationSize = mr.size; a.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); CK(vkAllocateMemory(g_dev, &a, NULL, outM)); CK(vkBindImageMemory(g_dev, *outI, *outM, 0));
    VkImageViewCreateInfo v = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image = *outI; v.viewType = VK_IMAGE_VIEW_TYPE_CUBE; v.format = VK_FORMAT_R8G8B8A8_UNORM; v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; v.subresourceRange.levelCount = (uint32_t)mips; v.subresourceRange.layerCount = 6; CK(vkCreateImageView(g_dev, &v, NULL, outV));
    size_t offs[12], tot = 0; int sz[12]; for (int l = 0, s = n; l < mips; l++) { offs[l] = tot; sz[l] = s; tot += (size_t)s * s * 24; s = s > 1 ? s >> 1 : 1; }
    Arena st; arenaMake(&st, tot, VK_BUFFER_USAGE_TRANSFER_SRC_BIT); unsigned char *o = (unsigned char *)st.map; memcpy(o, rgba, (size_t)n * n * 24);
    for (int l = 1; l < mips; l++) { const int s = sz[l], p = sz[l - 1];
        for (int f = 0; f < 6; f++) { const unsigned char *src = o + offs[l - 1] + (size_t)f * p * p * 4; unsigned char *dst = o + offs[l] + (size_t)f * s * s * 4;
            for (int y = 0; y < s; y++) for (int x = 0; x < s; x++) for (int k = 0; k < 4; k++)
                dst[(y * s + x) * 4 + k] = (unsigned char)((src[((2 * y) * p + 2 * x) * 4 + k] + src[((2 * y) * p + 2 * x + 1) * 4 + k] + src[((2 * y + 1) * p + 2 * x) * 4 + k] + src[((2 * y + 1) * p + 2 * x + 1) * 4 + k] + 2) / 4); } }
    VkCommandBuffer cb = oneBegin();
    VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = *outI; b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; b.subresourceRange.levelCount = (uint32_t)mips; b.subresourceRange.layerCount = 6;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
    VkBufferImageCopy c[12]; memset(c, 0, sizeof c);
    for (int l = 0; l < mips; l++) { c[l].bufferOffset = offs[l]; c[l].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c[l].imageSubresource.mipLevel = (uint32_t)l; c[l].imageSubresource.layerCount = 6; c[l].imageExtent.width = c[l].imageExtent.height = (uint32_t)sz[l]; c[l].imageExtent.depth = 1; }
    vkCmdCopyBufferToImage(cb, st.b, *outI, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)mips, c);
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
    oneEnd(cb); vkDestroyBuffer(g_dev, st.b, NULL); vkFreeMemory(g_dev, st.m, NULL); *mipsOut = mips;
}
static void envBind(void) {
    for (int i = 0; i < FRAMES; i++) { VkDescriptorImageInfo im = {g_smpRepeat, g_envView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = g_fr[i].dsView; w.dstBinding = 6; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); }
}
/* it only changes behind a load, like the baked light. What's in it: red, how much of the sky that way is open; green, whether the sun reaches what's there; the shader puts today's sky and ground on them */
/* (step 23) SKIN, WORKED OUT AHEAD: Penner's pre-integrated skin. Light that enters skin leaves it a little way off, by a profile that's been measured (d'Eon and Luebke's six
 * Gaussians: their widths in square millimetres, and a weight each for red, green and blue, red travelling furthest). On a surface curved to a radius r, what leaves a point is the
 * plain falloff at every point round the ring, weighted by that profile at the distance between them. So the table is: across, the cosine of the angle to the light, -1 to 1; down,
 * 1/r from 0 (flat: the plain falloff, exactly) to SKIN_TIGHT per millimetre. Each Gaussian is summed over its own width, because the narrow ones would fall between the steps of any
 * one grid and be lost, and they're most of blue. Square roots are stored, for the dark end's sake. The club's own sin, cos and exp, so it's the same bytes on every machine.
 * Made only where the skin setting's on: a machine without it binds one white texel. */
#define SKIN_LW 64
#define SKIN_LH 32
#define SKIN_TIGHT 0.32
#define SKIN_STEPS 48
double club_sin(double x); double club_cos(double x); double club_exp(double x);
static VkImage g_skinImg; static VkImageView g_skinView; static VkDeviceMemory g_skinMem;
static void skinLutMake(void) {
    static const double var[6] = {0.0064, 0.0484, 0.187, 0.567, 1.99, 7.41};
    static const double wt[6][3] = {{0.233, 0.455, 0.649}, {0.100, 0.336, 0.344}, {0.118, 0.198, 0.000}, {0.113, 0.007, 0.007}, {0.358, 0.004, 0.000}, {0.078, 0.000, 0.000}};
    const double PI = 3.14159265358979323846;
    unsigned char *px = malloc(SKIN_LW * SKIN_LH * 4); if (!px) return;
    for (int y = 0; y < SKIN_LH; y++) { const double c = SKIN_TIGHT * (double)y / (double)(SKIN_LH - 1);
        double R[6][SKIN_STEPS], ca[6][SKIN_STEPS], sa[6][SKIN_STEPS], k9[6];
        if (c > 0.0) for (int g = 0; g < 6; g++) { const double r = 1.0 / c; double X = 8.0 * sqrt(var[g]) / r; if (X > PI) X = PI; const double da = 2.0 * X / (double)SKIN_STEPS;
            for (int k = 0; k < SKIN_STEPS; k++) { const double a = -X + ((double)k + 0.5) * da, d = 2.0 * r * club_sin(0.5 * a); R[g][k] = club_exp(-d * d / (2.0 * var[g])); ca[g][k] = club_cos(a); sa[g][k] = club_sin(a); }
            k9[g] = da * r / sqrt(2.0 * PI * var[g]); }   /* the profile is round a point; summed across the way the light doesn't change, what's left along the ring is a Gaussian of the same width that adds up to 1, so each one carries its own weight of the light and no more. (Weighted by its height at the point instead, the narrow ones take nearly all of it and skin hardly scatters.) */
        for (int x = 0; x < SKIN_LW; x++) { const double ct = -1.0 + 2.0 * (double)x / (double)(SKIN_LW - 1), st = sqrt(1.0 - ct * ct > 0.0 ? 1.0 - ct * ct : 0.0); double out[3];
            if (c <= 0.0) out[0] = out[1] = out[2] = ct > 0.0 ? ct : 0.0;
            else { double num[3] = {0, 0, 0}, den[3] = {0, 0, 0};
                for (int g = 0; g < 6; g++) { double n = 0.0, d = 0.0;
                    for (int k = 0; k < SKIN_STEPS; k++) { const double l = ct * ca[g][k] - st * sa[g][k]; if (l > 0.0) n += l * R[g][k]; d += R[g][k]; }
                    for (int ch = 0; ch < 3; ch++) { num[ch] += wt[g][ch] * n * k9[g]; den[ch] += wt[g][ch] * d * k9[g]; } }
                for (int ch = 0; ch < 3; ch++) out[ch] = den[ch] > 0.0 ? num[ch] / den[ch] : 0.0; }
            unsigned char *o = px + ((size_t)y * SKIN_LW + (size_t)x) * 4;
            for (int ch = 0; ch < 3; ch++) { const double v = out[ch] < 0.0 ? 0.0 : out[ch] > 1.0 ? 1.0 : out[ch]; o[ch] = (unsigned char)(sqrt(v) * 255.0 + 0.5); }
            o[3] = 255; } }
    if (getenv("REVA_SKINLUT")) { FILE *f = fopen(getenv("REVA_SKINLUT"), "wb"); if (f) { fprintf(f, "P6\n%d %d\n255\n", SKIN_LW, SKIN_LH); for (int i = 0; i < SKIN_LW * SKIN_LH; i++) fwrite(px + i * 4, 1, 3, f); fclose(f); } }   /* the table as a picture, to look at */
    img3D(px, SKIN_LW, SKIN_LH, 1, &g_skinImg, &g_skinView, &g_skinMem); free(px);
}
static void skinBind(void) {
    for (int i = 0; i < FRAMES; i++) { VkDescriptorImageInfo im = {g_smpClamp, g_skinView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = g_fr[i].dsView; w.dstBinding = 7; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); }
}
void gfxEnvCube(const unsigned char *rgba, int size, const float *at) {
    (void)at; if (!g_dev || g_cfg.reflect < 2) return;
    if (!rgba && !g_envOn) return;
    vkDeviceWaitIdle(g_dev); vkDestroyImageView(g_dev, g_envView, NULL); vkDestroyImage(g_dev, g_envImg, NULL); vkFreeMemory(g_dev, g_envMem, NULL);
    static const unsigned char open[24] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    if (rgba && size >= 1 && !(size & (size - 1))) { imgCube(rgba, size, &g_envImg, &g_envView, &g_envMem, &g_envMips); g_envOn = 1; } else { imgCube(open, 1, &g_envImg, &g_envView, &g_envMem, &g_envMips); g_envOn = 0; }
    envBind();
}
/* a set's baked light. Like the grade, it only changes behind a load */
void gfxProbes(const unsigned char *rgba, int w, int h, int d, const float *geo) {
    if (!g_dev || !g_cfg.probes) return;
    if (!rgba && !g_prOn) return;
    vkDeviceWaitIdle(g_dev); vkDestroyImageView(g_dev, g_prView, NULL); vkDestroyImage(g_dev, g_prImg, NULL); vkFreeMemory(g_dev, g_prMem, NULL);
    static const unsigned char white[32] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
    if (rgba) { img3D(rgba, w, h, d, &g_prImg, &g_prView, &g_prMem); memcpy(g_prGeo, geo, 24); g_prDims[0] = w; g_prDims[1] = h; g_prDims[2] = d; g_prOn = 1; } else { img3D(white, 2, 2, 2, &g_prImg, &g_prView, &g_prMem); g_prOn = 0; }
    probeBind();
}
/* the grade only ever changes with the scene, behind a load, so it can wait for the GPU */
void gfxLut(const unsigned char *rgba, int n) {
    if (!g_dev || !g_cfg.hdr || !g_cfg.grade) return;
    if (!rgba && !g_lutSize) return;
    vkDeviceWaitIdle(g_dev); vkDestroyImageView(g_dev, g_lutView, NULL); vkDestroyImage(g_dev, g_lutImg, NULL); vkFreeMemory(g_dev, g_lutMem, NULL);
    static const unsigned char none[32] = {0, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 0, 255, 255, 255, 0, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255};
    if (rgba && n >= 2) { lutMake(rgba, n); g_lutSize = n; } else { lutMake(none, 2); g_lutSize = 0; }
    postWrite();
}
static void targetsMake(void) {
    g_depth = imgMakeX(g_depthFmt, (uint32_t)g_W, (uint32_t)g_H, 1, g_samples, 0, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | ((g_readDepth && g_samples == VK_SAMPLE_COUNT_1_BIT) ? VK_IMAGE_USAGE_SAMPLED_BIT : 0) | ((g_mid && g_samples == VK_SAMPLE_COUNT_1_BIT) ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0), VK_IMAGE_ASPECT_DEPTH_BIT, &g_depthView); g_depthMem = g_imgMem;
    if (g_samples > VK_SAMPLE_COUNT_1_BIT) { g_msImg = imgMakeX(g_sceneFmt, (uint32_t)g_W, (uint32_t)g_H, 1, g_samples, 0, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (g_mid ? 0 : VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT), VK_IMAGE_ASPECT_COLOR_BIT, &g_msView);   /* read mid-frame, it's kept from one half of the pass to the next */ g_msMem = g_imgMem; }
    if (!g_cfg.hdr) return;
    g_hdrImg = imgMake(g_sceneFmt, (uint32_t)g_W, (uint32_t)g_H, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | (g_mid ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0) | (g_lens ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0), VK_IMAGE_ASPECT_COLOR_BIT, &g_hdrView); g_hdrMem = g_imgMem; g_hdrDs = dsPost(g_hdrView);
    if (g_cfg.bloom) for (int i = 0, w = g_W / 2, h = g_H / 2; i < BLOOM_N; i++, w /= 2, h /= 2) { g_blW[i] = w > 1 ? w : 1; g_blH[i] = h > 1 ? h : 1; g_blImg[i] = imgMake(HDR_FMT, (uint32_t)g_blW[i], (uint32_t)g_blH[i], 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &g_blView[i]); g_blMem[i] = g_imgMem; g_blDs[i] = dsPost(g_blView[i]); }
    if (!g_lutImg) { static const unsigned char none[32] = {0, 0, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 0, 255, 255, 255, 0, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255}; lutMake(none, 2); }
    if (g_readDepth) {
        if (g_samples > VK_SAMPLE_COUNT_1_BIT) { g_zImg = imgMake(g_depthFmt, (uint32_t)g_W, (uint32_t)g_H, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | (g_mid ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0), VK_IMAGE_ASPECT_DEPTH_BIT, &g_zView); g_zMem = g_imgMem; g_zRead = g_zView; } else g_zRead = g_depthView;
        { VkDescriptorSetAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool = g_dp; a.descriptorSetCount = 1; a.pSetLayouts = &g_dslTex; CK(vkAllocateDescriptorSets(g_dev, &a, &g_zDs));
          VkDescriptorImageInfo im = {g_smpNear, g_zRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = g_zDs; w.dstBinding = 0; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); }
        int hw = g_W / 2 > 1 ? g_W / 2 : 1, hh = g_H / 2 > 1 ? g_H / 2 : 1;
        if (g_cfg.ao || g_cfg.contact) { for (int i = 0; i < 2; i++) { g_aoImg[i] = imgMake(AO_FMT, (uint32_t)hw, (uint32_t)hh, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &g_aoView[i]); g_aoMem[i] = g_imgMem; }
            g_blurDs = dsPost(g_aoView[0]); VkDescriptorImageInfo im = {g_smpNear, g_zRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = g_blurDs; w.dstBinding = 1; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &im; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); }
        if (g_cfg.volumetric) { g_volImg = imgMake(HDR_FMT, (uint32_t)hw, (uint32_t)hh, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &g_volView); g_volMem = g_imgMem; }
    }
    g_postDs = dsPost(g_hdrView); postWrite();
    if (g_lens) {   /* (step 25) the lens's passes: what they work in, what they read, and the last frame kept */
        const VkImageUsageFlags use = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; VkCommandBuffer cb0 = oneBegin(); VkImageView sv[5] = {g_hdrView, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
        if (g_cfg.dof || g_cfg.mblur) { g_scrImg[0] = imgMake(g_sceneFmt, (uint32_t)g_W, (uint32_t)g_H, 1, use, VK_IMAGE_ASPECT_COLOR_BIT, &g_scrView[0]); sv[SRC_SCR0] = g_scrView[0]; }
        if (g_cfg.dof && g_cfg.mblur) { g_scrImg[1] = imgMake(g_sceneFmt, (uint32_t)g_W, (uint32_t)g_H, 1, use, VK_IMAGE_ASPECT_COLOR_BIT, &g_scrView[1]); sv[SRC_SCR1] = g_scrView[1]; }
        if (g_cfg.taa) for (int i = 0; i < 2; i++) { g_histImg[i] = imgMake(g_sceneFmt, (uint32_t)g_W, (uint32_t)g_H, 1, use, VK_IMAGE_ASPECT_COLOR_BIT, &g_histView[i]); sv[SRC_HIST0 + i] = g_histView[i];
            barrierImg(cb0, g_histImg[i], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT, 1); }
        oneEnd(cb0);
        VkDescriptorImageInfo zi = {g_smpNear, g_zRead, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        for (int i = 0; i < 5; i++) if (sv[i]) { g_srcDs[i] = dsPost(sv[i]); w.dstSet = g_srcDs[i]; w.dstBinding = 3; w.pImageInfo = &zi; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); }
        if (g_cfg.taa) for (int i = 0; i < 2; i++) { VkDescriptorImageInfo hi = {g_smpClamp, g_histView[1 - i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; g_taaDs[i] = dsPost(g_hdrView);
            w.dstSet = g_taaDs[i]; w.dstBinding = 1; w.pImageInfo = &hi; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); w.dstBinding = 3; w.pImageInfo = &zi; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); } }
    if (g_mid) {   /* (step 24) where the scene's kept mid-frame */
        g_snapImg = imgMake(g_sceneFmt, (uint32_t)g_W, (uint32_t)g_H, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &g_snapView);
        g_zsImg = imgMake(g_depthFmt, (uint32_t)g_W, (uint32_t)g_H, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, &g_zsView);
        VkCommandBuffer cb0 = oneBegin(); barrierImg(cb0, g_snapImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT, 1);
        barrierImg(cb0, g_zsImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT, 1); oneEnd(cb0); }
}
__attribute__((unused)) static void targetsFree(void) {
    vkDestroyImageView(g_dev, g_depthView, NULL); vkDestroyImage(g_dev, g_depth, NULL); vkFreeMemory(g_dev, g_depthMem, NULL);
    if (g_readDepth) { vkFreeDescriptorSets(g_dev, g_dp, 1, &g_zDs); if (g_zImg) { vkDestroyImageView(g_dev, g_zView, NULL); vkDestroyImage(g_dev, g_zImg, NULL); vkFreeMemory(g_dev, g_zMem, NULL); g_zImg = VK_NULL_HANDLE; }
        if (g_cfg.ao || g_cfg.contact) { vkFreeDescriptorSets(g_dev, g_dp, 1, &g_blurDs); for (int i = 0; i < 2; i++) { vkDestroyImageView(g_dev, g_aoView[i], NULL); vkDestroyImage(g_dev, g_aoImg[i], NULL); vkFreeMemory(g_dev, g_aoMem[i], NULL); } }
        if (g_cfg.volumetric) { vkDestroyImageView(g_dev, g_volView, NULL); vkDestroyImage(g_dev, g_volImg, NULL); vkFreeMemory(g_dev, g_volMem, NULL); } }
    if (g_msImg) { vkDestroyImageView(g_dev, g_msView, NULL); vkDestroyImage(g_dev, g_msImg, NULL); vkFreeMemory(g_dev, g_msMem, NULL); g_msImg = VK_NULL_HANDLE; }
    if (!g_hdrImg) return;
    vkFreeDescriptorSets(g_dev, g_dp, 1, &g_hdrDs); vkFreeDescriptorSets(g_dev, g_dp, 1, &g_postDs); vkDestroyImageView(g_dev, g_hdrView, NULL); vkDestroyImage(g_dev, g_hdrImg, NULL); vkFreeMemory(g_dev, g_hdrMem, NULL); g_hdrImg = VK_NULL_HANDLE;
    if (g_cfg.bloom) for (int i = 0; i < BLOOM_N; i++) { vkFreeDescriptorSets(g_dev, g_dp, 1, &g_blDs[i]); vkDestroyImageView(g_dev, g_blView[i], NULL); vkDestroyImage(g_dev, g_blImg[i], NULL); vkFreeMemory(g_dev, g_blMem[i], NULL); }
}
static void commonMake(void) {
    g_sunImg = imgMake(g_depthFmt, (uint32_t)g_sunSize, (uint32_t)g_sunSize, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, &g_sunView);
    if (g_cfg.shadowTiles > 0) { g_atlasImg = imgMake(g_depthFmt, ATLAS_SIZE, ATLAS_SIZE, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, &g_atlasView); VkCommandBuffer cb0 = oneBegin(); barrierImg(cb0, g_atlasImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT, 1); oneEnd(cb0); }
    VkSamplerCreateInfo sc = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sc.magFilter = VK_FILTER_LINEAR; sc.minFilter = VK_FILTER_LINEAR; sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR; sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT; sc.maxLod = VK_LOD_CLAMP_NONE; if (g_cfg.aniso > 0) { sc.anisotropyEnable = 1; sc.maxAnisotropy = (float)g_cfg.aniso; } CK(vkCreateSampler(g_dev, &sc, NULL, &g_smpRepeat)); sc.anisotropyEnable = 0; sc.maxAnisotropy = 0.f;
    sc.compareEnable = 1; sc.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sc.maxLod = 0.f; CK(vkCreateSampler(g_dev, &sc, NULL, &g_smpShadow));
    sc.compareEnable = 0; CK(vkCreateSampler(g_dev, &sc, NULL, &g_smpClamp));
    sc.magFilter = VK_FILTER_NEAREST; sc.minFilter = VK_FILTER_NEAREST; CK(vkCreateSampler(g_dev, &sc, NULL, &g_smpNear));
    VkDescriptorSetLayoutBinding bv[10] = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};   /* 6: the set's surroundings as a cube (step 22). 7: skin's table (step 23). 8 and 9: the scene's colour and depth, read mid-frame (step 24) */
    VkDescriptorSetLayoutBinding bt[1] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo dl = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dl.bindingCount = 10; dl.pBindings = bv; CK(vkCreateDescriptorSetLayout(g_dev, &dl, NULL, &g_dslView));
    dl.bindingCount = 1; dl.pBindings = bt; CK(vkCreateDescriptorSetLayout(g_dev, &dl, NULL, &g_dslTex));
    { VkDescriptorSetLayoutBinding bp[5] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}, {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}}; dl.bindingCount = 5; dl.pBindings = bp; CK(vkCreateDescriptorSetLayout(g_dev, &dl, NULL, &g_dslPost));
      VkPushConstantRange pp = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128}; VkPipelineLayoutCreateInfo pl2 = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pl2.setLayoutCount = 1; pl2.pSetLayouts = &g_dslPost; pl2.pushConstantRangeCount = 1; pl2.pPushConstantRanges = &pp; CK(vkCreatePipelineLayout(g_dev, &pl2, NULL, &g_layoutPost)); }
    /* (step 22) a third set, the same shape as the second: a mesh's normal map, on its page. And 32 more bytes of push constants, at 80: what the mesh is made of */
    VkDescriptorSetLayout sets[3] = {g_dslView, g_dslTex, g_dslTex}; VkPushConstantRange pc = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};   /* 128 with step 23's eye glow: all a device has to offer */
    VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 3; pli.pSetLayouts = sets; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc; CK(vkCreatePipelineLayout(g_dev, &pli, NULL, &g_layout));
    VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 16}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_TEX * 2 + 128}}; VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; dpi.maxSets = MAX_TEX * 2 + 64; dpi.poolSizeCount = 3; dpi.pPoolSizes = ps; CK(vkCreateDescriptorPool(g_dev, &dpi, NULL, &g_dp));
    targetsMake();
    if (!g_mid) { g_dumImg = imgMake(VK_FORMAT_R8G8B8A8_UNORM, 1, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &g_dumView); VkCommandBuffer cb0 = oneBegin(); barrierImg(cb0, g_dumImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT, 1); oneEnd(cb0); }   /* a shader names the scene's depth whatever the settings, so there's always something bound: nothing's read from it */
    { VkCommandBuffer cb = oneBegin(); barrierImg(cb, g_sunImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT, 1); oneEnd(cb); }
    { static const unsigned char white[4] = {255, 255, 255, 255}; GTex t; memset(&t, 0, sizeof t); texUpload(&t, 1, 1, white); g_whiteDs = t.dsA; }
    arenaMake(&g_zero, 16u * 1024u * 1024u, VB_USE); memset(g_zero.map, 0, g_zero.size);
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = g_pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    for (int i = 0; i < FRAMES; i++) { Frame *f = &g_fr[i];
        CK(vkAllocateCommandBuffers(g_dev, &cai, &f->cb)); VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT; CK(vkCreateFence(g_dev, &fi, NULL, &f->fence));
        VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; CK(vkCreateSemaphore(g_dev, &si, NULL, &f->acq));
        arenaMake(&f->ring, RING_SIZE, VB_USE | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT); f->draws = malloc(sizeof(Draw) * MAX_DRAWS); f->bat = malloc(sizeof(Batch) * MAX_DRAWS);
        VkDescriptorSetAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool = g_dp; a.descriptorSetCount = 1; a.pSetLayouts = &g_dslView; CK(vkAllocateDescriptorSets(g_dev, &a, &f->dsView));
        VkDescriptorBufferInfo bu = {f->ring.b, 0, 1024}, bs = {f->ring.b, 0, VK_WHOLE_SIZE}; VkDescriptorImageInfo si2 = {g_smpShadow, g_sunView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, si3 = {g_smpShadow, g_atlasImg ? g_atlasView : g_sunView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet w[4] = {{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
        for (int k = 2; k < 4; k++) { w[k].dstSet = f->dsView; w[k].dstBinding = (uint32_t)k; w[k].descriptorCount = 1; w[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[k].pBufferInfo = &bs; }
        w[0].dstSet = f->dsView; w[0].dstBinding = 0; w[0].descriptorCount = 1; w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; w[0].pBufferInfo = &bu;
        w[1].dstSet = f->dsView; w[1].dstBinding = 1; w[1].descriptorCount = 1; w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[1].pImageInfo = &si2;
        vkUpdateDescriptorSets(g_dev, 4, w, 0, NULL);
        VkWriteDescriptorSet wa = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; wa.dstSet = f->dsView; wa.dstBinding = 4; wa.descriptorCount = 1; wa.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wa.pImageInfo = &si3; vkUpdateDescriptorSets(g_dev, 1, &wa, 0, NULL);
        { VkDescriptorImageInfo s8 = {g_smpClamp, g_mid ? g_snapView : g_dumView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, s9 = {g_smpNear, g_mid ? g_zsView : g_dumView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
          VkWriteDescriptorSet w8[2] = {{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
          for (int k = 0; k < 2; k++) { w8[k].dstSet = f->dsView; w8[k].dstBinding = (uint32_t)(8 + k); w8[k].descriptorCount = 1; w8[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w8[k].pImageInfo = k ? &s9 : &s8; }
          vkUpdateDescriptorSets(g_dev, 2, w8, 0, NULL); } }
    { static const unsigned char white[32] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255}; img3D(white, 2, 2, 2, &g_prImg, &g_prView, &g_prMem); probeBind(); }
    { static const unsigned char open[24] = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255}; imgCube(open, 1, &g_envImg, &g_envView, &g_envMem, &g_envMips); envBind(); }   /* no surroundings baked: the whole sky's open */
    if (g_cfg.skin) skinLutMake(); else { static const unsigned char one[4] = {255, 255, 255, 255}; img3D(one, 1, 1, 1, &g_skinImg, &g_skinView, &g_skinMem); }
    skinBind();   /* the shader names the table whatever the settings, so there's always one bound */
    if (getenv("REVA_SHOT") || getenv("REVA_SCENEFPS") || getenv("REVA_LABCTL") || getenv("REVA_BRIDGE") || getenv("REVA_SELSHOT") || getenv("REVA_MODESHOT")) capMake((g_present && !g_hdrOut) ? g_GW : g_W, (g_present && !g_hdrOut) ? g_GH : g_H);
    pipesMake();
    printf("[gfx] ready: %dx%d, depth format %d, sun map %d, %d shadow tap(s), %d pipelines built at start-up\n", g_W, g_H, g_depthFmt, g_sunSize, g_taps, g_cfg.hdr ? P_N : P_POST);
    printf("[gfx] settings: anisotropic %d, MSAA %d, linear %d, HDR %d, tonemap %d, bloom %d, grade %d, hemisphere %d, lights %d, cascades %d, occlusion %d, contact %d, volumetric %d, shadow tiles %d, probes %d, sky %d, screens %d, mirrors %d; highlights %d, reflections %d, normal maps %d, detail %d, parallax %d; skin %d, hair %d, eyes %d, sheen %d, fur %d\n", g_cfg.aniso, g_cfg.msaa, g_cfg.linear, g_cfg.hdr, g_cfg.tonemap, g_cfg.bloom, g_cfg.grade, g_cfg.hemi, g_cfg.lights, g_cfg.cascades, g_cfg.ao, g_cfg.contact, g_cfg.volumetric, g_cfg.shadowTiles, g_cfg.probes, g_cfg.sky, g_cfg.screens, g_cfg.mirrors, g_cfg.spec, g_cfg.reflect, g_cfg.normals, g_cfg.detail, g_cfg.parallax, g_cfg.skin, g_cfg.hair, g_cfg.eyes, g_cfg.sheen, g_cfg.fur); fflush(stdout); printf("[gfx] effects and the world: particles %d, soft %d, decals %d, detail %d, refraction %d, foliage %d, beam lights %d; the scene is %s mid-frame%c", g_cfg.particles, g_cfg.softfx, g_cfg.decals, g_cfg.lod, g_cfg.refract, g_cfg.foliage, g_cfg.beamlights, g_mid ? "read" : "not read", 10); printf("[gfx] the display: depth of field %d, motion blur %d, over time %d, the tube's look %d, HDR out %d, between ticks %d%c", g_cfg.dof, g_cfg.mblur, g_cfg.taa, g_cfg.tube, g_cfg.hdrout, g_cfg.interp, 10); fflush(stdout);
}
void gfxConfig(const GfxConfig *c) { g_cfg = *c; g_taps = c->shadowTaps; g_sunSize = c->sunMap; }

#ifdef REVA_SDL
/* ---- the SDL road: a window and a swapchain ---------------------------------------------- */
static VkSwapchainKHR g_chain; static VkSurfaceKHR g_surf;
void gfxInit(void *sdlWindow, int *W, int *H) {
    SDL_Window *win = (SDL_Window *)sdlWindow; unsigned ne = 0; SDL_Vulkan_GetInstanceExtensions(win, &ne, NULL); const char *ex[16]; SDL_Vulkan_GetInstanceExtensions(win, &ne, ex);
    { uint32_t n = 0; vkEnumerateInstanceExtensionProperties(NULL, &n, NULL); VkExtensionProperties *xp = malloc(sizeof *xp * (n ? n : 1)); vkEnumerateInstanceExtensionProperties(NULL, &n, xp);   /* (step 25) a display's colour spaces other than sRGB are named by an extension */
      for (uint32_t i = 0; i < n; i++) if (!strcmp(xp[i].extensionName, "VK_EXT_swapchain_colorspace") && ne < 15) { ex[ne++] = "VK_EXT_swapchain_colorspace"; g_csExt = 1; break; }
      free(xp); }
    const char *de[1] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME}; deviceMake(ex, ne, de, 1);
    if (!SDL_Vulkan_CreateSurface(win, g_inst, &g_surf)) { printf("[gfx] surface: %s\n", SDL_GetError()); exit(1); }
    VkBool32 ok = 0; vkGetPhysicalDeviceSurfaceSupportKHR(g_gpu, g_qf, g_surf, &ok); if (!ok) { printf("[gfx] this queue can't present\n"); exit(1); }
    VkSurfaceCapabilitiesKHR sc; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_gpu, g_surf, &sc);
    if (g_cfg.hdrout) { g_hdrOut = 0;   /* (step 25) HDR on the glass: only if this display, in the mode it's in, says it takes HDR10 */
        if (g_cfg.hdr && g_cfg.tonemap && !g_cfg.tube && g_csExt) { uint32_t nf = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(g_gpu, g_surf, &nf, NULL); VkSurfaceFormatKHR *sf = malloc(sizeof *sf * (nf ? nf : 1)); vkGetPhysicalDeviceSurfaceFormatsKHR(g_gpu, g_surf, &nf, sf);
            for (uint32_t i = 0; i < nf; i++) if (sf[i].format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 && sf[i].colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) { g_glassFmt = sf[i].format; g_glassCs = sf[i].colorSpace; g_hdrOut = 1; }
            free(sf); }
        if (g_hdrOut) printf("[gfx] HDR out: the display takes HDR10. The picture goes on at 200 nits for white, and what was past white is added up to %d%c", g_cfg.hdrout > 1 ? g_cfg.hdrout : 1000, 10);
        else printf("[gfx] HDR out: this display, in the mode it is in, does not offer HDR10 (or a setting it needs is off: hdr, tonemap, no tube). The picture is as it was%c", 10);
        fflush(stdout);
        if (!g_hdrOut) g_cfg.hdrout = 0; }
    VkSwapchainCreateInfoKHR sw = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR}; sw.surface = g_surf; sw.minImageCount = sc.minImageCount < 3 ? 3 : sc.minImageCount; if (sc.maxImageCount && sw.minImageCount > sc.maxImageCount) sw.minImageCount = sc.maxImageCount; sw.imageFormat = g_glassFmt; sw.imageColorSpace = g_glassCs; sw.imageExtent = sc.currentExtent; sw.imageArrayLayers = 1; sw.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; sw.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; sw.preTransform = sc.currentTransform; sw.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; sw.presentMode = VK_PRESENT_MODE_FIFO_KHR; sw.clipped = 1;
    CK(vkCreateSwapchainKHR(g_dev, &sw, NULL, &g_chain)); g_nImg = MAX_IMAGES; vkGetSwapchainImagesKHR(g_dev, g_chain, &g_nImg, g_img);
    g_W = (int)sc.currentExtent.width; g_H = (int)sc.currentExtent.height; pictureSize(); *W = g_W; *H = g_H;
    VkSemaphoreCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < g_nImg; i++) { VkImageViewCreateInfo v = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image = g_img[i]; v.viewType = VK_IMAGE_VIEW_TYPE_2D; v.format = g_glassFmt; v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; v.subresourceRange.levelCount = 1; v.subresourceRange.layerCount = 1; CK(vkCreateImageView(g_dev, &v, NULL, &g_view[i])); CK(vkCreateSemaphore(g_dev, &si, NULL, &g_rendered[i])); }
    commonMake();    pictureMake();
}
void gfxInitKms(int a, uint32_t b, uint32_t c, void *d, int e, int f, void (*g)(unsigned, unsigned)) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; printf("[gfx] the direct road is not in this build\n"); exit(1); }
#else
/* ---- the direct road: scanout buffers the display card owns, imported as linear images, flipped by us ---- */
static int g_kfd; static uint32_t g_kCrtc, g_kConn, g_kFb[MAX_IMAGES]; static drmModeModeInfo g_kMode; static int g_kFirst = 1, g_kPending = 0; static void (*g_kOnFlip)(unsigned, unsigned);
static drmEventContext g_kEv;
static void kFlipDone(int f, unsigned a, unsigned sec, unsigned usec, void *d) { (void)f; (void)a; (void)d; if (g_kOnFlip) g_kOnFlip(sec, usec); g_kPending = 0; }
/* REVA_FLIPSPIN=1 waits for the flip awake, which keeps the CPU governor at full clock with no setting changed: worst frame work 3.6 ms against 6.4 in the light game, for one core and about 2 C.
 * Off by default: the late frames that first asked for it were the tick clock's (clockOwed), and with that fixed the two are the same, 0 of 7,486 at 240p with the clock moving 532 times. */
static void kWaitFlip(void) {
    static int spin = -1; if (spin < 0) { const char *e = getenv("REVA_FLIPSPIN"); spin = e && e[0] == '1'; }
    int idle = 0;
    while (g_kPending) { fd_set s; FD_ZERO(&s); FD_SET(g_kfd, &s); struct timeval tv = {spin ? 0 : 1, 0}; int r = select(g_kfd + 1, &s, NULL, NULL, &tv);
        if (r > 0) drmHandleEvent(g_kfd, &g_kEv); else if (!spin || r < 0 || ++idle > 2000000) break; }
}
static void kShow(uint32_t i) { if (g_kFirst) { if (drmModeSetCrtc(g_kfd, g_kCrtc, g_kFb[i], 0, 0, &g_kConn, 1, &g_kMode)) { perror("[gfx] setcrtc"); exit(1); } g_kFirst = 0; return; } kWaitFlip(); g_kPending = 1; if (drmModePageFlip(g_kfd, g_kCrtc, g_kFb[i], DRM_MODE_PAGE_FLIP_EVENT, NULL)) { g_kPending = 0; static long refused = 0; if (refused++ < 50) { printf("[gfx] flip %ld refused: %s\n", refused, strerror(errno)); fflush(stdout); } } }
void gfxInit(void *a, int *b, int *c) { (void)a; (void)b; (void)c; printf("[gfx] this build is the direct road: gfxInitKms\n"); exit(1); }
static int g_prevImg, g_prevSlot; static int g_kNext __attribute__((unused));
/* the scanout buffers: dumb buffers the display card owns, imported as linear images */
static uint32_t g_kDumb[MAX_IMAGES]; static VkDeviceMemory g_kMem[MAX_IMAGES];
static void kImagesMake(int W, int H) {
    PFN_vkGetMemoryFdPropertiesKHR gfp = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(g_dev, "vkGetMemoryFdPropertiesKHR"); g_nImg = 4;
    for (uint32_t i = 0; i < g_nImg; i++) { struct drm_mode_create_dumb cd; memset(&cd, 0, sizeof cd); cd.width = (uint32_t)W; cd.height = (uint32_t)H; cd.bpp = 32; if (drmIoctl(g_kfd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) { perror("[gfx] dumb"); exit(1); } if (drmModeAddFB(g_kfd, (uint32_t)W, (uint32_t)H, 24, 32, cd.pitch, cd.handle, &g_kFb[i])) { perror("[gfx] addfb"); exit(1); } g_kDumb[i] = cd.handle; int pfd = -1; if (drmPrimeHandleToFD(g_kfd, cd.handle, O_RDWR | O_CLOEXEC, &pfd)) { perror("[gfx] prime"); exit(1); }
        VkSubresourceLayout pl = {0}; pl.size = cd.size; pl.rowPitch = cd.pitch; VkImageDrmFormatModifierExplicitCreateInfoEXT md = {.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT}; md.drmFormatModifier = DRM_FORMAT_MOD_LINEAR; md.drmFormatModifierPlaneCount = 1; md.pPlaneLayouts = &pl; VkExternalMemoryImageCreateInfo em = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO}; em.pNext = &md; em.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.pNext = &em; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = COLOR_FMT; ii.extent.width = (uint32_t)W; ii.extent.height = (uint32_t)H; ii.extent.depth = 1; ii.mipLevels = 1; ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT; ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; CK(vkCreateImage(g_dev, &ii, NULL, &g_img[i]));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_dev, g_img[i], &mr); VkMemoryFdPropertiesKHR fp = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR}; CK(gfp(g_dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, pfd, &fp)); VkMemoryDedicatedAllocateInfo dd = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO}; dd.image = g_img[i]; VkImportMemoryFdInfoKHR im = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR}; im.pNext = &dd; im.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT; im.fd = pfd; VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ma.pNext = &im; ma.allocationSize = mr.size; ma.memoryTypeIndex = memType(mr.memoryTypeBits & fp.memoryTypeBits, 0); CK(vkAllocateMemory(g_dev, &ma, NULL, &g_kMem[i])); CK(vkBindImageMemory(g_dev, g_img[i], g_kMem[i], 0));
        VkImageViewCreateInfo v = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image = g_img[i]; v.viewType = VK_IMAGE_VIEW_TYPE_2D; v.format = COLOR_FMT; v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; v.subresourceRange.levelCount = 1; v.subresourceRange.layerCount = 1; CK(vkCreateImageView(g_dev, &v, NULL, &g_view[i])); }
}
void gfxInitKms(int drmFd, uint32_t crtc, uint32_t connector, void *drmMode, int W, int H, void (*onFlip)(unsigned, unsigned)) {
    g_kfd = drmFd; g_kCrtc = crtc; g_kConn = connector; g_kMode = *(drmModeModeInfo *)drmMode; g_kOnFlip = onFlip; g_W = W; g_H = H; memset(&g_kEv, 0, sizeof g_kEv); g_kEv.version = 2; g_kEv.page_flip_handler = kFlipDone;
    const char *de[3] = {"VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf", "VK_EXT_image_drm_format_modifier"}; deviceMake(NULL, 0, de, 3);
    kImagesMake(W, H);
    commonMake();
}
/* a new mode: everything sized to the raster goes, and the next frame shows on the new one. The pipelines don't care. ⛔ The caller's whitelist is what makes this safe for a set */
void gfxKmsRemode(void *drmMode, int W, int H) {
    vkDeviceWaitIdle(g_dev); kWaitFlip();
    for (uint32_t i = 0; i < g_nImg; i++) { vkDestroyImageView(g_dev, g_view[i], NULL); vkDestroyImage(g_dev, g_img[i], NULL); vkFreeMemory(g_dev, g_kMem[i], NULL); drmModeRmFB(g_kfd, g_kFb[i]); struct drm_mode_destroy_dumb dd; memset(&dd, 0, sizeof dd); dd.handle = g_kDumb[i]; drmIoctl(g_kfd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd); }
    targetsFree();
    g_kMode = *(drmModeModeInfo *)drmMode; g_W = W; g_H = H;
    kImagesMake(W, H);
    targetsMake();
    if (g_cap.map) { vkDestroyBuffer(g_dev, g_cap.b, NULL); vkFreeMemory(g_dev, g_cap.m, NULL); capMake(W, H); }
    g_kFirst = 1; g_prevImg = -1; g_prevSlot = -1; g_kNext = 0;
    printf("[gfx] now %dx%d\n", W, H); fflush(stdout);
}
#endif
/* ---- a frame ----------------------------------------------------------------------------- */
static int g_prevImg = -1, g_prevSlot = -1; __attribute__((unused)) static int g_kNext = 0;
static double g_waited = 0.0;
static double nowMs(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
double gfxWaited(void) { double w = g_waited; g_waited = 0.0; return w; }
/* (step 25) a frame between two ticks. The engine asked for nothing, so what it asks for each frame outside the packet has to stand from the last one: the screens in the
 * world, the floor's shadow, and every dynamic buffer, whose place is in a frame's ring and good for that frame alone. They were written into the other frame's ring a
 * moment ago, which is still whole, and are carried over into this one */
void gfxBackBetween(void) {
    Frame *f = &g_fr[g_slot]; g_between = 1; g_nScr = g_nScrWas; g_fsOn = g_fsWas;
    for (unsigned i = 1; i < g_nBuf; i++) { GBuf *g = &g_buf[i]; if (!g->dyn || !g->cap || !g->p || g->at != g_ringStamp - 1) continue;
        uint32_t o; char *np = ringAlloc(f, g->cap, 16, &o); memcpy(np, g->p, (size_t)g->cap); g->p = np; g->b = f->ring.b; g->off = o; g->at = g_ringStamp; }
}
void gfxBackBegin(void) {
    Frame *f = &g_fr[g_slot]; double t0 = nowMs(); CK(vkWaitForFences(g_dev, 1, &f->fence, VK_TRUE, UINT64_MAX)); g_waited += nowMs() - t0; f->ring.used = 0; g_ringStamp++; f->nDraw = 0; g_frameLive = 1;
}
/* one of the packet's draws, in this machine's handles */
static void drawOf(Frame *f, const GfxDraw *m, uint32_t base) {
    if (f->nDraw >= MAX_DRAWS) return;
    int skin = m->bones > 0;
    GBuf *p = bufOf(m->vb[0]); if (!p->b) return;
    Draw *d = &f->draws[f->nDraw];
    int shadow = (m->flags & GFX_SHADOW) != 0;
    /* a stream it hasn't got reads zeros. Positions are required, and the indices if it has any. */
    const unsigned order[5] = {m->vb[0], m->vb[1], m->vb[2], m->vb[3], m->vb[5]};
    for (int i = 0; i < 5; i++) { GBuf *g = order[i] ? bufOf(order[i]) : NULL; if (g && g->b) { d->vb[i] = g->b; d->off[i] = g->off; } else { d->vb[i] = g_zero.b; d->off[i] = 0; } }
    { GBuf *r9 = m->rest ? bufOf(m->rest) : NULL; if (r9 && r9->b) { d->restVb = r9->b; d->restOff = r9->off; } else { d->restVb = d->vb[0]; d->restOff = d->off[0]; } d->restScale = m->restScale; }   /* taken before a moved face's positions take the stream's place: at rest is where the pack has them */
    if (m->posOn) { d->vb[0] = f->ring.b; d->off[0] = (VkDeviceSize)base + m->posOff; }   /* a face that's moved: its positions came in the packet */
    memcpy(d->mouth, m->mouth, 32);
    if (m->nVerts * 12u > g_zero.size) { static int said = 0; if (!said) { said = 1; printf("[gfx] a mesh of %u vertices is past the zero stream\n", m->nVerts); } }
    d->ib = VK_NULL_HANDLE; d->nIndex = m->nIndex; d->nVerts = m->nVerts;
    if (m->nIndex) { GBuf *g = bufOf(m->vb[4]); if (!g->b) return; d->ib = g->b; d->ibOff = g->off; }
    d->extra[2] = 0.f;
    if (!shadow) { const GTex *t = (m->tex && m->tex < g_nTex && g_tex[m->tex].dsA) ? &g_tex[m->tex] : NULL; d->tex = t ? t->dsA : g_whiteDs; d->extra[2] = t ? (float)t->layer : 0.f; d->texKey = !t ? 0 : t->page ? t->page : (int)m->tex + MAX_PAGES; }
    d->tint[0] = m->tint[0]; d->tint[1] = m->tint[1]; d->tint[2] = m->tint[2]; d->tint[3] = m->useTex;
    d->extra[0] = m->alpha; d->extra[1] = (m->flags & GFX_GLOW) ? 1.f : 0.f;
    { const int said = m->mat[0] > 0.f || m->mat[1] > 0.f || m->mat[2] > 0.f || m->mat[3] > 0.f;   /* nothing said: an ordinary rough surface that isn't a metal */
      d->mat[0] = said ? m->mat[0] : 0.85f; d->mat[1] = m->mat[1]; d->mat[2] = m->mat[2]; d->mat[3] = m->mat[3]; d->mat[5] = m->kind; d->mat[6] = d->mat[7] = 0.f; memcpy(d->mat + 8, m->eye, 16);
      if (m->kind > 5.5f && m->kind < 6.5f) { d->mat[7] = d->mat[1]; d->mat[1] = 0.f; }   /* fur: its second number says how curly, not how much a metal. It rides where the fur's shader reads it, and the skin under it is no metal */
      const GTex *nt = (!shadow && g_cfg.normals + g_cfg.parallax > 0 && m->ntex && m->ntex < g_nTex && g_tex[m->ntex].dsA) ? &g_tex[m->ntex] : NULL; d->ntex = nt ? nt->dsA : g_whiteDs; d->mat[4] = nt ? (float)nt->layer : -1.f; }
    d->skin = skin; d->kind = (int)(m->id >> 28); d->flags = m->flags; if (!shadow && m->tex && m->tex < g_nTex && g_tex[m->tex].holes && m->useTex >= 0.5f) d->flags |= GFX_CUTOUT;
    memcpy(d->model, m->model, 64); d->bonesOff = base + m->palOff;
    f->nDraw++;
}
/* a GL clip matrix's depth, in Vulkan's 0..1: z = (z + w) / 2. Y is the negative-height viewport's job, so picture, sun and HUD all keep GL's y-up. */
static void clipFix(float *m) { for (int c = 0; c < 4; c++) m[c * 4 + 2] = 0.5f * (m[c * 4 + 2] + m[c * 4 + 3]); }
typedef struct { float vp[16], sunVP[16], eye[4], sunDir[4], sunCol[4], sky[4], fog[4], misc[4], misc2[4], ground[4], fwd[4], split[4], cl[4], cl2[4], cl3[4], casc[4][16], inv[16], probe[4], probeS[4], skyZ[4], skyH[4], skyG[4], skyS[4], shv[16]; } ViewUBO;
/* ---- the PC's sun: cascades, each its own tile of the one map --------------------------------- */
static void mul44(float *r, const float *a, const float *b) { for (int c = 0; c < 4; c++) for (int i = 0; i < 4; i++) r[c * 4 + i] = a[0 * 4 + i] * b[c * 4 + 0] + a[1 * 4 + i] * b[c * 4 + 1] + a[2 * 4 + i] * b[c * 4 + 2] + a[3 * 4 + i] * b[c * 4 + 3]; }
static int inv44(const float *m, float *o) {
    float t[16]; t[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    t[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    t[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    t[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    t[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    t[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    t[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    t[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    t[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    t[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    t[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    t[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    t[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    t[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    t[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    t[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float d = m[0]*t[0] + m[1]*t[4] + m[2]*t[8] + m[3]*t[12]; if (d == 0.f) return 0; d = 1.f / d; for (int i = 0; i < 16; i++) o[i] = t[i] * d; return 1;
}
/* one cascade: a sun lens on a sphere round the slice of the view between two depths, moved in whole texels so its edge doesn't crawl as the camera moves */
static void cascadeOf(const float *vpGL, const float *eye, const float *fwd, const float *sun, float d0, float d1, int tile, float *out) {
    float inv[16]; if (!inv44(vpGL, inv)) return;
    float c[3] = {0, 0, 0}, pts[8][3]; int n = 0;
    for (int xi = -1; xi <= 1; xi += 2) for (int yi = -1; yi <= 1; yi += 2) {
        float q[4]; for (int r = 0; r < 4; r++) q[r] = inv[0 * 4 + r] * (float)xi + inv[1 * 4 + r] * (float)yi + inv[2 * 4 + r] * (-1.f) + inv[3 * 4 + r];
        float dir[3] = {q[0] / q[3] - eye[0], q[1] / q[3] - eye[1], q[2] / q[3] - eye[2]}; float along = dir[0] * fwd[0] + dir[1] * fwd[1] + dir[2] * fwd[2]; if (along < 1e-4f) along = 1e-4f;
        for (int e = 0; e < 2; e++) { float dist = e ? d1 : d0; for (int k = 0; k < 3; k++) { pts[n][k] = eye[k] + dir[k] / along * dist; c[k] += pts[n][k] / 8.f; } n++; } }
    float rad = 0.f; for (int i = 0; i < 8; i++) { float dx = pts[i][0] - c[0], dy = pts[i][1] - c[1], dz = pts[i][2] - c[2], dd = dx * dx + dy * dy + dz * dz; if (dd > rad) rad = dd; } rad = sqrtf(rad) * 1.02f;
    float up[3] = {0.f, 1.f, 0.f}; if (fabsf(sun[1]) > 0.97f) { up[0] = 1.f; up[1] = 0.f; }
    float zv[3] = {sun[0], sun[1], sun[2]}; { float l = sqrtf(zv[0] * zv[0] + zv[1] * zv[1] + zv[2] * zv[2]); if (l > 0.f) { zv[0] /= l; zv[1] /= l; zv[2] /= l; } }
    float xv[3] = {up[1] * zv[2] - up[2] * zv[1], up[2] * zv[0] - up[0] * zv[2], up[0] * zv[1] - up[1] * zv[0]}; { float l = sqrtf(xv[0] * xv[0] + xv[1] * xv[1] + xv[2] * xv[2]); xv[0] /= l; xv[1] /= l; xv[2] /= l; }
    float yv[3] = {zv[1] * xv[2] - zv[2] * xv[1], zv[2] * xv[0] - zv[0] * xv[2], zv[0] * xv[1] - zv[1] * xv[0]};
    float tiles = g_cfg.cascades > 1 ? 2.f : 1.f, texel = rad * 2.f / ((float)g_sunSize / tiles);
    float cx = xv[0] * c[0] + xv[1] * c[1] + xv[2] * c[2], cy = yv[0] * c[0] + yv[1] * c[1] + yv[2] * c[2], cz = zv[0] * c[0] + zv[1] * c[1] + zv[2] * c[2];
    cx = floorf(cx / texel) * texel; cy = floorf(cy / texel) * texel;
    /* GL's clip, like every other matrix here: x and y from -1 to 1 across the sphere, and depth from -1 toward the sun, with room there for what casts from outside it */
    float F = rad + 220.f, B = rad + 40.f, k = -2.f / (F + B);
    float V4[16] = {xv[0] / rad, yv[0] / rad, k * zv[0], 0.f, xv[1] / rad, yv[1] / rad, k * zv[1], 0.f, xv[2] / rad, yv[2] / rad, k * zv[2], 0.f, -cx / rad, -cy / rad, -k * cz + k * B + 1.f, 1.f};
    (void)tile; memcpy(out, V4, 64);
}
/* ---- lights, clustered: the view cut into cells, and each cell's short list ------------------- */
#define CL_X 24
#define CL_Y 14
#define CL_Z 16
#define CL_MAX 48
static unsigned short g_clList[CL_X * CL_Y * CL_Z][CL_MAX]; static unsigned char g_clN[CL_X * CL_Y * CL_Z];
static void clustersFill(Frame *f, ViewUBO *w, const float *vpGL, const float *eye, const float *fwd, const GfxView *v, int rx, int ry, int rw, int rh, uint32_t lightsAt) {
    const float nearZ = 1.5f, farZ = 500.f, scale = (float)CL_Z / logf(farZ / nearZ);
    memset(g_clN, 0, sizeof g_clN);
    int n = v->nLights < g_cfg.lights ? v->nLights : g_cfg.lights;
    for (int i = 0; i < n; i++) { const GfxLight *L = &v->lights[i];
        float m = L->col[0] > L->col[1] ? L->col[0] : L->col[1]; if (L->col[2] > m) m = L->col[2]; float fo = L->falloff > 0.001f ? L->falloff : 0.001f, rad = sqrtf(fmaxf(0.f, (m / 0.015f - 1.f) / fo)); if (rad < 0.5f) continue;
        float dz = (L->pos[0] - eye[0]) * fwd[0] + (L->pos[1] - eye[1]) * fwd[1] + (L->pos[2] - eye[2]) * fwd[2]; if (dz + rad < 0.f) continue;
        int z0 = (int)(logf(fmaxf(dz - rad, nearZ) / nearZ) * scale), z1 = (int)(logf(fmaxf(dz + rad, nearZ) / nearZ) * scale); if (z0 < 0) z0 = 0; if (z1 >= CL_Z) z1 = CL_Z - 1;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; int all = 0;
        for (int k = 0; k < 8; k++) { float p[3] = {L->pos[0] + ((k & 1) ? rad : -rad), L->pos[1] + ((k & 2) ? rad : -rad), L->pos[2] + ((k & 4) ? rad : -rad)};
            float cw = vpGL[3] * p[0] + vpGL[7] * p[1] + vpGL[11] * p[2] + vpGL[15]; if (cw < 0.1f) { all = 1; break; }
            float nx = (vpGL[0] * p[0] + vpGL[4] * p[1] + vpGL[8] * p[2] + vpGL[12]) / cw, ny = (vpGL[1] * p[0] + vpGL[5] * p[1] + vpGL[9] * p[2] + vpGL[13]) / cw;
            if (nx < x0) x0 = nx;
            if (nx > x1) x1 = nx;
            if (ny < y0) y0 = ny;
            if (ny > y1) y1 = ny; }
        int cx0 = 0, cx1 = CL_X - 1, cy0 = 0, cy1 = CL_Y - 1;
        if (!all) { if (x1 < -1.f || x0 > 1.f || y1 < -1.f || y0 > 1.f) continue; cx0 = (int)((x0 * 0.5f + 0.5f) * CL_X); cx1 = (int)((x1 * 0.5f + 0.5f) * CL_X); cy0 = (int)((0.5f - y1 * 0.5f) * CL_Y); cy1 = (int)((0.5f - y0 * 0.5f) * CL_Y); if (cx0 < 0) cx0 = 0; if (cx1 >= CL_X) cx1 = CL_X - 1; if (cy0 < 0) cy0 = 0; if (cy1 >= CL_Y) cy1 = CL_Y - 1; }
        for (int z = z0; z <= z1; z++) for (int y = cy0; y <= cy1; y++) for (int x = cx0; x <= cx1; x++) { int c = x + CL_X * (y + CL_Y * z); if (g_clN[c] < CL_MAX) g_clList[c][g_clN[c]++] = (unsigned short)i; } }
    size_t total = 0; for (int c = 0; c < CL_X * CL_Y * CL_Z; c++) total += g_clN[c];
    uint32_t off; uint32_t *u = ringAlloc(f, (VkDeviceSize)(CL_X * CL_Y * CL_Z + total + 4) * 4, 16, &off); uint32_t first = off / 4, at = first + CL_X * CL_Y * CL_Z;
    for (int c = 0; c < CL_X * CL_Y * CL_Z; c++) { u[c] = (at << 8) | g_clN[c]; for (int k = 0; k < g_clN[c]; k++) ((uint32_t *)f->ring.map)[at++] = lightsAt / 16 + (uint32_t)g_clList[c][k] * 4u; }
    w->cl[0] = (float)first; w->cl[1] = (float)CL_X; w->cl[2] = (float)CL_Y; w->cl[3] = (float)CL_Z; w->cl2[0] = scale; w->cl2[1] = nearZ; w->cl2[2] = (float)rx; w->cl2[3] = (float)ry; w->cl3[0] = (float)rw; w->cl3[1] = (float)rh;
}
/* ---- ONE CALL FOR EVERY COPY OF A MESH (engine plan, Phase 2) --------------------------------------
 * The packet lists draws and doesn't know or care what could share a call, so the game and its receipts can't tell. Here, draws of one mesh with the same pipeline and page become one instanced call, and what's
 * different about each goes in a stream of its own. Opaque draws and the sun's: their order only matters where two surfaces tie for depth. What's blended keeps its order and its own calls. */
#define BAT_HASH (1u << 15)
static int g_calls, g_binds, g_stretched;
/* what turns a mesh's normals. Scaled the same every way that's its own matrix, so a plain prop's light doesn't change by a bit. Stretched, normals don't follow the matrix: they stay square to the surface, which is the matrix's cofactors */
static void normalRows(const float *m, float *n) {
    float a = m[0] * m[0] + m[1] * m[1] + m[2] * m[2], b = m[4] * m[4] + m[5] * m[5] + m[6] * m[6], c = m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
    { static int off9 = -1; if (off9 < 0) { const char *e9 = getenv("REVA_NOSTRETCHN"); off9 = (e9 && e9[0] != '0') ? 1 : 0; if (off9) { printf("[gfx] REVA_NOSTRETCHN: a stretched prop's normals are only stretched, the control\n"); fflush(stdout); } } if (off9) { n[0] = m[0]; n[1] = m[4]; n[2] = m[8]; n[3] = m[1]; n[4] = m[5]; n[5] = m[9]; n[6] = m[2]; n[7] = m[6]; n[8] = m[10]; return; } }
    float hi = a > b ? a : b, lo = a < b ? a : b; if (c > hi) hi = c; if (c < lo) lo = c;
    if (hi - lo <= hi * 0.002f || hi <= 0.f) { n[0] = m[0]; n[1] = m[4]; n[2] = m[8]; n[3] = m[1]; n[4] = m[5]; n[5] = m[9]; n[6] = m[2]; n[7] = m[6]; n[8] = m[10]; return; }
    float x0 = m[5] * m[10] - m[6] * m[9], y0 = m[6] * m[8] - m[4] * m[10], z0 = m[4] * m[9] - m[5] * m[8];   /* column 1 x column 2 */
    float x1 = m[9] * m[2] - m[10] * m[1], y1 = m[10] * m[0] - m[8] * m[2], z1 = m[8] * m[1] - m[9] * m[0];   /* column 2 x column 0 */
    float x2 = m[1] * m[6] - m[2] * m[5], y2 = m[2] * m[4] - m[0] * m[6], z2 = m[0] * m[5] - m[1] * m[4];     /* column 0 x column 1 */
    float k = (m[0] * x0 + m[1] * y0 + m[2] * z0 < 0.f ? -1.f : 1.f) / hi;   /* a mirrored mesh keeps its normals' sense; the size only has to stay near 1, the fragment normalizes */
    n[0] = k * x0; n[1] = k * x1; n[2] = k * x2; n[3] = k * y0; n[4] = k * y1; n[5] = k * y2; n[6] = k * z0; n[7] = k * z1; n[8] = k * z2; g_stretched++;
}
static int sameMesh(const Draw *a, const Draw *b, int L) {
    if (a->skin != b->skin || a->ib != b->ib || a->ibOff != b->ibOff || a->nIndex != b->nIndex || a->nVerts != b->nVerts) return 0;
    if (a->mouth[6] > 0.f || b->mouth[6] > 0.f) return 0;   /* an open mouth is its own call: its numbers are pushed */
    if ((a->flags ^ b->flags) & (GFX_TOON | GFX_TOONROOM | GFX_NOINK | GFX_MIRRORED | GFX_MIRCULL | GFX_REFL)) return 0;
    if (L == L_OPAQUE && (a->tex != b->tex || (a->flags & GFX_CUTOUT) != (b->flags & GFX_CUTOUT))) return 0;
    if (memcmp(a->mat + 8, b->mat + 8, 16)) return 0;   /* two bodies whose eyes glow differently are two calls: the glow is pushed with the call */
    for (int i = 0; i < 5; i++) if (a->vb[i] != b->vb[i] || a->off[i] != b->off[i]) return 0;
    return 1;
}
static int batchCmp(const void *a, const void *b) { const Batch *x = a, *y = b; if (x->list != y->list) return x->list - y->list; if (x->key != y->key) return x->key - y->key; return x->order - y->order; }
static void batchesMake(Frame *f) {
    static struct { unsigned stamp; int bat; } H[BAT_HASH]; static unsigned stamp; stamp++;
    Batch *B = f->bat; int nB = 0;
    for (int i = 0; i < f->nDraw; i++) { Draw *d = &f->draws[i]; d->next = -1;
        int L = (d->flags & GFX_SHADOW) ? L_SUN : ((d->flags & GFX_HOLO) && d->skin) ? L_HOLO : (d->flags & GFX_BLEND) ? L_BLEND : ((d->flags & GFX_GLOSS) && g_cfg.mirrors) ? L_GLOSS : L_OPAQUE, into = -1;
        if (g_cfg.merge && L <= L_OPAQUE) { uint64_t h = ((uint64_t)(uintptr_t)d->vb[0] >> 4) * 0x9E3779B1u + d->off[0] * 31u + (uint64_t)d->nIndex * 131u + (uint64_t)d->nVerts + (uint64_t)L * 17u;
            for (uint32_t k = (uint32_t)(h ^ (h >> 17)) & (BAT_HASH - 1);; k = (k + 1) & (BAT_HASH - 1)) {
                if (H[k].stamp != stamp) { H[k].stamp = stamp; H[k].bat = nB; break; }
                const Batch *b = &B[H[k].bat]; if (b->list == L && sameMesh(&f->draws[b->d0], d, L)) { into = H[k].bat; break; } } }
        if (into >= 0) { Batch *b = &B[into]; f->draws[b->dLast].next = i; b->dLast = i; b->n++; continue; }
        Batch *b = &B[nB]; b->d0 = b->dLast = i; b->n = 1; b->list = L; b->order = nB; nB++;
        b->key = !g_cfg.merge ? 0 : L == L_OPAQUE ? (((d->skin ? 2 : 0) | ((d->flags & GFX_CUTOUT) ? 1 : 0) | ((d->flags & GFX_TOON) ? 4 : 0)) << 24) | d->texKey : L == L_SUN ? d->skin : 0; }
    qsort(B, (size_t)nB, sizeof *B, batchCmp);
    uint32_t at; GInst *o = ringAlloc(f, (VkDeviceSize)f->nDraw * sizeof(GInst), 16, &at); f->instAt = at; uint32_t k = 0; int L = 0; f->start[0] = 0;
    for (int b = 0; b < nB; b++) { Batch *ba = &B[b]; while (L < ba->list) f->start[++L] = b; ba->first = k;
        for (int i = ba->d0; i >= 0; i = f->draws[i].next) { const Draw *d = &f->draws[i]; const float *m = d->model; GInst *g = &o[k++];
            if (m[3] != 0.f || m[7] != 0.f || m[11] != 0.f || m[15] != 1.f) { static int said; if (!said) { said = 1; printf("[gfx] a mesh's model matrix isn't affine: %g %g %g %g\n", m[3], m[7], m[11], m[15]); fflush(stdout); } }
            g->r[0] = m[0]; g->r[1] = m[4]; g->r[2] = m[8]; g->r[3] = m[12]; g->r[4] = m[1]; g->r[5] = m[5]; g->r[6] = m[9]; g->r[7] = m[13]; g->r[8] = m[2]; g->r[9] = m[6]; g->r[10] = m[10]; g->r[11] = m[14];
            g->tint[0] = d->tint[0]; g->tint[1] = d->tint[1]; g->tint[2] = d->tint[2]; g->tint[3] = d->tint[3]; g->extra[0] = d->extra[0]; g->extra[1] = d->extra[1]; g->extra[2] = d->extra[2]; g->bone = d->bonesOff / 16u;
            if (!d->skin && !(d->flags & GFX_SHADOW)) normalRows(m, g->n); } }
    while (L < L_N) f->start[++L] = nB;
}
/* a call's streams: the mesh's, then its copies'. shadowPass: a mesh with no bones needs only its positions */
static void streamsBind(const Frame *f, VkCommandBuffer cb, const Batch *ba, const Draw *d, int shadowPass) {
    VkBuffer b[6]; VkDeviceSize o[6]; uint32_t n = 0;
    if (d->skin) { for (int i = 0; i < 5; i++) { b[n] = d->vb[i]; o[n++] = d->off[i]; } }
    else if (shadowPass) { b[0] = d->vb[0]; o[0] = d->off[0]; n = 1; }
    else { b[0] = d->vb[0]; b[1] = d->vb[1]; b[2] = d->vb[4]; o[0] = d->off[0]; o[1] = d->off[1]; o[2] = d->off[4]; n = 3; }
    b[n] = f->ring.b; o[n++] = (VkDeviceSize)f->instAt + (VkDeviceSize)ba->first * sizeof(GInst);
    vkCmdBindVertexBuffers(cb, 0, n, b, o);
    if (d->nIndex) { vkCmdBindIndexBuffer(cb, d->ib, d->ibOff, VK_INDEX_TYPE_UINT32); vkCmdDrawIndexed(cb, d->nIndex, (uint32_t)ba->n, 0, 0, 0); }
    else vkCmdDraw(cb, d->nVerts, (uint32_t)ba->n, 0, 0);
    g_calls++;
}
/* a shell of fur: the same streams, and one more, where each vertex was at rest */
static void streamsBindFur(const Frame *f, VkCommandBuffer cb, const Batch *ba, const Draw *d) {
    VkBuffer b[7]; VkDeviceSize o[7]; uint32_t n = 0;
    if (d->skin) { for (int i = 0; i < 5; i++) { b[n] = d->vb[i]; o[n++] = d->off[i]; } }
    else { b[0] = d->vb[0]; b[1] = d->vb[1]; b[2] = d->vb[4]; o[0] = d->off[0]; o[1] = d->off[1]; o[2] = d->off[4]; n = 3; }
    b[n] = d->restVb; o[n++] = d->restOff;
    b[n] = f->ring.b; o[n++] = (VkDeviceSize)f->instAt + (VkDeviceSize)ba->first * sizeof(GInst);
    vkCmdBindVertexBuffers(cb, 0, n, b, o);
    if (d->nIndex) { vkCmdBindIndexBuffer(cb, d->ib, d->ibOff, VK_INDEX_TYPE_UINT32); vkCmdDrawIndexed(cb, d->nIndex, (uint32_t)ba->n, 0, 0, 0); }
    else vkCmdDraw(cb, d->nVerts, (uint32_t)ba->n, 0, 0);
    g_calls++;
}
static struct { float t[4], ink, inkC[3]; } g_toon; static float g_viewAsp = 1.f;
void gfxToonSet(float cel, float rim, float keyFromLens, float roomGradient, float ink, const float *inkRGB) { g_toon.t[0] = cel; g_toon.t[1] = rim; g_toon.t[2] = keyFromLens; g_toon.t[3] = roomGradient; g_toon.ink = ink; memcpy(g_toon.inkC, inkRGB, 12); }
/* blendPass: 0 opaque, 1 blended, 2 a hologram's depth, 3 a hologram's colour, 4 the glossy floor */
static int g_offScreen;   /* the list is going into a picture drawn off-screen */
static float g_glossNum[4];   /* this frame's GfxView.gloss */
static int g_wallOnly;   /* this frame's GfxView.wall */
static void drawList(Frame *f, VkCommandBuffer cb, uint32_t viewOff, int shadowPass, int blendPass) {
    VkPipeline cur = VK_NULL_HANDLE; VkDescriptorSet curTex = VK_NULL_HANDLE, curN = g_whiteDs; int L = shadowPass ? L_SUN : blendPass == 0 ? L_OPAQUE : blendPass == 1 ? L_BLEND : blendPass == 4 ? L_GLOSS : L_HOLO;
    const int matOn = !shadowPass && !g_offScreen && (g_cfg.spec | g_cfg.reflect | g_cfg.normals | g_cfg.detail | g_cfg.parallax | g_cfg.skin | g_cfg.hair | g_cfg.eyes | g_cfg.sheen);   /* a setting that reads a mesh's material is on: each call pushes it */
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &f->dsView, 1, &viewOff);
    if (!shadowPass) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 2, 1, &g_whiteDs, 0, NULL);   /* the shader names a normal map whether or not a setting reads it, so there's always one bound */
    int eyeLive = 0;   /* (step 23) the call before pushed eyes that glow, so the next has to push its own, even if that's none */
    if (!shadowPass && !matOn) { static const float none[12] = {0.85f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}; vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 80, 48, none); }   /* the shader reads a glow whatever the settings, so there's always one pushed: none */
    for (int i = f->start[L]; i < f->start[L + 1]; i++) { const Batch *ba = &f->bat[i]; const Draw *d = &f->draws[ba->d0];
        if (g_wallOnly && !shadowPass && d->kind != GFX_KIND_HERO && d->kind != GFX_KIND_CAST) continue;
        VkPipeline p = g_offScreen ? g_pipe[d->skin ? ((d->flags & GFX_CUTOUT) ? P_T_SKIN_CUT : P_T_SKIN) : ((d->flags & GFX_CUTOUT) ? P_T_STATIC_CUT : P_T_STATIC)] : shadowPass ? g_pipe[d->skin ? P_SH_SKIN : P_SH_STATIC] : blendPass == 2 ? g_pipe[P_HOLO_Z] : blendPass == 3 ? g_pipe[P_HOLO] : blendPass == 4 ? g_pipe[(d->flags & GFX_TOON) ? P_TOON_GLOSS : P_GLOSS] : blendPass ? g_pipe[d->skin ? P_SKIN_BLEND : P_STATIC_BLEND] : (d->flags & GFX_CUTOUT) ? g_pipe[d->skin ? P_SKIN_CUT : P_STATIC_CUT] : g_pipe[d->skin ? P_SKIN : P_STATIC];
        const int mo9 = !g_offScreen && !shadowPass && !blendPass && d->skin && d->mouth[6] > 0.f, cut9 = (d->flags & GFX_CUTOUT) != 0, toon9 = !g_offScreen && !shadowPass && blendPass < 2 && (d->flags & GFX_TOON), mir9 = !g_offScreen && !shadowPass && !blendPass && (d->flags & GFX_MIRRORED), refl9 = mir9 && (d->flags & GFX_REFL), gl9 = !g_offScreen && blendPass == 4;
        if (mo9) p = g_pipe[cut9 ? P_MOUTH_CUT : P_MOUTH];
        if (blendPass == 1 && (d->flags & GFX_WATER)) { p = g_pipe[P_WATER]; if (!p) continue; }
        if (toon9) p = g_pipe[blendPass ? P_TOON_BLEND : mo9 ? (cut9 ? P_TOON_MOUTH_CUT : P_TOON_MOUTH) : d->skin ? (cut9 ? P_TOON_CUT : P_TOON) : (cut9 ? P_TOON_STATIC_CUT : P_TOON_STATIC)];
        if (refl9) p = g_pipe[(d->flags & GFX_TOON) ? (d->skin ? (cut9 ? P_TR_SKIN_CUT : P_TR_SKIN) : (cut9 ? P_TR_STATIC_CUT : P_TR_STATIC)) : (d->skin ? (cut9 ? P_R_SKIN_CUT : P_R_SKIN) : (cut9 ? P_R_STATIC_CUT : P_R_STATIC))];
        else if (mir9 && !d->skin) p = g_pipe[(d->flags & GFX_TOON) ? (cut9 ? P_M_TOON_STATIC_CUT : P_M_TOON_STATIC) : (cut9 ? P_M_STATIC_CUT : P_M_STATIC)];
        if (p != cur) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p); cur = p; }
        if (mir9 && !d->skin) vkCmdSetCullMode(cb, (d->flags & GFX_MIRCULL) ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE);
        if (mo9 || toon9 || refl9 || gl9) { float pc[20]; memcpy(pc, d->mouth, 32); pc[7] = (d->flags & GFX_TOONROOM) ? 1.f : 0.f; memcpy(pc + 8, g_toon.t, 16); memcpy(pc + 12, g_toon.inkC, 12); pc[15] = g_toon.ink; pc[16] = g_viewAsp; pc[17] = refl9 ? g_glossNum[0] : gl9 ? g_glossNum[2] : 0.f; pc[18] = refl9 ? g_glossNum[1] : gl9 ? g_glossNum[3] : 0.f; pc[19] = 0.f; vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, pc); }
        if (!shadowPass && d->tex != curTex) { vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 1, 1, &d->tex, 0, NULL); curTex = d->tex; g_binds++; }
        { const int eye9 = !shadowPass && !g_offScreen && d->mat[11] > 0.f;   /* this call's body has eyes that glow */
          if (matOn || eye9 || eyeLive) { vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 80, 48, d->mat); eyeLive = eye9; }
          if (matOn && d->ntex != curN) { vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 2, 1, &d->ntex, 0, NULL); curN = d->ntex; g_binds++; } }
        streamsBind(f, cb, ba, d, shadowPass);
        if (toon9 && !blendPass && !(d->flags & GFX_NOINK) && g_toon.ink > 0.f) { p = g_pipe[mir9 ? (d->skin ? P_M_INK : P_M_INK_STATIC) : (d->skin ? P_INK : P_INK_STATIC)]; vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p); cur = p; streamsBind(f, cb, ba, d, shadowPass); }   /* the line, the same copies again */
        /* (step 23) fur: the same copies again, once for each shell, each further out along its strands and leaning the way the body's fur is pushed. A cel-shaded body's has its own pipelines; a mirror's copies go without */
        if (g_cfg.fur > 0 && !g_offScreen && !shadowPass && !blendPass && d->mat[5] > 5.5f && d->mat[5] < 6.5f && d->mat[3] > 0.f && !(d->flags & (GFX_MIRRORED | GFX_BLEND))) {
            float pc[12]; memcpy(pc, d->mat, 48); pc[11] = d->restScale > 0.f ? d->restScale : 0.f;
            p = g_pipe[(d->flags & GFX_TOON) ? (d->skin ? P_TOON_FUR : P_TOON_FUR_STATIC) : (d->skin ? P_FUR : P_FUR_STATIC)]; vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p); cur = p;
            for (int s9 = 1; s9 <= g_cfg.fur; s9++) { pc[6] = (float)s9 / (float)g_cfg.fur; vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 80, 48, pc); streamsBindFur(f, cb, ba, d); }
            eyeLive = 1; }   /* what's pushed now is this mesh's fur, so the next call pushes its own */
        if (gl9 && (d->flags & GFX_TOON) && g_cfg.hdr && !g_filmPost) { p = g_pipe[P_TOON_GLOSS_A]; vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p); cur = p; streamsBind(f, cb, ba, d, shadowPass); } }   /* a toon floor stands off the post pass as the rest of the toon does: its alpha's the blend's, so that's written after */
}
/* REVA_SHOT: each frame's picture is also copied out, because pixels are the only proof. Off, this costs a branch. */
int gfxRead(void *rgba, int w, int h) {
    if (!g_cap.map || w != ((g_present && !g_hdrOut) ? g_GW : g_W) || h != ((g_present && !g_hdrOut) ? g_GH : g_H)) return 0;
    vkDeviceWaitIdle(g_dev);
    /* The capture buffer is host-coherent memory, which the CPU reads uncached: swizzled a byte at a time straight out of it, a
     * 1280x720 frame took half a second, and a take fell from 57 frames a second to 2 (11-04). One memcpy brings it over in
     * wide loads, and the swizzle runs on the copy. */
    size_t n = (size_t)w * (size_t)h * 4; unsigned char *c = (unsigned char *)malloc(n); if (!c) return 0;
    memcpy(c, g_cap.map, n); unsigned char *o = rgba;
    for (int y = 0; y < h; y++) { const unsigned char *r = c + (size_t)(h - 1 - y) * w * 4; for (int x = 0; x < w; x++) { o[0] = r[2]; o[1] = r[1]; o[2] = r[0]; o[3] = r[3]; o += 4; r += 4; } }
    free(c); return 1;
}
/* a star: the surface and the cut faces solid, the corona and the loops added. Its numbers go in the push block, and its spots in the ring */
static void starPass(Frame *f, VkCommandBuffer cb, const GfxStar *s) {
    if (!s->on || !g_pipe[P_STAR_SURF]) return;
    GBuf *sv = bufOf(s->sphVB), *si = bufOf(s->sphIB), *qv = bufOf(s->quadVB); if (!sv->b || !si->b || !qv->b) return;
    uint32_t at; float *o = ringAlloc(f, sizeof s->spot, 16, &at); memcpy(o, s->spot, sizeof s->spot);
    float pc[24]; memcpy(pc, s->c, 16); memcpy(pc + 4, s->s, 16); memcpy(pc + 8, s->t, 16); memcpy(pc + 12, s->cut, 12); pc[15] = (float)(at / 16u); memcpy(pc + 16, s->rt, 12); pc[19] = 0.f; memcpy(pc + 20, s->up, 12); pc[23] = 0.f;
    const VkShaderStageFlags st = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    vkCmdPushConstants(cb, g_layout, st, 0, 96, pc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_STAR_SURF]); vkCmdBindVertexBuffers(cb, 0, 1, &sv->b, &sv->off); vkCmdBindIndexBuffer(cb, si->b, si->off, VK_INDEX_TYPE_UINT32); vkCmdDrawIndexed(cb, s->sphN, 1, 0, 0, 0);
    if (s->faceVB && s->faceN) { GBuf *fv = bufOf(s->faceVB); if (fv->b) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_STAR_FACE]); vkCmdBindVertexBuffers(cb, 0, 1, &fv->b, &fv->off); vkCmdDraw(cb, s->faceN, 1, 0, 0); } }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_STAR_CORONA]); vkCmdBindVertexBuffers(cb, 0, 1, &qv->b, &qv->off); vkCmdDraw(cb, 6, 1, 0, 0);
    if (s->nLoop > 0) { GBuf *lv = bufOf(s->loopVB); if (lv->b) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_STAR_LOOP]); vkCmdBindVertexBuffers(cb, 0, 1, &lv->b, &lv->off);
        for (int i = 0; i < s->nLoop && i < 24; i++) { pc[16] = s->loopAng[i]; pc[17] = s->loopPh[i]; vkCmdPushConstants(cb, g_layout, st, 0, 96, pc); vkCmdDraw(cb, (uint32_t)(s->loopOff[i + 1] - s->loopOff[i]), 1, (uint32_t)s->loopOff[i], 0); } } }
}
/* the packet's effects, in the order they were asked for */
/* (step 24) THE SCENE, READ MID-FRAME. Where a setting asks (softfx, decals, refract) and the picture has its HDR target, a frame that has something to read it is drawn in two
 * halves: everything solid, then its depth and colour are copied aside (g_zsImg, g_snapImg: set 0's bindings 9 and 8), then what's laid over it, which can read them. A frame with
 * nothing that reads them is one pass, as it always was. g_midLive: this is the second half, so a particle's soft pipeline and a decal's projection have something true to read. */
static int g_midLive; static uint32_t g_dcAt, g_dcFlatAt; static int g_dcN[2], g_dcTexN, g_dcFlatN[2]; static unsigned g_dcTexName[GFX_MAX_DECALS];
/* the frame's decals into the ring. Projected: ten vec4s each (its matrix, the way back, its colour, its numbers), stains first, then paint, then pictures. Flat: six effect vertices each */
static void decalsUp(Frame *f, const GfxPacket *pk, int projected) {
    g_dcN[0] = g_dcN[1] = g_dcTexN = g_dcFlatN[0] = g_dcFlatN[1] = 0; if (pk->nDecal <= 0) return;
    if (projected) { float *o = ringAlloc(f, (VkDeviceSize)pk->nDecal * 160, 16, &g_dcAt); int at = 0;
        for (int pass = 0; pass < 3; pass++) for (int i = 0; i < pk->nDecal; i++) { const GfxDecal *d = &pk->decal[i]; const int k = (int)(d->kind + 0.5f), cls = k >= 6 ? 2 : k == 4 ? 1 : 0; if (cls != pass) continue;
            float *q = o + at * 40; memcpy(q, d->m, 64); if (!inv44(d->m, q + 16)) memset(q + 16, 0, 64); memcpy(q + 32, d->col, 16); q[36] = d->kind; q[37] = d->fade; q[38] = d->seed;
            q[39] = (cls == 2 && d->tex && d->tex < g_nTex && g_tex[d->tex].dsA) ? (float)g_tex[d->tex].layer : 0.f; at++;
            if (cls < 2) g_dcN[cls]++; else g_dcTexName[g_dcTexN++] = d->tex; }
        return; }
    static const float Q[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}}; float *o = ringAlloc(f, (VkDeviceSize)pk->nDecal * 216, 4, &g_dcFlatAt); int at = 0;
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < pk->nDecal; i++) { const GfxDecal *d = &pk->decal[i]; const int k = (int)(d->kind + 0.5f), cls = k == 4 ? 1 : 0; if (cls != pass || k >= 6) continue;   /* a picture has no flat stand-in: the effect path has no texture */
        const float *m = d->m; float ly = sqrtf(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]); if (ly < 1e-6f) ly = 1.f;
        for (int c = 0; c < 6; c++) { float u = Q[c][0], w = Q[c][1], *p = o + (at * 6 + c) * 9; for (int a = 0; a < 3; a++) p[a] = m[12 + a] + m[a] * 0.5f * u + m[8 + a] * 0.5f * w + m[4 + a] / ly * 0.03f;
            if (cls) { p[3] = d->col[0]; p[4] = d->col[1]; p[5] = d->col[2]; p[6] = d->col[3] * d->fade; }
            else { const float av = (d->col[0] + d->col[1] + d->col[2]) / 3.f; p[3] = d->col[0] * 0.3f; p[4] = d->col[1] * 0.3f; p[5] = d->col[2] * 0.3f; p[6] = d->col[3] * d->fade * (1.f - 0.9f * (av > 1.f ? 1.f : av)); }   /* a stain's colour is what it leaves of the light: laid flat, that's a dark mark, stronger the darker its colour */
            p[7] = u; p[8] = w; }
        at++; g_dcFlatN[cls]++; }
}
static void decalsDraw(Frame *f, VkCommandBuffer cb, const GfxPacket *pk, int projected) {
    if (pk->nDecal <= 0) return;
    if (projected) { if (!g_pipe[P_DECAL] || !g_pipe[P_DECAL_PAINT]) return; float pc[4] = {(float)(g_dcAt / 16u), 0.f, 0.f, 0.f};
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 1, 1, &g_whiteDs, 0, NULL);
        if (g_dcN[0]) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_DECAL]); vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc); vkCmdDraw(cb, 36, (uint32_t)g_dcN[0], 0, 0); }
        if (g_dcN[1] || g_dcTexN) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_DECAL_PAINT]); vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc); }
        if (g_dcN[1]) vkCmdDraw(cb, 36, (uint32_t)g_dcN[1], 0, (uint32_t)g_dcN[0]);
        for (int i = 0; i < g_dcTexN; i++) { const unsigned t = g_dcTexName[i]; vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 1, 1, (t && t < g_nTex && g_tex[t].dsA) ? &g_tex[t].dsA : &g_whiteDs, 0, NULL); vkCmdDraw(cb, 36, 1, 0, (uint32_t)(g_dcN[0] + g_dcN[1] + i)); }
        return; }
    for (int c = 0; c < 2; c++) if (g_dcFlatN[c]) { VkDeviceSize o = (VkDeviceSize)g_dcFlatAt + (VkDeviceSize)(c ? g_dcFlatN[0] : 0) * 216u; vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[c ? P_FX_ALPHA : P_FX_SOFT]); vkCmdBindVertexBuffers(cb, 0, 1, &f->ring.b, &o); vkCmdDraw(cb, (uint32_t)g_dcFlatN[c] * 6u, 1, 0, 0); }
}
static void fxDraw(Frame *f, VkCommandBuffer cb, const GfxPacket *pk, uint32_t base, int under, int vi) {
    VkPipeline cur = VK_NULL_HANDLE;
    for (int i = 0; i < pk->nFx; i++) { const GfxFx *x = &pk->fx[i]; if (x->under != under || (x->view >= 0 && x->view != vi)) continue;
        VkPipeline p = g_pipe[x->how == GFX_FX_SKY ? P_SKY : x->how == GFX_FX_ADD ? P_FX_ADD : x->how == GFX_FX_SOFT ? P_FX_SOFT : x->how == GFX_FX_SOFTZ ? ((g_midLive && g_cfg.softfx) ? P_FX_SOFTZ : P_FX_SOFT) : x->how == GFX_FX_GLOW ? P_FX_GLOW : x->how == GFX_FX_GLOWZ ? ((g_midLive && g_cfg.softfx) ? P_FX_GLOWZ : P_FX_GLOW) : x->how == GFX_FX_PUFF ? P_FX_PUFF : x->how == GFX_FX_WEB ? P_FX_WEB : x->how == GFX_FX_WEBX ? P_FX_WEBX : P_FX_ALPHA]; if (!p) continue;
        if (x->how == GFX_FX_PUFF && (x->tex >= g_nTex || !g_tex[x->tex].dsNear)) continue;
        if (p != cur) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p); cur = p; }
        if (x->how == GFX_FX_PUFF) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 1, 1, &g_tex[x->tex].dsNear, 0, NULL);
        VkBuffer b = f->ring.b; VkDeviceSize o = (VkDeviceSize)base + x->off;
        if (x->vb) { GBuf *g = bufOf(x->vb); if (!g->b) continue; b = g->b; o = g->off; }
        vkCmdBindVertexBuffers(cb, 0, 1, &b, &o); vkCmdDraw(cb, (uint32_t)x->n, 1, 0, 0); }
}
/* an off-screen picture's pass */
static void tgBegin(VkCommandBuffer cb, GTarget *g, const float *clear) { GTex *t = &g_tex[g->name];
    barrierImg(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    barrierImg(cb, g->depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 1);
    VkRenderingAttachmentInfo c = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, d = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; c.imageView = t->view; c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; c.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; c.storeOp = VK_ATTACHMENT_STORE_OP_STORE; for (int k = 0; k < 3; k++) c.clearValue.color.float32[k] = clear[k]; c.clearValue.color.float32[3] = 1.f;
    d.imageView = g->depthView; d.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; d.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; d.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; d.clearValue.depthStencil.depth = 1.f;
    VkRenderingInfo r = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO}; r.renderArea.extent.width = (uint32_t)g->w; r.renderArea.extent.height = (uint32_t)g->h; r.layerCount = 1; r.colorAttachmentCount = 1; r.pColorAttachments = &c; r.pDepthAttachment = &d; vkCmdBeginRendering(cb, &r);
    VkViewport vw = {0.f, (float)g->h, (float)g->w, -(float)g->h, 0.f, 1.f}; VkRect2D sc = {{0, 0}, {(uint32_t)g->w, (uint32_t)g->h}}; vkCmdSetViewport(cb, 0, 1, &vw); vkCmdSetScissor(cb, 0, 1, &sc); }
static void tgEnd(VkCommandBuffer cb, GTarget *g) { vkCmdEndRendering(cb);
    barrierImg(cb, g_tex[g->name].img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1); }
/* Holly's head */
static void headDraw(Frame *f, VkCommandBuffer cb) { const GfxHead *h = &g_head; GTarget *g = tgOf(h->target); GBuf *vb = bufOf(h->vbo), *ib = bufOf(h->ebo); if (!g || !vb->b || !ib->b || h->tex >= g_nTex || !g_tex[h->tex].ds) return;
    static const float black[3] = {0.f, 0.f, 0.f}; tgBegin(cb, g, black);
    if (h->flatN > 0 && bufOf(h->ivbo)->b && bufOf(h->iebo)->b) { GBuf *fv = bufOf(h->ivbo), *fi = bufOf(h->iebo); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_HEADFLAT]); vkCmdBindVertexBuffers(cb, 0, 1, &fv->b, &fv->off); vkCmdBindIndexBuffer(cb, fi->b, fi->off, VK_INDEX_TYPE_UINT16);
        for (int i = 0; i < h->flatN; i++) { float pc[20]; memcpy(pc, h->flat[i].mvp, 64); clipFix(pc); memcpy(pc + 16, h->flat[i].col, 12); pc[19] = 1.f; vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, pc); vkCmdDrawIndexed(cb, (uint32_t)h->flat[i].count, 1, (uint32_t)h->flat[i].first, 0, 0); } }
    uint32_t off; float *u = ringAlloc(f, 256, g_uboAlign, &off); memcpy(u, h->mvp, 64); clipFix(u); memcpy(u + 16, h->model, 64); memcpy(u + 32, h->weights, 32); memcpy(u + 40, &h->open, 24 * 4);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_HEAD]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &f->dsView, 1, &off); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 1, 1, &g_tex[h->tex].ds, 0, NULL);
    VkBuffer bs[9]; VkDeviceSize os[9]; bs[0] = vb->b; os[0] = vb->off; for (int m = 0; m < 8; m++) { GBuf *q = h->morph[m] ? bufOf(h->morph[m]) : NULL; if (q && q->b) { bs[1 + m] = q->b; os[1 + m] = q->off; } else { bs[1 + m] = g_zero.b; os[1 + m] = 0; } }
    vkCmdBindVertexBuffers(cb, 0, 9, bs, os); vkCmdBindIndexBuffer(cb, ib->b, ib->off, VK_INDEX_TYPE_UINT16); vkCmdDrawIndexed(cb, h->nIndex, 1, 0, 0, 0);
    tgEnd(cb, g); }
/* the world, through another lens */
static void feedDraw(Frame *f, VkCommandBuffer cb, const ViewUBO *u) { GTarget *g = tgOf(g_feed.tex); if (!g) return;
    uint32_t off; ViewUBO *w = ringAlloc(f, sizeof(ViewUBO), g_uboAlign, &off); memcpy(w, u, sizeof(ViewUBO)); memcpy(w->vp, g_feed.vp, 64); clipFix(w->vp); w->eye[0] = g_feed.eye[0]; w->eye[1] = g_feed.eye[1]; w->eye[2] = g_feed.eye[2]; w->misc[1] = 0.f; w->cl[0] = 0.f;
    static const float dusk[3] = {0.03f, 0.f, 0.05f}; tgBegin(cb, g, dusk); g_offScreen = 1; drawList(f, cb, off, 0, 0); drawList(f, cb, off, 0, 4); g_offScreen = 0; tgEnd(cb, g); }
/* the floor's shadow map: every body, white, through its slanted lens */
static void floorMapDraw(Frame *f, VkCommandBuffer cb, const ViewUBO *u) { GTarget *g = tgOf(g_fs.tex); if (!g) return;
    uint32_t off; ViewUBO *w = ringAlloc(f, sizeof(ViewUBO), g_uboAlign, &off); memcpy(w, u, sizeof(ViewUBO)); memcpy(w->vp, g_fs.vp, 64); clipFix(w->vp);
    static const float black[3] = {0.f, 0.f, 0.f}; tgBegin(cb, g, black);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_B_SKIN]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &f->dsView, 1, &off);
    for (int i = f->start[L_OPAQUE]; i < f->start[L_N]; i++) { const Batch *ba = &f->bat[i]; const Draw *d = &f->draws[ba->d0]; if (d->skin && !(d->flags & GFX_MIRRORED)) streamsBind(f, cb, ba, d, 0); }
    tgEnd(cb, g); }
/* the screens, in the world */
static void screensDraw(VkCommandBuffer cb) { if (!g_nScr) return; vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_SCREEN]);
    for (int i = 0; i < g_nScr; i++) { unsigned t = g_scr[i].tex; if (t >= g_nTex || !g_tex[t].ds) continue; vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 1, 1, &g_tex[t].ds, 0, NULL); float pc[20]; memcpy(pc, g_scr[i].model, 64); pc[16] = g_scr[i].amp; pc[17] = pc[18] = pc[19] = 0.f; vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, pc); vkCmdDraw(cb, 6, 1, 0, 0); } }
/* the world's been drawn somewhere other than the glass: bring it across, and leave the glass open for the HUD */
static void passBegin(VkCommandBuffer cb, VkImageView view, int w, int h, VkAttachmentLoadOp load) {
    VkRenderingAttachmentInfo c = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; c.imageView = view; c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; c.loadOp = load; c.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo r = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO}; r.renderArea.extent.width = (uint32_t)w; r.renderArea.extent.height = (uint32_t)h; r.layerCount = 1; r.colorAttachmentCount = 1; r.pColorAttachments = &c; vkCmdBeginRendering(cb, &r);
    VkViewport vw = {0.f, (float)h, (float)w, -(float)h, 0.f, 1.f}; VkRect2D sc = {{0, 0}, {(uint32_t)w, (uint32_t)h}}; vkCmdSetViewport(cb, 0, 1, &vw); vkCmdSetScissor(cb, 0, 1, &sc);
}
static void bloomStep(VkCommandBuffer cb, int to, int pipe, VkDescriptorSet src, int srcW, int srcH, VkAttachmentLoadOp load, VkImageLayout from) {
    barrierImg(cb, g_blImg[to], VK_IMAGE_ASPECT_COLOR_BIT, from, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    passBegin(cb, g_blView[to], g_blW[to], g_blH[to], load);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[pipe]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layoutPost, 0, 1, &src, 0, NULL);
    float pc[8] = {1.f / (float)srcW, 1.f / (float)srcH, (g_filmPost && pipe == P_BLOOM0) ? 0.95f : 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}; vkCmdPushConstants(cb, g_layoutPost, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, pc); vkCmdDraw(cb, 3, 1, 0, 0); vkCmdEndRendering(cb);
    barrierImg(cb, g_blImg[to], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
}
/* a half-size pass that reads the depth image, once per view */
static void depthPass(Frame *f, VkCommandBuffer cb, VkImage img, VkImageView view, int pipe, const GfxView *v, const uint32_t *viewOffs, int nv, float a, float b, float c) {
    barrierImg(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    int hw = g_W / 2 > 1 ? g_W / 2 : 1, hh = g_H / 2 > 1 ? g_H / 2 : 1; passBegin(cb, view, hw, hh, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[pipe]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 1, 1, &g_zDs, 0, NULL);
    for (int i = 0; i < nv; i++) { int rx = 0, ry = 0, rw = g_W, rh = g_H; if (v->nViews > 0) { rx = v->views[i].x; ry = v->views[i].y; rw = v->views[i].w; rh = v->views[i].h; }
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &f->dsView, 1, &viewOffs[i]);
        VkViewport vw = {(float)rx / 2.f, (float)(ry + rh) / 2.f, (float)rw / 2.f, -(float)rh / 2.f, 0.f, 1.f}; VkRect2D sc = {{rx / 2, ry / 2}, {(uint32_t)(rw / 2), (uint32_t)(rh / 2)}}; vkCmdSetViewport(cb, 0, 1, &vw); vkCmdSetScissor(cb, 0, 1, &sc);
        float pc[20]; memset(pc, 0, sizeof pc); pc[0] = (float)rx / (float)g_W; pc[1] = (float)ry / (float)g_H; pc[2] = (float)rw / (float)g_W; pc[3] = (float)rh / (float)g_H; pc[16] = a; pc[17] = b; pc[18] = c;
        vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, pc); vkCmdDraw(cb, 3, 1, 0, 0); }
    vkCmdEndRendering(cb);
    barrierImg(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
}
/* (step 25) THE LENS'S PASSES, between the scene and the post pass, on the scene's own light: anti-aliasing over time (taa), depth of field (dof) and motion blur (mblur).
 * Each is a full-screen pass from one picture into another, and whatever the last one leaves is copied back to the HDR target, so bloom and the post pass read it
 * where they always did. A frame whose lens asks for none of them runs none: depth of field wants a focus and a softness, motion blur a shutter. Split screen has none.
 * Where each point was in the last frame comes from its depth and the two lenses, so it's the lens's own move that's followed. */
static void lensOne(VkCommandBuffer cb, int pipe, VkDescriptorSet ds, VkImage to, VkImageView toView, const float *pc) {
    barrierImg(cb, to, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    passBegin(cb, toView, g_W, g_H, VK_ATTACHMENT_LOAD_OP_DONT_CARE); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[pipe]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layoutPost, 0, 1, &ds, 0, NULL);
    vkCmdPushConstants(cb, g_layoutPost, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, pc); vkCmdDraw(cb, 3, 1, 0, 0); vkCmdEndRendering(cb);
    barrierImg(cb, to, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
}
static void lensPasses(Frame *f, VkCommandBuffer cb, const GfxView *v, const uint32_t *viewOffs, int nv) {
    if (!g_lens) return;
    const ViewUBO *u = (const ViewUBO *)(f->ring.map + viewOffs[0]);
    float cur[16], inv[16], re[16], pc[32]; memcpy(cur, v->nViews > 0 ? v->views[0].vp : v->vp, 64); clipFix(cur);
    const int one = nv == 1 && inv44(cur, inv), was = one && g_prevOK && !v->reset && v->cuts == g_prevCuts;   /* was: the last frame was this lens's too, a moment ago */
    if (was) mul44(re, g_prevVP, inv);
    int src = SRC_HDR, taaDone = 0;
    if (one && g_cfg.taa) { const int i = g_taaAt; memset(pc, 0, sizeof pc); if (was) memcpy(pc, re, 64); pc[16] = 1.f / (float)g_W; pc[17] = 1.f / (float)g_H; pc[18] = (was && g_taaOK) ? 1.f : 0.f; pc[19] = 0.9f;
        lensOne(cb, P_TAA, g_taaDs[i], g_histImg[i], g_histView[i], pc); src = SRC_HIST0 + i; g_taaAt ^= 1; taaDone = 1; }
    g_taaOK = taaDone;
    if (one && g_cfg.dof && v->dof > 0.f && v->focus > 0.f) { memset(pc, 0, sizeof pc); memcpy(pc, inv, 64); memcpy(pc + 16, u->eye, 12); pc[19] = v->focus; memcpy(pc + 20, u->fwd, 12); pc[23] = v->dof;
        pc[24] = 1.f / (float)g_W; pc[25] = 1.f / (float)g_H; pc[26] = 0.022f * (float)g_H; pc[27] = (float)g_H;
        lensOne(cb, P_LENS_DOF, g_srcDs[src], g_scrImg[0], g_scrView[0], pc); src = SRC_SCR0; }
    if (was && g_cfg.mblur && v->shutter > 0.f) { memset(pc, 0, sizeof pc); memcpy(pc, re, 64); pc[16] = v->shutter > 1.f ? 1.f : v->shutter; pc[17] = 0.06f; pc[24] = 1.f / (float)g_W; pc[25] = 1.f / (float)g_H; pc[27] = (float)g_H;
        const int to = src == SRC_SCR0 ? 1 : 0; lensOne(cb, P_LENS_MB, g_srcDs[src], g_scrImg[to], g_scrView[to], pc); src = SRC_SCR0 + to; }
    if (src != SRC_HDR) { VkImage si = src == SRC_HIST0 ? g_histImg[0] : src == SRC_HIST1 ? g_histImg[1] : g_scrImg[src - SRC_SCR0];   /* what the last pass left goes back where bloom and the post pass read the scene */
        VkImageCopy r = {0}; r.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; r.srcSubresource.layerCount = 1; r.dstSubresource = r.srcSubresource; r.extent.width = (uint32_t)g_W; r.extent.height = (uint32_t)g_H; r.extent.depth = 1;
        barrierImg(cb, si, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, 1);
        barrierImg(cb, g_hdrImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, 1);
        vkCmdCopyImage(cb, si, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_hdrImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        barrierImg(cb, g_hdrImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
        barrierImg(cb, si, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, 1); }
    memcpy(g_prevVP, cur, 64); g_prevOK = 1; g_prevCuts = v->cuts;
}
/* (step 25) THE PICTURE AND THE GLASS. The picture is what the engine draws and everything in this file works in (g_W x g_H). The glass is the swapchain. They were
 * always the same; now a setting can make the picture its own size (`width`, `height`), its own shape (`aspect`), or a tube's (`tube`: 640 x 480, or 640 x 240 shown 4:3
 * as the Pi's 240p is). When they differ, every pass draws into g_picImg where it drew into the swapchain (g_img and g_view name it, and the real ones are kept in g_glass*),
 * and one last pass puts the picture on the glass, in its place, with black round it: plainly, or as a tube would show it. When they're the same none of this runs. */
static void pictureSize(void) {
    g_GW = g_W; g_GH = g_H; int w = g_W, h = g_H; float shown = 0.f;
    if (g_cfg.tube == 1) { w = 640; h = 480; } else if (g_cfg.tube == 2) { w = 640; h = 240; shown = 4.f / 3.f; }
    else { if (g_cfg.resH > 0) { h = g_cfg.resH; w = g_cfg.resW > 0 ? g_cfg.resW : (int)((float)h * (float)g_GW / (float)g_GH + 0.5f); }
        if (g_cfg.aspect > 0) { const float a = (float)g_cfg.aspect / 1000.f; if ((float)w / (float)h > a) w = (int)((float)h * a + 0.5f); else h = (int)((float)w / a + 0.5f); } }
    w &= ~1; h &= ~1; if (w < 32) w = 32; if (h < 32) h = 32;
    g_present = (w != g_GW || h != g_GH || g_cfg.tube || g_hdrOut); g_W = w; g_H = h; g_picShown = shown;
    if (g_present) printf("[gfx] the picture is %d x %d%s, on a glass of %d x %d%s\n", w, h, shown > 0.f ? " shown 4:3" : "", g_GW, g_GH, g_cfg.tube ? ", as a tube shows it" : "");
}
static void pictureMake(void) {
    if (!g_present) return;
    g_picImg = imgMake(COLOR_FMT, (uint32_t)g_W, (uint32_t)g_H, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &g_picView); g_picDs = dsPost(g_picView);
    for (uint32_t i = 0; i < g_nImg; i++) { g_glassImg[i] = g_img[i]; g_glassView[i] = g_view[i]; g_img[i] = g_picImg; g_view[i] = g_picView; }
    VkImageView xv = g_picView;   /* the presenter's shader names a second picture whatever the glass is, so there's always one bound */
    if (g_hdrOut) { g_exImg = imgMake(HDR_FMT, (uint32_t)g_W, (uint32_t)g_H, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &g_exView); xv = g_exView;
        VkCommandBuffer cb0 = oneBegin(); barrierImg(cb0, g_exImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, VK_ACCESS_SHADER_READ_BIT, 1); oneEnd(cb0); }
    { VkDescriptorImageInfo xi = {g_smpClamp, xv, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w.dstSet = g_picDs; w.dstBinding = 1; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &xi; vkUpdateDescriptorSets(g_dev, 1, &w, 0, NULL); }
}
static void presentPass(VkCommandBuffer cb, uint32_t idx) {
    barrierImg(cb, g_picImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
    barrierImg(cb, g_glassImg[idx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    VkRenderingAttachmentInfo c = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; c.imageView = g_glassView[idx]; c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; c.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; c.storeOp = VK_ATTACHMENT_STORE_OP_STORE; c.clearValue.color.float32[3] = 1.f;
    VkRenderingInfo r = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO}; r.renderArea.extent.width = (uint32_t)g_GW; r.renderArea.extent.height = (uint32_t)g_GH; r.layerCount = 1; r.colorAttachmentCount = 1; r.pColorAttachments = &c; vkCmdBeginRendering(cb, &r);
    const float shape = g_picShown > 0.f ? g_picShown : (float)g_W / (float)g_H; float dw = (float)g_GH * shape, dh = (float)g_GH; if (dw > (float)g_GW) { dw = (float)g_GW; dh = dw / shape; }
    const int iw = (int)(dw + 0.5f), ih = (int)(dh + 0.5f), x0 = (g_GW - iw) / 2, y0 = (g_GH - ih) / 2;
    VkViewport vw = {(float)x0, (float)(y0 + ih), (float)iw, -(float)ih, 0.f, 1.f}; VkRect2D sc = {{x0, y0}, {(uint32_t)iw, (uint32_t)ih}}; vkCmdSetViewport(cb, 0, 1, &vw); vkCmdSetScissor(cb, 0, 1, &sc);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[g_cfg.tube == 2 ? P_TUBE240 : g_cfg.tube ? P_TUBE480 : g_hdrOut ? P_PRESENT_HDR : P_PRESENT]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layoutPost, 0, 1, &g_picDs, 0, NULL);
    if (g_cfg.tube) { static unsigned field; const float *t = g_tubeP; const float pitch = t[5] >= 3.f ? t[5] : (ih > 1500 ? 4.f : 3.f);
        float gain = t[10]; if (gain <= 0.f) { const float beam = 2.5066283f * t[2] * (g_cfg.tube == 2 ? 0.5f : 0.5f * (1.f + t[7])), mask = t[4] > 0.5f ? (1.f + 2.f * (1.f - t[6])) / 3.f * (t[4] < 1.5f ? 1.f - t[6] / (2.f * pitch) : 1.f) : 1.f; gain = 0.92f / (beam * mask); if (gain > 1.7f) gain = 1.7f; }
        float pc[16] = {(float)g_W, (float)g_H, (float)iw, (float)ih, t[0], t[1], t[2], t[3], t[4], pitch, t[6], t[7], t[8], (float)(field++ & 1u), t[9], gain};
        vkCmdPushConstants(cb, g_layoutPost, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, pc); }
    else { float pc[4] = {200.f, g_exFresh ? 1.f : 0.f, 0.f, 0.f}; vkCmdPushConstants(cb, g_layoutPost, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc); }
    g_exFresh = 0;
    vkCmdDraw(cb, 3, 1, 0, 0); vkCmdEndRendering(cb);
}
/* the light past white, for an HDR10 glass: the same scene, bloom and exposure the post pass is about to read, into a picture of its own. A frame through a fisheye or a phone's frame has none */
static void exPass(VkCommandBuffer cb, const float *pc, const GfxView *v) {
    barrierImg(cb, g_exImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    passBegin(cb, g_exView, g_W, g_H, VK_ATTACHMENT_LOAD_OP_CLEAR);
    if (!(v->fish > 0.f) && !(v->feed > 0.5f)) { float pe[4] = {(float)(g_cfg.hdrout > 1 ? g_cfg.hdrout : 1000) / 200.f, 0.f, 0.f, 0.f};
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_HDREX]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layoutPost, 0, 1, &g_postDs, 0, NULL);
        vkCmdPushConstants(cb, g_layoutPost, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, pc); vkCmdPushConstants(cb, g_layoutPost, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 64, 16, pe); vkCmdDraw(cb, 3, 1, 0, 0); }
    vkCmdEndRendering(cb); g_exFresh = 1;
    barrierImg(cb, g_exImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
}
void gfxTube(const float *p) { static const float own[11] = {448.f, 139.f, 0.47f, 0.25f, 1.f, 0.f, 0.30f, 0.55f, 2.4f, 0.025f, 0.f}; memcpy(g_tubeP, p ? p : own, sizeof g_tubeP); }
float gfxShownAspect(void) { return g_picShown; }
void gfxShotSize(int *w, int *h) { if (g_present && !g_hdrOut) { *w = g_GW; *h = g_GH; } }
static void toGlass(Frame *f, VkCommandBuffer cb, uint32_t idx, const GfxView *v, const uint32_t *viewOffs, int nv) {
    vkCmdEndRendering(cb);
    if (!g_hdrImg) { barrierImg(cb, g_img[idx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1); passBegin(cb, g_view[idx], g_W, g_H, VK_ATTACHMENT_LOAD_OP_LOAD); return; }
    barrierImg(cb, g_hdrImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
    if (g_readDepth) { VkImage zi = g_zImg ? g_zImg : g_depth;
        barrierImg(cb, zi, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
        if (g_cfg.ao || g_cfg.contact) { depthPass(f, cb, g_aoImg[0], g_aoView[0], P_AO, v, viewOffs, nv, 2.2f, g_cfg.ao ? 1.f : 0.f, g_cfg.contact ? 1.f : 0.f);
            int hw = g_W / 2 > 1 ? g_W / 2 : 1, hh = g_H / 2 > 1 ? g_H / 2 : 1;
            barrierImg(cb, g_aoImg[1], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
            passBegin(cb, g_aoView[1], hw, hh, VK_ATTACHMENT_LOAD_OP_DONT_CARE); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_AOBLUR]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layoutPost, 0, 1, &g_blurDs, 0, NULL);
            float pb[8] = {1.f / (float)hw, 1.f / (float)hh, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f}; vkCmdPushConstants(cb, g_layoutPost, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, pb); vkCmdDraw(cb, 3, 1, 0, 0); vkCmdEndRendering(cb);
            barrierImg(cb, g_aoImg[1], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1); }
        if (g_cfg.volumetric && v->haze > 0.f) depthPass(f, cb, g_volImg, g_volView, P_VOL, v, viewOffs, nv, v->haze, 90.f, 0.f); }
    lensPasses(f, cb, v, viewOffs, nv);
    if (g_cfg.bloom) {
        const int nb = g_filmPost ? 2 : BLOOM_N;   /* film's is a tight glow on what's saturated */
        for (int i = 0; i < nb; i++) bloomStep(cb, i, i ? P_BLOOM_DOWN : P_BLOOM0, i ? g_blDs[i - 1] : g_hdrDs, i ? g_blW[i - 1] : g_W, i ? g_blH[i - 1] : g_H, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED);
        for (int i = nb - 2; i >= 0; i--) bloomStep(cb, i, P_BLOOM_UP, g_blDs[i + 1], g_blW[i + 1], g_blH[i + 1], VK_ATTACHMENT_LOAD_OP_LOAD, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    const int fo9 = !g_filmPost || v->filmPostOn;   /* film's pass can be off for a take, and then it's a straight copy */
    float pc[16] = {(v->exposure > 0.f ? v->exposure : 1.f) * ((!g_filmPost && v->dayExposure > 0.f) ? v->dayExposure : 1.f), (g_cfg.bloom && fo9) ? (g_filmPost ? v->filmBloom * 0.5f : 0.35f) : 0.f, (float)g_lutSize, g_cfg.tonemap ? 1.f : 0.f, fo9 ? v->knee : 0.f, (g_cfg.ao || g_cfg.contact) ? 1.f : 0.f, (g_cfg.volumetric && v->haze > 0.f) ? 1.f : 0.f, g_cfg.linear ? 0.f : 1.f, fo9 ? v->filmGrade : 0.f, fo9 ? v->filmVig : 0.f, (float)g_W / (float)g_H, 0.f, v->fish, v->fishTh, v->feed, v->feedT};
    float pe[8] = {fo9 ? v->sepia : 0.f, fo9 ? v->flash : 0.f, fo9 ? v->burn : 0.f, fo9 ? v->grain : 0.f, fo9 ? v->weave[0] : 0.f, fo9 ? v->weave[1] : 0.f, v->tsec, 0.f};   /* (step 25) film's look for the frame */
    if (g_hdrOut) exPass(cb, pc, v);   /* (step 25) the light past white, for an HDR10 glass */
    passBegin(cb, g_view[idx], g_W, g_H, VK_ATTACHMENT_LOAD_OP_DONT_CARE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_POST]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layoutPost, 0, 1, &g_postDs, 0, NULL);
    vkCmdPushConstants(cb, g_layoutPost, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, pc); vkCmdPushConstants(cb, g_layoutPost, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 64, 32, pe); vkCmdDraw(cb, 3, 1, 0, 0);
}
void gfxBackEnd(const GfxPacket *pk, const GfxView *v) {
    Frame *f = &g_fr[g_slot]; if (!g_frameLive) return; g_frameLive = 0;
    /* the packet's memory, whole: the bones and the 2D go straight up, at the offsets it gave them */
    g_viewAsp = (float)g_H / (float)g_W; memcpy(g_glossNum, v->gloss, 16); g_wallOnly = v->wall;
    uint32_t base = 0; if (pk->memUsed) { void *up = ringAlloc(f, (VkDeviceSize)pk->memUsed, 256, &base); memcpy(up, pk->mem, pk->memUsed); }
    for (int i = 0; i < pk->nDraw; i++) if (g_cfg.mirrors || !(pk->draws[i].flags & GFX_MIRRORED)) drawOf(f, &pk->draws[i], base);
    g_calls = g_binds = g_stretched = 0; batchesMake(f);
    int nv = v->nViews > 0 ? v->nViews : 1; uint32_t viewOffs[16];
    uint32_t viewOff; ViewUBO *u = ringAlloc(f, sizeof(ViewUBO), g_uboAlign, &viewOff); viewOffs[0] = viewOff;
    memcpy(u->vp, v->vp, 64); clipFix(u->vp); memcpy(u->sunVP, v->sunVP, 64); clipFix(u->sunVP);
    if (g_lens && g_cfg.taa && v->nViews <= 0) {   /* (step 25) anti-aliasing over time: each frame is drawn a part of a pixel off the last, eight places in turn */
        static const float H2[8] = {0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f}, H3[8] = {0.3333333f, 0.6666667f, 0.1111111f, 0.4444444f, 0.7777778f, 0.2222222f, 0.5555556f, 0.8888889f}; static unsigned jn; const int k = (int)(jn++ & 7u);
        const float jx = (H2[k] - 0.5f) * 2.f / (float)g_W, jy = (H3[k] - 0.5f) * 2.f / (float)g_H; for (int c = 0; c < 4; c++) { u->vp[c * 4] += jx * u->vp[c * 4 + 3]; u->vp[c * 4 + 1] += jy * u->vp[c * 4 + 3]; } }
    u->eye[0] = v->eye[0]; u->eye[1] = v->eye[1]; u->eye[2] = v->eye[2]; u->eye[3] = 1.f; u->sunDir[0] = v->sunDir[0]; u->sunDir[1] = v->sunDir[1]; u->sunDir[2] = v->sunDir[2]; u->sunDir[3] = v->sunI;
    memcpy(u->sunCol, v->sunCol, 12); u->sunCol[3] = 1.f; memcpy(u->sky, v->sky, 12); u->sky[3] = 1.f; memcpy(u->fog, v->fog, 12); u->fog[3] = v->fogD;
    if (g_cfg.linear) for (int i = 0; i < 3; i++) { u->sunCol[i] = powf(u->sunCol[i], 2.2f); u->sky[i] = powf(u->sky[i], 2.2f); u->fog[i] = powf(u->fog[i], 2.2f); }   /* the sets' colours are authored as they look */
    u->misc[0] = g_filmPost ? ((!v->filmPostOn && v->filmTone > 0.f) ? v->filmTone : 100.f) : v->knee; u->misc[1] = v->shadowOn ? 1.f : 0.f; u->misc[2] = 0.5f / (float)g_sunSize; u->misc[3] = v->tsec; u->misc2[0] = (float)g_H / 720.f; u->misc2[1] = g_envOn ? 1.f : 0.f; u->misc2[2] = (float)(g_envMips - 1); u->misc2[3] = 0.f;   /* y: the set has its surroundings baked; z: that cube's smallest level */
    memset(u->ground, 0, (size_t)((char *)(u + 1) - (char *)u->ground));
    { float gs = 0.f; for (int i = 0; i < 3; i++) gs += v->ground[i]; for (int i = 0; i < 3; i++) u->ground[i] = gs > 0.f ? (g_cfg.linear ? powf(v->ground[i], 2.2f) : v->ground[i]) : u->sky[i] * (i == 0 ? 0.50f : i == 1 ? 0.44f : 0.36f); }
    if (g_fsOn) memcpy(u->shv, g_fs.vp, 64);
    if (g_cfg.sky) { static const float ZK[3] = {1.35f, 1.45f, 1.75f}, GK[3] = {0.55f, 0.58f, 0.50f}; for (int i = 0; i < 3; i++) { u->skyZ[i] = v->sky[i] * ZK[i]; u->skyH[i] = v->clear[i]; u->skyG[i] = v->sky[i] * GK[i]; u->skyS[i] = v->sunCol[i]; } u->skyS[3] = v->sunI > 0.f ? v->sunI : 0.15f; }
    { float l = sqrtf(v->vp[3] * v->vp[3] + v->vp[7] * v->vp[7] + v->vp[11] * v->vp[11]); if (l > 0.f) { u->fwd[0] = v->vp[3] / l; u->fwd[1] = v->vp[7] / l; u->fwd[2] = v->vp[11] / l; } }
    /* the lights, in the ring as three vec4s each. Linear, if the light is */
    uint32_t lightsAt = 0; int nL = (g_cfg.lights > 0 && v->nLights > 0) ? (v->nLights < g_cfg.lights ? v->nLights : g_cfg.lights) : 0;
    int nTile = 0; float tileVP[16][16];
    if (nL) { float *o = ringAlloc(f, (VkDeviceSize)nL * 64, 16, &lightsAt); for (int i = 0; i < nL; i++) { const GfxLight *L = &v->lights[i]; float dl = L->dir[0] * L->dir[0] + L->dir[1] * L->dir[1] + L->dir[2] * L->dir[2];
            o[0] = L->pos[0]; o[1] = L->pos[1]; o[2] = L->pos[2]; o[3] = L->falloff;
            float m = L->col[0] > L->col[1] ? L->col[0] : L->col[1]; if (L->col[2] > m) m = L->col[2]; for (int k = 0; k < 3; k++) o[4 + k] = (g_cfg.linear && m > 0.f) ? powf(L->col[k] / m, 2.2f) * m : L->col[k];
            o[7] = L->cosInner; o[8] = L->dir[0]; o[9] = L->dir[1]; o[10] = L->dir[2]; o[11] = dl > 0.01f ? L->cosOuter : -2.f;
            /* its own shadow, while the atlas has room: a spot takes a tile, a lamp six */
            int want = dl > 0.01f ? 1 : 6; o[12] = -1.f; o[13] = dl > 0.01f ? 0.f : 1.f; o[14] = o[15] = 0.f;
            if (L->shadow && nTile + want <= g_cfg.shadowTiles && nTile + want <= 16) { o[12] = (float)nTile;
                float mc = m > 0.f ? m : 1.f, fo = L->falloff > 0.001f ? L->falloff : 0.001f, far = sqrtf(fmaxf(1.f, (mc / 0.015f - 1.f) / fo)), near = 0.15f;
                for (int k = 0; k < want; k++) { static const float FD[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    const float *d = want == 1 ? L->dir : FD[k]; float up[3] = {0.f, 1.f, 0.f}; if (fabsf(d[1]) > 0.9f) { up[0] = 0.f; up[1] = 0.f; up[2] = 1.f; }
                    float ang = want == 1 ? 2.f * acosf(L->cosOuter) * 1.05f : 1.5707963f; if (ang > 2.9f) ang = 2.9f; float fl = 1.f / tanf(ang * 0.5f);
                    float zv[3] = {-d[0], -d[1], -d[2]}, xv[3] = {up[1] * zv[2] - up[2] * zv[1], up[2] * zv[0] - up[0] * zv[2], up[0] * zv[1] - up[1] * zv[0]}; float xl = sqrtf(xv[0] * xv[0] + xv[1] * xv[1] + xv[2] * xv[2]); xv[0] /= xl; xv[1] /= xl; xv[2] /= xl;
                    float yv[3] = {zv[1] * xv[2] - zv[2] * xv[1], zv[2] * xv[0] - zv[0] * xv[2], zv[0] * xv[1] - zv[1] * xv[0]};
                    float vw[16] = {xv[0], yv[0], zv[0], 0.f, xv[1], yv[1], zv[1], 0.f, xv[2], yv[2], zv[2], 0.f, -(xv[0] * L->pos[0] + xv[1] * L->pos[1] + xv[2] * L->pos[2]), -(yv[0] * L->pos[0] + yv[1] * L->pos[1] + yv[2] * L->pos[2]), -(zv[0] * L->pos[0] + zv[1] * L->pos[1] + zv[2] * L->pos[2]), 1.f};
                    float pj[16] = {fl, 0.f, 0.f, 0.f, 0.f, fl, 0.f, 0.f, 0.f, 0.f, -(far + near) / (far - near), -1.f, 0.f, 0.f, -2.f * far * near / (far - near), 0.f};
                    mul44(tileVP[nTile], pj, vw); clipFix(tileVP[nTile]); nTile++; } }
            o += 16; } }
    uint32_t tilesAt = 0; if (nTile) { float *tm = ringAlloc(f, (VkDeviceSize)nTile * 64, 16, &tilesAt); memcpy(tm, tileVP, (size_t)nTile * 64); }
    if (g_prOn) { u->probe[0] = g_prGeo[0]; u->probe[1] = g_prGeo[1]; u->probe[2] = g_prGeo[2]; u->probe[3] = 1.f; for (int i = 0; i < 3; i++) u->probeS[i] = 1.f / (g_prGeo[3 + i] * (float)(g_prDims[i == 1 ? 1 : i == 2 ? 2 : 0] - 1)); }
    if (nTile) u->cl3[3] = (float)(tilesAt / 16);
    /* the sun's cascades, from the first view */
    uint32_t cascOff[4]; const float CD[5] = {1.f, 9.f, 24.f, 64.f, 170.f};
    if (g_cfg.cascades > 1 && v->shadowOn && v->sunI > 0.f) {
        const float *eg = v->eye, *vg = v->vp; if (v->nViews > 0) { vg = v->views[0].vp; eg = v->views[0].eye; }
        float fg[3] = {vg[3], vg[7], vg[11]}; { float l = sqrtf(fg[0] * fg[0] + fg[1] * fg[1] + fg[2] * fg[2]); if (l > 0.f) { fg[0] /= l; fg[1] /= l; fg[2] /= l; } }
        for (int i = 0; i < 4; i++) { float m[16]; cascadeOf(vg, eg, fg, v->sunDir, CD[i], CD[i + 1], i, m); memcpy(u->casc[i], m, 64); clipFix(u->casc[i]); u->split[i] = CD[i + 1]; } }
    /* split screen: each view is the same uniforms with its own lens */
    if (v->nViews > 0) for (int i = nv - 1; i >= 0; i--) { ViewUBO *w = u; if (i > 0) { w = ringAlloc(f, sizeof(ViewUBO), g_uboAlign, &viewOffs[i]); memcpy(w, u, sizeof(ViewUBO)); }
        memcpy(w->vp, v->views[i].vp, 64); clipFix(w->vp); w->eye[0] = v->views[i].eye[0]; w->eye[1] = v->views[i].eye[1]; w->eye[2] = v->views[i].eye[2];
        { float l = sqrtf(v->views[i].vp[3] * v->views[i].vp[3] + v->views[i].vp[7] * v->views[i].vp[7] + v->views[i].vp[11] * v->views[i].vp[11]); if (l > 0.f) { w->fwd[0] = v->views[i].vp[3] / l; w->fwd[1] = v->views[i].vp[7] / l; w->fwd[2] = v->views[i].vp[11] / l; } }
        if (nL) clustersFill(f, w, v->views[i].vp, v->views[i].eye, w->fwd, v, v->views[i].x, v->views[i].y, v->views[i].w, v->views[i].h, lightsAt); }
    if (v->nViews <= 0 && nL) clustersFill(f, u, v->vp, v->eye, u->fwd, v, 0, 0, g_W, g_H, lightsAt);
    if (g_readDepth || (g_cfg.sky && v->skyDome) || v->space) for (int i = 0; i < nv; i++) { ViewUBO *w = i == 0 ? u : (ViewUBO *)(f->ring.map + viewOffs[i]); float fx[16]; if (v->nViews > 0) { memcpy(fx, v->views[i].vp, 64); clipFix(fx); } else { memcpy(fx, v->vp, 64); clipFix(fx); } inv44(fx, w->inv); w->cl2[2] = (float)(v->nViews > 0 ? v->views[i].x : 0); w->cl2[3] = (float)(v->nViews > 0 ? v->views[i].y : 0);
        int vw = v->nViews > 0 ? v->views[i].w : g_W, vh = v->nViews > 0 ? v->views[i].h : g_H; w->cl3[0] = (float)vw; w->cl3[1] = (float)vh; w->cl3[2] = (float)vh / (float)vw; g_viewAsp = (float)vh / (float)vw; }
    uint32_t tileOff[16]; for (int i = 0; i < nTile; i++) { ViewUBO *cu = ringAlloc(f, sizeof(ViewUBO), g_uboAlign, &tileOff[i]); memcpy(cu, u, sizeof(ViewUBO)); memcpy(cu->sunVP, tileVP[i], 64); }
    /* each cascade draws through its own copy of the view's block, with its lens where the sun's goes */
    if (g_cfg.cascades > 1 && v->shadowOn && v->sunI > 0.f) for (int i = 0; i < 4; i++) { ViewUBO *cu = ringAlloc(f, sizeof(ViewUBO), g_uboAlign, &cascOff[i]); memcpy(cu, u, sizeof(ViewUBO)); memcpy(cu->sunVP, u->casc[i], 64); }
    uint32_t idx = 0;
#ifdef REVA_SDL
    { double t0 = nowMs(); VkResult r = vkAcquireNextImageKHR(g_dev, g_chain, UINT64_MAX, f->acq, VK_NULL_HANDLE, &idx); g_waited += nowMs() - t0; if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { printf("[gfx] acquire: %d\n", r); fflush(stdout); return; } }
#else
    idx = (uint32_t)g_kNext; g_kNext = (g_kNext + 1) % (int)g_nImg;
#endif
    VkCommandBuffer cb = f->cb; vkResetCommandBuffer(cb, 0); VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; vkBeginCommandBuffer(cb, &bi);
    /* textures that arrived since the last frame */
    for (int i = 0; i < g_nUpl; i++) { GUpload *u = &g_upl[i];
        barrierImg(cb, u->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, u->mips);
        vkCmdCopyBufferToImage(cb, g_upRing.b, u->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, u->nc, u->c);
        barrierImg(cb, u->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, u->mips); }
    g_nUpl = 0;
    /* and what no frame in flight can still be using */
    g_frames++;
    for (int i = 0; i < g_nGone;) { GGone *g = &g_gone[i]; if (g_frames - g->at <= FRAMES + 1) { i++; continue; }
        vkFreeDescriptorSets(g_dev, g_dp, 1, &g->ds); vkDestroyImageView(g_dev, g->view, NULL); if (g->dsA) vkFreeDescriptorSets(g_dev, g_dp, 1, &g->dsA); if (g->viewA) vkDestroyImageView(g_dev, g->viewA, NULL); vkDestroyImage(g_dev, g->img, NULL); vkFreeMemory(g_dev, g->mem, NULL); *g = g_gone[--g_nGone]; }
    /* the sun's map */
    if (v->shadowOn) {
        barrierImg(cb, g_sunImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 1);
        VkRenderingAttachmentInfo d = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; d.imageView = g_sunView; d.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; d.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; d.storeOp = VK_ATTACHMENT_STORE_OP_STORE; d.clearValue.depthStencil.depth = 1.f;
        VkRenderingInfo r = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO}; r.renderArea.extent.width = (uint32_t)g_sunSize; r.renderArea.extent.height = (uint32_t)g_sunSize; r.layerCount = 1; r.pDepthAttachment = &d; vkCmdBeginRendering(cb, &r);
        if (g_cfg.cascades > 1 && v->sunI > 0.f) { int ts = g_sunSize / 2; for (int i = 0; i < 4; i++) { int tx = (i & 1) * ts, ty = (i >> 1) * ts;
                VkViewport vw = {(float)tx, (float)(ty + ts), (float)ts, -(float)ts, 0.f, 1.f}; VkRect2D sc = {{tx, ty}, {(uint32_t)ts, (uint32_t)ts}}; vkCmdSetViewport(cb, 0, 1, &vw); vkCmdSetScissor(cb, 0, 1, &sc); drawList(f, cb, cascOff[i], 1, 0); } }
        else { VkViewport vw = {0.f, (float)g_sunSize, (float)g_sunSize, -(float)g_sunSize, 0.f, 1.f}; VkRect2D sc = {{0, 0}, {(uint32_t)g_sunSize, (uint32_t)g_sunSize}}; vkCmdSetViewport(cb, 0, 1, &vw); vkCmdSetScissor(cb, 0, 1, &sc); drawList(f, cb, viewOff, 1, 0); }
        vkCmdEndRendering(cb);
        barrierImg(cb, g_sunImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1); }
    /* pictures drawn off-screen, for the screens */
    if (g_headOn) { headDraw(f, cb); g_headOn = 0; }
    if (g_feedOn) { feedDraw(f, cb, u); g_feedOn = 0; }
    if (g_fsOn && !g_between) floorMapDraw(f, cb, u);
    /* the lights' own shadows */
    if (nTile) { barrierImg(cb, g_atlasImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 1);
        VkRenderingAttachmentInfo d = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; d.imageView = g_atlasView; d.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; d.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; d.storeOp = VK_ATTACHMENT_STORE_OP_STORE; d.clearValue.depthStencil.depth = 1.f;
        VkRenderingInfo r = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO}; r.renderArea.extent.width = ATLAS_SIZE; r.renderArea.extent.height = ATLAS_SIZE; r.layerCount = 1; r.pDepthAttachment = &d; vkCmdBeginRendering(cb, &r);
        int ts = ATLAS_SIZE / 4; for (int i = 0; i < nTile; i++) { int tx = (i % 4) * ts, ty = (i / 4) * ts; VkViewport vw = {(float)tx, (float)(ty + ts), (float)ts, -(float)ts, 0.f, 1.f}; VkRect2D sc = {{tx, ty}, {(uint32_t)ts, (uint32_t)ts}}; vkCmdSetViewport(cb, 0, 1, &vw); vkCmdSetScissor(cb, 0, 1, &sc); drawList(f, cb, tileOff[i], 1, 0); }
        vkCmdEndRendering(cb);
        barrierImg(cb, g_atlasImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1); }
    /* (step 24) does anything in this frame read the scene mid-frame? Then it's drawn in two halves */
    int mid = 0; if (g_mid && g_hdrImg) { if (g_cfg.decals && pk->nDecal > 0) mid = 1;
        if (!mid && g_cfg.softfx) for (int i = 0; i < pk->nFx && !mid; i++) if (!pk->fx[i].under && (pk->fx[i].how == GFX_FX_SOFTZ || pk->fx[i].how == GFX_FX_GLOWZ)) mid = 1;
        if (!mid && g_cfg.refract) for (int i = 0; i < pk->nDraw && !mid; i++) if (pk->draws[i].flags & GFX_WATER) mid = 1; }
    const int dcProj = mid && g_cfg.decals; decalsUp(f, pk, dcProj);
    /* the picture */
    barrierImg(cb, g_img[idx], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    barrierImg(cb, g_depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 1);
    if (g_msImg) barrierImg(cb, g_msImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    if (g_hdrImg) barrierImg(cb, g_hdrImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1);
    for (int part = 0; part < (mid ? 2 : 1); part++) {
    { VkRenderingAttachmentInfo c = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}, d = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO}; c.imageView = g_msImg ? g_msView : g_hdrImg ? g_hdrView : g_view[idx]; c.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; c.loadOp = part ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR; c.storeOp = (g_msImg && !(mid && part == 0)) ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE; for (int k = 0; k < 3; k++) c.clearValue.color.float32[k] = v->bars ? 0.f : g_cfg.linear == 2 ? powf(v->clear[k], 2.2f) : v->clear[k]; c.clearValue.color.float32[3] = 1.f;
      if (g_msImg) { c.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT; c.resolveImageView = g_hdrImg ? g_hdrView : g_view[idx]; c.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }
      if (g_zImg) { barrierImg(cb, g_zImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 1); d.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT; d.resolveImageView = g_zView; d.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; }
      d.imageView = g_depthView; d.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; d.loadOp = part ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR; d.storeOp = g_readDepth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE; d.clearValue.depthStencil.depth = 1.f;
      VkRenderingInfo r = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO}; r.renderArea.extent.width = (uint32_t)g_W; r.renderArea.extent.height = (uint32_t)g_H; r.layerCount = 1; r.colorAttachmentCount = 1; r.pColorAttachments = &c; r.pDepthAttachment = &d; vkCmdBeginRendering(cb, &r); }
    for (int vi = 0; vi < nv; vi++) { int rx = 0, ry = 0, rw = g_W, rh = g_H; if (v->nViews > 0) { rx = v->views[vi].x; ry = v->views[vi].y; rw = v->views[vi].w; rh = v->views[vi].h; } viewOff = viewOffs[vi];
    { VkViewport vw = {(float)rx, (float)(ry + rh), (float)rw, -(float)rh, 0.f, 1.f}; VkRect2D sc = {{rx, ry}, {(uint32_t)rw, (uint32_t)rh}}; vkCmdSetViewport(cb, 0, 1, &vw); vkCmdSetScissor(cb, 0, 1, &sc); }
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &f->dsView, 1, &viewOff);
    if (part == 0) {
    if (v->bars) { VkClearAttachment ca = {0}; ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; for (int k = 0; k < 3; k++) ca.clearValue.color.float32[k] = g_cfg.linear == 2 ? powf(v->clear[k], 2.2f) : v->clear[k]; ca.clearValue.color.float32[3] = 1.f;   /* (step 25) the scene's own colour inside its view, black outside */
        VkClearRect cr = {{{rx, ry}, {(uint32_t)rw, (uint32_t)rh}}, 0, 1}; vkCmdClearAttachments(cb, 1, &ca, 1, &cr); }
    if (g_cfg.sky && v->skyDome) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_SKYDOME]); vkCmdDraw(cb, 3, 1, 0, 0); }
    if (v->space) { uint32_t at; float *o = ringAlloc(f, 288, 16, &at); memcpy(o, v->space, 288); float pc[4] = {(float)(at / 16u), 0.f, 0.f, 0.f}; vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_SPACE]); vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc); vkCmdDraw(cb, 3, 1, 0, 0); }
    fxDraw(f, cb, pk, base, 1, vi);
    if (g_fsOn && g_tex[g_fs.tex].ds) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_FLOORSH]); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 1, 1, &g_tex[g_fs.tex].ds, 0, NULL); float pc[20]; memset(pc, 0, 64); memcpy(pc + 16, g_fs.rect, 16); vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, pc); vkCmdDraw(cb, 6, 1, 0, 0); }
    drawList(f, cb, viewOff, 0, 0);
    screensDraw(cb);
    if (f->start[L_GLOSS + 1] > f->start[L_GLOSS]) drawList(f, cb, viewOff, 0, 4); }
    if (part == 1 || !mid) { g_midLive = mid;
    decalsDraw(f, cb, pk, dcProj);
    if (pk->nDecal) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &f->dsView, 1, &viewOff);
    drawList(f, cb, viewOff, 0, 1);
    starPass(f, cb, &pk->star);
    if (pk->nBeam) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[P_BEAM]); for (int i = 0; i < pk->nBeam; i++) { vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 96, pk->beam[i]); vkCmdDraw(cb, 36, 1, 0, 0); } }
    if (pk->nLine) { for (int i = 0; i < pk->nLine; i++) { VkPipeline lp9 = g_pipe[pk->lines[i].over ? P_LINE_OVER : P_LINE]; if (!lp9) continue; vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, lp9); VkDeviceSize o = (VkDeviceSize)base + pk->lines[i].off; vkCmdBindVertexBuffers(cb, 0, 1, &f->ring.b, &o); vkCmdDraw(cb, (uint32_t)pk->lines[i].n, 1, 0, 0); } }
    fxDraw(f, cb, pk, base, 0, vi);
    drawList(f, cb, viewOff, 0, 2);
    drawList(f, cb, viewOff, 0, 3); g_midLive = 0; } }
    if (mid && part == 0) {   /* (step 24) everything solid is down: its depth and its colour are copied to where what's drawn next can read them, and the pass goes on from what it has */
        vkCmdEndRendering(cb); VkImage zs = g_zImg ? g_zImg : g_depth; VkImageCopy r = {0}; r.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT; r.srcSubresource.layerCount = 1; r.dstSubresource = r.srcSubresource; r.extent.width = (uint32_t)g_W; r.extent.height = (uint32_t)g_H; r.extent.depth = 1;
        barrierImg(cb, zs, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 1);
        barrierImg(cb, g_zsImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, 1);
        vkCmdCopyImage(cb, zs, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_zsImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        barrierImg(cb, g_zsImg, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
        if (zs == g_depth) barrierImg(cb, g_depth, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 1);
        r.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; r.dstSubresource = r.srcSubresource;
        barrierImg(cb, g_hdrImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 1);
        barrierImg(cb, g_snapImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, 1);
        vkCmdCopyImage(cb, g_hdrImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_snapImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        barrierImg(cb, g_snapImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 1);
        barrierImg(cb, g_hdrImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 1); } }
    if (!g_between) { g_nScrWas = g_nScr; g_fsWas = g_fsOn; } g_between = 0;
    g_nScr = 0; g_fsOn = 0;
    if (g_split) toGlass(f, cb, idx, v, viewOffs, nv);
    if (nv > 1) { VkViewport vw = {0.f, (float)g_H, (float)g_W, -(float)g_H, 0.f, 1.f}; VkRect2D sc = {{0, 0}, {(uint32_t)g_W, (uint32_t)g_H}}; vkCmdSetViewport(cb, 0, 1, &vw); vkCmdSetScissor(cb, 0, 1, &sc); }
    if (pk->nHud) { int curP = -1; for (int i = 0; i < pk->nHud; i++) { const GfxHudRec *h = &pk->hud[i]; int sp = h->tex && h->tex < g_nTex && g_tex[h->tex].dsNear; if (h->tex && !sp) continue;
            int want = sp ? P_SPRITE : P_HUD; if (want != curP) { vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipe[want]); curP = want; }
            if (sp) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 1, 1, &g_tex[h->tex].dsNear, 0, NULL);
            float pc[8] = {h->scr[0], h->scr[1], 0.f, 0.f, h->col[0], h->col[1], h->col[2], h->col[3]}; vkCmdPushConstants(cb, g_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32, pc); VkDeviceSize o = (VkDeviceSize)base + h->off; vkCmdBindVertexBuffers(cb, 0, 1, &f->ring.b, &o); vkCmdDraw(cb, (uint32_t)h->n, 1, 0, 0); } }
    vkCmdEndRendering(cb);
    if (g_present) presentPass(cb, idx);   /* (step 25) the picture onto the glass */
    if (g_hdrOut && g_cap.map) { VkBufferImageCopy c = {0}; c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.layerCount = 1; c.imageExtent.width = (uint32_t)g_W; c.imageExtent.height = (uint32_t)g_H; c.imageExtent.depth = 1;   /* a shot of an HDR10 glass is the picture under it */
        barrierImg(cb, g_picImg, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, 1); vkCmdCopyImageToBuffer(cb, g_picImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_cap.b, 1, &c); }
    { VkImageLayout from = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; VkImage gi9 = g_present ? g_glassImg[idx] : g_img[idx]; const int gw9 = g_present ? g_GW : g_W, gh9 = g_present ? g_GH : g_H;
      if (g_cap.map && !g_hdrOut) { barrierImg(cb, gi9, VK_IMAGE_ASPECT_COLOR_BIT, from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 1);
          VkBufferImageCopy c = {0}; c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.layerCount = 1; c.imageExtent.width = (uint32_t)gw9; c.imageExtent.height = (uint32_t)gh9; c.imageExtent.depth = 1; vkCmdCopyImageToBuffer(cb, gi9, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_cap.b, 1, &c); from = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; }
#ifdef REVA_SDL
      barrierImg(cb, gi9, VK_IMAGE_ASPECT_COLOR_BIT, from, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0, 1);
#endif
    }
    vkEndCommandBuffer(cb);
    CK(vkResetFences(g_dev, 1, &f->fence));
    VkSubmitInfo su = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO}; su.commandBufferCount = 1; su.pCommandBuffers = &cb;
#ifdef REVA_SDL
    VkPipelineStageFlags ws = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; su.waitSemaphoreCount = 1; su.pWaitSemaphores = &f->acq; su.pWaitDstStageMask = &ws; su.signalSemaphoreCount = 1; su.pSignalSemaphores = &g_rendered[idx];
#endif
    CK(vkQueueSubmit(g_q, 1, &su, f->fence));
#ifdef REVA_SDL
    { VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &g_rendered[idx]; pi.swapchainCount = 1; pi.pSwapchains = &g_chain; pi.pImageIndices = &idx; double t0 = nowMs(); VkResult r = vkQueuePresentKHR(g_q, &pi); g_waited += nowMs() - t0; if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { printf("[gfx] present: %d\n", r); fflush(stdout); } }
    (void)g_prevImg; (void)g_prevSlot;
#else
    /* two frames overlap: the frame before this one flips when its own fence says it's whole */
    if (g_prevSlot >= 0) { double t0 = nowMs(); CK(vkWaitForFences(g_dev, 1, &g_fr[g_prevSlot].fence, VK_TRUE, UINT64_MAX)); kShow((uint32_t)g_prevImg); g_waited += nowMs() - t0; }
    g_prevImg = (int)idx; g_prevSlot = g_slot;
#endif
    g_slot = (g_slot + 1) % FRAMES;
}
void gfxBackStats(int *calls, int *binds, int *pages, int *stretched) { *calls = g_calls; *binds = g_binds; *stretched = g_stretched; int n = 0; for (int i = 0; i < MAX_PAGES; i++) if (g_page[i].img) n++; *pages = n; }
