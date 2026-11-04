/* gl_null.c -- a graphics library that draws nothing, for the engine's no-display build (-DREVA_HEADLESS).
 * Linked in place of EGL and GLES, so not one call site changes: the sim runs exactly as it does on the glass, on any machine, with no GPU,
 * no window and no vsync. It is how a tape's hash gets compared across machines without the display road in the way (engine plan, Phase 1).
 * Build it with -DKHRONOS_STATIC on Windows, or the headers ask for ANGLE's DLL. */
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
/* -DREVA_VK: the same nothing, but geometry and textures go through to the Vulkan renderer, named by what the engine already calls them (gfx.h). */
#ifdef REVA_VK
#include "gfx.h"
static GLuint g_arrayBuf, g_elemBuf, g_curTex;
#endif

static GLuint g_next = 1;   /* names are never 0: the engine reads 0 as "failed" */
static void names(GLsizei n, GLuint *out) { for (GLsizei i = 0; i < n; i++) out[i] = g_next++; }
/* ⛔ 11-04: a buffer's or a texture's name comes back when it's deleted, the oldest first and only once there's a good few, because the renderer's tables are as long as the highest name. Without this, a world that streams made them longer for as long as it ran */
#define NAME_RING 65536u
typedef struct { GLuint q[NAME_RING]; unsigned r, w; } NameRing;
static NameRing g_freeBuf, g_freeTex;
static void nameBack(NameRing *R, GLuint n) { if (n && R->w - R->r < NAME_RING) R->q[R->w++ % NAME_RING] = n; }
static void namesFrom(NameRing *R, GLsizei n, GLuint *out) { for (GLsizei i = 0; i < n; i++) out[i] = R->w - R->r > 256u ? R->q[R->r++ % NAME_RING] : g_next++; }

EGLint eglGetError(void) { return EGL_SUCCESS; }

void glActiveTexture(GLenum a) { (void)a; }
void glAttachShader(GLuint a, GLuint b) { (void)a; (void)b; }
void glBindAttribLocation(GLuint a, GLuint b, const GLchar *c) { (void)a; (void)b; (void)c; }
#ifdef REVA_VK
void glBindBuffer(GLenum a, GLuint b) { if (a == GL_ARRAY_BUFFER) g_arrayBuf = b; else g_elemBuf = b; }
void glBindTexture(GLenum a, GLuint b) { (void)a; g_curTex = b; }
void glBufferData(GLenum a, GLsizeiptr b, const void *c, GLenum d) { gfxBufferData(a == GL_ARRAY_BUFFER ? g_arrayBuf : g_elemBuf, (long)b, c, d != GL_STATIC_DRAW); }
void glBufferSubData(GLenum a, GLintptr b, GLsizeiptr c, const void *d) { gfxBufferSub(a == GL_ARRAY_BUFFER ? g_arrayBuf : g_elemBuf, (long)b, (long)c, d); }
void glTexImage2D(GLenum a, GLint l, GLint c, GLsizei w, GLsizei h, GLint f, GLenum fmt, GLenum t, const void *px) { (void)a; (void)c; (void)f; if (l == 0 && px && fmt == GL_RGBA && t == GL_UNSIGNED_BYTE) gfxTexture(g_curTex, w, h, px); }
#else
void glBindBuffer(GLenum a, GLuint b) { (void)a; (void)b; }
void glBindTexture(GLenum a, GLuint b) { (void)a; (void)b; }
void glBufferData(GLenum a, GLsizeiptr b, const void *c, GLenum d) { (void)a; (void)b; (void)c; (void)d; }
void glBufferSubData(GLenum a, GLintptr b, GLsizeiptr c, const void *d) { (void)a; (void)b; (void)c; (void)d; }
void glTexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void *i) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; }
#endif
void glBindFramebuffer(GLenum a, GLuint b) { (void)a; (void)b; }
void glBindRenderbuffer(GLenum a, GLuint b) { (void)a; (void)b; }
void glBlendFunc(GLenum a, GLenum b) { (void)a; (void)b; }
GLenum glCheckFramebufferStatus(GLenum a) { (void)a; return GL_FRAMEBUFFER_COMPLETE; }
void glClear(GLbitfield a) { (void)a; }
void glClearColor(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { (void)a; (void)b; (void)c; (void)d; }
void glColorMask(GLboolean a, GLboolean b, GLboolean c, GLboolean d) { (void)a; (void)b; (void)c; (void)d; }
void glCompileShader(GLuint a) { (void)a; }
GLuint glCreateProgram(void) { return g_next++; }
GLuint glCreateShader(GLenum a) { (void)a; return g_next++; }
void glCullFace(GLenum a) { (void)a; }
#ifdef REVA_VK
void glDeleteBuffers(GLsizei a, const GLuint *b) { for (GLsizei i = 0; i < a; i++) { gfxBufferFree(b[i]); nameBack(&g_freeBuf, b[i]); } }
#else
void glDeleteBuffers(GLsizei a, const GLuint *b) { for (GLsizei i = 0; i < a; i++) nameBack(&g_freeBuf, b[i]); }
#endif
void glDeleteProgram(GLuint a) { (void)a; }
#ifdef REVA_VK
void glDeleteTextures(GLsizei a, const GLuint *b) { for (GLsizei i = 0; i < a; i++) { gfxTextureFree(b[i]); nameBack(&g_freeTex, b[i]); } }
#else
void glDeleteTextures(GLsizei a, const GLuint *b) { for (GLsizei i = 0; i < a; i++) nameBack(&g_freeTex, b[i]); }
#endif
void glDepthFunc(GLenum a) { (void)a; }
void glDepthMask(GLboolean a) { (void)a; }
void glDisable(GLenum a) { (void)a; }
void glDisableVertexAttribArray(GLuint a) { (void)a; }
void glDrawArrays(GLenum a, GLint b, GLsizei c) { (void)a; (void)b; (void)c; }
void glDrawElements(GLenum a, GLsizei b, GLenum c, const void *d) { (void)a; (void)b; (void)c; (void)d; }
void glEnable(GLenum a) { (void)a; }
void glEnableVertexAttribArray(GLuint a) { (void)a; }
void glFinish(void) {}
void glFramebufferRenderbuffer(GLenum a, GLenum b, GLenum c, GLuint d) { (void)a; (void)b; (void)c; (void)d; }
void glFramebufferTexture2D(GLenum a, GLenum b, GLenum c, GLuint d, GLint e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void glFrontFace(GLenum a) { (void)a; }
void glGenBuffers(GLsizei n, GLuint *o) { namesFrom(&g_freeBuf, n, o); }
void glGenFramebuffers(GLsizei n, GLuint *o) { names(n, o); }
void glGenRenderbuffers(GLsizei n, GLuint *o) { names(n, o); }
void glGenTextures(GLsizei n, GLuint *o) { namesFrom(&g_freeTex, n, o); }
void glGenerateMipmap(GLenum a) { (void)a; }
GLenum glGetError(void) { return GL_NO_ERROR; }
void glGetFloatv(GLenum a, GLfloat *o) { (void)a; o[0] = 0.f; }
void glGetIntegerv(GLenum a, GLint *o) {
    switch (a) {
    case GL_VIEWPORT: o[0] = o[1] = 0; o[2] = o[3] = 1; break;
    case GL_MAX_TEXTURE_SIZE: case GL_MAX_RENDERBUFFER_SIZE: o[0] = 4096; break;
    case GL_MAX_VERTEX_UNIFORM_VECTORS: case GL_MAX_FRAGMENT_UNIFORM_VECTORS: o[0] = 4095; break;
    case GL_MAX_VARYING_VECTORS: o[0] = 16; break;
    default: o[0] = 0; break;
    }
}
void glGetProgramInfoLog(GLuint a, GLsizei b, GLsizei *c, GLchar *d) { (void)a; (void)b; if (c) *c = 0; if (d) d[0] = 0; }
void glGetProgramiv(GLuint a, GLenum b, GLint *o) { (void)a; (void)b; o[0] = GL_TRUE; }
void glGetShaderInfoLog(GLuint a, GLsizei b, GLsizei *c, GLchar *d) { (void)a; (void)b; if (c) *c = 0; if (d) d[0] = 0; }
void glGetShaderiv(GLuint a, GLenum b, GLint *o) { (void)a; (void)b; o[0] = GL_TRUE; }
const GLubyte *glGetString(GLenum a) { (void)a; return (const GLubyte *)"none (gl_null)"; }
GLint glGetUniformLocation(GLuint a, const GLchar *b) { (void)a; (void)b; return 1; }
void glGetUniformfv(GLuint a, GLint b, GLfloat *o) { (void)a; (void)b; o[0] = 0.f; }
GLboolean glIsEnabled(GLenum a) { (void)a; return GL_FALSE; }
GLboolean glIsTexture(GLuint a) { return a != 0; }
void glLineWidth(GLfloat a) { (void)a; }
void glLinkProgram(GLuint a) { (void)a; }
void glPixelStorei(GLenum a, GLint b) { (void)a; (void)b; }
void glPolygonOffset(GLfloat a, GLfloat b) { (void)a; (void)b; }
void glReadPixels(GLint a, GLint b, GLsizei w, GLsizei h, GLenum c, GLenum d, void *o) { (void)a; (void)b; (void)c; (void)d;
#ifdef REVA_VK
    if (gfxRead(o, w, h)) return;
#endif
    memset(o, 0, (size_t)w * (size_t)h * 4); }
void glRenderbufferStorage(GLenum a, GLenum b, GLsizei c, GLsizei d) { (void)a; (void)b; (void)c; (void)d; }
void glScissor(GLint a, GLint b, GLsizei c, GLsizei d) { (void)a; (void)b; (void)c; (void)d; }
void glShaderSource(GLuint a, GLsizei b, const GLchar *const *c, const GLint *d) { (void)a; (void)b; (void)c; (void)d; }
void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
void glUniform1f(GLint a, GLfloat b) { (void)a; (void)b; }
void glUniform1fv(GLint a, GLsizei b, const GLfloat *c) { (void)a; (void)b; (void)c; }
void glUniform1i(GLint a, GLint b) { (void)a; (void)b; }
void glUniform2f(GLint a, GLfloat b, GLfloat c) { (void)a; (void)b; (void)c; }
void glUniform3f(GLint a, GLfloat b, GLfloat c, GLfloat d) { (void)a; (void)b; (void)c; (void)d; }
void glUniform3fv(GLint a, GLsizei b, const GLfloat *c) { (void)a; (void)b; (void)c; }
void glUniform4f(GLint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void glUniform4fv(GLint a, GLsizei b, const GLfloat *c) { (void)a; (void)b; (void)c; }
void glUniformMatrix4fv(GLint a, GLsizei b, GLboolean c, const GLfloat *d) { (void)a; (void)b; (void)c; (void)d; }
void glUseProgram(GLuint a) { (void)a; }
void glVertexAttrib3f(GLuint a, GLfloat b, GLfloat c, GLfloat d) { (void)a; (void)b; (void)c; (void)d; }
void glVertexAttrib4f(GLuint a, GLfloat b, GLfloat c, GLfloat d, GLfloat e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
void glVertexAttribPointer(GLuint a, GLint b, GLenum c, GLboolean d, GLsizei e, const void *f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void glViewport(GLint a, GLint b, GLsizei c, GLsizei d) { (void)a; (void)b; (void)c; (void)d; }
