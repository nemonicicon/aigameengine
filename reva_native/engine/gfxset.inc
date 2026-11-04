/* engine/gfxset.inc -- what this machine draws with (engine plan, Phase 7). One table, chosen at setup and never moved by a timer: no automatic cuts, on the PC either.
 * The rung (REVA_TIER) picks a row. save/graphics.txt moves any of it, a line like `msaa 2`, and REVA_GFX="msaa=1,hdr=0" overrides both, which is every feature's off-switch for an A/B.
 * At the pi rung everything's off and the picture is what it was. The rules never see any of this: the same tape gives the same hash at every setting. */
static float g_shadowFar=1.f;   /* how far past the one map's reach the sun's casters are gathered: further with cascades */
#ifdef REVA_VK
static int g_gfxProbesOn=0;
static GfxConfig gfxRow(int t){
    /*                        taps sun aniso msaa linear hdr tonemap bloom grade hemi lights cascades ao contact volumetric tiles probes sky screens merge pages mirrors */
    static const GfxConfig R[TIER_N]={
        /* pi    */ {1,0, 0,1,0,0,0,0,0, 0,  0,1, 0,0,0,  0,0, 0, 0, 1,1, 0},
        /* low   */ {4,0, 4,2,1,0,0,0,0, 1,  6,1, 0,0,0,  0,1, 1, 1, 1,1, 1},
        /* mid   */ {4,0, 8,2,2,1,1,1,1, 1, 64,4, 1,0,0,  4,1, 2, 1, 1,1, 1},
        /* high  */ {4,0,16,4,2,1,1,1,1, 1,256,4, 1,1,1, 16,1, 2, 1, 1,1, 1},
        /* ultra */ {4,0,16,8,2,1,1,1,1, 1,256,4, 1,1,1, 16,1, 2, 1, 1,1, 1},
        /* film  */ {4,0,16,4,0,1,0,1,0, 0,  6,1, 0,0,0,  0,0, 0, 1, 1,1, 1}};   /* its post pass is always there, because a scene can turn it on mid-take: off, it's a straight copy */
    GfxConfig c=R[t<0?0:t>=TIER_N?TIER_N-1:t]; c.sunMap=g_sunMap; return c; }
static void gfxSetOne(GfxConfig*c,const char*k,int v){
    if(!strcmp(k,"taps")) c->shadowTaps=v; else if(!strcmp(k,"sun")) c->sunMap=v; else if(!strcmp(k,"aniso")) c->aniso=v; else if(!strcmp(k,"msaa")) c->msaa=v;
    else if(!strcmp(k,"linear")) c->linear=v; else if(!strcmp(k,"hdr")) c->hdr=v; else if(!strcmp(k,"tonemap")) c->tonemap=v; else if(!strcmp(k,"bloom")) c->bloom=v; else if(!strcmp(k,"grade")) c->grade=v;
    else if(!strcmp(k,"hemi")) c->hemi=v; else if(!strcmp(k,"lights")) c->lights=v>GFX_MAX_LIGHTS?GFX_MAX_LIGHTS:v; else if(!strcmp(k,"cascades")) c->cascades=v>1?4:1;
    else if(!strcmp(k,"ao")) c->ao=v; else if(!strcmp(k,"contact")) c->contact=v; else if(!strcmp(k,"volumetric")) c->volumetric=v; else if(!strcmp(k,"tiles")) c->shadowTiles=v; else if(!strcmp(k,"probes")) c->probes=v; else if(!strcmp(k,"sky")) c->sky=v; else if(!strcmp(k,"screens")) c->screens=v; else if(!strcmp(k,"merge")) c->merge=v; else if(!strcmp(k,"pages")) c->pages=v; else if(!strcmp(k,"mirrors")) c->mirrors=v;
    else { printf("[gfxset] %s isn't a setting (taps sun aniso msaa linear hdr tonemap bloom grade hemi lights cascades ao contact volumetric tiles probes sky screens merge pages mirrors)\n",k); fflush(stdout); } }
static void gfxConfigBoot(void){
    GfxConfig c=gfxRow(g_tier);
    /* a take's film look (driver.py scene --film): REVA_POST goes through the post pass on the same colours, for the grade and the vignette, and REVA_SSAA is every sample there is */
    { const char*e=getenv("REVA_SSAA"); if(e&&atoi(e)>=2) c.msaa=8; e=getenv("REVA_POST"); if(e&&atoi(e)>0){ c.hdr=1; c.bloom=1; } }
    FILE*f=fopen("save/graphics.txt","r");
    if(f){ char ln[128],k[32]; int v; while(fgets(ln,sizeof ln,f)) if(ln[0]!='#'&&sscanf(ln,"%31s %d",k,&v)==2) gfxSetOne(&c,k,v); fclose(f); }
    const char*e=getenv("REVA_GFX");
    if(e){ char b[256]; snprintf(b,sizeof b,"%s",e); for(char*t=strtok(b,", ");t;t=strtok(NULL,", ")){ char*q=strchr(t,'='); if(q){ *q=0; gfxSetOne(&c,t,atoi(q+1)); } } }
    if(c.cascades>1){ c.sunMap*=2; if(c.sunMap>8192) c.sunMap=8192; g_shadowFar=4.f; }   /* each cascade is a tile, and what casts into them reaches further */
    g_gfxProbesOn=c.probes;
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
