/* gl_probe.c -- what the PC's graphics path really offers through SDL + ANGLE, the way the engine creates its context.
 * Build (mingw): gcc -O2 -o gl_probe.exe gl_probe.c -Dmain=SDL_main -lmingw32 -lSDL2main -lSDL2 -lEGL -lGLESv2
 * Run from reva_native so the ANGLE DLLs resolve; opens a hidden window, prints, exits. */
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

static void probe(int major,int minor){
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,minor);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_Window*w=SDL_CreateWindow("probe",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    if(!w){ printf("ES %d.%d: window failed: %s\n",major,minor,SDL_GetError()); return; }
    SDL_GLContext c=SDL_GL_CreateContext(w);
    if(!c){ printf("ES %d.%d: no context: %s\n",major,minor,SDL_GetError()); SDL_DestroyWindow(w); return; }
    printf("\n=== asked for ES %d.%d\nGL_VERSION  %s\nGL_RENDERER %s\nGLSL        %s\n",major,minor,glGetString(GL_VERSION),glGetString(GL_RENDERER),glGetString(GL_SHADING_LANGUAGE_VERSION));
    GLint v=0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&v); printf("max texture %d\n",v);
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS,&v); printf("vertex uniform vec4s %d\n",v);
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS,&v); printf("fragment uniform vec4s %d\n",v);
    if(major>=3){
        glGetIntegerv(GL_MAX_SAMPLES,&v); printf("max MSAA samples %d\n",v);
        glGetIntegerv(GL_MAX_DRAW_BUFFERS,&v); printf("max draw buffers %d\n",v);
        glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS,&v); printf("max colour attachments %d\n",v);
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS,&v); printf("uniform buffer bindings %d\n",v);
        glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE,&v); printf("uniform block bytes %d\n",v);
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS,&v); printf("array texture layers %d\n",v);
        glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE,&v); printf("cube map size %d\n",v);
    }
    if(major==3&&minor==1){ GLint g[3]={0,0,0}; glGetIntegeri_v(0x91BE,0,&g[0]); printf("compute work groups (x) %d, gl error 0x%x\n",g[0],glGetError()); }
    const char*ext=(const char*)glGetString(GL_EXTENSIONS);
    const char*want[]={"GL_EXT_color_buffer_float","GL_EXT_color_buffer_half_float","GL_OES_texture_float_linear","GL_EXT_texture_filter_anisotropic","GL_EXT_sRGB","GL_EXT_sRGB_write_control","GL_OES_depth_texture","GL_EXT_shadow_samplers",
        "GL_EXT_texture_compression_s3tc","GL_EXT_texture_compression_dxt1","GL_ANGLE_texture_compression_dxt5","GL_EXT_texture_compression_bptc","GL_EXT_texture_compression_astc_hdr","GL_KHR_texture_compression_astc_ldr","GL_OES_compressed_ETC2_RGB8_texture",
        "GL_ANGLE_instanced_arrays","GL_EXT_disjoint_timer_query","GL_EXT_multisampled_render_to_texture","GL_EXT_shader_framebuffer_fetch","GL_ANGLE_base_vertex_base_instance","GL_ANGLE_multi_draw","GL_EXT_clip_cull_distance","GL_EXT_shader_texture_lod","GL_EXT_frag_depth",NULL};
    for(int i=0;want[i];i++) printf("%-46s %s\n",want[i],(ext&&strstr(ext,want[i]))?"yes":"no");
    SDL_GL_DeleteContext(c); SDL_DestroyWindow(w);
}
int main(int argc,char**argv){
    (void)argc;(void)argv;
    SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER,"1");
    if(SDL_Init(SDL_INIT_VIDEO)){ printf("SDL: %s\n",SDL_GetError()); return 1; }
    probe(2,0);
    probe(3,0);
    probe(3,1);
    probe(3,2);
    SDL_Quit();
    fflush(stdout);
    return 0;
}
