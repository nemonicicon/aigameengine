/* ---- 2D (11-03, engine plan Phase 6) -------------------------------------------------------------------------------------------------------------------------------------
 * Sprites on a canvas of 360 x 240 that every display agrees on. On composite each canvas pixel is two raster pixels wide and one tall at 240p, two by two at 480i, so nothing ever scales by a fraction,
 * and with the standard's 15:11 picture a canvas pixel is nearly square. A window gets the biggest whole square scale that fits, in the middle. Sprites keep to the 80% box if they must be read, and a game can
 * draw to the canvas's edges for what doesn't matter. Images are loaded once and sampled nearest. This is a Vulkan feature: GL stays for film and the old doors, and only draws the flat rectangles. */
#define SPR_W 360
#define SPR_H 240
#define SPR_MAXV (6*4096)
#define SPR_MAXI 64
static int g_sprSx=1,g_sprSy=1,g_sprX0=0,g_sprY0=0,g_sprW=0,g_sprH=0;
/* an image's texture, its size, where it sits in that texture and how big that texture is, and its pixels until they're on the GPU */
static struct { unsigned tex; int w,h,x,y,tw,th; unsigned char*px; } g_sprImg[SPR_MAXI]; static int g_sprNI=1,g_sprPacked=0;
static float g_sprV[SPR_MAXV*8]; static int g_sprN=0; static unsigned g_sprTex=0;
static float g_flatClear[3]={0.f,0.f,0.f};
static void sprInit(int W,int H,int composite){ g_sprW=W; g_sprH=H; g_sprSx=W/SPR_W; g_sprSy=H/SPR_H;
    if(g_sprSx<1) g_sprSx=1;
    if(g_sprSy<1) g_sprSy=1;
    if(!composite){ int s=g_sprSx<g_sprSy?g_sprSx:g_sprSy; g_sprSx=g_sprSy=s; }
    g_sprX0=(W-SPR_W*g_sprSx)/2; g_sprY0=(H-SPR_H*g_sprSy)/2; }
static void sprFlush(void){ if(!g_sprN) return;
    if(!g_noDraw) gfxSprites(g_sprTex,g_sprV,g_sprN,(float)g_sprW,(float)g_sprH);
    g_sprN=0; }
/* an image on the GPU by itself */
static unsigned sprTexture(const unsigned char*px,int w,int h){ unsigned t; t = gfxTextureNew();  gfxTexture(t, w, h, px);
    gfxTextureNear(t);
    return t; }
static void sprUpload(int i){ g_sprImg[i].tex=sprTexture(g_sprImg[i].px,g_sprImg[i].w,g_sprImg[i].h); stbi_image_free(g_sprImg[i].px); g_sprImg[i].px=NULL; }
/* ---- A SHEET (engine plan, Phase 2: atlases). A change of texture breaks a batch of sprites, so a game's small images go on one sheet, packed at the first draw once boot() has loaded them. A game can still make its
 * own sheet, as games/rocks does: one image alone stays as it is, as does one too big to share. A gap of a pixel keeps a neighbour from showing. What's loaded after the first draw has a texture of its own. */
#define SPR_SHEET 1024
#define SPR_SHARE 256
static void sprPack(void){ g_sprPacked=1; int want=0;
    for(int i=1;i<g_sprNI;i++) if(g_sprImg[i].px&&g_sprImg[i].w<=SPR_SHARE&&g_sprImg[i].h<=SPR_SHARE) want++;
    if(want>=2){ unsigned char*sheet=(unsigned char*)calloc(SPR_SHEET*SPR_SHEET,4);
        if(sheet){ int cx=1,cy=1,rowH=0; unsigned t=0; int put[SPR_MAXI],nPut=0;
            for(int i=1;i<g_sprNI;i++){ int w=g_sprImg[i].w,h=g_sprImg[i].h; if(!g_sprImg[i].px||w>SPR_SHARE||h>SPR_SHARE) continue;
                if(cx+w+1>SPR_SHEET){ cx=1; cy+=rowH+1; rowH=0; }
                if(cy+h+1>SPR_SHEET) continue;
                for(int y=0;y<h;y++) memcpy(sheet+((size_t)(cy+y)*SPR_SHEET+cx)*4,g_sprImg[i].px+(size_t)y*w*4,(size_t)w*4);
                g_sprImg[i].x=cx; g_sprImg[i].y=cy; g_sprImg[i].tw=g_sprImg[i].th=SPR_SHEET; put[nPut++]=i; cx+=w+1; if(h>rowH) rowH=h; }
            if(nPut){ t=sprTexture(sheet,SPR_SHEET,SPR_SHEET); for(int k=0;k<nPut;k++){ g_sprImg[put[k]].tex=t; stbi_image_free(g_sprImg[put[k]].px); g_sprImg[put[k]].px=NULL; } printf("[2d] %d images on one sheet\n",nPut); fflush(stdout); }
            free(sheet); } }
    for(int i=1;i<g_sprNI;i++) if(g_sprImg[i].px) sprUpload(i); }
/* an image file from the game's folder, or 0 and a loud line */
static int sprImage(const char*path){ int w,h,c; if(g_sprNI>=SPR_MAXI){ printf("[2d] more than %d images\n",SPR_MAXI); return 0; }
    clubOpened(path); stbi_set_flip_vertically_on_load(0); unsigned char*px=stbi_load(path,&w,&h,&c,4);
    if(!px){ printf("[2d] can't load %s\n",path); fflush(stdout); return 0; }
    int i=g_sprNI++; g_sprImg[i].w=g_sprImg[i].tw=w; g_sprImg[i].h=g_sprImg[i].th=h; g_sprImg[i].x=g_sprImg[i].y=0; g_sprImg[i].px=px; g_sprImg[i].tex=0;
    if(g_sprPacked) sprUpload(i);
    return i; }
/* x y w h on the canvas; u0 v0 u1 v1 in the image's own pixels */
static void sprDraw(int im,float x,float y,float w,float h,float u0,float v0,float u1,float v1,float r,float g,float b,float a){
    if(im<1||im>=g_sprNI) return;
    if(!g_sprPacked) sprPack();
    if(g_sprImg[im].tex!=g_sprTex||g_sprN+6>SPR_MAXV){ sprFlush(); g_sprTex=g_sprImg[im].tex; }
    float iw=1.f/(float)g_sprImg[im].tw, ih=1.f/(float)g_sprImg[im].th;
    if(g_sprImg[im].tw!=g_sprImg[im].w){ float wI=(float)g_sprImg[im].w,hI=(float)g_sprImg[im].h,xI=(float)g_sprImg[im].x,yI=(float)g_sprImg[im].y;   /* on a sheet, stay inside its own rectangle */
        u0=(u0<0.f?0.f:u0>wI?wI:u0)+xI; u1=(u1<0.f?0.f:u1>wI?wI:u1)+xI; v0=(v0<0.f?0.f:v0>hI?hI:v0)+yI; v1=(v1<0.f?0.f:v1>hI?hI:v1)+yI; }
    float x0=(float)g_sprX0+x*(float)g_sprSx, y0=(float)g_sprY0+y*(float)g_sprSy, x1=x0+w*(float)g_sprSx, y1=y0+h*(float)g_sprSy;
    float Q[4][4]={{x0,y0,u0*iw,v0*ih},{x1,y0,u1*iw,v0*ih},{x1,y1,u1*iw,v1*ih},{x0,y1,u0*iw,v1*ih}};
    static const int I[6]={0,1,2,0,2,3}; float*o=g_sprV+g_sprN*8;
    for(int k=0;k<6;k++){ memcpy(o,Q[I[k]],16); o[4]=r; o[5]=g; o[6]=b; o[7]=a; o+=8; }
    g_sprN+=6; }
static void sprRect(float x,float y,float w,float h,float r,float g,float b,float a){ float v[12];
    float x0=(float)g_sprX0+x*(float)g_sprSx, y0=(float)g_sprY0+y*(float)g_sprSy; tvRect(v,x0,y0,x0+w*(float)g_sprSx,y0+h*(float)g_sprSy); hudTris(v,6,(float)g_sprW,(float)g_sprH,r,g,b,a); }
static void sprClear(float r,float g,float b){ g_flatClear[0]=r; g_flatClear[1]=g; g_flatClear[2]=b; }
