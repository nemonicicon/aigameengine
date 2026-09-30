/* Holly's head rendered into an FBO, for screens inside the dancefloor.
 *
 * WHY THIS INCLUDES A .c FILE. Every symbol in holly.c is static, so it cannot be linked against
 * -- but it also means including it costs nothing at link time and collides with nothing in
 * crowd_kms.c, which has its own die(), mat4 and friends. Including rather than copying keeps ONE
 * implementation of the loader, the shaders and the rig constants: edit holly.c and this follows.
 * Its main() is renamed aside rather than deleted, so the standalone renderer still builds from the
 * same source.
 */
#define main holly_standalone_main_unused
#include "holly.c"
#undef main

#include "holly_fbo.h"

static struct {
  int ok, w, h;
  Model model;
  int rigged;
  GLuint head_prog, flat_prog;
  GLuint vbo, ebo, tex;
  GLuint morph_vbo[MAX_MORPHS];
  GLuint ivbo, iebo;
  int cavity_start, cavity_count, upper_start, upper_count, lower_start, lower_count;
  GLuint fbo, color, depth;
} H;

int holly_fbo_width(void) { return H.w; }
int holly_fbo_height(void) { return H.h; }
unsigned int holly_fbo_texture(void) { return H.ok ? H.color : 0u; }

int holly_fbo_init(int w, int h, const char *model_path) {
  if (H.ok) return 1;
  H.w = w > 0 ? w : 160;
  H.h = h > 0 ? h : 120;

  /* [2026-09-30] holly.hlm is CONTENT (not in the repo). load_model() die()s on a missing file,
   * which took the whole club down on a checkout that has the engine and no head -- while every
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

  H.head_prog = link_program(HEAD_VS, HEAD_FS);
  H.flat_prog = link_program(FLAT_VS, FLAT_FS);

  glGenBuffers(1, &H.vbo);
  glBindBuffer(GL_ARRAY_BUFFER, H.vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)H.model.vertex_count * 32, H.model.vertices,
               GL_STATIC_DRAW);
  glGenBuffers(1, &H.ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, H.ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)H.model.index_count * 2, H.model.indices,
               GL_STATIC_DRAW);

  for (uint32_t i = 0; i < H.model.morph_count; i++) {
    glGenBuffers(1, &H.morph_vbo[i]);
    glBindBuffer(GL_ARRAY_BUFFER, H.morph_vbo[i]);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)H.model.vertex_count * 12, H.model.morphs[i],
                 GL_STATIC_DRAW);
  }

  glGenTextures(1, &H.tex);
  glBindTexture(GL_TEXTURE_2D, H.tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)H.model.tex_w, (GLsizei)H.model.tex_h, 0, GL_RGB,
               GL_UNSIGNED_BYTE, H.model.texels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  /* The interior the mesh does not have: a cavity and two dental arches behind the aperture the
   * fragment shader discards. Skipped at draw time for a rigged head, which brings its own. */
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

    glGenBuffers(1, &H.ivbo);
    glBindBuffer(GL_ARRAY_BUFFER, H.ivbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vcount * 12, interior, GL_STATIC_DRAW);
    glGenBuffers(1, &H.iebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, H.iebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)icount * 2, interior_idx, GL_STATIC_DRAW);
  }

  /* The off-screen target. Depth is a renderbuffer: nothing samples it, and a renderbuffer is the
   * cheaper object when it is never read. */
  glGenTextures(1, &H.color);
  glBindTexture(GL_TEXTURE_2D, H.color);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, H.w, H.h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
  /* NEAREST: this is a low-res arena screen, and letting the sampler blur it would undo the
   * crush that makes the jaw stretch read as a mouth in the first place. */
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glGenRenderbuffers(1, &H.depth);
  glBindRenderbuffer(GL_RENDERBUFFER, H.depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, H.w, H.h);

  glGenFramebuffers(1, &H.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, H.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, H.color, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, H.depth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "[holly] framebuffer incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 0;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  fprintf(stderr, "[holly] head -> %dx%d fbo, texture %u\n", H.w, H.h, H.color);
  H.ok = 1;
  return 1;
}

/* ep 9 probe: how many times the head has actually been drawn into the target */
static unsigned H_renders = 0;
unsigned holly_fbo_renders(void) { return H_renders; }

/* ep 9 probe: the target's completeness NOW (0x8CD5 = complete), binding restored */
int holly_fbo_status(void) {
  if (!H.ok) return 0;
  GLint prev = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
  glBindFramebuffer(GL_FRAMEBUFFER, H.fbo);
  int st = (int)glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
  return st;
}

void holly_fbo_render(float open, float wide, float seal, double t) {
  if (!H.ok) return;
  H_renders++;

  /* Whatever the venue had bound comes back at the end. */
  GLint prev_fbo = 0, prev_vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glGetIntegerv(GL_VIEWPORT, prev_vp);

  mat4 proj = mat4_perspective(30.0f, (float)H.w / (float)H.h, 0.1f, 100.0f);
  mat4 view = mat4_translate(0, 0, -CAMERA_Z);
  mat4 model_m = mat4_mul(mat4_rot_y(sinf((float)t / 3.4f) * 0.07f),
                          mat4_rot_x(sinf((float)t / 2.6f) * 0.035f));
  model_m.m[13] += sinf((float)t / 2.1f) * 0.012f;
  mat4 mvp = mat4_mul(proj, mat4_mul(view, model_m));

  glBindFramebuffer(GL_FRAMEBUFFER, H.fbo);
  glViewport(0, 0, H.w, H.h);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  /* Black, so the head floats -- the effect was done originally with a lit face in a dark studio,
   * and the edge falloff in the shader reproduces it. */
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!H.rigged) {
    glEnable(GL_CULL_FACE);
    glUseProgram(H.flat_prog);
    glUniformMatrix4fv(glGetUniformLocation(H.flat_prog, "uMVP"), 1, GL_FALSE, mvp.m);
    glBindBuffer(GL_ARRAY_BUFFER, H.ivbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, H.iebo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void *)0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);

    GLint flat_col = glGetUniformLocation(H.flat_prog, "uColor");
    glUniform3f(flat_col, 0.078f, 0.043f, 0.043f);
    glDrawElements(GL_TRIANGLES, H.cavity_count, GL_UNSIGNED_SHORT,
                   (void *)(intptr_t)(H.cavity_start * 2));
    glUniform3f(flat_col, 0.29f, 0.255f, 0.216f);
    glDrawElements(GL_TRIANGLES, H.upper_count, GL_UNSIGNED_SHORT,
                   (void *)(intptr_t)(H.upper_start * 2));

    /* the lower arch swings on the joint the shader turns the chin about */
    float amt = open * (1.0f - seal);
    mat4 jaw = mat4_translate(0, H.model.min_y + RIG_PIVOT_Y * H.model.height,
                              RIG_PIVOT_Z * H.model.height);
    jaw = mat4_mul(jaw, mat4_rot_x(amt * RIG_ANGLE));
    jaw = mat4_mul(jaw, mat4_translate(0,
                                       -(H.model.min_y + RIG_PIVOT_Y * H.model.height) -
                                           amt * RIG_DROP * H.model.height,
                                       -RIG_PIVOT_Z * H.model.height));
    mat4 jaw_mvp = mat4_mul(mvp, jaw);
    glUniformMatrix4fv(glGetUniformLocation(H.flat_prog, "uMVP"), 1, GL_FALSE, jaw_mvp.m);
    glDrawElements(GL_TRIANGLES, H.lower_count, GL_UNSIGNED_SHORT,
                   (void *)(intptr_t)(H.lower_start * 2));
  }

  /* --- the head --- */
  glUseProgram(H.head_prog);
  glBindBuffer(GL_ARRAY_BUFFER, H.vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, H.ebo);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void *)12);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)24);

  float weights[MAX_MORPHS];
  shape_weights(&H.model, open, wide, seal, weights);
  for (int i = 0; i < MAX_MORPHS; i++) {
    GLuint loc = (GLuint)(3 + i);
    if ((uint32_t)i < H.model.morph_count) {
      glBindBuffer(GL_ARRAY_BUFFER, H.morph_vbo[i]);
      glEnableVertexAttribArray(loc);
      glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 12, (void *)0);
    } else {
      glDisableVertexAttribArray(loc);
      glVertexAttrib3f(loc, 0, 0, 0);
    }
  }
  glUniform1fv(glGetUniformLocation(H.head_prog, "uMorph"), MAX_MORPHS, weights);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, H.tex);
  glUniform1i(glGetUniformLocation(H.head_prog, "uTex"), 0);
  glUniformMatrix4fv(glGetUniformLocation(H.head_prog, "uMVP"), 1, GL_FALSE, mvp.m);
  glUniformMatrix4fv(glGetUniformLocation(H.head_prog, "uModel"), 1, GL_FALSE, model_m.m);

#define SETF(name, value) glUniform1f(glGetUniformLocation(H.head_prog, name), (value))
  SETF("uOpen", open);
  SETF("uWide", wide);
  SETF("uSeal", seal);
  SETF("uLipY", RIG_LIP_Y);
  SETF("uFalloff", RIG_FALLOFF);
  SETF("uLipHalf", RIG_LIP_HALF);
  SETF("uDrop", RIG_DROP);
  SETF("uPivotY", RIG_PIVOT_Y);
  SETF("uPivotZ", RIG_PIVOT_Z);
  SETF("uAngle", RIG_ANGLE);
  SETF("uMouthW", RIG_MOUTH_W);
  SETF("uRound", RIG_ROUND);
  SETF("uSpread", RIG_SPREAD);
  SETF("uPress", RIG_PRESS);
  SETF("uMouthDark", RIG_MOUTH_DARK);
  SETF("uAperture", RIG_APERTURE);
  SETF("uEdge", RIG_EDGE);
  SETF("uEdgeSharp", RIG_EDGE_SHARP);
  SETF("uLift", RIG_LIFT);
  SETF("uMinY", H.model.min_y);
  SETF("uHeight", H.model.height);
  SETF("uCenterX", H.model.center_x);
  SETF("uHalfWidth", H.model.half_width);
  SETF("uDeform", H.rigged ? 0.0f : 1.0f);
#undef SETF

  /* the mesh is doubleSided and the aperture shows its inside */
  glDisable(GL_CULL_FACE);
  glDrawElements(GL_TRIANGLES, (GLsizei)H.model.index_count, GL_UNSIGNED_SHORT, (void *)0);

  /* hand the venue back exactly what it had */
  for (int i = 0; i < MAX_MORPHS; i++) glDisableVertexAttribArray((GLuint)(3 + i));
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
}

void holly_fbo_dump(const char *path) {
  if (!H.ok) return;
  unsigned char *px = (unsigned char *)malloc((size_t)H.w * H.h * 4);
  if (!px) return;
  GLint prev = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
  glBindFramebuffer(GL_FRAMEBUFFER, H.fbo);
  glReadPixels(0, 0, H.w, H.h, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
  FILE *f = fopen(path, "wb");
  if (f) {
    fprintf(f, "P6\n%d %d\n255\n", H.w, H.h);
    /* GL origin is bottom-left; PPM is top-down. */
    for (int y = H.h - 1; y >= 0; y--)
      for (int x = 0; x < H.w; x++) fwrite(px + ((size_t)y * H.w + x) * 4, 1, 3, f);
    fclose(f);
    /* how much of it is not black -- a head that failed to draw is a black rectangle */
    long lit = 0;
    for (long i = 0; i < (long)H.w * H.h; i++)
      if (px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2] > 24) lit++;
    fprintf(stderr, "[holly] dumped %s  %dx%d  %.1f%% non-black\n", path, H.w, H.h,
            100.0 * (double)lit / ((double)H.w * H.h));
  }
  free(px);
}
