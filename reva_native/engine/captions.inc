/* ================= THE CAPTIONS (10-23) =====================================================================
 * Captions and subtitles have to be readable. They were stb_easy_font -- a segment font of six pixels a character
 * -- on ONE line that shrank to fit, and the line was cut at 191 characters: 123 of the show's 340 lines lost their
 * ends. Now a real typeface (Segoe UI Semibold here; REVA_CAPFONT / REVA_CAPFONTI name others; DejaVu on Linux),
 * packed once into an alpha atlas with 2x2 oversampling (stb_truetype, which door 11 already compiles in), drawn as
 * textured quads on the HUD's 1280-wide canvas: near-white with a dark outline over a soft rounded box, at most two
 * rows balanced against each other. What is shown is the line's PAGE for this moment -- compile_scene.py cuts a line
 * into pages of two rows and times each off the voice (`cap t0 t1 text`); a line without pages is wrapped whole. The
 * off-screen voice (slot 4: the narrator) is set in italic with no name; an on-screen speaker's name sits over the
 * box in the slot's colour. No font found, or REVA_OLDCAPS=1 (the control): the old captions.
 * ⭐ (queue step 28, 11-05) THE TYPEFACE IS THE TV FONT NOW, grown (engine/tvgrown.inc): the rule is that nothing of anyone else's ships, and a font read off
 * whatever machine a take was made on isn't the engine's. It's the same atlas and the same quads, with every letter the font has (accents, Greek, Cyrillic); the
 * narrator's italic is the upright letter leaned over. REVA_CAPFONT=<a .ttf> sets captions in a font of the maker's own, and REVA_CAPFONT=system in the machine's,
 * as they were: that's the control, and the way back for a take that wants it. */
#define CAP_PX   64.f              /* the packed size */
#define CAP_SIZE 36.f              /* drawn on the 1280-wide canvas: 5% of a 720 frame */
#define CAP_AW 2048
#define CAP_AH 2048
#define CAP_ROWS 4
static const int CAP_EXTRA[8]={0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2026,0x2022};
static stbtt_packedchar g_capA[2][95], g_capB[2][96], g_capC[2][8];
static int g_capOK=0; static unsigned g_capTex=0;
/* the TV font as a typeface: a record a glyph, as stb_truetype's packer would have made. A font pixel is 2.5 on the 1280-wide canvas at the captions' size, so a letter is 20 across */
#define CAPTV_CELL 36
#define CAPTV_U (2.5f*CAP_PX/CAP_SIZE)
static stbtt_packedchar g_capTvCh[96+TV_MORE]; static int g_capTv=0;
static int capTvInit(void){ const int per=CAP_AW/CAPTV_CELL,n=96+TV_MORE; if((n+per-1)/per*CAPTV_CELL>CAP_AH) return 0;
    unsigned char*px=(unsigned char*)malloc((size_t)CAP_AW*CAP_AH*4); if(!px) return 0;
    for(size_t i=0;i<(size_t)CAP_AW*CAP_AH;i++){ px[i*4]=px[i*4+1]=px[i*4+2]=255; px[i*4+3]=0; }
    for(int g=0;g<n;g++){ unsigned char a[TVG*TVG]; tvGrown(g,a); const int cx=(g%per)*CAPTV_CELL+2,cy=(g/per)*CAPTV_CELL+2;
        for(int y=0;y<TVG;y++) for(int x=0;x<TVG;x++) px[((size_t)(cy+y)*CAP_AW+(size_t)(cx+x))*4+3]=a[y*TVG+x];
        stbtt_packedchar*c=&g_capTvCh[g]; c->x0=(unsigned short)cx; c->y0=(unsigned short)cy; c->x1=(unsigned short)(cx+TVG); c->y1=(unsigned short)(cy+TVG);
        c->xoff=0.f; c->yoff=-7.f*CAPTV_U; c->xoff2=8.f*CAPTV_U; c->yoff2=CAPTV_U; c->xadvance=8.f*CAPTV_U; }
    g_capTex=gfxTextureNew(); gfxTexture(g_capTex,CAP_AW,CAP_AH,px); gfxTextureHud(g_capTex); free(px); return 1; }
static unsigned char*capFile(const char*p){
    if(!p||!p[0]) return NULL;
    FILE*f=fopen(p,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char*b=(n>0)?(unsigned char*)malloc((size_t)n):NULL;
    if(b&&fread(b,1,(size_t)n,f)!=(size_t)n){ free(b); b=NULL; }
    fclose(f); return b; }
static int capCp(const char**s){   /* one UTF-8 codepoint */
    const unsigned char*p=(const unsigned char*)*s; int c=p[0];
    if(c<0x80){ *s+=1; return c; }
    if((c&0xE0)==0xC0&&(p[1]&0xC0)==0x80){ *s+=2; return ((c&0x1F)<<6)|(p[1]&0x3F); }
    if((c&0xF0)==0xE0&&(p[1]&0xC0)==0x80&&(p[2]&0xC0)==0x80){ *s+=3; return ((c&0x0F)<<12)|((p[1]&0x3F)<<6)|(p[2]&0x3F); }
    *s+=1; return '?'; }
static stbtt_packedchar*capGlyph(int f,int c){
    if(g_capTv){ const int g=(c>=0&&c<65536)?tvGlyphOf((TvCh)c):-1; return &g_capTvCh[g<0?'?'-32:g]; }
    if(c>=32&&c<=126) return &g_capA[f][c-32];
    if(c>=160&&c<=255) return &g_capB[f][c-160];
    for(int k=0;k<8;k++) if(CAP_EXTRA[k]==c) return &g_capC[f][k];
    return &g_capA[f]['?'-32]; }
static void capInit(void){
    const char*e=getenv("REVA_OLDCAPS");
    if(e&&e[0]&&e[0]!='0'){ printf("[caps] REVA_OLDCAPS: the old segment-font captions\n"); fflush(stdout); return; }
    { const char*nm=getenv("REVA_CAPFONT");   /* (step 28) nothing named: the TV font, grown. It's the one that ships */
      if(!nm||!nm[0]){ if(capTvInit()){ g_capOK=1; g_capTv=1; printf("[caps] the TV font, grown to %.0f px: %d letters. REVA_CAPFONT=<a .ttf>, or =system, sets them in another\n",8.f*2.5f,96+TV_MORE); }
                       else printf("[caps] no memory for the caption sheet -- the old segment-font captions\n");
                       fflush(stdout); return; } }
    const char*cand[2][6]={{getenv("REVA_CAPFONT"),"C:/Windows/Fonts/seguisb.ttf","C:/Windows/Fonts/segoeui.ttf","C:/Windows/Fonts/arial.ttf",
                            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"},
                           {getenv("REVA_CAPFONTI"),"C:/Windows/Fonts/seguisbi.ttf","C:/Windows/Fonts/segoeuii.ttf","C:/Windows/Fonts/ariali.ttf",
                            "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf","/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf"}};
    unsigned char*ttf[2]={NULL,NULL}; const char*got[2]={"",""};
    for(int f=0;f<2;f++) for(int k=0;k<6&&!ttf[f];k++){ ttf[f]=capFile(cand[f][k]); if(ttf[f]) got[f]=cand[f][k]; }
    if(!ttf[0]){ printf("[caps] NO CAPTION FONT (REVA_CAPFONT names a .ttf) -- the old segment-font captions\n"); fflush(stdout); free(ttf[1]); return; }
    int haveI=(ttf[1]!=NULL);
    unsigned char*bmp=(unsigned char*)calloc((size_t)CAP_AW*CAP_AH,1);
    stbtt_pack_context pc;
    if(!bmp||!stbtt_PackBegin(&pc,bmp,CAP_AW,CAP_AH,0,3,NULL)){ printf("[caps] the atlas: out of memory -- the old captions\n"); free(bmp); free(ttf[0]); free(ttf[1]); return; }
    stbtt_PackSetOversampling(&pc,2,2);
    int ok=1;
    for(int f=0;f<2;f++){
        if(f==1&&!haveI){ memcpy(g_capA[1],g_capA[0],sizeof g_capA[0]); memcpy(g_capB[1],g_capB[0],sizeof g_capB[0]); memcpy(g_capC[1],g_capC[0],sizeof g_capC[0]); break; }
        stbtt_pack_range r[3]; memset(r,0,sizeof r);
        r[0].font_size=CAP_PX; r[0].first_unicode_codepoint_in_range=32;  r[0].num_chars=95; r[0].chardata_for_range=g_capA[f];
        r[1].font_size=CAP_PX; r[1].first_unicode_codepoint_in_range=160; r[1].num_chars=96; r[1].chardata_for_range=g_capB[f];
        r[2].font_size=CAP_PX; r[2].array_of_unicode_codepoints=(int*)CAP_EXTRA; r[2].num_chars=8; r[2].chardata_for_range=g_capC[f];
        if(!stbtt_PackFontRanges(&pc,ttf[f],0,r,3)) ok=0; }
    stbtt_PackEnd(&pc); free(ttf[0]); free(ttf[1]);
    if(!ok) printf("[caps] some glyphs did not fit the atlas -- they draw as '?'\n");
    g_capTex = gfxTextureNew();
#ifdef REVA_VK
    { unsigned char*rg9=(unsigned char*)malloc((size_t)CAP_AW*CAP_AH*4); for(int i9=0;i9<CAP_AW*CAP_AH;i9++){ rg9[i9*4]=rg9[i9*4+1]=rg9[i9*4+2]=255; rg9[i9*4+3]=bmp[i9]; } gfxTexture(g_capTex, CAP_AW, CAP_AH, rg9); free(rg9); }
#endif
    gfxTextureHud(g_capTex);
    free(bmp);
    /* solid > 0: the box, a rounded rectangle of corner radius `solid` (an antialiased distance), else a glyph */
    g_capOK=1;
    printf("[caps] %s%s%s packed at %.0f px (2x2 oversampled) into %dx%d, drawn at %.0f px%s\n",got[0],haveI?" + ":"",got[1],
           CAP_PX,CAP_AW,CAP_AH,CAP_SIZE,haveI?"":" (no italic face: the narrator in the upright one)"); fflush(stdout); }
static float capWidth(int f,const char*s,const char*e,float sz){
    float w=0.f; while(s<e&&*s){ int c=capCp(&s); w+=capGlyph(f,c)->xadvance; } return w*sz/CAP_PX; }
static int capQuads(int f,const char*s,const char*e,float x,float y,float sz,float*v,int cap){   /* y: the baseline; floats written */
    float k=sz/CAP_PX, px=0.f, py=0.f; int n=0;
    while(s<e&&*s&&n+24<=cap){ int c=capCp(&s); stbtt_aligned_quad q;
        stbtt_GetPackedQuad(capGlyph(f,c),CAP_AW,CAP_AH,0,&px,&py,&q,0);
        float x0=x+q.x0*k, x1=x+q.x1*k, y0=y+q.y0*k, y1=y+q.y1*k;
        const float lean=(g_capTv&&f==1)?0.20f:0.f, t0=(y-y0)*lean, t1=(y-y1)*lean;   /* the TV font has no italic of its own: the narrator's is the letter leaned over, about its baseline */
        float Q[24]={x0+t0,y0,q.s0,q.t0, x1+t0,y0,q.s1,q.t0, x1+t1,y1,q.s1,q.t1, x0+t0,y0,q.s0,q.t0, x1+t1,y1,q.s1,q.t1, x0+t1,y1,q.s0,q.t1};
        memcpy(v+n,Q,sizeof Q); n+=24; }
    return n; }
/* the page in rows no wider than maxW: one if it fits, else the two-row break that makes the longer row shortest (a
 * subtitle's two rows are read as one shape), else as many whole-word rows as it takes */
static int capWrap(int f,const char*t,float sz,float maxW,const char**ra,const char**rb){
    const char*end=t+strlen(t);
    while(t<end&&*t==' ') t++;
    while(end>t&&end[-1]==' ') end--;
    if(t>=end) return 0;
    if(capWidth(f,t,end,sz)<=maxW){ ra[0]=t; rb[0]=end; return 1; }
    const char*best=NULL; float bw=1e9f;
    for(const char*p=t;p<end;p++) if(*p==' '){ float w1=capWidth(f,t,p,sz), w2=capWidth(f,p+1,end,sz);
        float m=(w1>w2?w1:w2)+(w1>w2?0.5f:0.f);       /* a tie goes to the shorter TOP row */
        if(m<bw){ bw=m; best=p; } }
    if(best&&bw<=maxW+0.5f){ ra[0]=t; rb[0]=best; ra[1]=best+1; rb[1]=end; return 2; }
    int n=0; const char*a=t;
    while(a<end&&n<CAP_ROWS){ const char*fit=NULL;
        for(const char*p=a;p<=end;p++) if(p==end||*p==' '){ if(!fit||capWidth(f,a,p,sz)<=maxW) fit=p; else break; }
        ra[n]=a; rb[n]=fit; n++;
        a=fit; while(a<end&&*a==' ') a++; }
    return n; }
/* one row of text for the renderer: its glyphs, as sprites, moved and in this colour */
static void capVk(const float*q,int nv,float ox,float oy,float r,float g,float b,float a,float W,float H){ static float s[8*6*480]; const int n=nv/4; if(n<=0||n>6*480) return; for(int i=0;i<n;i++){ s[i*8]=q[i*4]+ox; s[i*8+1]=q[i*4+1]+oy; s[i*8+2]=q[i*4+2]; s[i*8+3]=q[i*4+3]; s[i*8+4]=r; s[i*8+5]=g; s[i*8+6]=b; s[i*8+7]=a; } gfxSprites(g_capTex,s,n,W,H); }
/* the box behind them: a rounded rectangle, as a fan */
static void capBoxVk(float x0,float y0,float x1,float y1,float rd,float W,float H){ float v[2*3*40]; int n=0; const float cx=(x0+x1)*0.5f,cy=(y0+y1)*0.5f; float pv[2]={0.f,0.f},fst[2]={0.f,0.f}; int have=0; static const float C[4][3]={{1,1,0.f},{-1,1,1.5708f},{-1,-1,3.14159f},{1,-1,4.71239f}};
    for(int c=0;c<4;c++) for(int k=0;k<=9;k++){ const float a=C[c][2]+1.5708f*(float)k/9.f,px=(C[c][0]>0.f?x1-rd:x0+rd)+cosf(a)*rd,py=(C[c][1]>0.f?y1-rd:y0+rd)+sinf(a)*rd; if(have&&n<40){ v[n*6]=cx; v[n*6+1]=cy; v[n*6+2]=pv[0]; v[n*6+3]=pv[1]; v[n*6+4]=px; v[n*6+5]=py; n++; } if(!have){ fst[0]=px; fst[1]=py; } pv[0]=px; pv[1]=py; have=1; }
    if(n<40){ v[n*6]=cx; v[n*6+1]=cy; v[n*6+2]=pv[0]; v[n*6+3]=pv[1]; v[n*6+4]=fst[0]; v[n*6+5]=fst[1]; n++; }
    gfxHud(v,n*3,W,H,0.02f,0.025f,0.04f,0.46f); }
static void capDraw(float HW,float HH){
    int li=scnActive(); if(li<0) return;
    ScnLine*L=&g_scnL[li]; if(L->silent) return;
    const char*tx=L->text; float lt=g_scnT-L->t0;
    if(L->capN>0){ int k=L->capA;
        for(int i=L->capA;i<L->capA+L->capN&&i<g_scnCapN;i++) if(lt>=g_scnCap[i].t0-0.02f) k=i;
        tx=g_scnCap[k].text; }
    int sp=L->spk; if(sp<0||sp>=SCN_MAXCAST) sp=0;
    int f=(sp==SCN_VOICE)?1:0;
    const char*ra[CAP_ROWS], *rb[CAP_ROWS]; float sz=CAP_SIZE, maxW=HW*0.70f; int nr=0;
    for(int tries=0;tries<4;tries++){ nr=capWrap(f,tx,sz,maxW,ra,rb); if(nr<=2) break; sz*=0.9f; maxW=HW*0.78f; }
    if(nr<=0) return;
    float lineH=sz*1.30f, wmax=0.f;
    for(int r=0;r<nr;r++){ float w=capWidth(f,ra[r],rb[r],sz); if(w>wmax) wmax=w; }
    float base=HH-46.f;                               /* the last row's baseline */
    float top=base-(float)(nr-1)*lineH-sz*0.74f;      /* the first row's cap height */
    float padX=sz*0.60f, padY=sz*0.34f;
    float bx0=HW*0.5f-wmax*0.5f-padX, bx1=HW*0.5f+wmax*0.5f+padX, by0=top-padY, by1=base+sz*0.24f+padY;
    capBoxVk(bx0,by0,bx1,by1,sz*0.30f,HW,HH);
    static float v[24*480];
    /* the name of an on-screen speaker, over the box's left edge, in the slot's colour */
    static const float SPK[SCN_MAXCAST][3]={{0.55f,0.95f,1.00f},{1.00f,0.78f,0.42f},{0.62f,0.94f,0.58f},{0.92f,0.72f,0.96f},{0.86f,0.86f,0.90f}};
    if(!f&&g_scnWho[sp][0]&&strcmp(g_scnWho[sp],"-")){
        char nm[24]; int k=0; for(const char*s=g_scnWho[sp];*s&&k<23;s++) nm[k++]=(*s=='_')?' ':*s; nm[k]=0;
        float nsz=sz*0.66f; int nv=capQuads(0,nm,nm+k,bx0+padX*0.30f,by0-7.f,nsz,v,24*480);   /* over the box, not on its edge */
        for(int d=0;d<8;d++){ float a=d*0.785398f;
            capVk(v,nv,cosf(a)*1.4f,sinf(a)*1.4f,0.f,0.f,0.f,0.85f,HW,HH);
        }
        capVk(v,nv,0.f,0.f,SPK[sp][0],SPK[sp][1],SPK[sp][2],1.f,HW,HH);
           }
    /* the rows: a dark outline (twelve offsets), then the face */
    for(int r=0;r<nr;r++){ float w=capWidth(f,ra[r],rb[r],sz);
        int nv=capQuads(f,ra[r],rb[r],HW*0.5f-w*0.5f,base-(float)(nr-1-r)*lineH,sz,v,24*480);
        for(int d=0;d<12;d++){ float a=d*0.523599f;
            capVk(v,nv,cosf(a)*2.0f,sinf(a)*2.0f,0.01f,0.01f,0.02f,0.90f,HW,HH);
        }
        capVk(v,nv,0.f,0.f,f?1.0f:0.98f,f?0.97f:0.98f,f?0.90f:0.96f,1.f,HW,HH);
         } }

/* P7: pose the partner from one clip and draw her. Deliberately the whole pipeline in
 * one place and nothing more: rest pose, clip tracks over it, FK, skin, draw. She has
 * no state tree and no grounding, so what the score authored is exactly what appears --
 * which is the only way a two-body contact point can be trusted. Returns 0 if there is
 * nothing to draw. `pal` is the caller's scratch palette. */
static int labDrawPartner(Pack*P,int clip,double phase,const float*heroModel,float*pal){
    /* ⛔ A BORROWED CLIP IS STILL A CLIP. clip is -1 whenever the staging resolved her
     * line out of the BANK rather than her own pack -- which is the normal case -- and
     * refusing to draw on that basis made the partner absent from every scene in the
     * film. Her head mark is published below, so her absence also silently disabled the
     * conversation camera, which is gated on both marks. */
    int borrowed9=(g_labPDnP&&g_labPDnC>=0&&(unsigned)g_labPDnC<g_labPDnP->nClips);
    if(!P||P->nNodes>1024) return 0;
    if(g_camHoldSlot==g_castI) return 0;   /* ⭐ this shot is coming out of her own camera */
    if(!borrowed9&&(clip<0||(unsigned)clip>=P->nClips)) return 0;
    static float T[1024][3], Q[1024][4], S[1024][3]; static M4 W[1024];
    bodyRest(P,T,Q,S);
    /* ⛔ A PACK WITH NO CLIPS OF ITS OWN (10-19). Two bodies own no clip at all -- every move they make is
     * borrowed from a bank -- and the placeholder below read clips[0] out of an EMPTY table: a segfault the first time
     * either stood in a scene. An empty clip stands in (no tracks: the borrowed pose is all there is). */
    static PClip noClip9;
    PClip*c=P->nClips?&P->clips[(clip>=0&&(unsigned)clip<P->nClips)?clip:0]:&noClip9;   /* borrowed: index 0 is only a placeholder -- donorPoseF overwrites the pose */
    /* ⭐ a TRANSITION plays once in a scene and holds its last frame (clipHoldsEnd) */
    if(g_scnT>=0.f&&!borrowed9&&clip>=0&&(unsigned)clip<P->nClips&&phase>(double)(c->frames-1)
       &&clipHoldsEnd(P,c,g_castI,packStr(P,c->name)))
        phase=(double)(c->frames-1);
    /* ⭐ a TRICK plays to its last frame and no further; trickTick cuts back on the next exposure */
    if(g_scnT>=0.f&&g_castI>=1&&g_castI<SCN_MAXCAST&&g_trkOn[g_castI])
        trickClampEnd(g_castI,&phase,borrowed9?&g_labPDnP->clips[g_labPDnC]:c);
    unsigned f=(unsigned)phase % (c->frames?c->frames:1);
    unsigned char*tr=P->pk+c->offTracks;
    float*cw9=g_clipMorphW[(g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1]; memset(cw9,0,sizeof(float)*MORPH_MAX);
    for(unsigned t=0;t<c->nTracks;t++){
        unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
        if(type==3){ if(!borrowed9&&node<(unsigned)P->nMorph&&node<MORPH_MAX) cw9[node]=((float*)(P->pk+offD))[f]; continue; }   /* (11-11) her clip's face shapes: laid over the scene's before the blend */
        if(node>=P->nNodes) continue;
        float*d=(float*)(P->pk+offD); int comp=type==0?4:3;
        if(type>=3) continue; else if(type==0) memcpy(Q[node],d+(size_t)f*comp,16);
        else if(type==1) memcpy(T[node],d+(size_t)f*comp,12);
        else memcpy(S[node],d+(size_t)f*comp,12); }
    /* A STAGED CLIP SHE DOES NOT OWN. x_convo1/x_convo2 live in the mob1 bank, not in
     * her pack, so the scene's role-cast reaches her the same way the zombie clips reach
     * a citizen: pose the bank's rig, transfer the canonical bones. */
    if(g_labPDnP&&g_labPDnC>=0&&(unsigned)g_labPDnC<g_labPDnP->nClips)
        donorPoseF(g_labPDnP,g_labPDnC,phase,P,Q);
    /* ⭐ SHE WALKS TO HER MARK. Above this the staged clip has already been sampled into
     * T/Q/S; if the staging is currently MOVING her, overwrite it with a walk whose phase
     * runs at gaitRate() for her measured speed. Mixed in and out over ~0.18s so the
     * hand-off never pops, and taken from her own pack when she has a walk, otherwise off
     * a bank through the same canonical transfer the zombie clips use. */
    if(g_scnT>=0.f){
        /* ⭐ ONE WALK STATE PER BODY, keyed on its WORLD speed (Loco). These names were
         * globals every partner slot shared; for this block they are this slot's own. */
        Loco*Lw9=&g_loco[(g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1];
#define g_partSpd     (Lw9->spd)
#define g_partWalkMix (Lw9->walkMix)
#define g_partWalkPh  (Lw9->walkPh)
#define g_partWalkP   (Lw9->walkP)
#define g_partWalkC   (Lw9->walkC)
        const float WALK_ON=0.22f;                    /* world units/sec worth walking for */
        float want=(g_partSpd>WALK_ON&&!P->nWheel&&!castRides(g_castI))?1.f:0.f;   /* ⭐ a rider rolls */
        float stp=animStepS(simStepS());
        g_partWalkMix += (want-g_partWalkMix)*(1.f-expf(-stp/0.18f));
        if(g_partWalkMix>0.02f){
            if(g_partWalkP!=P){                        /* resolve her walk once per body */
                g_partWalkP=NULL; g_partWalkC=-1;
                for(unsigned c=0;c<P->nClips;c++){ const char*nm=packStr(P,P->clips[c].name);
                    if(!strcmp(nm,"w_walk")||!strcmp(nm,"walking")||!strcmp(nm,"v_walk1")){
                        g_partWalkP=P; g_partWalkC=(int)c; break; } }
                if(g_partWalkC<0&&P->cbOK&&P->pbOK>=10){
                    int bc=-1; Pack*bp=bankFind("w_walk",&bc);
                    if(bp){ g_partWalkP=bp; g_partWalkC=bc; } }
                if(g_partWalkC<0) g_partWalkP=P;        /* nothing to borrow: leave her be */ }
            if(g_partWalkC>=0&&g_partWalkP){
                PClip*wc=&g_partWalkP->clips[g_partWalkC];
                g_partWalkPh += stp*wc->fps*gaitRate(g_partWalkP,g_partWalkC,g_partSpd);
                static float WQ[1024][4], WT[1024][3], WS[1024][3];
                for(unsigned i=0;i<P->nNodes;i++){
                    memcpy(WQ[i],P->nodes[i].q,16); memcpy(WT[i],P->nodes[i].t,12);
                    memcpy(WS[i],P->nodes[i].s,12); }
                if(g_partWalkP==P) clipSampleF(P,wc,g_partWalkPh,WT,WQ,WS);
                else               donorPoseF(g_partWalkP,g_partWalkC,g_partWalkPh,P,WQ);
                float m=g_partWalkMix;
                for(unsigned i=0;i<P->nNodes;i++) qnlerp(Q[i],Q[i],WQ[i],m); } }
        else { g_partWalkPh=0.0; }
#undef g_partSpd
#undef g_partWalkMix
#undef g_partWalkPh
#undef g_partWalkP
#undef g_partWalkC
        /* ⭐ THE UPPER-BODY LAYER (09-11), per cast slot */
        if(g_castI>=1&&g_castI<SCN_MAXCAST&&g_overClip[g_castI][0])
            poseOverlay(P,Q,g_overClip[g_castI],overPhase(P,g_overClip[g_castI],now_ms()*0.001),g_overW[g_castI]);
        /* ⭐ and her motion energy for the cutter (09-11), against last frame's pose */
        if(g_castI>=1&&g_castI<SCN_MAXCAST){ static float cpq[SCN_MAXCAST][160][4]; static int cpqOk[SCN_MAXCAST]; int s=g_castI; float e=0.f;
            unsigned n=P->nNodes<160?P->nNodes:160;
            if(cpqOk[s]) for(unsigned i=0;i<n;i++){ float d=fabsf(Q[i][0]*cpq[s][i][0]+Q[i][1]*cpq[s][i][1]+Q[i][2]*cpq[s][i][2]+Q[i][3]*cpq[s][i][3]); if(d>1.f)d=1.f; e+=2.f*acosf(d); }
            for(unsigned i=0;i<n;i++) memcpy(cpq[s][i],Q[i],16);
            cpqOk[s]=1;
            float sdt=simStepS(); g_motE[s]=(sdt>1e-5f)?e/sdt:0.f; }
        /* REVA_STAGEDBG=1: is she WALKING to her mark or gliding to it? A contact sheet
         * cannot answer that -- animation is verified in motion -- so the engine says. */
        { static int sd=-1; if(sd<0){ const char*e=getenv("REVA_STAGEDBG"); sd=(e&&e[0]!='0'); }
          if(sd){ static int n=0; static float peak=0.f, peakMix=0.f;
            if(g_partSpd>peak) peak=g_partSpd;
            if(g_partWalkMix>peakMix) peakMix=g_partWalkMix;
            if((n++%60)==0) printf("[stagewalk] spd %.3f (peak %.3f)  mix %.2f (peak %.2f)  clip %s\n",
                g_partSpd,peak,g_partWalkMix,peakMix,
                (g_partWalkC>=0&&g_partWalkP)?packStr(g_partWalkP,g_partWalkP->clips[g_partWalkC].name):"-"),
                fflush(stdout); } } }
    scnHeadLayer(P,Q,1);
    bodyFK(P,T,Q,S,W);
    /* ⭐ THE WILLIAMS LAYER ON THE PARTNER, after the head layer so a look-at is what
     * DRAGS. She walks whenever the stage mark moves her, so she wants the weight as much
     * as the hero does -- and standing on a porch she still gets the overlap, which is the
     * half of this that answers "reva is just standing there". */
    /* ⭐ A z_rm_ CLIP OWNS HER ROOT in a scene, as it owns the hero's: the Williams bounce and belt
     * tilt stand down -- an authored arc is exact, and a pelvis bobbing on its stride lifts feet off a deck */
    int rmOwn9=0;
    if(g_scnT>=0.f){ const char*rn9=borrowed9?packStr(g_labPDnP,g_labPDnP->clips[g_labPDnC].name)
                                              :((clip>=0&&(unsigned)clip<P->nClips)?packStr(P,c->name):"");
        rmOwn9=(strncmp(rn9,"z_rm_",5)==0); }
    if(!g_wmsOff&&!rmOwn9){ /* ⛔ PER CAST SLOT. One static state for every partner meant the two
                    * partners overwrote each other's drag lags and bounce every frame; on a held
                    * stop-motion exposure the co-star was posed against the OTHER woman's lag, so she moved
                    * on frames that should repeat (A/B: held pairs 8-9.6% on, 0.15-0.60% off). */
                   static WmsState pw9[SCN_MAXCAST];
                   WmsState*pwp=&pw9[(g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1];
        wmsApply(P,T,Q,W,pwp,(float)g_chDt,g_wms);
        bodyFK(P,T,Q,S,W); }
    /* HER WIDTH, from the FK that has just run. Doing it here rather than off her rest
     * node translations is the difference between 0.37 and a bogus 0.11. */
    if(g_partShoulderW<=0.f&&P->pb[7]>=0&&P->pb[10]>=0){
        float*a7=W[P->pb[7]],*b7=W[P->pb[10]];
        float dx=a7[12]-b7[12], dy=a7[13]-b7[13], dz=a7[14]-b7[14];
        g_partShoulderW=sqrtf(dx*dx+dy*dy+dz*dz)*P->scale; }
    stretchTick(P,W);                                          /* ⭐ drapes held between two limbs (09-29) */
    chainSim(P,W,(g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1);   /* H1: each partner's chains, in her own slot */
    /* Her own model matrix, built from HER pack's scale and ground -- the hero's cannot
     * be reused, because it carries the hero's scale. She stands g_labPGap in front of
     * the hero along the hero's facing, turned g_labPYaw to meet her. */
    float sc=P->scale, ang=g_rvHead+g_labPYaw*0.0174533f+stepBoil(g_castI+1,0)*0.006f;
    g_partYaw=ang;               /* the look-at needs her frame, not just the hero's */
    float ca=cosf(ang), sa=sinf(ang);
    M4 model; memset(model,0,64);
    model[0]=ca*sc; model[2]=-sa*sc; model[5]=sc; model[8]=sa*sc; model[10]=ca*sc; model[15]=1.f;
    model[12]=heroModel[12]+sinf(g_rvHead)*g_labPGap+cosf(g_rvHead)*g_labPSide;
    model[14]=heroModel[14]+cosf(g_rvHead)*g_labPGap-sinf(g_rvHead)*g_labPSide;
    model[12]+=stepBoil(g_castI+1,1)*0.005f; model[14]+=stepBoil(g_castI+1,2)*0.005f;   /* her own animator's hand */
    /* and she collides too -- outside a scene she is placed relative to the hero, and
     * the hero can walk her into a wall she cannot see. */
    if(!g_stageOn) colResolve(&model[12],&model[14],0.42f);
    /* ⭐ WHERE SHE ACTUALLY IS. Framing a partner tightly means knowing her world spot,
     * and it falls out of the hero's heading and the gap -- so print it instead of
     * solving that triangle by hand in a harness every time. */
    /* ⭐ A BORROWED LAY TAKES HER DOWN WITH IT (Z4's donorHipDrop, which had only ever
     * been wired into the ACTOR draw). donorPoseF is rotation-only, so a curl-up sampled
     * off a bank arrives correctly oriented and floating at HER OWN standing hip height --
     * which on a bed mark reads exactly as a woman standing on the bed. */
    float pHipDrop9=(g_labPDnP&&g_labPDnC>=0&&(unsigned)g_labPDnC<g_labPDnP->nClips)
                    ? donorHipDrop(g_labPDnP,g_labPDnC,phase,P) : 0.f;
    { static int p9=0; if(getenv("REVA_PDBG")&&(p9++%90)==0){
        printf("[partner] world %.3f,%.3f,%.3f  head %.3f  gap %.2f side %.2f  hipdrop %.3f (donor %s clip %d)%c",
               model[12],model[13],model[14],g_rvHead,g_labPGap,g_labPSide,
               pHipDrop9,g_labPDnP?g_labPDnP->label:"-",g_labPDnC,10);
        fflush(stdout); } }
    model[13]=-P->mny*sc+pHipDrop9*sc;
    float bdFloor9=castOnBoard(g_castI)?boardAuthoredFloor(g_castI,P,sc):NAN;   /* ⭐ a @board rider's authored floor */
    if(bdFloor9==bdFloor9) model[13]=-bdFloor9*sc;
    /* ⭐ UNLESS SHE IS ON A MARK. A body on a chair is placed by the FURNITURE, not by the
     * person she is talking to -- world position, world yaw, and a HEIGHT, which is the
     * thing the ground plane could never give her. This is what lets a body sit in the
     * chair the script has put her in for two drafts, and the co-star lie on a bed instead of
     * through it. `pmark=off` hands her back to the pair relation. */
    { SetMark*MK=markFind(g_castMark[g_castI]);
      if(MK){ locoPublish(); Loco*Lm9=&g_loco[(g_castI>=0&&g_castI<SCN_MAXCAST)?g_castI:1];
              float ma=(Lm9->pon||Lm9->phold)?Lm9->pyaw:MK->yaw*0.0174533f, mc=cosf(ma), ms=sinf(ma);
              g_partYaw=ma;
              memset(model,0,64);
              model[0]=mc*sc; model[2]=-ms*sc; model[5]=sc; model[8]=ms*sc; model[10]=mc*sc; model[15]=1.f;
              /* \u26d4 THE MARK OVERWRITES HER Y, so the hip drop has to be re-applied
               * here or the one case that needs it most -- a body lying on a bed, placed
               * by the furniture -- is the one case that never gets it. */
              model[12]=(Lm9->pon||Lm9->phold)?Lm9->px:MK->x; model[14]=(Lm9->pon||Lm9->phold)?Lm9->pz:MK->z;   /* ⭐ walking there, or on it */
              model[13]=MK->y-P->mny*sc+pHipDrop9*sc;
              if(bdFloor9==bdFloor9) model[13]=MK->y-bdFloor9*sc;
              /* a `hips` mark pins the PELVIS to the height instead: seated, feet below the seat */
              if(MK->hips&&P->pb[0]>=0) model[13]=MK->y-W[P->pb[0]][13]*sc; }
      g_castFloorY[g_castI]=MK?MK->y:0.f; }
    /* ⭐ on a deck she stands ON it -- and where she was drawn, for the next walk */
    model[13]+=castDeckLift(g_castI);
    g_castAt[g_castI][0]=model[12]; g_castAt[g_castI][1]=model[14]; g_castAt[g_castI][2]=g_partYaw; g_castAtOK[g_castI]=1;
    /* ⭐ THE CARVE -- after the spot is published: a lean must not move where the body is said to stand */
    { int s10=(g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1;
      if((g_loco[s10].pon||g_loco[s10].phold)&&markFind(g_castMark[s10])) locoLeanModel(model,s10,g_castFloorY[s10]);
      pipeModel(s10,model,P,sc); rollModel(s10,model); }   /* ⭐ on a half-pipe the ride places her, over all of the above; (10-22) the roll over that */
    /* THE SHARED GEOMETRY, reported once. Two bodies of different sizes cannot each
     * reach "above my own shoulder" and expect to touch -- the meeting point has to be
     * chosen in the world they share, and only the engine knows both bodies. These are
     * the numbers the scores need to aim at a common point. */
    if(g_labHi5){ static int said=0; if(!said){ said=1;
        int sh=P->pb[10], el=P->pb[11];                 /* upperarm_r, lowerarm_r */
        if(sh>=0&&el>=0){
            float shy=W[sh][13]*sc-P->mny*sc;
            float a1=sqrtf((W[sh][12]-W[el][12])*(W[sh][12]-W[el][12])
                          +(W[sh][13]-W[el][13])*(W[sh][13]-W[el][13])
                          +(W[sh][14]-W[el][14])*(W[sh][14]-W[el][14]));
            int tip=-1; for(unsigned n8=0;n8<P->nNodes;n8++) if(P->nodes[n8].parent==el){ tip=(int)n8; break; }
            float a2=(tip>=0)?sqrtf((W[el][12]-W[tip][12])*(W[el][12]-W[tip][12])
                                   +(W[el][13]-W[tip][13])*(W[el][13]-W[tip][13])
                                   +(W[el][14]-W[tip][14])*(W[el][14]-W[tip][14])):0.f;
            printf("[hi5] PARTNER geometry: shoulder %.2fu above ground, arm %.2fu (scale %.4f)\n",
                   shy,(a1+a2)*sc,sc); fflush(stdout); } } }
    /* THE CONTACT GATE. A two-body move has an invariant no single-body check can see:
     * the hands must actually MEET. Both are reported in world units so "they touched"
     * is a number, not an impression -- and the wall then says whether it LOOKS right. */
    if(g_labHi5&&g_labHeroHandOK&&P->pb[g_labHi5==2?8:(g_labHi5==3?5:(g_labHi5==4?10:11))]>=0){
        int hb=P->pb[g_labHi5==2?8:(g_labHi5==3?5:(g_labHi5==4?10:11))]; int tip=-1;   /* 2: her LEFT hand; 3: her HEAD; 4: her right SHOULDER (the massage) */
        if(g_labHi5!=3&&g_labHi5!=4)
            for(unsigned n9=0;n9<P->nNodes;n9++) if(P->nodes[n9].parent==hb){ tip=(int)n9; break; }
        if(tip<0) tip=hb;
        float hx=W[tip][12],hy=W[tip][13],hz=W[tip][14];
        float wx=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
        float wy=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
        float wz=model[2]*hx+model[6]*hy+model[10]*hz+model[14];
        float dx=wx-g_labHeroHand[0], dy=wy-g_labHeroHand[1], dz=wz-g_labHeroHand[2];
        float d=sqrtf(dx*dx+dy*dy+dz*dz);
        /* report once per LOOP: the cursor grows without bound, so the wrap has to be
         * found in the wrapped frame, not in the raw phase (which passes 2.0 exactly
         * once, at boot, and never reports again). */
        double wph=fmod(phase,(double)(c->frames?c->frames:1));
        static float best=1e9f; static int bestF=-1; static double lastW=-1;
        static float bh[3],bp[3]; static float wmax=0.f;
        static float wmaxH=0.f; static int wmaxHF=-1;
        if(d<best){ best=d; bestF=(int)wph;
            bh[0]=g_labHeroHand[0]; bh[1]=g_labHeroHand[1]; bh[2]=g_labHeroHand[2];
            bp[0]=wx; bp[1]=wy; bp[2]=wz; }
        if(d>wmax) wmax=d;
        static float hh[3],hp[3];
        if(wph>0.20*c->frames&&wph<0.80*c->frames&&d>wmaxH){ wmaxH=d; wmaxHF=(int)wph;
            hh[0]=g_labHeroHand[0]; hh[1]=g_labHeroHand[1]; hh[2]=g_labHeroHand[2];
            hp[0]=wx; hp[1]=wy; hp[2]=wz; }
        if(wph<lastW&&best<1e8f){
            /* hi5=2 judges a HOLD, and a hold is judged by its worst moment INSIDE
             * the hold window (the middle of the clip) -- the ends legitimately
             * reach for and release the hand */
            if(g_labHi5==2)
                printf("[hold] hands: worst-in-hold %.3fu at frame %d/%u  hero (%.2f,%.2f,%.2f) partner (%.2f,%.2f,%.2f)  (whole %.3f, best %.3f)\n",
                       wmaxH,wmaxHF,c->frames,hh[0],hh[1],hh[2],hp[0],hp[1],hp[2],wmax,best);
            else if(g_labHi5==4)
                printf("[rub] hand-to-shoulder: closest %.3fu at frame %d/%u  worst-in-hold %.3fu  hero (%.2f,%.2f,%.2f) partner (%.2f,%.2f,%.2f)\n",
                       best,bestF,c->frames,wmaxH,bh[0],bh[1],bh[2],bp[0],bp[1],bp[2]);
            else if(g_labHi5==3)
                printf("[kiss] heads: closest %.3fu at frame %d/%u  worst-in-hold %.3fu  hero (%.2f,%.2f,%.2f) partner (%.2f,%.2f,%.2f)\n",
                       best,bestF,c->frames,wmaxH,bh[0],bh[1],bh[2],bp[0],bp[1],bp[2]);
            else
                printf("[hi5] closest %.3fu at frame %d/%u   hero hand (%.2f,%.2f,%.2f)  "
                       "partner (%.2f,%.2f,%.2f)   miss dx %.2f dy %.2f dz %.2f\n",
                       best,bestF,c->frames,bh[0],bh[1],bh[2],bp[0],bp[1],bp[2],
                       bp[0]-bh[0],bp[1]-bh[1],bp[2]-bh[2]);
            fflush(stdout); best=1e9f; bestF=-1; wmax=0.f; wmaxH=0.f; wmaxHF=-1; }
        lastW=wph; }
    ragdollTick(P,W,(const float*)model,g_castI);   /* ⭐ limp, if she is */
    jiggleTick(P,W,(const float*)model,g_castI);   /* ⭐ soft mass, then */
    ctSolve(P,W,(const float*)model,g_castI);   /* ⭐ THE CONTACT PASS: before the grip, the audit and the skin */
    /* ⭐ HER GRIP, in world space, published exactly like the head mark below it and
     * measured from the same FK. This is what a coffee cup hangs on. */
    { int gb=(P->gripNode>=0)?P->gripNode:P->pb[11];
      if(gb>=0){ ptWorld(W[gb],model,g_castGripW[g_castI]); g_castGripOK[g_castI]=1;
                 m4_mul(g_castGripM[g_castI],(float*)model,W[gb]); }
      attPublish(P,W,model,g_castI);
      /* ⭐ and her board off her clip's channel, at the phase she was sampled at (after any hold) */
      boardPublish(g_castI,borrowed9?g_labPDnP:P,borrowed9?g_labPDnC:clip,phase,borrowed9,model);
      clipAuditBody(P,W,model,g_castI); }   /* ⭐ this slot, exactly as drawn */
    if(P->pb[5]>=0){ float hx=W[P->pb[5]][12],hy=W[P->pb[5]][13],hz=W[P->pb[5]][14];
        g_partHeadW[0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
        g_partHeadW[1]=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
        g_partHeadW[2]=model[2]*hx+model[6]*hy+model[10]*hz+model[14];
        g_partHeadOK=1;
        /* ⭐ A MOUTH WITHOUT A FACE. The co-star (in her dressed pack) carries no eye marks on purpose -- with them the
         * aperture would cut her morph-driven mouth open -- so her exhales left a GUESS 0.054 u behind her lips and
         * no drag ever registered. A lone `face_mouth` (face_marks.py --only face_mouth) is published here for the
         * smoke; the aperture and the gaze still need the full set. */
        g_cast[g_castI].mouthOK=0;
        if(P->fmMouth>=0){ ptWorld(W[P->fmMouth],model,g_partMouthW); g_cast[g_castI].mouthOK=1; }
        if(P->fmMouth>=0&&P->fmEyeL>=0&&P->fmEyeR>=0){
            ptWorld(W[P->fmMouth],model,g_partMouthW);
            float el[3],er[3];
            ptWorld(W[P->fmEyeL],model,el); ptWorld(W[P->fmEyeR],model,er);
            for(int k9=0;k9<3;k9++) g_partEyeW[k9]=0.5f*(el[k9]+er[k9]);
            g_partIOD=sqrtf((el[0]-er[0])*(el[0]-er[0])+(el[1]-er[1])*(el[1]-er[1])+(el[2]-er[2])*(el[2]-er[2]));
            g_partFaceOK=1; } }
    if(g_scnT>=0.f){ const float*cw9=g_clipMorphW[(g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1];   /* (11-11) her clip's face shapes over the scene's: the larger of the two */
        for(int m9=0;m9<P->nMorph&&m9<MORPH_MAX;m9++) if(cw9[m9]>g_partMorphW[m9]) g_partMorphW[m9]=cw9[m9]; }
    { static int wasActP9[SCN_MAXCAST]; morphBlend(P,g_partMorphW,&wasActP9[(g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1]); }   /* her face talks too */
    chainPublish((g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1,model);   /* her chains feel where she goes */
    g_vkOwner=((unsigned)GFX_KIND_CAST<<28)|(((unsigned)g_castI&0xffffu)<<12);
    if(g_eyeEver&&g_castI>=1&&g_castI<8) memcpy(g_vkEye,g_castEyeG[g_castI],16);   /* (step 23) her eyes glow, if a line said so */
    { static const float wh9[3]={1.f,1.f,1.f}; BodyGL g9={g_castI,g_partMorphW,0,0,0,wh9}; bodyMeshes(P,W,model,pal,&g9); }
    tractDraw(P,W,model,(g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1);   /* ⭐ (11-06) her airway, if her line carries it (engine/scene.inc) */
    g_vkEye[3]=0.f;
    return 1; }
static float clipStride(Pack*P,int clip){
    /* Returns how far the CONTACT foot travels backwards, in engine units per clip frame.
     * Average stride-per-cycle is the wrong quantity for a run: contact is only a third
     * of the cycle, so a clip matched on cycle average still slides while the foot is
     * down. What actually locks a foot is its backward speed WHILE PLANTED equalling the
     * body's forward speed, so that is what gets measured -- over the low-foot frames
     * only. Rate then follows: rate = speed / (fps * this). */
    if(!P||clip<0||(unsigned)clip>=P->nClips||P->footL<0||P->footR<0||P->nNodes>512) return 0.f;
    PClip*c=&P->clips[clip]; if(c->frames<4) return 0.f;
    static float T[512][3],Q[512][4],S[512][3]; static M4 W[512];
    unsigned N=c->frames; if(N>60) N=60;
    static float fx[61][2],fz[61][2],fy[61][2];
    for(unsigned k=0;k<N;k++){
        unsigned f=(unsigned)((float)k/(float)N*(float)c->frames)%c->frames;
        bodyRest(P,T,Q,S);
        unsigned char*tr=P->pk+c->offTracks;
        for(unsigned t=0;t<c->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12),type=*(unsigned*)(tr+t*12+4),offD=*(unsigned*)(tr+t*12+8);
            if(node>=P->nNodes) continue;
            float*d=(float*)(P->pk+offD); int comp=type==0?4:3;
            if(type>=3) continue; else if(type==0) memcpy(Q[node],d+(size_t)f*comp,16);
            else if(type==1) memcpy(T[node],d+(size_t)f*comp,12);
            else memcpy(S[node],d+(size_t)f*comp,12); }
        bodyFK(P,T,Q,S,W);
        int fb[2]={P->footL,P->footR};
        for(int e=0;e<2;e++){ fx[k][e]=W[fb[e]][12]*P->scale;
                              fy[k][e]=W[fb[e]][13]*P->scale;
                              fz[k][e]=W[fb[e]][14]*P->scale; } }
    float lo=1e9f,hi=-1e9f;
    for(unsigned k=0;k<N;k++) for(int e=0;e<2;e++){ if(fy[k][e]<lo) lo=fy[k][e]; if(fy[k][e]>hi) hi=fy[k][e]; }
    float thr=lo+(hi-lo)*0.25f;      /* the lowest quarter of the foot's travel = weight-bearing */
    double sum=0.0; int n=0;
    for(unsigned k=1;k<N;k++) for(int e=0;e<2;e++){
        if(fy[k][e]>thr||fy[k-1][e]>thr) continue;
        float d9=hypotf(fx[k][e]-fx[k-1][e],fz[k][e]-fz[k-1][e]);
        if(d9>0.6f) continue;         /* a cycle wrap, not a step */
        sum+=d9; n++; }
    if(!n) return 0.f;
    float perSample=(float)(sum/n);
    /* IS THIS CYCLE LOCKABLE? In a real in-place gait the planted foot travels backwards
     * at a STEADY rate, so one playback rate can cancel it. If that speed swings wildly
     * through contact, no rate locks the foot and the clip is stylised, not locomotion.
     * Report the spread so the role audit can see it. */
    { double var=0.0; int m=0;
      for(unsigned k=1;k<N;k++) for(int e=0;e<2;e++){
          if(fy[k][e]>thr||fy[k-1][e]>thr) continue;
          float d9=hypotf(fx[k][e]-fx[k-1][e],fz[k][e]-fz[k-1][e]);
          if(d9>0.6f) continue;
          var+=(d9-perSample)*(d9-perSample); m++; }
      float cv=(m>1&&perSample>1e-5f)?(float)(sqrt(var/m)/perSample):0.f;
      if(getenv("REVA_GAITDBG"))
          printf("[gait] %s clip %d '%s': contact %.4fu/frame  spread CV %.2f  %s\n",
                 P->label,clip,packStr(P,c->name),perSample,cv,
                 cv>0.65f?"<- UNLOCKABLE: foot speed swings through contact":"steady"); }
    return perSample*((float)N/(float)c->frames);   /* -> per CLIP frame */ }
static float clipStrideCached(Pack*P,int clip){
    static struct { Pack*p; int c; float s; } cache[24]; static int cn=0;
    for(int i=0;i<cn;i++) if(cache[i].p==P&&cache[i].c==clip) return cache[i].s;
    float st=clipStride(P,clip);
    if(cn<24){ cache[cn].p=P; cache[cn].c=clip; cache[cn].s=st; cn++;
        if(getenv("REVA_GAITDBG")) printf("[gait] %s clip %d: contact-foot %.4fu/frame (%u frames @ %.0ffps)\n",
            P->label,clip,st,P->clips[clip].frames,P->clips[clip].fps); }
    return st; }
