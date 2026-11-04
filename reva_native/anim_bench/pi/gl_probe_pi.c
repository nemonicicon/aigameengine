/* gl_probe_pi.c -- what Mesa offers on the Pi 4's GPU, on the render node, with no display and no effect on the TV.
 * Build: gcc -O2 -o gl_probe_pi gl_probe_pi.c -lgbm -lEGL -lGLESv2 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>

int main(void){
    int fd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC); if(fd<0){ perror("renderD128"); return 1; }
    struct gbm_device*g=gbm_create_device(fd); if(!g){ printf("no gbm\n"); return 1; }
    EGLDisplay d=eglGetDisplay((EGLNativeDisplayType)g); if(!eglInitialize(d,NULL,NULL)){ printf("egl init failed\n"); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint at[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig cf=0; EGLint n=0; eglChooseConfig(d,at,&cf,1,&n);
    if(n<1){ EGLint at2[]={EGL_RENDERABLE_TYPE,0x40,EGL_NONE}; eglChooseConfig(d,at2,&cf,1,&n); }
    EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,1,EGL_NONE};
    EGLContext c=eglCreateContext(d,cf,EGL_NO_CONTEXT,ca); if(!c){ printf("no ES 3.1 context\n"); return 1; }
    EGLint pb[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE}; EGLSurface s=eglCreatePbufferSurface(d,cf,pb);
    if(!eglMakeCurrent(d,s,s,c) && !eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c)){ printf("makecurrent failed\n"); return 1; }
    printf("GL_VERSION  %s\nGL_RENDERER %s\nGLSL        %s\n",glGetString(GL_VERSION),glGetString(GL_RENDERER),glGetString(GL_SHADING_LANGUAGE_VERSION));
    GLint v=0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&v); printf("max texture %d\n",v);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS,&v); printf("vertex uniform vec4s %d\n",v);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS,&v); printf("fragment uniform vec4s %d\n",v);
    glGetIntegerv(GL_MAX_SAMPLES,&v); printf("max MSAA samples %d\n",v);
    glGetIntegerv(GL_MAX_DRAW_BUFFERS,&v); printf("max draw buffers %d\n",v);
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS,&v); printf("uniform buffer bindings %d\n",v);
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE,&v); printf("uniform block bytes %d\n",v);
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS,&v); printf("array texture layers %d\n",v);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,0,&v); printf("compute work groups (x) %d, gl error 0x%x\n",v,glGetError());
    const char*ext=(const char*)glGetString(GL_EXTENSIONS);
    const char*want[]={"GL_EXT_color_buffer_float","GL_EXT_color_buffer_half_float","GL_OES_texture_float_linear","GL_EXT_texture_filter_anisotropic","GL_EXT_sRGB","GL_EXT_sRGB_write_control","GL_EXT_texture_sRGB_decode",
        "GL_EXT_texture_compression_s3tc","GL_EXT_texture_compression_dxt1","GL_ANGLE_texture_compression_dxt5","GL_EXT_texture_compression_bptc","GL_KHR_texture_compression_astc_ldr","GL_OES_compressed_ETC2_RGB8_texture",
        "GL_EXT_disjoint_timer_query","GL_EXT_multisampled_render_to_texture","GL_EXT_discard_framebuffer","GL_EXT_shader_framebuffer_fetch","GL_EXT_clip_cull_distance","GL_EXT_shader_texture_lod","GL_EXT_frag_depth","GL_OES_primitive_bounding_box",NULL};
    for(int i=0;want[i];i++) printf("%-46s %s\n",want[i],(ext&&strstr(ext,want[i]))?"yes":"no");
    return 0;
}
