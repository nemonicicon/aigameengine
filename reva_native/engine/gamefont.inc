/* ---- A GAME'S OWN TYPEFACE (11-12, the Godot proof of concept's step 9: the skate game's UI is set in two marker fonts, under the SIL Open Font
 * License). The TV font stays the engine's own, and the only one the engine ships (step 28); a game may bring its own from its folder, its
 * licence beside it. E->font(path, size) bakes a TrueType file's printable ASCII at that size of the 2D canvas (a line's height) in the display's
 * own pixels: the canvas's whole-number scale times the size, so a glyph is a texel to a pixel and the sprites' nearest sampling keeps it as sharp
 * as the screen. E->fontText draws a line on the canvas like a sprite, from the top-left of its line; E->fontWidth measures one. stb_truetype, which
 * the captions already compile in, reads the outlines. E->fontEdge gives a font an edge (their HUD's labels have one, so white letters read on a
 * white park): its sheet's coverage grown outward that far, a second sheet drawn under the letters in the edge's colour. Every font is packed
 * with room round each glyph for one.
 * A font is baked for the canvas's scale when it's asked for, and (11-12) baked again by itself, in its place, when the canvas's scale changes (the window
 * made bigger or smaller, fullscreen and back): a game keeps its handle.
 * ⛔ stb_truetype trusts its file: a game's font is the game's own, read like its code, never one a player hands it.
 * (11-12) 64 faces and sizes at once: 16 was a guess, and the skate game's own UI (their HUD, their gear wheel, their menus and their session) sets
 * type in three faces at over twenty sizes. A size is a sheet of its own, a few hundred kilobytes */
#define GF_MAX 64
typedef struct { int img, W, H, pad, edgeImg; float sc, ascent, line, edgePx, edgeC[3], size, edgeW; unsigned char *alpha; stbtt_packedchar ch[95]; char path[200]; int em; } GameFont;
static GameFont g_gf[GF_MAX]; static int g_gfN=0;
static void gfEdgeMake(GameFont*F);
/* the font's sheet at the canvas's scale now: into F, a new image or (again) its own image's place */
static int gfBake(GameFont*F,const char*path,float size,int again);
static int gfOpen(const char*path,float size,int em){
    if(!path||g_gfN>=GF_MAX||!(size>0.f)||size>240.f){ printf("[font] %s at %.1f: no (64 fonts, a size up to the canvas's height)\n",path?path:"?",size); return 0; }
    GameFont*F=&g_gf[g_gfN]; memset(F,0,sizeof *F); F->em=em;
    if(!gfBake(F,path,size,0)) return 0;
    snprintf(F->path,sizeof F->path,"%s",path); F->size=size;
    return ++g_gfN; }
static int gfFont(const char*path,float size){ return gfOpen(path,size,0); }
/* (11-12) A SIZE AS GODOT SAYS IT (E->fontEm; the skate game's comparison: their words came out smaller than theirs): the em, as Godot, a browser and a
 * word processor size type, where E->font's size is the whole line, top of the tallest letter to the foot of the deepest. A face's line is taller than its
 * em by its own measure (Arial Black's by 1.41, Permanent Marker's by 1.5), so a game laying out a Godot UI asks for its sizes this way */
static int gfFontEm(const char*path,float size){ return gfOpen(path,size,1); }
static int gfBake(GameFont*F,const char*path,float size,int again){
    FILE*f=fopen(path,"rb"); if(!f){ printf("[font] can't open %s\n",path); return 0; }
    fseek(f,0,SEEK_END); const long n=ftell(f); fseek(f,0,SEEK_SET);
    unsigned char*ttf=(n>64&&n<(32L<<20))?(unsigned char*)malloc((size_t)n):NULL;
    if(!ttf||fread(ttf,1,(size_t)n,f)!=(size_t)n){ fclose(f); free(ttf); printf("[font] can't read %s\n",path); return 0; }
    fclose(f); clubOpened(path);
    stbtt_fontinfo info; const int off=stbtt_GetFontOffsetForIndex(ttf,0);
    if(off<0||!stbtt_InitFont(&info,ttf,off)){ free(ttf); printf("[font] %s isn't a TrueType font\n",path); return 0; }
    F->sc=(float)(g_sprSy>0?g_sprSy:1);
    const float px=size*F->sc, s=F->em?stbtt_ScaleForMappingEmToPixels(&info,px):stbtt_ScaleForPixelHeight(&info,px);
    int asc=0,desc=0,gap=0; stbtt_GetFontVMetrics(&info,&asc,&desc,&gap);
    F->ascent=(float)asc*s; F->line=(float)(asc-desc+gap)*s;
    int W=256,H=256,ok=0; unsigned char*alpha=NULL; F->pad=(int)(px*0.14f)+2;   /* room round each glyph for an edge */
    for(int tries=0;tries<6&&!ok;tries++){
        free(alpha); alpha=(unsigned char*)calloc((size_t)W*(size_t)H,1); if(!alpha) break;
        stbtt_pack_context pc; if(!stbtt_PackBegin(&pc,alpha,W,H,0,F->pad,NULL)) break;
        ok=stbtt_PackFontRange(&pc,ttf,0,F->em?STBTT_POINT_SIZE(px):px,32,95,F->ch); stbtt_PackEnd(&pc);
        if(!ok){ if(W<=H) W*=2; else H*=2; } }
    free(ttf);
    if(!ok){ free(alpha); printf("[font] %s at %.0f px didn't fit a sheet\n",path,px); return 0; }
    unsigned char*rgba=(unsigned char*)malloc((size_t)W*(size_t)H*4); if(!rgba){ free(alpha); return 0; }
    for(int i=0;i<W*H;i++){ rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=255; rgba[i*4+3]=alpha[i]; }
    free(F->alpha); F->alpha=alpha;   /* kept, for an edge */
    if(again) sprImageReplace(F->img,rgba,W,H); else { F->img=sprImagePixels(rgba,W,H); if(!F->img) return 0; }
    F->W=W; F->H=H;
    printf("[font] %s at %.1f of the canvas: %.0f px, a %d x %d sheet%s\n",path,size,px,W,H,again?" (baked again for the canvas's new scale)":""); fflush(stdout);
    return 1; }
/* the canvas's scale moved since a font was baked (the window's size changed): baked again, and its edge with it */
static void gfFresh(GameFont*F){ const float sc=(float)(g_sprSy>0?g_sprSy:1); if(F->sc==sc||!F->path[0]) return;
    if(!gfBake(F,F->path,F->size,1)){ F->sc=sc; return; }
    if(F->edgeImg) gfEdgeMake(F); }
/* an edge, width canvas units round each letter (up to the room it was packed with), in this colour: the sheet's coverage grown outward by a disc */
static void gfEdge(int font,float width,float r,float g,float b){
    if(font<1||font>g_gfN) return;
    GameFont*F=&g_gf[font-1]; if(!F->alpha||F->edgeImg) return;
    F->edgeW=width; F->edgeC[0]=r; F->edgeC[1]=g; F->edgeC[2]=b;
    gfEdgeMake(F); }
static void gfEdgeMake(GameFont*F){
    int rad=(int)(F->edgeW*F->sc+0.5f); if(rad>F->pad-1) rad=F->pad-1;
    if(rad<1||!F->alpha) return;
    unsigned char*rgba=(unsigned char*)malloc((size_t)F->W*(size_t)F->H*4); if(!rgba) return;
    for(int y=0;y<F->H;y++) for(int x=0;x<F->W;x++){ int best=0;
        for(int dy=-rad;dy<=rad&&best<255;dy++){ const int yy=y+dy; if(yy<0||yy>=F->H) continue;
            for(int dx=-rad;dx<=rad;dx++){ const int xx=x+dx; if(xx<0||xx>=F->W||dx*dx+dy*dy>rad*rad) continue; const int v=F->alpha[(size_t)yy*(size_t)F->W+(size_t)xx]; if(v>best) best=v; } }
        unsigned char*o=rgba+((size_t)y*(size_t)F->W+(size_t)x)*4; o[0]=o[1]=o[2]=255; o[3]=(unsigned char)best; }
    if(F->edgeImg) sprImageReplace(F->edgeImg,rgba,F->W,F->H); else F->edgeImg=sprImagePixels(rgba,F->W,F->H);
    F->edgePx=(float)rad; }
static void gfText(int font,float x,float y,const char*s,float r,float g,float b,float a){
    if(font<1||font>g_gfN||!s) return;
    GameFont*F=&g_gf[font-1]; gfFresh(F);
    const float ox=floorf(x*F->sc+0.5f), oy=floorf(y*F->sc+0.5f);   /* the line's corner on a whole pixel, so a glyph lands texel on pixel */
    for(int pass=F->edgeImg?0:1;pass<2;pass++){   /* the edge under every letter first, then the letters */
        const float e=pass?0.f:F->edgePx; const int im=pass?F->img:F->edgeImg; const float*c3=pass?(const float[3]){r,g,b}:F->edgeC;
        float px=0.f, py=F->ascent;
        for(const unsigned char*c=(const unsigned char*)s;*c;c++){
            if(*c=='\n'){ px=0.f; py+=F->line; continue; }
            if(*c<32||*c>126) continue;
            stbtt_aligned_quad q; stbtt_GetPackedQuad(F->ch,F->W,F->H,*c-32,&px,&py,&q,1);
            if(q.x1<=q.x0) continue;
            sprDraw(im,(ox+q.x0-e)/F->sc,(oy+q.y0-e)/F->sc,(q.x1-q.x0+2.f*e)/F->sc,(q.y1-q.y0+2.f*e)/F->sc,q.s0*(float)F->W-e,q.t0*(float)F->H-e,q.s1*(float)F->W+e,q.t1*(float)F->H+e,c3[0],c3[1],c3[2],a); } } }
static float gfWidth(int font,const char*s){
    if(font<1||font>g_gfN||!s) return 0.f;
    GameFont*F=&g_gf[font-1]; gfFresh(F); float w=0.f, best=0.f;
    for(const unsigned char*c=(const unsigned char*)s;*c;c++){
        if(*c=='\n'){ if(w>best) best=w; w=0.f; continue; }
        if(*c>=32&&*c<=126) w+=F->ch[*c-32].xadvance; }
    if(w>best) best=w;
    return best/F->sc; }
