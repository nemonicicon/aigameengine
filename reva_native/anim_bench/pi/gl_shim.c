/* gl_shim.c -- an LD_PRELOAD instrument, no engine rebuild: per-frame GL call counts, exact late frames, CPU share.
 * Build on the Pi: gcc -O2 -shared -fPIC -o gl_shim.so gl_shim.c -ldl -lrt
 * Frame boundary is eglSwapBuffers on the KMS build and SDL_GL_SwapWindow on the SDL build (SDL finds EGL itself).
 * Every 300 frames it prints one line to stderr. Interval unit: SHIM_FIELD_MS (default 16.68, one field). */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long nElems, nArrays, nFbo, nProg, nTex, nMat, nClear;
static unsigned long long nVerts;
static long frames, late1, late2, totalFrames, totalLate;
static double lastT, maxGap, sumGap, sumCpu, lastCpu, fieldMs;

static double nowMs(clockid_t c){ struct timespec t; clock_gettime(c,&t); return t.tv_sec*1000.0+t.tv_nsec/1e6; }

static void frameMark(void){
    double t=nowMs(CLOCK_MONOTONIC), c=nowMs(CLOCK_THREAD_CPUTIME_ID);
    if(fieldMs==0.0){ const char*e=getenv("SHIM_FIELD_MS"); fieldMs=e?atof(e):16.68; }
    if(lastT>0.0){
        double gap=t-lastT; sumGap+=gap; sumCpu+=c-lastCpu; frames++; totalFrames++;
        if(gap>maxGap) maxGap=gap;
        if(gap>fieldMs*1.5){ late1++; totalLate++; }
        if(gap>fieldMs*2.5) late2++;
    }
    lastT=t; lastCpu=c;
    if(frames>=300){
        fprintf(stderr,"[shim] %ld frames: %.1f ms avg, max %.1f, late %ld (2+ fields %ld) | total late %ld of %ld | cpu %.1f ms/frame | per frame: draws %.0f verts %.0f fbo %.1f prog %.1f tex %.1f mat %.1f clear %.1f\n",
            frames,sumGap/frames,maxGap,late1,late2,totalLate,totalFrames,sumCpu/frames,
            (double)(nElems+nArrays)/frames,(double)nVerts/frames,(double)nFbo/frames,(double)nProg/frames,(double)nTex/frames,(double)nMat/frames,(double)nClear/frames);
        frames=late1=late2=0; maxGap=sumGap=sumCpu=0.0; nElems=nArrays=nFbo=nProg=nTex=nMat=nClear=0; nVerts=0;
    }
}

#define REAL(name) static __typeof__(&name) real; if(!real) real=(__typeof__(&name))dlsym(RTLD_NEXT,#name)

typedef unsigned int GLenum,GLuint,GLbitfield; typedef int GLint,GLsizei; typedef unsigned char GLboolean; typedef float GLfloat;
void glDrawElements(GLenum m,GLsizei n,GLenum t,const void*p);
void glDrawArrays(GLenum m,GLint f,GLsizei n);
void glBindFramebuffer(GLenum t,GLuint f);
void glUseProgram(GLuint p);
void glBindTexture(GLenum t,GLuint x);
void glUniformMatrix4fv(GLint l,GLsizei n,GLboolean tr,const GLfloat*v);
void glClear(GLbitfield b);
unsigned int eglSwapBuffers(void*d,void*s);
void SDL_GL_SwapWindow(void*w);

void glDrawElements(GLenum m,GLsizei n,GLenum t,const void*p){ REAL(glDrawElements); nElems++; nVerts+=n; real(m,n,t,p); }
void glDrawArrays(GLenum m,GLint f,GLsizei n){ REAL(glDrawArrays); nArrays++; nVerts+=n; real(m,f,n); }
void glBindFramebuffer(GLenum t,GLuint f){ REAL(glBindFramebuffer); nFbo++; real(t,f); }
void glUseProgram(GLuint p){ REAL(glUseProgram); nProg++; real(p); }
void glBindTexture(GLenum t,GLuint x){ REAL(glBindTexture); nTex++; real(t,x); }
void glUniformMatrix4fv(GLint l,GLsizei n,GLboolean tr,const GLfloat*v){ REAL(glUniformMatrix4fv); nMat+=n; real(l,n,tr,v); }
void glClear(GLbitfield b){ REAL(glClear); nClear++; real(b); }
unsigned int eglSwapBuffers(void*d,void*s){ REAL(eglSwapBuffers); unsigned int r=real(d,s); frameMark(); return r; }
void SDL_GL_SwapWindow(void*w){ REAL(SDL_GL_SwapWindow); real(w); frameMark(); }
