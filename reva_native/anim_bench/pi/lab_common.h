/* lab_common.h -- what v3d_lab2.c (GL) and vk_lab.c (Vulkan) share, so the two APIs draw the same work: the meshes, the placement,
 * the mip chain, the bone poses and the report line. */
#ifndef LAB_COMMON_H
#define LAB_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>

typedef struct { float p[3],n[3],t[2]; unsigned char j[4],w[4]; } LV;   /* 40 bytes, the attribute layout on both APIs */
typedef struct { LV*v; uint32_t*ix; int nv,ni; } LMesh;
typedef struct { unsigned char*px; int w,h; } LTex;

static double labMs(clockid_t c){ struct timespec t; clock_gettime(c,&t); return t.tv_sec*1000.0+t.tv_nsec/1e6; }

static LMesh labGrid(int G){   /* an indexed grid with a soft bump; 32 bones along y, four weights */
    LMesh m; m.nv=(G+1)*(G+1); m.ni=G*G*6; m.v=malloc(sizeof(LV)*m.nv); m.ix=malloc(4*m.ni);
    for(int y=0;y<=G;y++) for(int x=0;x<=G;x++){ LV*q=&m.v[y*(G+1)+x]; float u=(float)x/G*2-1,t=(float)y/G*2-1,z=0.5f*(1-u*u)*(1-t*t);
        q->p[0]=u; q->p[1]=t; q->p[2]=z; float l=sqrtf(u*u+t*t+1); q->n[0]=u/l; q->n[1]=t/l; q->n[2]=1/l; q->t[0]=(float)x/G; q->t[1]=(float)y/G;
        int b=(int)((t*0.5f+0.5f)*30.99f); q->j[0]=b; q->j[1]=b+1; q->j[2]=(b+2)%32; q->j[3]=(b+3)%32; q->w[0]=128; q->w[1]=64; q->w[2]=40; q->w[3]=23; }
    int k=0; for(int y=0;y<G;y++) for(int x=0;x<G;x++){ uint32_t a=y*(G+1)+x,b=a+1,c=a+G+1,d=c+1; m.ix[k++]=a; m.ix[k++]=b; m.ix[k++]=c; m.ix[k++]=b; m.ix[k++]=d; m.ix[k++]=c; }
    return m;
}
static int labLoad(const char*path,LMesh*m,LTex*t){   /* a real body, from export_lab_mesh.py */
    FILE*f=fopen(path,"rb"); if(!f){ printf("cannot open %s\n",path); return 0; }
    char mg[4]; uint32_t h[4]; if(fread(mg,1,4,f)!=4||memcmp(mg,"LAB1",4)||fread(h,4,4,f)!=4){ fclose(f); return 0; }
    m->nv=h[0]; m->ni=h[1]; t->w=h[2]; t->h=h[3];
    m->v=malloc(sizeof(LV)*m->nv); m->ix=malloc(4*m->ni); t->px=malloc((size_t)t->w*t->h*4);
    size_t a=fread(m->v,sizeof(LV),m->nv,f),b=fread(m->ix,4,m->ni,f),c=fread(t->px,4,(size_t)t->w*t->h,f); fclose(f);
    printf("[lab] body %s: %d vertices, %d indices, texture %dx%d\n",path,m->nv,m->ni,t->w,t->h);
    return a==(size_t)m->nv&&b==(size_t)m->ni&&c==(size_t)t->w*t->h;
}
/* one mip level down, a box filter; returns a malloc'd level, or NULL at 1x1 */
static unsigned char*labMipDown(const unsigned char*s,int w,int h,int*nw,int*nh){
    if(w<=1&&h<=1) return NULL; int W=w>1?w/2:1,H=h>1?h/2:1; unsigned char*d=malloc((size_t)W*H*4);
    for(int y=0;y<H;y++) for(int x=0;x<W;x++) for(int c=0;c<4;c++){ int x1=x*2<w?x*2:w-1,x2=x*2+1<w?x*2+1:w-1,y1=y*2<h?y*2:h-1,y2=y*2+1<h?y*2+1:h-1;
        d[(y*W+x)*4+c]=(unsigned char)((s[(y1*w+x1)*4+c]+s[(y1*w+x2)*4+c]+s[(y2*w+x1)*4+c]+s[(y2*w+x2)*4+c]+2)/4); }
    *nw=W; *nh=H; return d;
}
static float labBoneV[768*4];   /* eight poses of 32 bones, three vec4 a bone */
static void labBones(float t){ for(int p=0;p<8;p++) for(int b=0;b<32;b++){ float a=0.08f*sinf(t+b*0.3f+p),s=sinf(a),co=cosf(a); float*r=&labBoneV[(p*96+b*3)*4];
        r[0]=co; r[1]=-s; r[2]=0; r[3]=0; r[4]=s; r[5]=co; r[6]=0; r[7]=0; r[8]=0; r[9]=0; r[10]=1; r[11]=0; } }
/* a grid of N cells over clip space: where body i stands */
static void labPlace(int i,int N,float*x,float*y,float*s){ int side=(int)ceilf(sqrtf((float)N)); float cell=2.0f/side; *x=-1+cell*(i%side+0.5f); *y=-1+cell*(i/side+0.5f); *s=cell*0.45f; }

/* the GPU's own books: time and jobs per queue, from the kernel. Bin is the vertex side, render the pixel side, and neither includes waiting */
static double labG0[2],labG1[2]; static long labJ0[2],labJ1[2];
static void labGpuSnap(int which){ FILE*f=fopen("/sys/devices/platform/v3dbus/fec00000.v3d/gpu_stats","r"); char q[32]; unsigned long long ts,jobs,rt; char line[256]; if(!f) return;
    while(fgets(line,sizeof line,f)){ if(sscanf(line,"%31s %llu %llu %llu",q,&ts,&jobs,&rt)!=4) continue; int k=!strcmp(q,"bin")?0:!strcmp(q,"render")?1:-1; if(k<0) continue;
        if(which==0){ labG0[k]=rt/1e6; labJ0[k]=(long)jobs; } else { labG1[k]=rt/1e6; labJ1[k]=(long)jobs; } } fclose(f); }
/* a live run's books: every flip's gap by the kernel's clock */
static long labFlips,labLate,labLate2; static double labLastFlip,labMaxGap,labFieldMs=16.68;
static void labFlipMark(unsigned sec,unsigned usec){ double t=sec*1000.0+usec/1000.0; if(labLastFlip>0.0){ double g=t-labLastFlip; labFlips++; if(g>labMaxGap) labMaxGap=g; if(g>labFieldMs*1.5) labLate++; if(g>labFieldMs*2.5) labLate2++; } labLastFlip=t; }
static void labLiveStart(void){ labFlips=labLate=labLate2=0; labLastFlip=labMaxGap=0.0; labGpuSnap(0); }
static void labLiveReport(const char*name,double seconds,double cpuMsPerFrame){ labGpuSnap(1); double fr=labFlips>0?(double)labFlips:1.0;
    printf("LIVE %-21s %5ld flips in %.1f s, LATE %ld (2+ fields %ld), worst gap %.1f ms | cpu %.2f | gpu bin %.2f render %.2f ms a frame, busy bin %.0f%% render %.0f%%\n",name,labFlips,seconds,labLate,labLate2,labMaxGap,cpuMsPerFrame,(labG1[0]-labG0[0])/fr,(labG1[1]-labG0[1])/fr,(labG1[0]-labG0[0])/(seconds*10.0),(labG1[1]-labG0[1])/(seconds*10.0)); fflush(stdout); }
static int labCmp(const void*a,const void*b){ double x=*(const double*)a,y=*(const double*)b; return x<y?-1:x>y; }
static void labReport(const char*name,double*w,double*c,int n){
    qsort(w,n,sizeof(double),labCmp); qsort(c,n,sizeof(double),labCmp); int o=0; for(int k=0;k<n;k++) if(w[k]>16.6) o++;
    printf("%-26s work %6.2f med %6.2f p95 %6.2f max | cpu %5.2f | gpu bin %5.2f render %5.2f | jobs %4.1f %4.1f | over %d of %d\n",name,w[n/2],w[n*95/100],w[n-1],c[n/2],(labG1[0]-labG0[0])/n,(labG1[1]-labG0[1])/n,(double)(labJ1[0]-labJ0[0])/n,(double)(labJ1[1]-labJ0[1])/n,o,n); fflush(stdout);
}
#endif
