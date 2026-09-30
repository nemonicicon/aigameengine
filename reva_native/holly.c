// Holly, on the tube, with nothing in the way.
//
// No compositor, no browser, no window system. This opens the DRM device,
// takes the composite connector, makes an EGL context on a GBM surface and
// page-flips it at the video clock. On reva that replaces labwc, lightdm and
// Chromium — about 400MB of resident software whose entire contribution was
// putting one triangle mesh on a 720x240 screen.
//
// The mouth arrives as three floats over UDP. There is no handshake to fumble,
// no connection to lose and nothing to reconnect: the frames are idempotent
// state, so a dropped one is corrected 16ms later by the next.
//
// The model is a flat blob produced by scripts/bake-model.mjs. Parsing glTF and
// decoding JPEG is done once on the desktop rather than every boot here.
//
//   cc -O2 -o holly holly.c -ldrm -lgbm -lEGL -lGLESv2 -lm
//   ./holly [udp-port]

#define _GNU_SOURCE
/* [linux-sdl port 2026-09-30] The DRM/GBM half is the STANDALONE renderer only. holly_fbo.c
 * (inside the club) needs the model + shader half on any platform: Windows, or Linux built
 * with -DREVA_SDL, where the club presents through SDL and no KMS device is open. */
#if defined(_WIN32) || defined(REVA_SDL)
#define HOLLY_NO_KMS 1
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#ifndef _WIN32
#include <arpa/inet.h>
#endif
#include <errno.h>
#include <fcntl.h>
#ifndef HOLLY_NO_KMS
#include <gbm.h>
#endif
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include <time.h>
#include <unistd.h>
#ifndef HOLLY_NO_KMS
#include <xf86drm.h>
#endif
#ifndef HOLLY_NO_KMS
#include <xf86drmMode.h>
#endif

#define WANT_W 720
#define WANT_H 240
#define DEFAULT_PORT 9410

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t want_shot = 0;
static void on_signal(int s) { (void)s; running = 0; }
static void on_usr1(int s) { (void)s; want_shot = 1; }

// Taking the display away from Chromium also took away the remote debugger,
// which was the only way to see what reva was actually drawing. kill -USR1
// puts it back: the framebuffer, as a PPM, which scp reads fine.
static void grab_screenshot(int w, int h) {
  uint8_t *px = malloc((size_t)w * h * 4);
  if (!px) return;
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);

  FILE *f = fopen("/tmp/holly-shot.ppm", "wb");
  if (f) {
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    // GL hands back bottom-up; PPM wants top-down.
    for (int y = h - 1; y >= 0; y--)
      for (int x = 0; x < w; x++) fwrite(px + ((size_t)y * w + x) * 4, 1, 3, f);
    fclose(f);
  }
  free(px);
}

static void die(const char *what) {
  fprintf(stderr, "holly: %s: %s\n", what, strerror(errno));
  exit(1);
}

// ---------------------------------------------------------------- maths ----
// Just enough linear algebra. Column-major, to match what GL expects.

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

#ifndef HOLLY_NO_KMS   /* [win32 port 2026-08-21] standalone-only: holly_fbo.c uses the model+shader half offscreen */
// -------------------------------------------------------------- display ----

typedef struct {
  int fd;
  drmModeModeInfo mode;
  uint32_t conn_id, crtc_id;
  drmModeCrtc *saved_crtc;

  struct gbm_device *gbm;
  struct gbm_surface *surface;
  EGLDisplay egl;
  EGLContext ctx;
  EGLSurface egl_surface;

  struct gbm_bo *current_bo;
  uint32_t current_fb;
} Display;

static uint32_t fb_for_bo(int fd, struct gbm_bo *bo) {
  // Cache the framebuffer id on the bo: GBM hands the same buffers round in a
  // small ring, so this ends up allocating two or three and then never again.
  uint32_t *cached = gbm_bo_get_user_data(bo);
  if (cached) return *cached;

  uint32_t handles[4] = {gbm_bo_get_handle(bo).u32, 0, 0, 0};
  uint32_t strides[4] = {gbm_bo_get_stride(bo), 0, 0, 0};
  uint32_t offsets[4] = {0, 0, 0, 0};
  uint32_t fb;
  if (drmModeAddFB2(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo),
                    gbm_bo_get_format(bo), handles, strides, offsets, &fb, 0)) {
    die("drmModeAddFB2");
  }

  cached = malloc(sizeof *cached);
  *cached = fb;
  gbm_bo_set_user_data(bo, cached, NULL);
  return fb;
}

static void display_init(Display *d) {
  // card1 is the vc4 display engine on a Pi 4; card0 is the v3d render node,
  // which has no connectors at all.
  d->fd = open("/dev/dri/card1", O_RDWR | O_CLOEXEC);
  if (d->fd < 0) die("opening /dev/dri/card1");

  drmModeRes *res = drmModeGetResources(d->fd);
  if (!res) die("drmModeGetResources");

  drmModeConnector *conn = NULL;
  for (int i = 0; i < res->count_connectors; i++) {
    drmModeConnector *c = drmModeGetConnector(d->fd, res->connectors[i]);
    if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
      conn = c;
      break;
    }
    if (c) drmModeFreeConnector(c);
  }
  if (!conn) {
    fprintf(stderr, "holly: nothing connected\n");
    exit(1);
  }
  d->conn_id = conn->connector_id;

  // Prefer the progressive 240p mode. 480i is 59.94 FIELDS, so only 29.97 whole
  // frames, and alternate lines shimmer against each other. 240p paints every
  // line every time at a true 60 — which is what these sets were built for.
  int best = -1;
  for (int i = 0; i < conn->count_modes; i++) {
    drmModeModeInfo *mo = &conn->modes[i];
    bool interlaced = (mo->flags & DRM_MODE_FLAG_INTERLACE) != 0;
    if (mo->hdisplay == WANT_W && mo->vdisplay == WANT_H && !interlaced) { best = i; break; }
  }
  if (best < 0) best = 0;
  d->mode = conn->modes[best];
  printf("mode      %s %dx%d@%d%s\n", d->mode.name, d->mode.hdisplay, d->mode.vdisplay,
         d->mode.vrefresh, (d->mode.flags & DRM_MODE_FLAG_INTERLACE) ? "i" : "p");

  drmModeEncoder *enc = drmModeGetEncoder(d->fd, conn->encoder_id);
  d->crtc_id = enc ? enc->crtc_id : res->crtcs[0];
  if (enc) drmModeFreeEncoder(enc);
  d->saved_crtc = drmModeGetCrtc(d->fd, d->crtc_id);

  d->gbm = gbm_create_device(d->fd);
  if (!d->gbm) die("gbm_create_device");
  d->surface = gbm_surface_create(d->gbm, d->mode.hdisplay, d->mode.vdisplay,
                                  GBM_FORMAT_XRGB8888,
                                  GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  if (!d->surface) die("gbm_surface_create");

  // eglGetPlatformDisplay, not eglGetDisplay: the latter has to guess what kind
  // of pointer it was handed, and guesses wrong often enough to matter.
  PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
  d->egl = get_platform_display ? get_platform_display(EGL_PLATFORM_GBM_KHR, d->gbm, NULL)
                                : eglGetDisplay((EGLNativeDisplayType)d->gbm);
  if (!eglInitialize(d->egl, NULL, NULL)) die("eglInitialize");
  eglBindAPI(EGL_OPENGL_ES_API);

  // The config has to agree with the GBM surface on pixel format, and
  // eglChooseConfig will not check that for you — it happily returns something
  // plausible whose EGL_NATIVE_VISUAL_ID is a different FourCC, and then
  // eglCreateWindowSurface fails with a completely unrelated errno. Ask for all
  // of them and match on the visual id.
  //
  // Multisampling matters more than usual here: aliased edges crawl on
  // composite, and MSAA is far cheaper on a Pi than rendering large and scaling.
  EGLint cfg_attr[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                       EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                       EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                       EGL_ALPHA_SIZE, 0, EGL_DEPTH_SIZE, 16, EGL_NONE};
  EGLint total = 0;
  eglChooseConfig(d->egl, cfg_attr, NULL, 0, &total);
  if (total < 1) die("eglChooseConfig found nothing");
  EGLConfig *all = calloc((size_t)total, sizeof *all);
  eglChooseConfig(d->egl, cfg_attr, all, total, &total);

  EGLConfig config = NULL;
  EGLint best_samples = -1;
  for (EGLint i = 0; i < total; i++) {
    EGLint visual = 0, samples = 0;
    eglGetConfigAttrib(d->egl, all[i], EGL_NATIVE_VISUAL_ID, &visual);
    eglGetConfigAttrib(d->egl, all[i], EGL_SAMPLES, &samples);
    if ((uint32_t)visual != GBM_FORMAT_XRGB8888) continue;
    if (samples > 4) continue; // 4x is plenty; 8x costs more than it returns here
    if (samples > best_samples) {
      best_samples = samples;
      config = all[i];
    }
  }
  free(all);
  if (!config) die("no EGL config matching XRGB8888");
  printf("msaa      %dx\n", best_samples > 0 ? best_samples : 1);

  EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  d->ctx = eglCreateContext(d->egl, config, EGL_NO_CONTEXT, ctx_attr);
  if (d->ctx == EGL_NO_CONTEXT) die("eglCreateContext");

  d->egl_surface = eglCreateWindowSurface(d->egl, config, (EGLNativeWindowType)d->surface, NULL);
  if (d->egl_surface == EGL_NO_SURFACE) die("eglCreateWindowSurface");
  eglMakeCurrent(d->egl, d->egl_surface, d->egl_surface, d->ctx);
  printf("gl        %s\n", glGetString(GL_RENDERER));
}

static void page_flip_handler(int fd, unsigned f, unsigned s, unsigned us, void *data) {
  (void)fd; (void)f; (void)s; (void)us;
  *(bool *)data = false;
}

static void display_swap(Display *d) {
  eglSwapBuffers(d->egl, d->egl_surface);
  struct gbm_bo *bo = gbm_surface_lock_front_buffer(d->surface);
  uint32_t fb = fb_for_bo(d->fd, bo);

  if (!d->current_bo) {
    if (drmModeSetCrtc(d->fd, d->crtc_id, fb, 0, 0, &d->conn_id, 1, &d->mode)) die("drmModeSetCrtc");
  } else {
    bool waiting = true;
    int rc = drmModePageFlip(d->fd, d->crtc_id, fb, DRM_MODE_PAGE_FLIP_EVENT, &waiting);
    if (rc) {
      // Worth shouting about. A failed flip silently skips the wait below, so
      // the loop free-runs at a few hundred fps while the screen keeps showing
      // whatever drmModeSetCrtc last put there — busy, smooth-looking, wrong.
      static int complained = 0;
      if (complained++ < 3) fprintf(stderr, "holly: page flip failed: %s\n", strerror(-rc));
    } else {
      // Blocking here is how the render loop gets its clock: the flip completes
      // on vblank, so the loop runs at exactly the video rate with no timer.
      drmEventContext ev = {.version = 2, .page_flip_handler = page_flip_handler};
      while (waiting && running) {
        struct timeval tv = {.tv_sec = 1};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(d->fd, &fds);
        int r = select(d->fd + 1, &fds, NULL, NULL, &tv);
        if (r < 0 && errno == EINTR) continue; // a signal, not a problem
        if (r <= 0) {
          // Never leave a flip outstanding: the next one returns EBUSY and then
          // every one after it does too, which is how one missed event turns
          // into permanent free-running.
          fprintf(stderr, "holly: no flip event within 1s\n");
          break;
        }
        drmHandleEvent(d->fd, &ev);
      }
    }
    gbm_surface_release_buffer(d->surface, d->current_bo);
  }

  d->current_bo = bo;
  d->current_fb = fb;
}

static void display_finish(Display *d) {
  if (d->saved_crtc) {
    drmModeSetCrtc(d->fd, d->saved_crtc->crtc_id, d->saved_crtc->buffer_id, d->saved_crtc->x,
                   d->saved_crtc->y, &d->conn_id, 1, &d->saved_crtc->mode);
    drmModeFreeCrtc(d->saved_crtc);
  }
  if (d->current_bo) gbm_surface_release_buffer(d->surface, d->current_bo);
  eglDestroySurface(d->egl, d->egl_surface);
  eglDestroyContext(d->egl, d->ctx);
  eglTerminate(d->egl);
  gbm_surface_destroy(d->surface);
  gbm_device_destroy(d->gbm);
  close(d->fd);
}

#endif   /* !HOLLY_NO_KMS */
// ------------------------------------------------------------- shaders ----

static const char *HEAD_VS =
    "#version 300 es\n"
    "in vec3 aPos; in vec3 aNormal; in vec2 aUv;\n"
    // Up to eight morph targets as plain attributes. Unbound ones default to
    // zero and their weights are zero too, so the photo-shell path pays for a
    // few multiply-adds and nothing else.
    "in vec3 aM0; in vec3 aM1; in vec3 aM2; in vec3 aM3;\n"
    "in vec3 aM4; in vec3 aM5; in vec3 aM6; in vec3 aM7;\n"
    "uniform float uMorph[8];\n"
    "uniform mat4 uMVP; uniform mat4 uModel;\n"
    "uniform float uOpen, uWide, uSeal;\n"
    "uniform float uLipY, uFalloff, uLipHalf, uDrop;\n"
    "uniform float uPivotY, uPivotZ, uAngle;\n"
    "uniform float uMouthW, uRound, uSpread, uPress;\n"
    "uniform float uMinY, uHeight, uCenterX, uHalfWidth;\n"
    "uniform float uDeform;\n"
    "out vec2 vUv; out vec3 vNormal; out vec2 vFace; out float vMouth;\n"
    "void main() {\n"
    // Real shapes first. When these exist, uDeform is zero and everything
    // below contributes nothing — a rigged head has a jaw of its own and does
    // not want a vertex shader bending its face on top.
    "  vec3 p = aPos\n"
    "    + aM0 * uMorph[0] + aM1 * uMorph[1] + aM2 * uMorph[2] + aM3 * uMorph[3]\n"
    "    + aM4 * uMorph[4] + aM5 * uMorph[5] + aM6 * uMorph[6] + aM7 * uMorph[7];\n"
    "  float h = (aPos.y - uMinY) / uHeight;\n"
    // 0 at the lip line and above, 1 by uFalloff below. The hinge lives
    // entirely UNDER the lips or the nose and cheeks come down with the chin.
    "  float w = 1.0 - smoothstep(uLipY - uFalloff, uLipY, h);\n"
    "  float amt = w * uOpen * (1.0 - uSeal) * uDeform;\n"
    // A real hinge about a real joint: the mandible turns around a point level
    // with the ears and well back, so the chin travels on an arc and moves
    // backwards as it drops. Sliding it straight down is the nutcracker.
    "  vec3 pivot = vec3(uCenterX, uMinY + uPivotY * uHeight, uPivotZ * uHeight);\n"
    "  float ang = amt * uAngle;\n"
    "  vec3 rel = p - pivot;\n"
    "  float ca = cos(ang), sa = sin(ang);\n"
    "  p = pivot + vec3(rel.x, ca * rel.y - sa * rel.z, sa * rel.y + ca * rel.z);\n"
    "  p.y -= amt * uDrop * uHeight;\n"
    // Visemes: a band centred on the lip line, masked to the mouth itself so
    // the cheeks don't come along and narrow his whole face.
    "  float lip = 1.0 - smoothstep(0.0, uLipHalf, abs(h - uLipY));\n"
    "  float dx = (aPos.x - uCenterX) / uHalfWidth;\n"
    "  float mouth = lip * (1.0 - smoothstep(uMouthW, uMouthW + 0.25, abs(dx))) * uDeform;\n"
    "  float purse = max(0.0, -uWide), spread = max(0.0, uWide);\n"
    "  p.x -= dx * mouth * purse * uRound * uHeight;\n"
    "  p.z += mouth * (1.0 - abs(dx)) * purse * uRound * uHeight;\n"
    "  p.x += dx * mouth * spread * uSpread * uHeight;\n"
    "  p.z -= mouth * spread * uSpread * uHeight * 0.3;\n"
    "  p.y += (w - 0.5) * 2.0 * mouth * uSeal * uPress * uHeight;\n"
    "  vUv = aUv;\n"
    "  vNormal = mat3(uModel) * aNormal;\n"
    "  vFace = vec2(h, dx);\n"
    "  vMouth = mouth;\n"
    "  gl_Position = uMVP * vec4(p, 1.0);\n"
    "}\n";

static const char *HEAD_FS =
    "#version 300 es\n"
    // highp, not mediump: vertex shaders default to highp, and GLES insists a
    // uniform declared in both stages agrees on precision or the link fails.
    "precision highp float;\n"
    "in vec2 vUv; in vec3 vNormal; in vec2 vFace; in float vMouth;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uOpen, uSeal, uMouthDark, uAperture, uMouthW, uLipY;\n"
    "uniform float uEdge, uEdgeSharp, uLift, uDeform;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "  vec3 col = texture(uTex, vUv).rgb;\n"
    // Cut a hole. His lips are modelled shut — one continuous sheet of skin —
    // so opening the jaw could only stretch it, and anything behind stayed
    // hidden forever. Keyed on the REST position: the skin to remove is
    // identified by where it started, before the jaw carried it away.
    "  float aH = uAperture * uOpen * uDeform;\n"
    "  if (aH > 0.0005) {\n"
    "    float ax = vFace.y / max(1e-4, uMouthW * 0.92);\n"
    "    float ay = (vFace.x - (uLipY - aH * 0.75)) / max(1e-4, aH * 1.75);\n"
    "    float r = sqrt(ax * ax + ay * ay);\n"
    "    if (r < 1.0) discard;\n"
    // discard is all or nothing and leaves a stepped edge multisampling won't
    // touch. Rolling the skin just outside it into shadow hides that.
    "    col *= mix(0.12, 1.0, smoothstep(1.0, 1.5, r));\n"
    "  }\n"
    "  col *= 1.0 - vMouth * max(uOpen, uSeal * 0.55) * uMouthDark;\n"
    // The floating head. The texture is baked emissive at full strength, so
    // adding lamps cannot make him sit on black — the silhouette has to be
    // taken away instead. Surfaces turning from the camera fall into the
    // background, which is what dark clothes in a dark studio did for the
    // original.
    "  float facing = clamp(normalize(vNormal).z, 0.0, 1.0);\n"
    "  col *= mix(uEdge, 1.0, pow(facing, uEdgeSharp)) * uLift;\n"
    "  fragColor = vec4(col, 1.0);\n"
    "}\n";

static const char *FLAT_VS =
    "#version 300 es\n"
    "in vec3 aPos;\n"
    "uniform mat4 uMVP;\n"
    "void main() { gl_Position = uMVP * vec4(aPos, 1.0); }\n";

static const char *FLAT_FS =
    "#version 300 es\n"
    // highp, not mediump: vertex shaders default to highp, and GLES insists a
    // uniform declared in both stages agrees on precision or the link fails.
    "precision highp float;\n"
    "uniform vec3 uColor;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = vec4(uColor, 1.0); }\n";

static GLuint compile(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    fprintf(stderr, "holly: shader failed:\n%s\n", log);
    exit(1);
  }
  return s;
}

static GLuint link_program(const char *vs, const char *fs) {
  GLuint p = glCreateProgram();
  glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
  glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
  glBindAttribLocation(p, 0, "aPos");
  glBindAttribLocation(p, 1, "aNormal");
  glBindAttribLocation(p, 2, "aUv");
  static const char *morph_attr[MAX_MORPHS] = {"aM0", "aM1", "aM2", "aM3",
                                               "aM4", "aM5", "aM6", "aM7"};
  for (int i = 0; i < MAX_MORPHS; i++) glBindAttribLocation(p, (GLuint)(3 + i), morph_attr[i]);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetProgramInfoLog(p, sizeof log, NULL, log);
    fprintf(stderr, "holly: link failed:\n%s\n", log);
    exit(1);
  }
  return p;
}

#ifndef HOLLY_NO_KMS   /* [win32 port 2026-08-21] standalone-only: holly_fbo.c uses the model+shader half offscreen */
// ---------------------------------------------------------------- mouth ----

// Three floats behind a magic number. Deliberately not JSON: the sender is ours
// and a struct needs no parser at the other end.
#define MOUTH_MAGIC 0x484D3130u /* "HM10" */

typedef struct { float open, wide, seal; } Mouth;

static int mouth_socket(int port) {
  int s = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (s < 0) die("socket");
  struct sockaddr_in addr = {.sin_family = AF_INET,
                             .sin_port = htons((uint16_t)port),
                             .sin_addr.s_addr = htonl(INADDR_ANY)};
  if (bind(s, (struct sockaddr *)&addr, sizeof addr)) die("bind");
  return s;
}

// Drain the socket and keep only the newest. Under load the kernel queue may
// hold several frames; the older ones describe a mouth that has already moved.
static bool mouth_poll(int sock, Mouth *out) {
  uint8_t buf[16];
  bool got = false;
  for (;;) {
    ssize_t n = recv(sock, buf, sizeof buf, 0);
    if (n != (ssize_t)sizeof buf) break;
    uint32_t magic;
    memcpy(&magic, buf, 4);
    if (magic != MOUTH_MAGIC) continue;
    memcpy(&out->open, buf + 4, 4);
    memcpy(&out->wide, buf + 8, 4);
    memcpy(&out->seal, buf + 12, 4);
    got = true;
  }
  return got;
}

#endif   /* !HOLLY_NO_KMS */
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
static const float CAMERA_Z = 5.4f, DISPLAY_ASPECT = 4.0f / 3.0f;

#ifndef HOLLY_NO_KMS   /* [win32 port 2026-08-21] standalone-only: holly_fbo.c uses the model+shader half offscreen */
int main(int argc, char **argv) {
  int port = argc > 1 ? atoi(argv[1]) : DEFAULT_PORT;
  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);
  signal(SIGUSR1, on_usr1);

  const char *model_path = getenv("HOLLY_MODEL");
  Model model = load_model(model_path ? model_path : "holly.hlm");
  printf("model     %u verts, %u tris, %ux%u texture\n", model.vertex_count,
         model.index_count / 3, model.tex_w, model.tex_h);

  Display d = {0};
  display_init(&d);

  GLuint head_prog = link_program(HEAD_VS, HEAD_FS);
  GLuint flat_prog = link_program(FLAT_VS, FLAT_FS);

  GLuint vbo, ebo, tex;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)model.vertex_count * 32, model.vertices, GL_STATIC_DRAW);
  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)model.index_count * 2, model.indices, GL_STATIC_DRAW);

  // One buffer per morph target. Dense rather than sparse: a viseme touches
  // maybe a quarter of the mesh, but sparse deltas can't be vertex attributes,
  // and 29k verts times twelve bytes is a rounding error against the texture.
  GLuint morph_vbo[MAX_MORPHS] = {0};
  for (uint32_t i = 0; i < model.morph_count; i++) {
    glGenBuffers(1, &morph_vbo[i]);
    glBindBuffer(GL_ARRAY_BUFFER, morph_vbo[i]);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)model.vertex_count * 12, model.morphs[i],
                 GL_STATIC_DRAW);
  }
  const bool rigged = model.morph_count > 0;
  if (rigged) {
    printf("morphs    %u:", model.morph_count);
    for (uint32_t i = 0; i < model.morph_count; i++) printf(" %s", model.morph_names[i]);
    printf("\n");
  }

  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)model.tex_w, (GLsizei)model.tex_h, 0, GL_RGB,
               GL_UNSIGNED_BYTE, model.texels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // --- the interior, which the mesh does not have -------------------------
  const float H = model.height, HW = model.half_width;
  const float lip_y = model.min_y + RIG_LIP_Y * H;
  const float mouth_hw = RIG_MOUTH_W * HW;
  // Behind the lips. This mesh's face front sits near z = 0.70 in the baked
  // frame; everything here must clear that including its own half-thickness,
  // or a corner breaks out through his cheek.
  const float lip_z = 0.70f;

  float interior[4096];
  uint16_t interior_idx[4096];
  int vcount = 0, icount = 0;
  int cavity_start = icount;
  icount += build_box(interior + vcount * 3, interior_idx + icount, vcount,
                      model.center_x, lip_y - H * 0.03f, lip_z - H * 0.17f,
                      mouth_hw * 0.85f, H * 0.09f, H * 0.07f);
  int cavity_count = icount - cavity_start;
  vcount += 8;

  int upper_start = icount;
  icount += build_arch(interior + vcount * 3, interior_idx + icount, vcount,
                       model.center_x, lip_y - H * 0.012f, lip_z - H * 0.055f,
                       mouth_hw * 1.55f, H * 0.011f, 0.95f, true);
  int upper_count = icount - upper_start;
  vcount += 15 * 5;

  int lower_start = icount;
  icount += build_arch(interior + vcount * 3, interior_idx + icount, vcount,
                       model.center_x, lip_y - H * 0.05f, lip_z - H * 0.055f,
                       mouth_hw * 1.55f, H * 0.011f, 0.95f, false);
  int lower_count = icount - lower_start;
  vcount += 15 * 5;

  GLuint ivbo, iebo;
  glGenBuffers(1, &ivbo);
  glBindBuffer(GL_ARRAY_BUFFER, ivbo);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vcount * 12, interior, GL_STATIC_DRAW);
  glGenBuffers(1, &iebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)icount * 2, interior_idx, GL_STATIC_DRAW);

  int sock = mouth_socket(port);
  printf("mouth     udp/%d\n\n", port);

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glClearColor(0, 0, 0, 1); // "A disembodied human head on a black background."

  mat4 proj = mat4_perspective(30.0f, DISPLAY_ASPECT, 0.1f, 100.0f);
  mat4 view = mat4_translate(0, 0, -CAMERA_Z);

  Mouth mouth = {0, 0, 0};
  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  uint64_t frames = 0, frames_at_report = 0;
  double last_report = 0;

  while (running) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double t = (double)(now.tv_sec - t0.tv_sec) + (double)(now.tv_nsec - t0.tv_nsec) / 1e9;

    mouth_poll(sock, &mouth);

    // Idle life, so he isn't a corpse between sentences.
    mat4 model_m = mat4_mul(mat4_rot_y(sinf((float)t / 3.4f) * 0.07f),
                            mat4_rot_x(sinf((float)t / 2.6f) * 0.035f));
    model_m.m[13] += sinf((float)t / 2.1f) * 0.012f;
    mat4 mvp = mat4_mul(proj, mat4_mul(view, model_m));

    glViewport(0, 0, d.mode.hdisplay, d.mode.vdisplay);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- interior first, so the head's discard reveals it -----------------
    // Skipped entirely for a rigged head: it brings its own cavity, teeth and
    // tongue, and a second set behind them would poke through.
    if (!rigged) {
    glUseProgram(flat_prog);
    glUniformMatrix4fv(glGetUniformLocation(flat_prog, "uMVP"), 1, GL_FALSE, mvp.m);
    glBindBuffer(GL_ARRAY_BUFFER, ivbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, iebo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void *)0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);

    GLint flat_col = glGetUniformLocation(flat_prog, "uColor");
    // Warm, not black. Pure black is a hole cut in the picture; a mouth is a
    // dark red place.
    glUniform3f(flat_col, 0.078f, 0.043f, 0.043f);
    glDrawElements(GL_TRIANGLES, cavity_count, GL_UNSIGNED_SHORT,
                   (void *)(intptr_t)(cavity_start * 2));

    glUniform3f(flat_col, 0.29f, 0.255f, 0.216f);
    glDrawElements(GL_TRIANGLES, upper_count, GL_UNSIGNED_SHORT,
                   (void *)(intptr_t)(upper_start * 2));

    // The lower arch swings on the same joint the shader turns the chin about.
    float amt = mouth.open * (1.0f - mouth.seal);
    mat4 jaw = mat4_translate(0, model.min_y + RIG_PIVOT_Y * H, RIG_PIVOT_Z * H);
    jaw = mat4_mul(jaw, mat4_rot_x(amt * RIG_ANGLE));
    jaw = mat4_mul(jaw, mat4_translate(0, -(model.min_y + RIG_PIVOT_Y * H) - amt * RIG_DROP * H,
                                       -RIG_PIVOT_Z * H));
    mat4 jaw_mvp = mat4_mul(mvp, jaw);
    glUniformMatrix4fv(glGetUniformLocation(flat_prog, "uMVP"), 1, GL_FALSE, jaw_mvp.m);
    glDrawElements(GL_TRIANGLES, lower_count, GL_UNSIGNED_SHORT,
                   (void *)(intptr_t)(lower_start * 2));
    }

    // --- the head ----------------------------------------------------------
    glUseProgram(head_prog);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void *)12);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)24);

    float weights[MAX_MORPHS];
    shape_weights(&model, mouth.open, mouth.wide, mouth.seal, weights);
    for (int i = 0; i < MAX_MORPHS; i++) {
      GLuint loc = (GLuint)(3 + i);
      if ((uint32_t)i < model.morph_count) {
        glBindBuffer(GL_ARRAY_BUFFER, morph_vbo[i]);
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 12, (void *)0);
      } else {
        // Unbound attributes read a constant, so an absent target contributes
        // a zero vector rather than whatever was last in the register.
        glDisableVertexAttribArray(loc);
        glVertexAttrib3f(loc, 0, 0, 0);
      }
    }
    glUniform1fv(glGetUniformLocation(head_prog, "uMorph"), MAX_MORPHS, weights);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(head_prog, "uTex"), 0);
    glUniformMatrix4fv(glGetUniformLocation(head_prog, "uMVP"), 1, GL_FALSE, mvp.m);
    glUniformMatrix4fv(glGetUniformLocation(head_prog, "uModel"), 1, GL_FALSE, model_m.m);

#define SETF(name, value) glUniform1f(glGetUniformLocation(head_prog, name), (value))
    SETF("uOpen", mouth.open);
    SETF("uWide", mouth.wide);
    SETF("uSeal", mouth.seal);
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
    SETF("uMinY", model.min_y);
    SETF("uHeight", model.height);
    SETF("uCenterX", model.center_x);
    SETF("uHalfWidth", model.half_width);
    // One switch turns off every geometric fake at once. A head with real
    // shapes wants none of them.
    SETF("uDeform", rigged ? 0.0f : 1.0f);
#undef SETF

    // The mesh is doubleSided in the glTF and the aperture shows its inside.
    glDisable(GL_CULL_FACE);
    glDrawElements(GL_TRIANGLES, (GLsizei)model.index_count, GL_UNSIGNED_SHORT, 0);
    glEnable(GL_CULL_FACE);

    // Before the swap, while the back buffer still holds this frame.
    if (want_shot) {
      want_shot = 0;
      grab_screenshot(d.mode.hdisplay, d.mode.vdisplay);
    }

    display_swap(&d);
    frames++;

    // Every five seconds, on its own line. This usually ends up in the journal
    // rather than on a terminal, and a \r progress line there is one endless
    // unterminated record that never gets flushed.
    if (t - last_report >= 5.0) {
      printf("%.1f fps   open %.2f  wide %+.2f  seal %.2f\n",
             (double)(frames - frames_at_report) / (t - last_report), (double)mouth.open,
             (double)mouth.wide, (double)mouth.seal);
      fflush(stdout);
      last_report = t;
      frames_at_report = frames;
    }
  }

  printf("\n");
  display_finish(&d);
  free(model.blob);
  close(sock);
  return 0;
}

#endif   /* !HOLLY_NO_KMS */