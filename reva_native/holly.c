// Holly, the head on the venue's screens: his model, its maths, his mouth's shapes, the interior a photo shell
// lacks, and the rig's numbers. Once this was a renderer of its own on the tube (DRM, GBM and a GL context, the
// mouth arriving over UDP); since step 18 of the engine plan (11-04) the drawing is the engine's renderer's, and
// holly_fbo.c includes this file for the rest. Every symbol is static.
//
// The model is a flat blob produced by scripts/bake-model.mjs: parsing glTF and decoding JPEG is done once on
// the desktop rather than every boot here.

#define _GNU_SOURCE
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *what) {
  fprintf(stderr, "holly: %s: %s\n", what, strerror(errno));
  exit(1);
}

// ---------------------------------------------------------------- maths ----
// Just enough linear algebra. Column-major, as the renderer takes them.

typedef struct { float m[16]; } mat4;

static mat4 mat4_identity(void) {
  mat4 r = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  return r;
}

static mat4 mat4_mul(mat4 a, mat4 b) {
  mat4 r;
  for (int c = 0; c < 4; c++)
    for (int i = 0; i < 4; i++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += a.m[k * 4 + i] * b.m[c * 4 + k];
      r.m[c * 4 + i] = s;
    }
  return r;
}

// aspect is the shape of the picture ON THE GLASS, not of the framebuffer.
// Composite pixels are nowhere near square: 720x240 is painted across a 4:3
// screen, so passing the buffer's own 3:1 would stretch him to a smear.
static mat4 mat4_perspective(float fovy_deg, float aspect, float znear, float zfar) {
  float f = 1.0f / tanf(fovy_deg * (float)M_PI / 360.0f);
  mat4 r = {{0}};
  r.m[0] = f / aspect;
  r.m[5] = f;
  r.m[10] = (zfar + znear) / (znear - zfar);
  r.m[11] = -1;
  r.m[14] = (2 * zfar * znear) / (znear - zfar);
  return r;
}

static mat4 mat4_translate(float x, float y, float z) {
  mat4 r = mat4_identity();
  r.m[12] = x;
  r.m[13] = y;
  r.m[14] = z;
  return r;
}

static mat4 mat4_rot_x(float a) {
  mat4 r = mat4_identity();
  r.m[5] = cosf(a); r.m[6] = sinf(a);
  r.m[9] = -sinf(a); r.m[10] = cosf(a);
  return r;
}

static mat4 mat4_rot_y(float a) {
  mat4 r = mat4_identity();
  r.m[0] = cosf(a); r.m[2] = -sinf(a);
  r.m[8] = sinf(a); r.m[10] = cosf(a);
  return r;
}

// ---------------------------------------------------------------- model ----

#define MAX_MORPHS 8

typedef struct {
  uint32_t vertex_count, index_count, tex_w, tex_h;
  float min_y, height, center_x, half_width;
  uint8_t *vertices; // vertex_count * 32: pos[3] normal[3] uv[2]
  uint8_t *indices;  // index_count * 2, u16
  uint8_t *texels;   // tex_w * tex_h * 3, RGB
  uint32_t morph_count;
  char morph_names[MAX_MORPHS][16];
  uint8_t *morphs[MAX_MORPHS]; // vertex_count * 12 each, position deltas
  void *blob;
} Model;

// Where each shape sits in the analyser's own (open, wide, seal) space. Weights
// come from distance in that space rather than from any explicit classifier —
// the analyser emits a point, and the point is somewhere among these.
typedef struct { const char *name; float open, wide, seal; } ShapePoint;

static const ShapePoint SHAPE_POINTS[] = {
    {"rest", 0.00f, 0.00f, 0.0f}, {"ah", 1.00f, -0.05f, 0.0f},
    {"eh", 0.55f, 0.35f, 0.0f},   {"ee", 0.25f, 1.00f, 0.0f},
    {"oh", 0.55f, -0.70f, 0.0f},  {"oo", 0.30f, -1.00f, 0.0f},
    {"ss", 0.15f, 0.55f, 0.0f},   {"mm", 0.00f, 0.00f, 1.0f},
};
#define SHAPE_POINT_COUNT ((int)(sizeof SHAPE_POINTS / sizeof SHAPE_POINTS[0]))

static const ShapePoint *shape_point(const char *name) {
  for (int i = 0; i < SHAPE_POINT_COUNT; i++)
    if (strcmp(SHAPE_POINTS[i].name, name) == 0) return &SHAPE_POINTS[i];
  return NULL;
}

static Model load_model(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) die(path);
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *blob = malloc((size_t)size);
  if (!blob || fread(blob, 1, (size_t)size, f) != (size_t)size) die("reading model");
  fclose(f);

  bool v2 = memcmp(blob, "HLM2", 4) == 0;
  if (!v2 && memcmp(blob, "HLM1", 4) != 0) {
    fprintf(stderr, "holly: %s is not an HLM blob — run scripts/bake-model.mjs\n", path);
    exit(1);
  }

  Model m = {0};
  m.blob = blob;
  memcpy(&m.vertex_count, blob + 4, 4);
  memcpy(&m.index_count, blob + 8, 4);
  memcpy(&m.tex_w, blob + 12, 4);
  memcpy(&m.tex_h, blob + 16, 4);
  memcpy(&m.min_y, blob + 20, 4);
  memcpy(&m.height, blob + 24, 4);
  memcpy(&m.center_x, blob + 28, 4);
  memcpy(&m.half_width, blob + 32, 4);

  size_t at = 36;
  if (v2) {
    memcpy(&m.morph_count, blob + at, 4);
    at += 4;
    if (m.morph_count > MAX_MORPHS) {
      fprintf(stderr, "holly: %u morph targets, only %d supported\n", m.morph_count, MAX_MORPHS);
      exit(1);
    }
    for (uint32_t i = 0; i < m.morph_count; i++) {
      memcpy(m.morph_names[i], blob + at, 16);
      m.morph_names[i][15] = 0;
      at += 16;
    }
  }

  m.vertices = blob + at;
  m.indices = m.vertices + (size_t)m.vertex_count * 32;
  uint8_t *after = m.indices + (size_t)m.index_count * 2;
  for (uint32_t i = 0; i < m.morph_count; i++) {
    m.morphs[i] = after;
    after += (size_t)m.vertex_count * 12;
  }
  m.texels = after;
  return m;
}

/**
 * How much of each shape, given a point in (open, wide, seal) space.
 *
 * Inverse squared distance to each shape's canonical point, normalised. The
 * rest pose takes part in the normalisation but has no delta of its own, so it
 * acts as the thing everything relaxes toward — at (0,0,0) it wins outright and
 * every morph weight falls to nearly nothing, which is exactly a closed face.
 */
static void shape_weights(const Model *m, float open, float wide, float seal, float *out) {
  float w[MAX_MORPHS] = {0};
  float total = 0;

  const ShapePoint *rest = shape_point("rest");
  float dr = (open - rest->open) * (open - rest->open) + (wide - rest->wide) * (wide - rest->wide) +
             (seal - rest->seal) * (seal - rest->seal);
  total += 1.0f / (dr + 0.02f);

  for (uint32_t i = 0; i < m->morph_count; i++) {
    const ShapePoint *p = shape_point(m->morph_names[i]);
    if (!p) continue; // a name we have no opinion about simply never fires
    float d = (open - p->open) * (open - p->open) + (wide - p->wide) * (wide - p->wide) +
              (seal - p->seal) * (seal - p->seal);
    w[i] = 1.0f / (d + 0.02f);
    total += w[i];
  }

  for (uint32_t i = 0; i < m->morph_count; i++) out[i] = total > 0 ? w[i] / total : 0;
  for (uint32_t i = m->morph_count; i < MAX_MORPHS; i++) out[i] = 0;
}

// ----------------------------------------------------------- interior ----

// A dental arch, as a slice of a torus. A straight bar across the mouth reads
// as a letterbox however dim it is, because nothing on a face is straight.
static int build_arch(float *verts, uint16_t *idx, int base, float cx, float cy, float cz,
                      float radius, float tube, float arc, bool down) {
  const int RING = 5, SEG = 14;
  int v = 0, i = 0;
  float start = (down ? -(float)M_PI / 2 : (float)M_PI / 2) - arc / 2;
  for (int s = 0; s <= SEG; s++) {
    float u = start + arc * (float)s / SEG;
    for (int r = 0; r < RING; r++) {
      float rr = 2 * (float)M_PI * (float)r / RING;
      float t = radius + tube * cosf(rr);
      verts[v++] = cx + t * cosf(u);
      verts[v++] = cy + t * sinf(u) + (down ? radius : -radius);
      verts[v++] = cz + tube * sinf(rr);
    }
  }
  for (int s = 0; s < SEG; s++)
    for (int r = 0; r < RING; r++) {
      int a = base + s * RING + r, b = base + s * RING + (r + 1) % RING;
      int c = a + RING, d = b + RING;
      idx[i++] = (uint16_t)a; idx[i++] = (uint16_t)b; idx[i++] = (uint16_t)c;
      idx[i++] = (uint16_t)b; idx[i++] = (uint16_t)d; idx[i++] = (uint16_t)c;
    }
  return i;
}

static int build_box(float *verts, uint16_t *idx, int base, float cx, float cy, float cz,
                     float hx, float hy, float hz) {
  const float corner[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                              {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
  const uint16_t face[36] = {0,1,2, 0,2,3, 5,4,7, 5,7,6, 4,0,3, 4,3,7,
                             1,5,6, 1,6,2, 3,2,6, 3,6,7, 4,5,1, 4,1,0};
  for (int k = 0; k < 8; k++) {
    verts[k * 3 + 0] = cx + corner[k][0] * hx;
    verts[k * 3 + 1] = cy + corner[k][1] * hy;
    verts[k * 3 + 2] = cz + corner[k][2] * hz;
  }
  for (int k = 0; k < 36; k++) idx[k] = (uint16_t)(base + face[k]);
  return 36;
}

// -------------------------------------------------------------- the rig ----
// Measured, not guessed. lipY came from raycasting a column down the rendered
// face and reading back model-space height; the rest were tuned by eye against
// the actual tube. See the README.

static const float RIG_LIP_Y = 0.327f, RIG_FALLOFF = 0.06f, RIG_LIP_HALF = 0.04f;
static const float RIG_PIVOT_Y = 0.52f, RIG_PIVOT_Z = -0.18f, RIG_ANGLE = 0.26f;
static const float RIG_DROP = 0.02f, RIG_MOUTH_DARK = 0.9f, RIG_APERTURE = 0.022f;
static const float RIG_MOUTH_W = 0.32f, RIG_ROUND = 0.035f, RIG_SPREAD = 0.025f;
static const float RIG_PRESS = 0.012f;
static const float RIG_EDGE = 0.05f, RIG_EDGE_SHARP = 1.5f, RIG_LIFT = 1.1f;
static const float CAMERA_Z = 5.4f;
