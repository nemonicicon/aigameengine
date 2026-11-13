/* gfx_shd.inc -- ⭐ (11-12) A SURFACE'S OWN SHADER, on Vulkan (the Godot proof of concept's step 12: their Hedge Maze draws its hedges and its
 * ground with shaders of its own, and no plain material carries what those draw). Included by gfx_vk.c straight after pipeMake.
 *
 * A shader here is shaders/mesh.frag built with GODOT_HOOK round a game's surface code, which skate_src/godot/godot_shader.py translates from the
 * shader its maker wrote and compiles at the cook. So it's the world's own fragment shader, with the world's sun, lights, shadows, sky and fog,
 * and only what each pixel is made of is theirs: its colour, its normal, how rough, how much a metal, how much of the room it sees, what it gives
 * off. The vertex shader is the world's (mesh_static_vert). Its textures are set 3, SHD_TEX bindings, each a picture or an array of them, on the
 * sampler its code asked for. A draw names one in GfxMesh.shader and is drawn with it in the world's solid pass; everywhere else (the sun's pass,
 * a mirror, a picture drawn off-screen, the blended pass, a body) it's drawn plainly. Its pipeline layout is the world's with a fourth set, so
 * what the world's draws bound and pushed carries over (Vulkan's layout compatibility: the first three sets and the push constants are the same).
 * ⛔ GFX_SHADER_ABI (gfx.h) is mesh.frag's GODOT_HOOK_ABI: a shader cooked against another is refused by the engine (engine/setshader.inc),
 * because drawn it would read bindings that aren't there. */
#define SHD_MAX 128
#define SHD_TEX 8
typedef struct { VkPipeline pipe; VkDescriptorSet ds; VkImage img[SHD_TEX]; VkImageView view[SHD_TEX]; VkDeviceMemory mem[SHD_TEX]; int live; } GShd;
static GShd g_shd[SHD_MAX + 1];   /* 1 to SHD_MAX: 0 is none */
static VkDescriptorSetLayout g_dslShd; static VkPipelineLayout g_layoutShd; static VkDescriptorPool g_dpShd; static VkSampler g_smpShd[16];
static VkImageView g_shdDumA;   /* one white texel, as an array of one: what a binding no texture was given names */
static int g_skyShd;   /* the sky's own shader (a set's `skyshader` line), 0 the engine's own sky dome */
static void shdInit(void) {
    if (g_layoutShd) return;
    VkDescriptorSetLayoutBinding b[SHD_TEX]; memset(b, 0, sizeof b);
    for (int i = 0; i < SHD_TEX; i++) { b[i].binding = (uint32_t)i; b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b[i].descriptorCount = 1; b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; }
    VkDescriptorSetLayoutCreateInfo dl = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dl.bindingCount = SHD_TEX; dl.pBindings = b; CK(vkCreateDescriptorSetLayout(g_dev, &dl, NULL, &g_dslShd));
    VkDescriptorSetLayout sets[4] = {g_dslView, g_dslTex, g_dslTex, g_dslShd}; VkPushConstantRange pc = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    VkPipelineLayoutCreateInfo pli = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 4; pli.pSetLayouts = sets; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pc; CK(vkCreatePipelineLayout(g_dev, &pli, NULL, &g_layoutShd));
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SHD_MAX * SHD_TEX}; VkDescriptorPoolCreateInfo dpi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; dpi.maxSets = SHD_MAX; dpi.poolSizeCount = 1; dpi.pPoolSizes = &ps; CK(vkCreateDescriptorPool(g_dev, &dpi, NULL, &g_dpShd));
    { static const unsigned char white[4] = {255, 255, 255, 255}; GTex t; memset(&t, 0, sizeof t); texUpload(&t, 1, 1, white); g_shdDumA = t.viewA; }
}
/* the sampler a texture's code asked for: Godot's filter_ and repeat_ hints, as GFX_ST_ flags */
static VkSampler shdSampler(int flags) {
    const int k = (flags >> 1) & 15; if (g_smpShd[k]) return g_smpShd[k];
    const int nearest = (flags & GFX_ST_NEAREST) != 0, nomip = (flags & GFX_ST_NOMIP) != 0, clamp = (flags & GFX_ST_CLAMP) != 0;
    VkSamplerCreateInfo sc = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sc.magFilter = sc.minFilter = nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; sc.mipmapMode = nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sc.addressModeU = sc.addressModeV = sc.addressModeW = clamp ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT; sc.maxLod = nomip ? 0.f : VK_LOD_CLAMP_NONE;
    if ((flags & GFX_ST_ANISO) && !nearest && !nomip && g_cfg.aniso > 0) { sc.anisotropyEnable = 1; sc.maxAnisotropy = (float)g_cfg.aniso; }
    CK(vkCreateSampler(g_dev, &sc, NULL, &g_smpShd[k])); return g_smpShd[k];
}
void gfxSkyShader(int id) { g_skyShd = (id > 0 && id <= SHD_MAX && g_shd[id].live) ? id : 0; }
int gfxShaderNew(const uint32_t *spv, int words, int kind) {
    if (!g_dev || !spv || words < 5 || spv[0] != 0x07230203u) return 0;
    shdInit();
    int id = 0; for (int i = 1; i <= SHD_MAX; i++) if (!g_shd[i].live) { id = i; break; }
    if (!id) { printf("[gfx] more than %d surface shaders at once: the rest are drawn plainly\n", SHD_MAX); fflush(stdout); return 0; }
    GShd *s = &g_shd[id]; memset(s, 0, sizeof *s);
    PipeSpec ps = {SPV(mesh_static_vert), spv, (size_t)words * 4u, PF_DEPTH | PF_DWRITE, 0, 0, 0, 0, g_layoutShd};
    if (kind == 1) { PipeSpec sk = {SPV(post_vert), spv, (size_t)words * 4u, 0, g_cfg.sky, 0, 7, 0, g_layoutShd}; ps = sk; }   /* a sky: P_SKYDOME's shape, behind everything */
    g_pipeSoft = 1; s->pipe = pipeMake(&ps); g_pipeSoft = 0;
    if (!s->pipe) { printf("[gfx] a surface shader the driver wouldn't take: drawn plainly\n"); fflush(stdout); return 0; }
    VkDescriptorSetAllocateInfo a = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool = g_dpShd; a.descriptorSetCount = 1; a.pSetLayouts = &g_dslShd; CK(vkAllocateDescriptorSets(g_dev, &a, &s->ds));
    VkDescriptorImageInfo im[SHD_TEX]; VkWriteDescriptorSet w[SHD_TEX]; memset(w, 0, sizeof w);
    for (int i = 0; i < SHD_TEX; i++) { im[i].sampler = shdSampler(0); im[i].imageView = g_shdDumA; im[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = s->ds; w[i].dstBinding = (uint32_t)i; w[i].descriptorCount = 1; w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &im[i]; }
    vkUpdateDescriptorSets(g_dev, SHD_TEX, w, 0, NULL);
    s->live = 1; return id;
}
/* a texture of a shader's: w x h, layers of them one after another, each with its whole mip chain by the box filter every texture here gets */
void gfxShaderTexture(int id, int binding, int w, int h, int layers, const void *rgba, int flags) {
    if (!g_dev || id < 1 || id > SHD_MAX || !g_shd[id].live || binding < 0 || binding >= SHD_TEX || !rgba || w < 1 || h < 1 || layers < 1 || layers > 2048) return;
    GShd *s = &g_shd[id];
    if (s->img[binding]) { vkDeviceWaitIdle(g_dev); vkDestroyImageView(g_dev, s->view[binding], NULL); vkDestroyImage(g_dev, s->img[binding], NULL); vkFreeMemory(g_dev, s->mem[binding], NULL); s->img[binding] = VK_NULL_HANDLE; }
    uint32_t mips = 1; { int a = w, b = h; while (a > 1 || b > 1) { a = a > 1 ? a / 2 : 1; b = b > 1 ? b / 2 : 1; mips++; } }
    const VkFormat fmt = (flags & GFX_ST_SRGB) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = fmt; ii.extent.width = (uint32_t)w; ii.extent.height = (uint32_t)h; ii.extent.depth = 1; ii.mipLevels = mips; ii.arrayLayers = (uint32_t)layers; ii.samples = VK_SAMPLE_COUNT_1_BIT; ii.tiling = VK_IMAGE_TILING_OPTIMAL; ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    CK(vkCreateImage(g_dev, &ii, NULL, &s->img[binding]));
    { VkMemoryRequirements mr; vkGetImageMemoryRequirements(g_dev, s->img[binding], &mr); VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ma.allocationSize = mr.size; ma.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); CK(vkAllocateMemory(g_dev, &ma, NULL, &s->mem[binding])); CK(vkBindImageMemory(g_dev, s->img[binding], s->mem[binding], 0)); }
    size_t per = 0; { int a = w, b = h; for (uint32_t l = 0; l < mips; l++) { per += (size_t)a * b * 4; a = a > 1 ? a / 2 : 1; b = b > 1 ? b / 2 : 1; } }
    Arena st; arenaMake(&st, per * (size_t)layers, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    VkBufferImageCopy *cp = malloc(sizeof(VkBufferImageCopy) * mips * (size_t)layers); uint32_t nc = 0; size_t off = 0;
    for (int L = 0; L < layers; L++) {
        unsigned char *cur = malloc((size_t)w * h * 4); memcpy(cur, (const unsigned char *)rgba + (size_t)L * w * h * 4, (size_t)w * h * 4); int a = w, b = h;
        for (uint32_t l = 0; l < mips; l++) {
            memcpy(st.map + off, cur, (size_t)a * b * 4);
            VkBufferImageCopy c; memset(&c, 0, sizeof c); c.bufferOffset = off; c.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; c.imageSubresource.mipLevel = l; c.imageSubresource.baseArrayLayer = (uint32_t)L; c.imageSubresource.layerCount = 1; c.imageExtent.width = (uint32_t)a; c.imageExtent.height = (uint32_t)b; c.imageExtent.depth = 1; cp[nc++] = c;
            int na = a > 1 ? a / 2 : 1, nb = b > 1 ? b / 2 : 1; off += (size_t)a * b * 4;
            if (l + 1 < mips) { unsigned char *nx = malloc((size_t)na * nb * 4);
                for (int y = 0; y < nb; y++) for (int x = 0; x < na; x++) for (int k = 0; k < 4; k++) { int x1 = x * 2 < a ? x * 2 : a - 1, x2 = x * 2 + 1 < a ? x * 2 + 1 : a - 1, y1 = y * 2 < b ? y * 2 : b - 1, y2 = y * 2 + 1 < b ? y * 2 + 1 : b - 1;
                    nx[(y * na + x) * 4 + k] = (unsigned char)((cur[(y1 * a + x1) * 4 + k] + cur[(y1 * a + x2) * 4 + k] + cur[(y2 * a + x1) * 4 + k] + cur[(y2 * a + x2) * 4 + k] + 2) / 4); }
                free(cur); cur = nx; }
            a = na; b = nb; }
        free(cur); }
    VkCommandBuffer cb = oneBegin();
    VkImageMemoryBarrier pb = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; pb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; pb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; pb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; pb.srcQueueFamilyIndex = pb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; pb.image = s->img[binding]; pb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; pb.subresourceRange.levelCount = mips; pb.subresourceRange.layerCount = (uint32_t)layers;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &pb);
    vkCmdCopyBufferToImage(cb, st.b, s->img[binding], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, nc, cp);
    pb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; pb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; pb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; pb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &pb);
    oneEnd(cb);
    free(cp); vkDestroyBuffer(g_dev, st.b, NULL); vkFreeMemory(g_dev, st.m, NULL);
    VkImageViewCreateInfo v = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; v.image = s->img[binding]; v.viewType = (flags & GFX_ST_ARRAY) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D; v.format = fmt; v.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; v.subresourceRange.levelCount = mips; v.subresourceRange.layerCount = (flags & GFX_ST_ARRAY) ? (uint32_t)layers : 1u;
    CK(vkCreateImageView(g_dev, &v, NULL, &s->view[binding]));
    VkDescriptorImageInfo im = {shdSampler(flags), s->view[binding], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; wr.dstSet = s->ds; wr.dstBinding = (uint32_t)binding; wr.descriptorCount = 1; wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &im;
    vkUpdateDescriptorSets(g_dev, 1, &wr, 0, NULL);
}
/* gone with its set: everything waits for the GPU first, which a set's change can afford */
void gfxShaderDrop(int id) {
    if (!g_dev || id < 1 || id > SHD_MAX || !g_shd[id].live) return;
    GShd *s = &g_shd[id]; vkDeviceWaitIdle(g_dev); if (g_skyShd == id) g_skyShd = 0;
    for (int i = 0; i < SHD_TEX; i++) if (s->img[i]) { vkDestroyImageView(g_dev, s->view[i], NULL); vkDestroyImage(g_dev, s->img[i], NULL); vkFreeMemory(g_dev, s->mem[i], NULL); }
    vkFreeDescriptorSets(g_dev, g_dpShd, 1, &s->ds); vkDestroyPipeline(g_dev, s->pipe, NULL); memset(s, 0, sizeof *s);
}
