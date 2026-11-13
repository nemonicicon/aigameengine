/* engine/gfxset.inc -- what this machine draws with (engine plan, Phase 7). One table, chosen at setup and never moved by a timer: no automatic cuts, on the PC either.
 * The rung (REVA_TIER) picks a row. save/graphics.txt moves any of it, a line like `msaa 2`, and REVA_GFX="msaa=1,hdr=0" overrides both, which is every feature's off-switch for an A/B.
 * At the pi rung everything's off and the picture is what it was. The rules never see any of this: the same tape gives the same hash at every setting. */
static float g_shadowFar=1.f;   /* how far past the one map's reach the sun's casters are gathered: further with cascades */
/* (step 24) what the engine's own side reads of the settings: how many particles, how far detail lasts, how much foliage, whether beams are lights. With no renderer they're a game's defaults.
 * softfx, decals and refract are the renderer's alone: the engine asks for a soft particle, a decal and water the same way on every machine, so the packet doesn't change with them */
static int g_gfxParticles=2048, g_gfxLod=2, g_gfxFoliage=100, g_gfxBeamLights=0; __attribute__((unused)) static int g_gfxInterp=0;   /* the last is read where there's a window whose display has a rate of its own */
#ifdef REVA_VK
static int g_gfxProbesOn=0;
static GfxConfig gfxRow(int t){
    /*                        taps sun aniso msaa linear hdr tonemap bloom grade hemi lights cascades ao contact volumetric tiles probes sky screens merge pages mirrors | spec reflect normals detail parallax | skin hair eyes sheen fur | particles softfx decals lod refract foliage beamlights | dof mblur taa tube hdrout interp | resW resH aspect */
    static const GfxConfig R[TIER_N]={
        /* pi    */ {1,0, 0,1,0,0,0,0,0, 0,  0,1, 0,0,0,  0,0, 0, 0, 1,1, 0,  0,0,0,0,0,  0,0,0,0, 0,    512,0,0,0,0, 15,0,  0,0,0,0,0,0,  0,0,0},
        /* low   */ {4,0, 4,2,1,0,0,0,0, 1,  6,1, 0,0,0,  0,1, 1, 1, 1,1, 1,  0,0,1,0,0,  0,0,0,0, 0,   2048,0,0,1,0, 40,0,  0,0,0,0,0,1,  0,0,0},
        /* mid   */ {4,0, 8,2,2,1,1,1,1, 1, 64,4, 1,0,0,  4,1, 2, 1, 1,1, 1,  1,1,1,0,0,  0,1,1,0, 8,   6144,1,1,2,1, 70,1,  1,0,0,0,0,1,  0,0,0},
        /* high  */ {4,0,16,4,2,1,1,1,1, 1,256,4, 1,1,1, 16,1, 2, 1, 1,1, 1,  1,2,1,1,1,  1,1,1,1,16,  12288,1,1,3,1,100,2,  1,0,0,0,0,1,  0,0,0},
        /* ultra */ {4,0,16,8,2,1,1,1,1, 1,256,4, 1,1,1, 16,1, 2, 1, 1,1, 1,  1,2,1,1,1,  1,1,1,1,24,  16384,1,1,4,1,100,2,  1,0,0,0,0,1,  0,0,0},
        /* film  */ {4,0,16,4,0,1,0,1,0, 0,  6,1, 0,0,0,  0,0, 0, 1, 1,1, 1,  0,0,0,0,0,  0,0,0,0, 0,  12288,0,0,4,0,100,0,  1,1,0,0,0,0,  0,0,0}};   /* its post pass is always there, because a scene can turn it on mid-take: off, it's a straight copy. Its look is its own: no materials until a take asks */
    GfxConfig c=R[t<0?0:t>=TIER_N?TIER_N-1:t]; c.sunMap=g_sunMap; return c; }
static void gfxSetOne(GfxConfig*c,const char*k,int v){
    if(!strcmp(k,"taps")) c->shadowTaps=v; else if(!strcmp(k,"sun")) c->sunMap=v; else if(!strcmp(k,"aniso")) c->aniso=v; else if(!strcmp(k,"msaa")) c->msaa=v;
    else if(!strcmp(k,"linear")) c->linear=v; else if(!strcmp(k,"hdr")) c->hdr=v; else if(!strcmp(k,"tonemap")) c->tonemap=v; else if(!strcmp(k,"bloom")) c->bloom=v; else if(!strcmp(k,"grade")) c->grade=v;
    else if(!strcmp(k,"hemi")) c->hemi=v; else if(!strcmp(k,"lights")) c->lights=v>GFX_MAX_LIGHTS?GFX_MAX_LIGHTS:v; else if(!strcmp(k,"cascades")) c->cascades=v>1?4:1;
    else if(!strcmp(k,"ao")) c->ao=v; else if(!strcmp(k,"contact")) c->contact=v; else if(!strcmp(k,"volumetric")) c->volumetric=v; else if(!strcmp(k,"tiles")) c->shadowTiles=v; else if(!strcmp(k,"probes")) c->probes=v; else if(!strcmp(k,"sky")) c->sky=v; else if(!strcmp(k,"screens")) c->screens=v; else if(!strcmp(k,"merge")) c->merge=v; else if(!strcmp(k,"pages")) c->pages=v; else if(!strcmp(k,"mirrors")) c->mirrors=v;
    else if(!strcmp(k,"spec")) c->spec=v?1:0; else if(!strcmp(k,"reflect")) c->reflect=v<0?0:v>2?2:v; else if(!strcmp(k,"normals")) c->normals=v?1:0; else if(!strcmp(k,"detail")) c->detail=v?1:0; else if(!strcmp(k,"parallax")) c->parallax=v?1:0;
    else if(!strcmp(k,"skin")) c->skin=v?1:0; else if(!strcmp(k,"hair")) c->hair=v?1:0; else if(!strcmp(k,"eyes")) c->eyes=v?1:0; else if(!strcmp(k,"sheen")) c->sheen=v?1:0; else if(!strcmp(k,"fur")) c->fur=v<0?0:v>48?48:v;
    else if(!strcmp(k,"particles")) c->particles=v<0?0:v>16384?16384:v; else if(!strcmp(k,"softfx")) c->softfx=v?1:0; else if(!strcmp(k,"decals")) c->decals=v?1:0; else if(!strcmp(k,"lod")) c->lod=v<0?0:v>4?4:v;
    else if(!strcmp(k,"refract")) c->refract=v?1:0; else if(!strcmp(k,"foliage")) c->foliage=v<0?0:v>100?100:v; else if(!strcmp(k,"beamlights")) c->beamlights=v<0?0:v>2?2:v;
    else if(!strcmp(k,"dof")) c->dof=v?1:0; else if(!strcmp(k,"mblur")) c->mblur=v?1:0; else if(!strcmp(k,"taa")) c->taa=v?1:0; else if(!strcmp(k,"tube")) c->tube=v<0?0:v>2?2:v; else if(!strcmp(k,"hdrout")) c->hdrout=v<0?0:v>4000?4000:v; else if(!strcmp(k,"interp")) c->interp=v?1:0;
    else if(!strcmp(k,"width")) c->resW=v<0?0:v; else if(!strcmp(k,"height")) c->resH=v<0?0:v; else if(!strcmp(k,"aspect")) c->aspect=v<0?0:v; else if(!strcmp(k,"lines")) c->lines=v<0?0:v; else if(!strcmp(k,"zpre")) c->zpre=v?1:0; else if(!strcmp(k,"fullscreen")) (void)v;   /* fullscreen is the window's, read before it's made (displayPrefs) */
    else { printf("[gfxset] %s isn't a setting (taps sun aniso msaa linear hdr tonemap bloom grade hemi lights cascades ao contact volumetric tiles probes sky screens merge pages mirrors spec reflect normals detail parallax skin hair eyes sheen fur particles softfx decals lod refract foliage beamlights dof mblur taa tube hdrout interp width height aspect lines zpre fullscreen)\n",k); fflush(stdout); } }
/* a setting as it's written: a number, except the picture's shape, which is `4:3`, `16:9` or a number like 1.85 */
static void gfxSetStr(GfxConfig*c,const char*k,const char*vs){
    if(!strcmp(k,"aspect")){ float a=0.f,b=0.f; if(sscanf(vs,"%f:%f",&a,&b)==2&&b>0.f) a/=b; else a=(float)atof(vs); gfxSetOne(c,k,(a>0.2f&&a<8.f)?(int)(a*1000.f+0.5f):0); return; }
    gfxSetOne(c,k,atoi(vs)); }
/* (step 25) THE DISPLAY'S STATIC SETTINGS, in save/graphics.txt with the rest: `fullscreen 0|1`, `width W`, `height H`, `aspect 4:3`. Chosen once and never moved by a timer.
 * fullscreen 1 (as it's always been) is the whole panel at the desktop's own mode, which never changes the display's mode; 0 is a window. width and height are the
 * picture's size: a window is made that size, and fullscreen the picture is drawn at that size and shown at the panel's. aspect is the picture's shape: narrower than the
 * glass, it has black either side. REVA_WINDOWED=1 is still a window whatever the file says. The window's own part is read here, before there's a window */
/* (11-11) A HEADSET HAS ITS OWN PICTURE: with one asked for (REVA_XR), save/graphics_vr.txt is read in graphics.txt's place, so a headset's choices and the screen's
 * don't move each other */
static int gfxXrAsked(void){ const char*e=getenv("REVA_XR"); return e&&e[0]&&strcmp(e,"0"); }
static const char*gfxFileName(void){ return gfxXrAsked()?"save/graphics_vr.txt":"save/graphics.txt"; }
__attribute__((unused)) static void displayPrefs(int*windowed,int*W,int*H){   /* unused on the direct road, which has no window */
    FILE*f=fopen(gfxFileName(),"r"); if(!f) return;
    char ln[128],k[32]; int v,w=0,h=0,fs=-1;
    while(fgets(ln,sizeof ln,f)) if(ln[0]!='#'&&sscanf(ln,"%31s %d",k,&v)==2){ if(!strcmp(k,"fullscreen")) fs=v; else if(!strcmp(k,"width")) w=v; else if(!strcmp(k,"height")) h=v; }
    fclose(f);
    if(fs==0) *windowed=1;
    if(*windowed&&w>=160&&h>=120){ *W=w; *H=h; } }
static void gfxConfigBoot(void){
    GfxConfig c=gfxRow(g_tier);
    c.zpre=0;   /* (11-07) off: on the Pi early depth rejection turns nothing away (REVA_GPUCOUNT's FEP-clipped-quads is 0), so laying the depth first only adds work: 21.3 frames a second against 23.9 at 12th and Jackson. REVA_GFX=zpre=1 for when it does */
    /* (11-11) A HEADSET'S RUNG: the depth laid first, and nothing worked out on the picture after it's drawn. On the bridge at 2160 x 2160 an eye (GTX 1070) the depth first
     * took the solids from 37.8 ms to 15.6, and the occlusion was 5.9 more. Each eye would work out its own occlusion, contact shadows and blur, and the two would differ.
     * graphics_vr.txt (below) can put any of it back */
    if(gfxXrAsked()){ c.zpre=1; c.ao=0; c.contact=0; c.dof=0; c.mblur=0; c.taa=0; }
    /* a take's film look (driver.py scene --film): REVA_POST goes through the post pass on the same colours, for the grade and the vignette, and REVA_SSAA is every sample there is */
    { const char*e=getenv("REVA_SSAA"); if(e&&atoi(e)>=2) c.msaa=8; e=getenv("REVA_POST"); if(e&&atoi(e)>0){ c.hdr=1; c.bloom=1; } }
    FILE*f=fopen(gfxFileName(),"r");
    if(f){ char ln[128],k[32],vs[32]; while(fgets(ln,sizeof ln,f)) if(ln[0]!='#'&&sscanf(ln,"%31s %31s",k,vs)==2) gfxSetStr(&c,k,vs); fclose(f); }
    const char*e=getenv("REVA_GFX");
    if(e){ char b[256]; snprintf(b,sizeof b,"%s",e); for(char*t=strtok(b,", ");t;t=strtok(NULL,", ")){ char*q=strchr(t,'='); if(q){ *q=0; gfxSetStr(&c,t,q+1); } } }
    if(c.cascades>1){ c.sunMap*=2; if(c.sunMap>8192) c.sunMap=8192; g_shadowFar=4.f; }   /* each cascade is a tile, and what casts into them reaches further */
    g_gfxProbesOn=c.probes;
    g_gfxParticles=c.particles; g_gfxLod=c.lod; g_gfxFoliage=c.foliage; g_gfxBeamLights=c.beamlights; g_gfxInterp=c.interp;
    /* (step 25) the flat-panel look's numbers: data/tube.txt, a line a number, and the engine's own where a line is left out */
    if(c.tube){ static const char*const nm[11]={"luma","chroma","beam","bloom","mask","pitch","depth","field","gamma","curve","gain"}; float p[11]={448.f,139.f,0.47f,0.25f,1.f,0.f,0.30f,0.55f,2.4f,0.025f,0.f};
        FILE*t=fopen("data/tube.txt","r"); if(t){ char ln[160],k[32]; float v; while(fgets(ln,sizeof ln,t)) if(ln[0]!='#'&&sscanf(ln,"%31s %f",k,&v)==2) for(int i=0;i<11;i++) if(!strcmp(k,nm[i])) p[i]=v; fclose(t); }
        gfxTube(p); }
    gfxConfig(&c); }
/* a scene's colour grade: a .cube file, the format every grading tool writes. REVA_GRADE names one for an A/B. A size past 64 is refused, loudly */
static void gfxGradeLoad(const char*name){
    char path[256]; snprintf(path,sizeof path,"grades/%s.cube",name); FILE*f=fopen(path,"r"); if(!f) f=fopen(name,"r");
    if(!f){ printf("[grade] can't read %s\n",path); fflush(stdout); return; }
    char ln[256]; int n=0,got=0; unsigned char*px=NULL;
    while(fgets(ln,sizeof ln,f)){
        if(!strncmp(ln,"LUT_3D_SIZE",11)){ n=atoi(ln+11); if(n<2||n>64){ printf("[grade] %s: a size of %d isn't one this reads\n",path,n); fflush(stdout); fclose(f); return; } px=malloc((size_t)n*n*n*4); continue; }
        float r,g,b; if(!px||sscanf(ln,"%f %f %f",&r,&g,&b)!=3) continue;
        if(got<n*n*n){ unsigned char*o=px+(size_t)got*4; o[0]=(unsigned char)(r<0.f?0:r>1.f?255:r*255.f+0.5f); o[1]=(unsigned char)(g<0.f?0:g>1.f?255:g*255.f+0.5f); o[2]=(unsigned char)(b<0.f?0:b>1.f?255:b*255.f+0.5f); o[3]=255; got++; } }
    fclose(f);
    if(px&&got==n*n*n){ gfxLut(px,n); printf("[grade] %s, %d cubed\n",path,n); fflush(stdout); } else { printf("[grade] %s: %d of %d lines\n",path,got,n*n*n); fflush(stdout); }
    free(px); }
#endif
/* a set's spots, and the lights a game adds for one frame. They're drawn by the new renderer only: the GL path has its own six, for film */
static float g_ground[3]={0,0,0}, g_haze=0.f;
static GfxLight g_setSpot[32]; static int g_setNSpot=0;
static GfxLight g_frameLight[GFX_MAX_LIGHTS]; static int g_frameNL=0;
static void lightFill(GfxLight*L,float x,float y,float z,float r,float g,float b,float falloff,float dx,float dy,float dz,float deg){
    memset(L,0,sizeof *L); L->pos[0]=x; L->pos[1]=y; L->pos[2]=z; L->col[0]=r; L->col[1]=g; L->col[2]=b; L->falloff=falloff;
    float l=(float)sqrt(dx*dx+dy*dy+dz*dz); if(l>0.f&&deg>0.f){ L->dir[0]=dx/l; L->dir[1]=dy/l; L->dir[2]=dz/l; L->cosOuter=(float)cos(deg*3.14159265/180.0); L->cosInner=(float)cos(deg*0.75*3.14159265/180.0); } }
static void frameLightAdd(float x,float y,float z,float r,float g,float b,float falloff,float dx,float dy,float dz,float deg){ if(g_frameNL<GFX_MAX_LIGHTS) lightFill(&g_frameLight[g_frameNL++],x,y,z,r,g,b,falloff,dx,dy,dz,deg); }
static void setLookClear(void){
    g_exposure=0.f; g_ground[0]=g_ground[1]=g_ground[2]=0.f; g_setNSpot=0; g_haze=0.f;
    { const char*e=getenv("REVA_HAZE"); if(e&&e[0]) g_haze=(float)atof(e); }
    { const char*e=getenv("REVA_EXPOSURE"); if(e&&e[0]) g_exposure=(float)atof(e); }
#ifdef REVA_VK
    gfxLut(NULL,0); { const char*e=getenv("REVA_GRADE"); if(e&&e[0]&&!(e[0]>='0'&&e[0]<='9')) gfxGradeLoad(e); }   /* a number is film's grade, for the post pass: a name's a table */
#endif
}
