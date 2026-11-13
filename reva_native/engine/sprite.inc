/* ---- 2D (11-03, engine plan Phase 6) -------------------------------------------------------------------------------------------------------------------------------------
 * Sprites on a canvas of 360 x 240 that every display agrees on. On composite each canvas pixel is two raster pixels wide and one tall at 240p, two by two at 480i, so nothing ever scales by a fraction,
 * and with the standard's 15:11 picture a canvas pixel is nearly square. A window gets the biggest whole square scale that fits, in the middle. Sprites keep to the 80% box if they must be read, and a game can
 * draw to the canvas's edges for what doesn't matter. Images are loaded once and sampled nearest. This is a Vulkan feature: GL stays for film and the old doors, and only draws the flat rectangles. */
#define SPR_W 360
#define SPR_H 240
#define SPR_MAXV (6*4096)
#define SPR_MAXI 512   /* (11-12) 64 until a game painted pictures (the skate game's graffiti: up to 64 pieces a park, besides its UI's); 512 once a game's fonts could be 64 (two sheets each, the letters and their edge) */
static int g_sprSx=1,g_sprSy=1,g_sprX0=0,g_sprY0=0,g_sprW=0,g_sprH=0;
/* an image's texture, its size, where it sits in that texture and how big that texture is, and its pixels until they're on the GPU */
static struct { unsigned tex; int w,h,x,y,tw,th; unsigned char*px; } g_sprImg[SPR_MAXI]; static int g_sprNI=1,g_sprPacked=0;
static float g_sprV[SPR_MAXV*8]; static int g_sprN=0; static unsigned g_sprTex=0;
static float g_flatClear[3]={0.f,0.f,0.f};
static unsigned char g_sprSmooth[SPR_MAXI];   /* (11-12) drawn smoothly, on a texture of its own: a picture, not pixel art (E->imageSmooth) */
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
static void sprUpload(int i){
    if(g_sprSmooth[i]){ const unsigned t=gfxTextureNew(); gfxTexture(t,g_sprImg[i].w,g_sprImg[i].h,g_sprImg[i].px); gfxTextureHud(t); g_sprImg[i].tex=t; }   /* smooth, with its mips, on the sprites' descriptor: without one the 2D pass passes a texture over */
    else g_sprImg[i].tex=sprTexture(g_sprImg[i].px,g_sprImg[i].w,g_sprImg[i].h);
    stbi_image_free(g_sprImg[i].px); g_sprImg[i].px=NULL; }
/* ---- A SHEET (engine plan, Phase 2: atlases). A change of texture breaks a batch of sprites, so a game's small images go on one sheet, packed at the first draw once boot() has loaded them. A game can still make its
 * own sheet, as games/rocks does: one image alone stays as it is, as does one too big to share. A gap of a pixel keeps a neighbour from showing. What's loaded after the first draw has a texture of its own. */
#define SPR_SHEET 1024
#define SPR_SHARE 256
static void sprPack(void){ g_sprPacked=1; int want=0;
    for(int i=1;i<g_sprNI;i++) if(g_sprImg[i].px&&!g_sprSmooth[i]&&g_sprImg[i].w<=SPR_SHARE&&g_sprImg[i].h<=SPR_SHARE) want++;
    if(want>=2){ unsigned char*sheet=(unsigned char*)calloc(SPR_SHEET*SPR_SHEET,4);
        if(sheet){ int cx=1,cy=1,rowH=0; unsigned t=0; int put[SPR_MAXI],nPut=0;
            for(int i=1;i<g_sprNI;i++){ int w=g_sprImg[i].w,h=g_sprImg[i].h; if(!g_sprImg[i].px||g_sprSmooth[i]||w>SPR_SHARE||h>SPR_SHARE) continue;
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
/* (11-12) an image made of pixels the engine was handed (a font's sheet): RGBA, malloc'd, the 2D's from now on */
static int sprImagePixels(unsigned char*px,int w,int h){ if(g_sprNI>=SPR_MAXI){ printf("[2d] more than %d images\n",SPR_MAXI); free(px); return 0; }
    int i=g_sprNI++; g_sprImg[i].w=g_sprImg[i].tw=w; g_sprImg[i].h=g_sprImg[i].th=h; g_sprImg[i].x=g_sprImg[i].y=0; g_sprImg[i].px=px; g_sprImg[i].tex=0;
    if(g_sprPacked) sprUpload(i);
    return i; }
/* (11-12) an image's pixels replaced, at a new size (a font baked again for the canvas's new scale, after the window changed size): its own texture
 * from now on. The old one goes back if it was its own (after the frames that drew it: gfxTextureDrop waits), and stays if it's the shared sheet's */
static void sprImageReplace(int i,unsigned char*px,int w,int h){
    if(i<1||i>=g_sprNI||!px||w<1||h<1){ free(px); return; }
    if(g_sprImg[i].px){ stbi_image_free(g_sprImg[i].px); g_sprImg[i].px=NULL; }
    else if(g_sprImg[i].tex&&g_sprImg[i].tw==g_sprImg[i].w&&g_sprImg[i].th==g_sprImg[i].h) gfxTextureDrop(g_sprImg[i].tex);
    g_sprImg[i].w=g_sprImg[i].tw=w; g_sprImg[i].h=g_sprImg[i].th=h; g_sprImg[i].x=g_sprImg[i].y=0; g_sprImg[i].px=px; g_sprImg[i].tex=0;
    if(g_sprPacked) sprUpload(i); }
/* (11-12) an image drawn smoothly: one still waiting to go to the GPU goes up so, on a texture of its own; one already up on a texture of its own is
 * described again, smooth; one on the shared sheet can't be (the sheet's one sampler), so say it before the first draw packs the sheet */
static void sprImageSmooth(int im){ if(im<1||im>=g_sprNI) return;
    if(g_sprImg[im].px){ g_sprSmooth[im]=1; return; }
    if(g_sprImg[im].tw!=g_sprImg[im].w||g_sprImg[im].th!=g_sprImg[im].h){ printf("[2d] image %d is on the shared sheet: imageSmooth comes before the first draw\n",im); return; }
    g_sprSmooth[im]=1; gfxTextureHud(g_sprImg[im].tex); }
/* (11-12) an image's own size in pixels, for its u and v */
static void sprImageSize(int im,int*w,int*h){ const int ok=im>0&&im<g_sprNI;
    if(w) *w=ok?g_sprImg[im].w:0;
    if(h) *h=ok?g_sprImg[im].h:0; }
/* (11-12) A PICTURE A GAME PAINTS (E->imageMake, E->imagePaint; the Godot proof of concept's step 13). Blank and clear, smooth, on a texture of its own and on the
 * GPU at once, so painting it is putting a rectangle into that texture (gfxTextureSub, which makes its smaller levels again). Its pixels are the game's to keep */
static unsigned char g_sprGone[SPR_MAXI];   /* a painted picture let go (E->imageGone): its place is the next imageMake's */
static int sprImageMake(int w,int h){ if(w<1||h<1||w>4096||h>4096) return 0;
    int i=0; for(int k=1;k<g_sprNI;k++) if(g_sprGone[k]){ i=k; break; }
    if(!i&&g_sprNI>=SPR_MAXI){ printf("[2d] more than %d images\n",SPR_MAXI); fflush(stdout); return 0; }
    unsigned char*px=(unsigned char*)calloc((size_t)w*h,4); if(!px) return 0;
    if(!i){ i=g_sprNI++; } g_sprGone[i]=0; g_sprImg[i].w=g_sprImg[i].tw=w; g_sprImg[i].h=g_sprImg[i].th=h; g_sprImg[i].x=g_sprImg[i].y=0; g_sprImg[i].px=px; g_sprImg[i].tex=0;
    g_sprSmooth[i]=1; sprUpload(i);   /* its own texture, smooth: never the shared sheet, whose pictures can't be painted one by one */
    return i; }
/* a painted picture let go: its texture goes back, and its place is the next imageMake's. Only E->imageMake's: a loaded file's may share the sheet */
static void sprImageGone(int i){ if(i<1||i>=g_sprNI||g_sprGone[i]||!g_sprSmooth[i]||g_sprImg[i].tw!=g_sprImg[i].w) return;
    if(g_sprImg[i].tex){ gfxTextureDrop(g_sprImg[i].tex); } free(g_sprImg[i].px); memset(&g_sprImg[i],0,sizeof g_sprImg[i]); g_sprGone[i]=1; }
static unsigned sprImageTex(int im){ return (im>0&&im<g_sprNI)?g_sprImg[im].tex:0u; }   /* a decal's picture (engine/decals.inc) */
static void sprImagePaint(int i,int x,int y,int w,int h,const unsigned char*rgba){
    if(i<1||i>=g_sprNI||g_sprGone[i]||!rgba||w<1||h<1) return;
    if(g_sprImg[i].px){   /* not on the GPU yet: into its pixels */
        for(int r=0;r<h;r++){ const int yy=y+r; if(yy<0||yy>=g_sprImg[i].h) continue;
            for(int c=0;c<w;c++){ const int xx=x+c; if(xx<0||xx>=g_sprImg[i].w) continue; memcpy(g_sprImg[i].px+((size_t)yy*g_sprImg[i].w+xx)*4,rgba+((size_t)r*w+c)*4,4); } }
        return; }
    if(g_sprImg[i].tw!=g_sprImg[i].w||g_sprImg[i].th!=g_sprImg[i].h){ static int said; if(!said){ said=1; printf("[2d] image %d is on the shared sheet: E->imageMake's can be painted, a loaded file's can't\n",i); fflush(stdout); } return; }
    gfxTextureSub(g_sprImg[i].tex,x,y,w,h,rgba); }
/* (11-12) the screen's edges in canvas units: a 3:2 canvas in a wider screen has room either side */
static void sprToCanvas(float px,float py,float*cx,float*cy){ const float sx=(float)(g_sprSx>0?g_sprSx:1), sy=(float)(g_sprSy>0?g_sprSy:1);   /* (11-12) the picture's pixel on the canvas: the pointer's */
    *cx=(px-(float)g_sprX0)/sx; *cy=(py-(float)g_sprY0)/sy; }
static void sprCanvasEdges(float*x0,float*y0,float*x1,float*y1){
    const float sx=(float)(g_sprSx>0?g_sprSx:1), sy=(float)(g_sprSy>0?g_sprSy:1);
    if(x0) *x0=-(float)g_sprX0/sx;
    if(y0) *y0=-(float)g_sprY0/sy;
    if(x1) *x1=(float)(g_sprW-g_sprX0)/sx;
    if(y1) *y1=(float)(g_sprH-g_sprY0)/sy; }
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
/* (11-12) SHAPES IN 2D (E->poly, E->stroke; the Godot proof of concept's comparison: their HUD's slanted plates and strokes, their cards' blobs, their wheel).
 * The rectangle was the only shape, and a stroke was a row of small squares. Both go out as one list of flat triangles in the display's own pixels, as the
 * rectangle does (hudTris): a fill clipped into ears (an outline that doesn't cross itself, either way round, concave too; one that does is finished as a
 * fan), a stroke as a quad a segment between corners mitred to at most four times its half width */
#define SPR_POLY_MAX 512
static void sprPoly(const float*xy,int n,float r,float g,float b,float a){
    if(!xy||n<3||a<=0.f) return;
    if(n>SPR_POLY_MAX) n=SPR_POLY_MAX;
    static float P[SPR_POLY_MAX*2], T[SPR_POLY_MAX*6]; static int idx[SPR_POLY_MAX];
    const float sx=(float)g_sprSx, sy=(float)g_sprSy;
    for(int i=0;i<n;i++){ P[i*2]=(float)g_sprX0+xy[i*2]*sx; P[i*2+1]=(float)g_sprY0+xy[i*2+1]*sy; idx[i]=i; }
    float area=0.f; for(int i=0;i<n;i++){ const int j=(i+1)%n; area+=P[i*2]*P[j*2+1]-P[j*2]*P[i*2+1]; }
    const float wind=area>=0.f?1.f:-1.f;
    int m=n, nt=0;
    while(m>3){ int clipped=0;
        for(int k=0;k<m&&!clipped;k++){
            const int i0=idx[(k+m-1)%m], i1=idx[k], i2=idx[(k+1)%m];
            const float ax=P[i0*2],ay=P[i0*2+1],bx=P[i1*2],by=P[i1*2+1],cx=P[i2*2],cy=P[i2*2+1];
            if(((bx-ax)*(cy-ay)-(by-ay)*(cx-ax))*wind<=1e-6f) continue;   /* a corner turned in, or flat: not an ear */
            int inside=0;
            for(int q=0;q<m&&!inside;q++){ const int v=idx[q]; if(v==i0||v==i1||v==i2) continue;
                const float px=P[v*2],py=P[v*2+1];
                if(((bx-ax)*(py-ay)-(by-ay)*(px-ax))*wind>=0.f&&((cx-bx)*(py-by)-(cy-by)*(px-bx))*wind>=0.f&&((ax-cx)*(py-cy)-(ay-cy)*(px-cx))*wind>=0.f) inside=1; }
            if(inside) continue;
            float*t=T+nt*6; t[0]=ax; t[1]=ay; t[2]=bx; t[3]=by; t[4]=cx; t[5]=cy; nt++;
            for(int q=k;q<m-1;q++) idx[q]=idx[q+1];
            m--; clipped=1; }
        if(!clipped) break; }   /* it crosses itself: what's left goes as a fan */
    for(int k=1;k+1<m;k++){ float*t=T+nt*6; t[0]=P[idx[0]*2]; t[1]=P[idx[0]*2+1]; t[2]=P[idx[k]*2]; t[3]=P[idx[k]*2+1]; t[4]=P[idx[k+1]*2]; t[5]=P[idx[k+1]*2+1]; nt++; }
    if(nt) hudTris(T,nt*3,(float)g_sprW,(float)g_sprH,r,g,b,a); }
static void sprLine(const float*xy,int n,float width,float r,float g,float b,float a,int closed){
    if(!xy||n<2||width<=0.f||a<=0.f) return;
    if(n>SPR_POLY_MAX) n=SPR_POLY_MAX;
    if(n<3) closed=0;
    static float L[SPR_POLY_MAX*2], R[SPR_POLY_MAX*2], T[SPR_POLY_MAX*12];
    const float sx=(float)g_sprSx, sy=(float)g_sprSy, hw=width*0.5f;
    for(int i=0;i<n;i++){
        const int ip=closed?(i+n-1)%n:(i>0?i-1:i), in=closed?(i+1)%n:(i<n-1?i+1:i);
        float d0x=xy[i*2]-xy[ip*2], d0y=xy[i*2+1]-xy[ip*2+1], d1x=xy[in*2]-xy[i*2], d1y=xy[in*2+1]-xy[i*2+1];
        float l0=sqrtf(d0x*d0x+d0y*d0y), l1=sqrtf(d1x*d1x+d1y*d1y);
        if(l0<1e-6f){ d0x=d1x; d0y=d1y; l0=l1; }
        if(l1<1e-6f){ d1x=d0x; d1y=d0y; l1=l0; }
        if(l0<1e-6f){ L[i*2]=R[i*2]=xy[i*2]; L[i*2+1]=R[i*2+1]=xy[i*2+1]; continue; }
        d0x/=l0; d0y/=l0; d1x/=l1; d1y/=l1;
        float mx=-d0y-d1y, my=d0x+d1x; const float ml=sqrtf(mx*mx+my*my);
        if(ml<1e-4f){ mx=-d1y; my=d1x; } else { mx/=ml; my/=ml; }
        float k=mx*-d1y+my*d1x; if(k<0.25f) k=0.25f;
        const float o=hw/k;
        L[i*2]=xy[i*2]+mx*o; L[i*2+1]=xy[i*2+1]+my*o; R[i*2]=xy[i*2]-mx*o; R[i*2+1]=xy[i*2+1]-my*o; }
    const int segs=closed?n:n-1; int nt=0;
    for(int s=0;s<segs;s++){ const int i=s, j=(s+1)%n;
        float q[8]={L[i*2],L[i*2+1],R[i*2],R[i*2+1],R[j*2],R[j*2+1],L[j*2],L[j*2+1]};
        for(int c=0;c<4;c++){ q[c*2]=(float)g_sprX0+q[c*2]*sx; q[c*2+1]=(float)g_sprY0+q[c*2+1]*sy; }
        float*t=T+nt*6; t[0]=q[0]; t[1]=q[1]; t[2]=q[2]; t[3]=q[3]; t[4]=q[4]; t[5]=q[5]; nt++;
        t=T+nt*6; t[0]=q[0]; t[1]=q[1]; t[2]=q[4]; t[3]=q[5]; t[4]=q[6]; t[5]=q[7]; nt++; }
    if(nt) hudTris(T,nt*3,(float)g_sprW,(float)g_sprH,r,g,b,a); }
