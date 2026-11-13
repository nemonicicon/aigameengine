/* Holly's head, drawn off-screen into a target the venue's screens sample (holly_fbo.h).
 *
 * Since step 18 of the engine plan (11-04) the drawing is the renderer's: this file loads the model, builds the
 * interior a photo shell lacks, hands the geometry and the texture to gfx.h once, and each frame fills a GfxHead
 * (the matrices, the mouth, the rig's numbers) for gfxHead. holly.c is included for its loader, its maths, its mouth
 * shapes and the rig: every symbol in it is static, so including it collides with nothing in crowd_kms.c. */
#include "holly.c"
#include "holly_fbo.h"
#include "gfx.h"

static struct {
  int ok, w, h;
  Model model;
  int rigged;
  unsigned vbo, ebo, tex;
  unsigned morph_vbo[MAX_MORPHS];
  unsigned ivbo, iebo;
  int cavity_start, cavity_count, upper_start, upper_count, lower_start, lower_count;
  unsigned color;   /* the target the screens sample */
} H;

int holly_fbo_width(void) { return H.w; }
int holly_fbo_height(void) { return H.h; }
unsigned int holly_fbo_texture(void) { return H.ok ? H.color : 0u; }

int holly_fbo_init(int w, int h, const char *model_path) {
  if (H.ok) return 1;
  H.w = w > 0 ? w : 160;
  H.h = h > 0 ? h : 120;

  /* [2026-09-30] holly.hlm is CONTENT (not in the repo). load_model() die()s on a missing file,
   * which took the whole engine down on a checkout that has the engine and no head -- while every
   * caller already treats a failed init as "no screens tonight" (holly_fbo_texture()==0). Say it
   * loudly and let the floor run. */
  { const char *mp = model_path ? model_path : "holly.hlm";
    FILE *probe = fopen(mp, "rb");
    if (!probe) { fprintf(stderr, "[holly] ⛔ NO HEAD MODEL: %s (%s) -- venue screens stay dark\n", mp, strerror(errno)); return 0; }
    fclose(probe); }
  H.model = load_model(model_path ? model_path : "holly.hlm");
  H.rigged = H.model.morph_count > 0;
  fprintf(stderr, "[holly] model %u verts, %u tris, %ux%u texture%s\n", H.model.vertex_count,
          H.model.index_count / 3, H.model.tex_w, H.model.tex_h,
          H.rigged ? ", rigged" : ", photo shell");

  H.vbo = gfxBufferNew(); gfxBufferData(H.vbo, (long)H.model.vertex_count * 32, H.model.vertices, 0);
  H.ebo = gfxBufferNew(); gfxBufferData(H.ebo, (long)H.model.index_count * 2, H.model.indices, 0);
  for (uint32_t i = 0; i < H.model.morph_count; i++) {
    H.morph_vbo[i] = gfxBufferNew(); gfxBufferData(H.morph_vbo[i], (long)H.model.vertex_count * 12, H.model.morphs[i], 0); }

  H.tex = gfxTextureNew();
  { unsigned char *rgba = (unsigned char *)malloc((size_t)H.model.tex_w * H.model.tex_h * 4);
    if (rgba) { const unsigned char *in = (const unsigned char *)H.model.texels;
      for (size_t i = 0, n = (size_t)H.model.tex_w * H.model.tex_h; i < n; i++) { rgba[i * 4] = in[i * 3]; rgba[i * 4 + 1] = in[i * 3 + 1]; rgba[i * 4 + 2] = in[i * 3 + 2]; rgba[i * 4 + 3] = 255; }
      gfxTexture(H.tex, (int)H.model.tex_w, (int)H.model.tex_h, rgba); free(rgba); } }

  /* The interior the mesh does not have: a cavity and two dental arches behind the aperture the
   * renderer's head shader discards. Skipped at draw time for a rigged head, which brings its own. */
  {
    const float Hh = H.model.height, HW = H.model.half_width;
    const float lip_y = H.model.min_y + RIG_LIP_Y * Hh;
    const float mouth_hw = RIG_MOUTH_W * HW;
    const float lip_z = 0.70f;
    static float interior[4096];
    static uint16_t interior_idx[4096];
    int vcount = 0, icount = 0;

    H.cavity_start = icount;
    icount += build_box(interior + vcount * 3, interior_idx + icount, vcount, H.model.center_x,
                        lip_y - Hh * 0.03f, lip_z - Hh * 0.17f, mouth_hw * 0.85f, Hh * 0.09f,
                        Hh * 0.07f);
    H.cavity_count = icount - H.cavity_start;
    vcount += 8;

    H.upper_start = icount;
    icount += build_arch(interior + vcount * 3, interior_idx + icount, vcount, H.model.center_x,
                         lip_y - Hh * 0.012f, lip_z - Hh * 0.055f, mouth_hw * 1.55f, Hh * 0.011f,
                         0.95f, true);
    H.upper_count = icount - H.upper_start;
    vcount += 15 * 5;

    H.lower_start = icount;
    icount += build_arch(interior + vcount * 3, interior_idx + icount, vcount, H.model.center_x,
                         lip_y - Hh * 0.05f, lip_z - Hh * 0.055f, mouth_hw * 1.55f, Hh * 0.011f,
                         0.95f, false);
    H.lower_count = icount - H.lower_start;
    vcount += 15 * 5;

    H.ivbo = gfxBufferNew(); gfxBufferData(H.ivbo, (long)vcount * 12, interior, 0);
    H.iebo = gfxBufferNew(); gfxBufferData(H.iebo, (long)icount * 2, interior_idx, 0);
  }

  /* The off-screen target. NEAREST: this is a low-res arena screen, and letting the sampler blur it would
   * undo the crush that makes the jaw stretch read as a mouth in the first place. */
  H.color = gfxTextureNew();
  gfxTargetMake(H.color, H.w, H.h, 1);

  fprintf(stderr, "[holly] head -> %dx%d target, texture %u\n", H.w, H.h, H.color);
  H.ok = 1;
  return 1;
}

/* a probe: how many times the head has actually been drawn into the target */
static unsigned H_renders = 0;
unsigned holly_fbo_renders(void) { return H_renders; }

void holly_fbo_render(float open, float wide, float seal, double t) {
  if (!H.ok) return;
  H_renders++;

  mat4 proj = mat4_perspective(30.0f, (float)H.w / (float)H.h, 0.1f, 100.0f);
  mat4 view = mat4_translate(0, 0, -CAMERA_Z);
  mat4 model_m = mat4_mul(mat4_rot_y(sinf((float)t / 3.4f) * 0.07f),
                          mat4_rot_x(sinf((float)t / 2.6f) * 0.035f));
  model_m.m[13] += sinf((float)t / 2.1f) * 0.012f;
  mat4 mvp = mat4_mul(proj, mat4_mul(view, model_m));

  GfxHead g9; memset(&g9, 0, sizeof g9);
  g9.target = H.color; g9.vbo = H.vbo; g9.ebo = H.ebo; g9.tex = H.tex; g9.nIndex = H.model.index_count;
  for (uint32_t i = 0; i < H.model.morph_count && i < 8; i++) g9.morph[i] = H.morph_vbo[i];
  memcpy(g9.mvp, mvp.m, 64); memcpy(g9.model, model_m.m, 64); shape_weights(&H.model, open, wide, seal, g9.weights);
  g9.open = open; g9.wide = wide; g9.seal = seal; g9.lipY = RIG_LIP_Y; g9.falloff = RIG_FALLOFF; g9.lipHalf = RIG_LIP_HALF; g9.drop = RIG_DROP;
  g9.pivotY = RIG_PIVOT_Y; g9.pivotZ = RIG_PIVOT_Z; g9.angle = RIG_ANGLE; g9.mouthW = RIG_MOUTH_W; g9.round = RIG_ROUND; g9.spread = RIG_SPREAD;
  g9.press = RIG_PRESS; g9.mouthDark = RIG_MOUTH_DARK; g9.aperture = RIG_APERTURE; g9.edge = RIG_EDGE; g9.edgeSharp = RIG_EDGE_SHARP; g9.lift = RIG_LIFT;
  g9.minY = H.model.min_y; g9.height = H.model.height; g9.centerX = H.model.center_x; g9.halfWidth = H.model.half_width; g9.deform = H.rigged ? 0.f : 1.f;
  if (!H.rigged) {   /* the interior: the cavity and the upper arch still, the lower arch on the joint the chin turns about */
    g9.ivbo = H.ivbo; g9.iebo = H.iebo; g9.flatN = 3;
    float amt = open * (1.0f - seal);
    mat4 jaw = mat4_translate(0, H.model.min_y + RIG_PIVOT_Y * H.model.height, RIG_PIVOT_Z * H.model.height);
    jaw = mat4_mul(jaw, mat4_rot_x(amt * RIG_ANGLE));
    jaw = mat4_mul(jaw, mat4_translate(0, -(H.model.min_y + RIG_PIVOT_Y * H.model.height) - amt * RIG_DROP * H.model.height, -RIG_PIVOT_Z * H.model.height));
    mat4 jm = mat4_mul(mvp, jaw);
    g9.flat[0].first = H.cavity_start; g9.flat[0].count = H.cavity_count; g9.flat[0].col[0] = 0.078f; g9.flat[0].col[1] = 0.043f; g9.flat[0].col[2] = 0.043f; memcpy(g9.flat[0].mvp, mvp.m, 64);
    g9.flat[1].first = H.upper_start; g9.flat[1].count = H.upper_count; g9.flat[1].col[0] = 0.29f; g9.flat[1].col[1] = 0.255f; g9.flat[1].col[2] = 0.216f; memcpy(g9.flat[1].mvp, mvp.m, 64);
    g9.flat[2].first = H.lower_start; g9.flat[2].count = H.lower_count; g9.flat[2].col[0] = 0.29f; g9.flat[2].col[1] = 0.255f; g9.flat[2].col[2] = 0.216f; memcpy(g9.flat[2].mvp, jm.m, 64); }
  gfxHead(&g9);
}
