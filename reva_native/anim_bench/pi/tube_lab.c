/* tube_lab.c -- what the composite output really does, measured from the flip events:
 * exact refresh per mode, how late a flip can be issued, how long a 240p <-> 480i switch takes,
 * and whether alternating flips stay locked to field parity (eyes on the tube for that one).
 * Build: gcc -O2 -o tube_lab tube_lab.c -I/usr/include/libdrm -ldrm    Run as root, console stopped. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

typedef struct { uint32_t fb,handle,pitch; uint32_t*px; int w,h; } Buf;
static volatile int pending; static double evT; static unsigned evSeq;
static double nowMs(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000.0+t.tv_nsec/1e6; }
static void onFlip(int fd,unsigned seq,unsigned sec,unsigned usec,void*d){ (void)fd;(void)d; pending=0; evT=sec*1000.0+usec/1000.0; evSeq=seq; }
static drmEventContext ev;

static Buf mk(int fd,int w,int h,uint32_t col){
    Buf b; memset(&b,0,sizeof b); b.w=w; b.h=h;
    struct drm_mode_create_dumb c; memset(&c,0,sizeof c); c.width=w; c.height=h; c.bpp=32;
    if(drmIoctl(fd,DRM_IOCTL_MODE_CREATE_DUMB,&c)){ perror("create dumb"); exit(1); }
    b.handle=c.handle; b.pitch=c.pitch;
    if(drmModeAddFB(fd,w,h,24,32,c.pitch,c.handle,&b.fb)){ perror("addfb"); exit(1); }
    struct drm_mode_map_dumb m; memset(&m,0,sizeof m); m.handle=c.handle;
    if(drmIoctl(fd,DRM_IOCTL_MODE_MAP_DUMB,&m)){ perror("map dumb"); exit(1); }
    b.px=mmap(0,c.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,m.offset);
    if(b.px==MAP_FAILED){ perror("mmap"); exit(1); }
    for(int y=0;y<h;y++){ uint32_t*r=(uint32_t*)((char*)b.px+y*b.pitch); for(int x=0;x<w;x++) r[x]=col; }
    return b;
}
static int flipWait(int fd,uint32_t crtc,uint32_t fb){
    pending=1;
    if(drmModePageFlip(fd,crtc,fb,DRM_MODE_PAGE_FLIP_EVENT,NULL)){ perror("pageflip"); pending=0; return -1; }
    while(pending){ fd_set f; FD_ZERO(&f); FD_SET(fd,&f); struct timeval tv={1,0};
        int r=select(fd+1,&f,NULL,NULL,&tv); if(r>0) drmHandleEvent(fd,&ev); else { printf("  flip wait timed out\n"); pending=0; return -1; } }
    return 0;
}
static void refresh(int fd,uint32_t crtc,uint32_t conn,drmModeModeInfo*m,Buf*a,Buf*b,const char*name,int n){
    if(drmModeSetCrtc(fd,crtc,a->fb,0,0,&conn,1,m)){ perror("setcrtc"); return; }
    usleep(300000);
    flipWait(fd,crtc,b->fb);
    double t0=evT,prev=evT,mn=1e9,mx=0; unsigned s0=evSeq; int late=0;
    for(int i=0;i<n;i++){ flipWait(fd,crtc,(i&1)?b->fb:a->fb); double g=evT-prev; prev=evT; if(g<mn)mn=g; if(g>mx)mx=g; if(g>25.0)late++; }
    double nominal=m->clock*1000.0/((double)m->htotal*m->vtotal)*((m->flags&DRM_MODE_FLAG_INTERLACE)?2.0:1.0);
    printf("[refresh] %s: clock %u kHz, %u x %u total, %s; nominal %.4f Hz; measured %.4f Hz over %d flips (gap min %.3f max %.3f ms, late %d, vblank seq +%u)\n",
        name,m->clock,m->htotal,m->vtotal,(m->flags&DRM_MODE_FLAG_INTERLACE)?"interlaced":"progressive",nominal,n*1000.0/(evT-t0),n,mn,mx,late,evSeq-s0);
    fflush(stdout);
}
static void deadline(int fd,uint32_t crtc,Buf*a,Buf*b,const char*name,int n){
    printf("[deadline] %s: ms waited after a flip event before issuing the next, then flips that landed on the next vblank\n",name);
    for(int d=0;d<=17;d++){
        int ok=0,miss=0; flipWait(fd,crtc,a->fb); double prev=evT;
        for(int i=0;i<n;i++){ double t=nowMs(); while(nowMs()-t<d){} flipWait(fd,crtc,(i&1)?a->fb:b->fb); double g=evT-prev; prev=evT; if(g<25.0) ok++; else miss++; }
        printf("  wait %2d ms: on time %d, missed %d\n",d,ok,miss); fflush(stdout); }
}
int main(int argc,char**argv){ int quick=(argc>1&&!strcmp(argv[1],"quick"));
    int fd=open("/dev/dri/card1",O_RDWR|O_CLOEXEC); if(fd<0){ perror("open card1"); return 1; }
    drmModeRes*res=drmModeGetResources(fd); if(!res){ perror("resources"); return 1; }
    drmModeConnector*c=NULL;
    for(int i=0;i<res->count_connectors;i++){ drmModeConnector*k=drmModeGetConnector(fd,res->connectors[i]);
        if(k&&k->connector_type==DRM_MODE_CONNECTOR_Composite){ c=k; break; } if(k) drmModeFreeConnector(k); }
    if(!c){ printf("no composite connector\n"); return 1; }
    drmModeModeInfo mI,mP; int hI=0,hP=0;
    for(int i=0;i<c->count_modes;i++){ drmModeModeInfo*m=&c->modes[i];
        printf("[mode] %s clock %u h %u/%u/%u/%u v %u/%u/%u/%u flags 0x%x type 0x%x\n",m->name,m->clock,m->hdisplay,m->hsync_start,m->hsync_end,m->htotal,m->vdisplay,m->vsync_start,m->vsync_end,m->vtotal,m->flags,m->type);
        if(m->hdisplay==720&&m->vdisplay==480&&(m->flags&DRM_MODE_FLAG_INTERLACE)){ mI=*m; hI=1; }
        if(m->hdisplay==720&&m->vdisplay==240&&!(m->flags&DRM_MODE_FLAG_INTERLACE)){ mP=*m; hP=1; } }
    if(!hI||!hP){ printf("need both 720x480i and 720x240\n"); return 1; }
    uint32_t crtc=0;
    if(c->encoder_id){ drmModeEncoder*e=drmModeGetEncoder(fd,c->encoder_id); if(e){ crtc=e->crtc_id; drmModeFreeEncoder(e);} }
    if(!crtc){ for(int i=0;i<c->count_encoders&&!crtc;i++){ drmModeEncoder*e=drmModeGetEncoder(fd,c->encoders[i]); if(!e) continue;
        for(int j=0;j<res->count_crtcs;j++) if(e->possible_crtcs&(1u<<j)){ crtc=res->crtcs[j]; break; } drmModeFreeEncoder(e); } }
    if(!crtc){ printf("no crtc\n"); return 1; }
    uint32_t conn=c->connector_id;
    memset(&ev,0,sizeof ev); ev.version=2; ev.page_flip_handler=onFlip;

    /* 480i is blue, 240p is green, so eyes can tell which mode the tube is showing */
    Buf iA=mk(fd,720,480,0x00102060),iB=mk(fd,720,480,0x00102068);
    Buf pA=mk(fd,720,240,0x00105020),pB=mk(fd,720,240,0x00105820);

    if(!quick){ refresh(fd,crtc,conn,&mI,&iA,&iB,"720x480i",600);
    deadline(fd,crtc,&iA,&iB,"720x480i",30);
    refresh(fd,crtc,conn,&mP,&pA,&pB,"720x240",600);
    deadline(fd,crtc,&pA,&pB,"720x240",30); }

    printf("[switch] four round trips, 1.5 s held each way\n"); fflush(stdout);
    for(int k=0;k<4;k++){
        double t=nowMs(); drmModeSetCrtc(fd,crtc,iA.fb,0,0,&conn,1,&mI); double a=nowMs()-t; flipWait(fd,crtc,iB.fb); double a2=nowMs()-t; usleep(1500000);
        t=nowMs(); drmModeSetCrtc(fd,crtc,pA.fb,0,0,&conn,1,&mP); double b=nowMs()-t; flipWait(fd,crtc,pB.fb); double b2=nowMs()-t; usleep(1500000);
        printf("  to 480i: setcrtc %.1f ms, first flip done %.1f ms | to 240p: setcrtc %.1f ms, first flip done %.1f ms\n",a,a2,b,b2); fflush(stdout); }

    /* Field parity: E has white on even rows, O on odd rows. Alternating them every field shows solid white when
     * each buffer lands on its own field, nearly black when each lands on the other field. One repeated flip swaps it. */
    Buf E=mk(fd,720,480,0),O=mk(fd,720,480,0);
    for(int y=0;y<480;y++){ uint32_t*re=(uint32_t*)((char*)E.px+y*E.pitch),*ro=(uint32_t*)((char*)O.px+y*O.pitch);
        for(int x=0;x<720;x++){ re[x]=(y&1)?0:0x00808080; ro[x]=(y&1)?0x00808080:0; } }
    drmModeSetCrtc(fd,crtc,E.fb,0,0,&conn,1,&mI);
    int i=0;
    for(int phase=0;phase<4;phase++){
        printf("[parity] phase %d for 6 s at %.0f\n",phase,nowMs()/1000.0); fflush(stdout);
        double t=nowMs();
        while(nowMs()-t<6000.0){ flipWait(fd,crtc,(i&1)?O.fb:E.fb); i++; }
        /* repeat the last buffer once: the next phase is shifted by one field */
        flipWait(fd,crtc,((i-1)&1)?O.fb:E.fb);
    }
    printf("[done]\n");
    return 0;
}
