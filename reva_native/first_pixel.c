/* first_pixel.c — the native dancer's first frame. 2026-07-22.
 * Renders OFFSCREEN via the DRM render node (/dev/dri/renderD128): no KMS master, no display
 * ownership — the chromium kiosk keeps playing while this proves the whole native GL path:
 *   gbm device -> EGL (surfaceless) -> GLES2 context -> FBO 720x480 -> shader draw -> readback.
 * Output: /tmp/native_first.ppm (P6) + timing + GL_RENDERER on stdout.
 * Build (on the Pi):  gcc -O2 -o first_pixel first_pixel.c -lgbm -lEGL -lGLESv2
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#define W 720
#define H 480

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
static void die(const char *m) { fprintf(stderr, "FATAL: %s (egl 0x%x, gl 0x%x)\n", m, eglGetError(), glGetError()); exit(1); }

static GLuint sh(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); fprintf(stderr, "%s\n", log); die("shader"); }
    return s;
}

int main(void) {
    /* --- context on the render node (no display, no KMS) --- */
    int fd = open("/dev/dri/renderD128", O_RDWR);
    if (fd < 0) die("open renderD128");
    struct gbm_device *gbm = gbm_create_device(fd);
    if (!gbm) die("gbm_create_device");
    PFNEGLGETPLATFORMDISPLAYEXTPROC getDisp =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = getDisp ? getDisp(EGL_PLATFORM_GBM_KHR, gbm, NULL) : EGL_NO_DISPLAY;
    if (dpy == EGL_NO_DISPLAY) die("eglGetPlatformDisplay");
    if (!eglInitialize(dpy, NULL, NULL)) die("eglInitialize");
    eglBindAPI(EGL_OPENGL_ES_API);
    static const EGLint cfg_attr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg; EGLint ncfg = 0;
    eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg);
    static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, ncfg ? cfg : NULL, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) die("eglCreateContext");
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) die("makeCurrent (surfaceless)");
    printf("GL_RENDERER: %s\nGL_VERSION:  %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    /* --- FBO 720x480 --- */
    GLuint fbo, rb, drb;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, W, H); /* try RGBA8 via ext below */
    glGenRenderbuffers(1, &drb); glBindRenderbuffer(GL_RENDERBUFFER, drb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, drb);
    /* prefer 8-bit color if the driver offers it (v3d does) */
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, 0x8058 /*GL_RGBA8_OES*/, W, H);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA4, W, H);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) die("FBO incomplete");
    }
    glViewport(0, 0, W, H);

    /* --- the show: club-dark clear + one hologram-purple triangle + starfield points --- */
    const char *vs =
        "attribute vec2 p; varying vec2 uv;\n"
        "void main(){ uv=p; gl_Position=vec4(p,0.0,1.0); gl_PointSize=2.0; }\n";
    const char *fs =
        "precision mediump float; varying vec2 uv;\n"
        "void main(){ float g=1.0-length(uv)*0.9;\n"
        "  gl_FragColor=vec4(0.72*g+0.18, 0.25*g, 0.85*g+0.10, 1.0); }\n"; /* reva purple */
    GLuint prog = glCreateProgram();
    glAttachShader(prog, sh(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, sh(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(prog, 0, "p");
    glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok); if (!ok) die("link");
    glUseProgram(prog);

    static const GLfloat tri[] = { 0.0f, 0.72f, -0.62f, -0.55f, 0.62f, -0.55f };
    GLfloat stars[512 * 2];
    unsigned s = 12345;
    for (int i = 0; i < 512 * 2; i++) { s = s * 1103515245 + 12345; stars[i] = ((s >> 8) % 2000) / 1000.0f - 1.0f; }

    double t0 = now_ms();
    glClearColor(0.015f, 0.008f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, stars);
    glDrawArrays(GL_POINTS, 0, 512);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    double t1 = now_ms();

    /* --- readback -> PPM --- */
    unsigned char *px = malloc(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    double t2 = now_ms();
    FILE *f = fopen("/tmp/native_first.ppm", "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = H - 1; y >= 0; y--)          /* GL is bottom-up */
        for (int x = 0; x < W; x++) fwrite(px + (y * W + x) * 4, 1, 3, f);
    fclose(f);
    printf("draw+finish: %.2f ms   readback: %.2f ms   -> /tmp/native_first.ppm\n", t1 - t0, t2 - t1);
    return 0;
}
