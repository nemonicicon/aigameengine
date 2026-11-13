/* ---- THE TV'S TEXT (11-02, engine plan Phase 2) ---------------------------------------------------------------
 * A character grid, the way a console has always done it: an 8x8 font on whole pixels, 35 columns by 24 rows inside the title-safe box (80% of the standard's
 * active picture, 704 x 480), and the same layout at 480i, 240p and on a PC. A cell is 16 x 16 at 480i, 16 x 8 at 240p, and whole-number square on a window, so no
 * mode ever scales a glyph and blurs it. A game says which column and row, and doesn't know the mode. ⛔ Nothing is tuned to one set: a set that overscans more is what
 * the player's own screen fit is for (Phase 4). This is also where 2D goes through for both renderers: hudTris draws on GL and Vulkan.
 * THE READING SIZE (11-03, engine plan Phase 4, the last screen of setup): the player picks the smallest letters they can read, which covers small sets and poor decoders together. The bigger two are 23 x 12 and
 * 17 x 12 cells in the same safe box, on whole pixels, and the same shape on the glass at 480i and 240p. A game always places its text on 35 x 24 and doesn't know: its lines land in proportion, and a line that no
 * longer fits drops a size, wraps at its spaces, or moves out of the way of the one before it, in the order of their rows (tvFlush). At the smallest size none of that runs. */
#define TV_SAFE 0.80f   /* the font's two sheets, TvCh and tvGlyphOf are in engine/tvgrown.inc, ahead of this */
/* ⭐ TEXT BEYOND ASCII (queue step 26, 11-05). A string is UTF-8. Inside here it's one unsigned short a cell (TvCh), its code point, so everything that counts cells,
 * breaks lines and fits a page counts letters and not bytes. A letter and a combining mark after it become the one letter the font has (TV_PAIR). The font's second
 * sheet is accented Latin, Greek and Cyrillic (tvfont2.inc). A code point neither sheet has is looked for among the WIDE GLYPHS, 16 x 16 dots from a file a language
 * names (data/<name>.tvw, skate_src/author/fontbake.py), which is how Chinese, Japanese and Korean are drawn: one cell each at the large reading size, where a cell has
 * the room. What's in none of them is a `?`. Past the first 65,536 code points is a `?` too. */
typedef struct { unsigned char x,y,w,h; } TvRun;
static TvRun g_tvRun[96+TV_MORE][TV_RUNS]; static int g_tvNRun[96+TV_MORE];
/* a string as cells. How many, and never more than max-1 */
static int tvCells(TvCh*o,int max,const char*s){ int n=0; const unsigned char*p=(const unsigned char*)s;
    while(*p&&n<max-1){ unsigned c=*p++;
        if(c>=0xF0){ c=0xFFFD; for(int k=0;k<3&&(*p&0xC0)==0x80;k++) p++; }                                   /* past the 65,536 this keeps */
        else if(c>=0xE0){ if((p[0]&0xC0)!=0x80||(p[1]&0xC0)!=0x80){ while((*p&0xC0)==0x80) p++; continue; } c=((c&0x0Fu)<<12)|((p[0]&0x3Fu)<<6)|(p[1]&0x3Fu); p+=2; }   /* a letter cut short (a string trimmed by bytes) is left out */
        else if(c>=0xC0){ if((p[0]&0xC0)!=0x80) continue; c=((c&0x1Fu)<<6)|(p[0]&0x3Fu); p++; }
        else if(c>=0x80) c=0xFFFD;                                                                             /* a byte from the middle of a letter: the text isn't UTF-8 */
        if(c>=0x300&&c<0x370){ if(n>0) for(int k=0;k<TV_PAIRS;k++) if(TV_PAIR[k][0]==o[n-1]&&TV_PAIR[k][1]==c){ o[n-1]=TV_PAIR[k][2]; break; } continue; }   /* a mark for the letter before it */
        if(c==0xFEFF||c==0x200B||c==0x200C||c==0x200D) continue;                                               /* nothing to draw */
        if(c==0x3000) c=32;
        o[n++]=(TvCh)c; }
    o[n]=0; return n; }
static int tvLen(const char*s){ TvCh b[256]; return tvCells(b,256,s); }
static int tvLenW(const TvCh*s){ int n=0; while(s[n]) n++; return n; }
/* the letter at s as a code point, and how many bytes it is: for what has to walk a string by letters without making cells of it */
static unsigned tvLetter(const char*s,int*used){ const unsigned char*p=(const unsigned char*)s; unsigned c=p[0]; int n=1;
    if(c>=0xF0){ c=0xFFFD; while(n<4&&(p[n]&0xC0)==0x80) n++; }
    else if(c>=0xE0){ if((p[1]&0xC0)==0x80&&(p[2]&0xC0)==0x80){ c=((c&0x0Fu)<<12)|((p[1]&0x3Fu)<<6)|(p[2]&0x3Fu); n=3; } else { c=0xFFFD; while((p[n]&0xC0)==0x80) n++; } }
    else if(c>=0xC0){ if((p[1]&0xC0)==0x80){ c=((c&0x1Fu)<<6)|(p[1]&0x3Fu); n=2; } else c=0xFFFD; }
    else if(c>=0x80){ c=0xFFFD; while((p[n]&0xC0)==0x80) n++; }
    *used=n; return c; }
/* WHERE A ROW CAN END. At a space; and Chinese, Japanese and Korean have none, so beside any of their letters too, but never so that a closing mark starts the next row
 * or an opening one ends this one. For text with no such letter this is the space rule and nothing else */
#define TV_WIDECH(c) ((c)>=0x2E80u&&(c)!=0xFFFDu)
static int tvNoStart(unsigned c){ static const TvCh N[]={0x3001,0x3002,0xFF0C,0xFF0E,0xFF01,0xFF1F,0xFF1A,0xFF1B,0xFF09,0x300D,0x300F,0x3011,0x3015,0x3009,0x300B,0x30FC,0x3005,0x30FB,',','.','!','?',':',';',')',0};
    for(int i=0;N[i];i++) if(N[i]==c) return 1;
    return 0; }
static int tvNoEnd(unsigned c){ static const TvCh N[]={0xFF08,0x300C,0x300E,0x3010,0x3014,0x3008,0x300A,'(',0};
    for(int i=0;N[i];i++) if(N[i]==c) return 1;
    return 0; }
static int tvBreak(const TvCh*s,int i){ if(s[i]==' ') return 1;
    if(i<1||(!TV_WIDECH(s[i])&&!TV_WIDECH(s[i-1]))) return 0;
    return !tvNoStart(s[i])&&!tvNoEnd(s[i-1]); }
/* ---- the wide glyphs: a file of them, and the few in use kept as rectangles */
#define TVW_RUNS 112
#define TVW_KEEP 512
typedef struct { TvCh cp; unsigned char n; unsigned char r[TVW_RUNS][4]; } TvWide;
static unsigned char*g_tvwFile=NULL; static const unsigned char*g_tvwCp=NULL,*g_tvwBits=NULL; static int g_tvwN=0; static TvWide*g_tvwKeep=NULL;
static void tvWideDrop(void){ free(g_tvwFile); g_tvwFile=NULL; g_tvwN=0; if(g_tvwKeep) memset(g_tvwKeep,0,sizeof(TvWide)*TVW_KEEP); }
/* data/<name>.tvw: `TVW1`, how many, 16, then every code point (two bytes, low first), sorted, then 32 bytes a glyph: 16 rows, the left dot the top bit of the first byte */
static int tvWideLoad(const char*name){ tvWideDrop();
    for(const char*c=name;*c;c++) if(!((*c>='a'&&*c<='z')||(*c>='A'&&*c<='Z')||(*c>='0'&&*c<='9')||*c=='-'||*c=='_')) return 0;
    char path[96]; snprintf(path,sizeof path,"data/%s.tvw",name); FILE*f=fopen(path,"rb"); if(!f){ printf("[tv] no %s: its glyphs show as ?\n",path); return 0; }
    clubOpened(path);
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET); unsigned char*b=(n>=12&&n<(64<<20))?(unsigned char*)malloc((size_t)n):NULL;
    if(!b||fread(b,1,(size_t)n,f)!=(size_t)n){ fclose(f); free(b); printf("[tv] %s can't be read\n",path); return 0; }
    fclose(f);
    unsigned cnt=(unsigned)b[4]|((unsigned)b[5]<<8)|((unsigned)b[6]<<16)|((unsigned)b[7]<<24), dots=(unsigned)b[8]|((unsigned)b[9]<<8);
    if(memcmp(b,"TVW1",4)||dots!=16u||cnt>65535u||12u+(unsigned long)cnt*34u>(unsigned long)n){ free(b); printf("[tv] %s isn't a file of 16-dot glyphs\n",path); return 0; }
    for(unsigned i=1;i<cnt;i++) if((b[12+i*2]|(b[13+i*2]<<8))<=(b[10+i*2]|(b[11+i*2]<<8))){ free(b); printf("[tv] %s: its code points aren't in order\n",path); return 0; }   /* a search needs them sorted, and a file is never trusted */
    g_tvwFile=b; g_tvwCp=b+12; g_tvwBits=b+12+(size_t)cnt*2u; g_tvwN=(int)cnt;
    if(!g_tvwKeep) g_tvwKeep=(TvWide*)calloc(TVW_KEEP,sizeof(TvWide));
    printf("[tv] %s: %u wide glyphs\n",path,cnt); return g_tvwKeep!=NULL; }
static const TvWide* tvWideOf(TvCh c){
    if(!g_tvwN||!g_tvwKeep) return NULL;
    TvWide*K=&g_tvwKeep[(c*40503u)&(TVW_KEEP-1)]; if(K->cp==c&&K->n) return K;
    int lo=0,hi=g_tvwN-1,at=-1; while(lo<=hi){ const int m=(lo+hi)/2; const unsigned v=(unsigned)g_tvwCp[m*2]|((unsigned)g_tvwCp[m*2+1]<<8); if(v==c){ at=m; break; } if(v<c) lo=m+1; else hi=m-1; }
    if(at<0) return NULL;
    unsigned char a[16][16]; const unsigned char*g=g_tvwBits+(size_t)at*32u; int n=0;
    for(int y=0;y<16;y++) for(int x=0;x<16;x++) a[y][x]=(unsigned char)((g[y*2+(x>>3)]>>(7-(x&7)))&1u);
    for(int y=0;y<16;y++) for(int x=0;x<16;x++){ if(!a[y][x]) continue;
        int w=1; while(x+w<16&&a[y][x+w]) w++;
        int h=1; for(;y+h<16;h++){ int ok=1; for(int i=0;i<w;i++) if(!a[y+h][x+i]) ok=0; if(!ok) break; }
        for(int j=0;j<h;j++) for(int i=0;i<w;i++) a[y+j][x+i]=0;
        if(n<TVW_RUNS){ K->r[n][0]=(unsigned char)x; K->r[n][1]=(unsigned char)y; K->r[n][2]=(unsigned char)w; K->r[n][3]=(unsigned char)h; n++; } }
    K->cp=c; K->n=(unsigned char)n; return n?K:NULL; }
static int g_tvOk=0,g_tvW=0,g_tvH=0,g_tvSx=1,g_tvSy=1,g_tvX0=0,g_tvY0=0;
static int g_tvWide=0;   /* (11-06) a game's E->tune->hudWide: the grid as wide as the screen at the same size of letter (tvUse) */
static int g_tvCx=-1,g_tvCy=-1;   /* where a screen fit puts the grid's middle; -1 is the raster's */
/* (11-06) DESK, the fourth size: smaller letters than the small one, for a screen a player sits close to (at 2560 x 1440 the TV's 24 rows were very big).
 * The font's dot is a 360th of the height, on whole pixels (2 at 720 lines, 3 at 1080, 4 at 1440, 6 at 2160), and the grid is as many cells as fit in 92% of the screen,
 * because a PC's screen cuts nothing off: 60 x 41 at those heights, or the screen's width with E->tune->hudWide. Only where the pixels are square and there are 360 lines
 * (tvDeskFits): a composite raster or a smaller one is given the small size, and setup doesn't offer it there. A game still places on 35 x 24, and at desk its places are
 * taken in proportion, rows as well as columns (TV_KY), in the smaller letters. Nothing needs fitting: what didn't overlap at the small size can't at a smaller one */
#define TV_DESK 3
#define TV_NREAD 4
#define TV_DESKBOX 0.92f
static const struct { int cols,rows; const char*name; } TV_READ[TV_NREAD]={{TV_COLS,TV_ROWS,"small"},{23,12,"medium"},{17,12,"large"},{60,41,"desk"}};   /* desk's is what the standard heights give: tvUse works it out */
static int g_tvRead=0,g_tvCols=TV_COLS,g_tvRows=TV_ROWS,g_tvComp=0;   /* the player's reading size, and the grid it gives */
static int tvReadOf(const char*n){ for(int i=0;i<TV_NREAD;i++) if(!strcmp(n,TV_READ[i].name)) return i; return -1; }
/* a reading size with bigger letters than the small one: the sizes whose page is fitted (TV_FIT) */
#define TV_BIGREAD (TV_READ[g_tvRead].rows<TV_ROWS)
/* desk's dot, and whether this screen can have it: square pixels, 360 lines, and the 35 columns a game places on across */
static int tvDeskPx(void){ return g_tvH/360>0?g_tvH/360:1; }
static int tvDeskFits(void){ return !g_tvComp&&g_tvH>=360&&(int)(TV_DESKBOX*(float)g_tvW/(float)(8*tvDeskPx()))>=TV_COLS; }
#define TV_PEAK 0.8f   /* persistent text stays well below peak white: wear goes with beam current and static bright text is what burns (design: research, section 7) */
/* flat 2D, on a canvas of cw x ch with 0,0 at the top left, in whatever blend state the HUD block set. Sprites waiting in a batch go down first: what's drawn later is on top, whatever it is */
static void sprFlush(void);
static void hudTris(const float*xy,int nVerts,float cw,float ch,float r,float g,float b,float a){
    sprFlush();
    if(!g_noDraw) gfxHud(xy,nVerts,cw,ch,r,g,b,a);
}
/* a glyph is a few rectangles: each row's runs, grown downward while the rows below repeat them */
static void tvFontInit(void){
    for(int c=0;c<96+TV_MORE;c++){ char a[8][9]; int n=0;
        if(c<96) for(int y=0;y<8;y++){ memcpy(a[y],TV_FONT[(c/16)*8+y]+(c%16)*9,8); a[y][8]=0; }
        else for(int y=0;y<8;y++){ for(int x=0;x<8;x++) a[y][x]=((TV_MORE_BITS[c-96][y]>>(7-x))&1)?'#':'.'; a[y][8]=0; }   /* the second sheet, as rows of bits */
        for(int y=0;y<8;y++) for(int x=0;x<8;x++){ if(a[y][x]!='#') continue;
            int w=1; while(x+w<8&&a[y][x+w]=='#') w++;
            int h=1; for(;y+h<8;h++){ int ok=1; for(int i=0;i<w;i++) if(a[y+h][x+i]!='#') ok=0; if(!ok) break; }
            for(int j=0;j<h;j++) for(int i=0;i<w;i++) a[y+j][x+i]='.';
            if(n<TV_RUNS){ TvRun*R=&g_tvRun[c][n++]; R->x=(unsigned char)x; R->y=(unsigned char)y; R->w=(unsigned char)w; R->h=(unsigned char)h; } }
        g_tvNRun[c]=n; } }
/* the safe box and the cell, from the raster, for one reading size. composite: the 720-wide standard rasters, where 704 x the lines is the active picture and a pixel isn't square. */
static void sprInit(int W,int H,int composite);
static void tvUse(int read){ if(g_langWide) read=2;   /* (step 26) a language of wide glyphs is read at the large size: its letters need a cell that big */
    if(read==TV_DESK&&!tvDeskFits()){ static int told=0; if(!told){ told=1; printf("[tv] desk letters want square pixels and 360 lines: the small size on this %dx%d\n",g_tvW,g_tvH); fflush(stdout); } read=0; }
    int W=g_tvW,H=g_tvH,c=TV_READ[read].cols,r=TV_READ[read].rows;
    if(read==TV_DESK){ const int s=tvDeskPx(), cm=(int)(TV_DESKBOX*(float)W/(float)(8*s));   /* the rows that fit, the columns in the shape of the small size's, or all of them when wide */
        r=(int)(TV_DESKBOX*(float)H/(float)(8*s)); c=(r*TV_COLS+TV_ROWS/2)/TV_ROWS;
        if(g_tvWide||c>cm) c=cm;
        g_tvCols=c; g_tvRows=r; g_tvSx=g_tvSy=s; g_tvX0=(W-c*8*s)/2; g_tvY0=(H-r*8*s)/2; return; }
    float sw=TV_SAFE*(g_tvComp?704.f*(float)W/720.f:(float)W), sh=TV_SAFE*(float)H;
    /* (11-06) wide: the letter's size from the height alone, and as many columns as fit across, when that's at least the 35 a game places on. Never on a composite raster */
    if(g_tvWide&&!g_tvComp){ const int s=(int)(sh/(float)(r*8)), cw=s>0?(int)(sw/(float)(8*s)):0; if(s>=1&&cw>c) c=cw; }
    g_tvCols=c; g_tvRows=r;
    g_tvSx=(int)(sw/(float)(c*8)); g_tvSy=(int)(sh/(float)(r*8));
    if(g_tvSx<1) g_tvSx=1;
    if(g_tvSy<1) g_tvSy=1;
    if(!g_tvComp){ int s=g_tvSx<g_tvSy?g_tvSx:g_tvSy; if(g_langWide&&s>1) s&=~1; g_tvSx=g_tvSy=s; }   /* a wide glyph's dot is half a font pixel: an even one keeps it whole */
    g_tvX0=(W-c*8*g_tvSx)/2; g_tvY0=(H-r*8*g_tvSy)/2;
    if(g_tvCx>=0){ int x9=g_tvCx-c*4*g_tvSx, y9=g_tvCy-r*4*g_tvSy, xm=W-c*8*g_tvSx, ym=H-r*8*g_tvSy;
        g_tvX0=x9<0?0:(x9>xm?xm:x9); g_tvY0=y9<0?0:(y9>ym?ym:y9); } }
static void tvInit(int W,int H,int composite){
    if(!g_tvOk){ tvFontInit(); g_tvOk=1; }
    sprInit(W,H,composite);
    g_tvW=W; g_tvH=H; g_tvComp=composite;
    tvUse(g_tvRead);
    printf("[tv] %dx%d: a cell is %dx%d, the grid starts at %d,%d, %d x %d cells\n",W,H,8*g_tvSx,8*g_tvSy,g_tvX0,g_tvY0,g_tvCols,g_tvRows); }
static float* tvRect(float*o,float x0,float y0,float x1,float y1){
    *o++=x0;*o++=y0; *o++=x1;*o++=y0; *o++=x1;*o++=y1; *o++=x0;*o++=y0; *o++=x1;*o++=y1; *o++=x0;*o++=y1; return o; }
/* size 1 is one cell a character; size 2 is two wide and two tall */
static void tvPrint(float col,float row,int size,const TvCh*s,float dx,float dy,float r,float g,float b,float a){
    static float v[2*6*(TVW_RUNS+32*128)]; float*o=v; float px=(float)(g_tvSx*size),py=(float)(g_tvSy*size);
    float cx=(float)g_tvX0+col*8.f*(float)g_tvSx+dx,cy=(float)g_tvY0+row*8.f*(float)g_tvSy+dy;
    for(int i=0;s[i]&&i<128;i++){ int c=tvGlyphOf(s[i]); const TvWide*w=(c<0)?tvWideOf(s[i]):NULL;
        if(w){ const float hx=px*0.5f,hy=py*0.5f;   /* a wide glyph: 16 x 16 dots in the cell, each half a font pixel */
            for(int k=0;k<w->n;k++) o=tvRect(o,cx+w->r[k][0]*hx,cy+w->r[k][1]*hy,cx+(w->r[k][0]+w->r[k][2])*hx,cy+(w->r[k][1]+w->r[k][3])*hy); }
        else { if(c<0) c='?'-32;
            for(int k=0;k<g_tvNRun[c];k++){ const TvRun*R=&g_tvRun[c][k];
                o=tvRect(o,cx+R->x*px,cy+R->y*py,cx+(R->x+R->w)*px,cy+(R->y+R->h)*py); } }
        cx+=8.f*px;
        if((size_t)(o-v)>sizeof v/sizeof v[0]-(size_t)(12*TVW_RUNS)){ hudTris(v,(int)((o-v)/2),(float)g_tvW,(float)g_tvH,r,g,b,a); o=v; } }   /* a line of busy glyphs goes down in parts */
    int n=(int)((o-v)/2); if(n>0) hudTris(v,n,(float)g_tvW,(float)g_tvH,r,g,b,a); }
/* text at a column and row of the grid in use, with a hard shadow one font pixel down and right: a picture behind it is never the same brightness all the way along */
static void tvDrawW(float col,float row,int size,const TvCh*s,float r,float g,float b){
    tvPrint(col,row,size,s,(float)(g_tvSx*size),(float)(g_tvSy*size),0.f,0.f,0.f,0.85f);
    tvPrint(col,row,size,s,0.f,0.f,r*TV_PEAK,g*TV_PEAK,b*TV_PEAK,1.f); }
static void tvDraw(float col,float row,int size,const char*s,float r,float g,float b){ TvCh w[130]; tvCells(w,130,s); tvDrawW(col,row,size,w,r,g,b); }
/* a bar in cells, filled to frac, on a dark back */
static void tvBarDraw(float col,float row,float cols,float frac,float r,float g,float b){
    float v[12],x0=(float)g_tvX0+col*8.f*(float)g_tvSx,y0=(float)g_tvY0+row*8.f*(float)g_tvSy,w=cols*8.f*(float)g_tvSx,h=7.f*(float)g_tvSy;
    if(frac<0.f) frac=0.f;
    if(frac>1.f) frac=1.f;
    tvRect(v,x0,y0,x0+w,y0+h); hudTris(v,6,(float)g_tvW,(float)g_tvH,0.f,0.f,0.f,0.7f);
    float fw=(float)(int)(w*frac); if(fw>0.f){ tvRect(v,x0,y0,x0+fw,y0+h); hudTris(v,6,(float)g_tvW,(float)g_tvH,r*TV_PEAK,g*TV_PEAK,b*TV_PEAK,1.f); } }

/* ---- fitting a game's 35 x 24 onto bigger letters. The frame's text waits here until the HUD is done, because where a line goes depends on what else is on the page. Only strings and counts go into it, so it's the same on every machine, and the sim never sees it */
enum { TV_LEFT, TV_RIGHT, TV_CENTRE, TV_BARS };
#define TV_ITEMS 192
#define TV_WRAP 12
typedef struct { int how,size,asked,nl,y,y0,h,group,view; float col,row,cols,frac,r,g,b,x0,x1,ax; short st[TV_WRAP],len[TV_WRAP]; TvCh s[128]; } TvItem;   /* asked: the size the game said. y0, ax: where it would like to go */
static int g_tvView=-1,g_tvNViews=1;   /* E->tvView: whose picture of a split screen the text goes in. -1 is the whole screen */
/* that picture's part of the grid. The cells stay the same size, so it's fewer of them, and the safe box is still the whole screen's */
static void tvViewRect(int view,int*c0,int*r0,int*c,int*r){ *c0=0; *r0=0; *c=g_tvCols; *r=g_tvRows; if(view<0||g_tvNViews<2) return;
    *r=g_tvRows/2; *r0=(g_tvNViews>2?view/2:view)*(*r);
    if(g_tvNViews>2){ *c=(g_tvCols-1)/2; *c0=(view%2)*(*c+1); } }
static TvItem g_tvItem[TV_ITEMS]; static int g_tvNItem=0; static int g_tvGroup=0;   /* what's in one group gets a dark panel behind it, wherever the layout puts it */
static void tvQueue(int how,float col,float row,float cols,float frac,int size,const char*s,float r,float g,float b){
    if(g_tvNItem>=TV_ITEMS){ static int told=0; if(!told){ told=1; printf("[tv] more than %d pieces of text in one frame: the rest aren't drawn\n",TV_ITEMS); fflush(stdout); } return; }
    TvItem*t=&g_tvItem[g_tvNItem++]; t->how=how; t->group=g_tvGroup; t->view=g_tvView; t->col=col; t->row=row; t->cols=cols; t->frac=frac; t->size=t->asked=size<1?1:size; t->r=r; t->g=g; t->b=b; tvCells(t->s,128,s); }
/* break at spaces, and at a group of them if one's in reach, because that's where a hint's parts meet ("A TRY IT   B NOT NOW"). A word longer than the grid is cut. How many lines it took */
static int tvWrapAt(TvItem*t,int limit){ const TvCh*s=t->s; int at=0,n=tvLenW(s); t->nl=0;
    while(t->nl<TV_WRAP){ while(s[at]==' ') at++;
        if(at>=n) break;
        int end=at+limit;
        if(end>=n) end=n;
        else { int e=end,g=0; for(int i=end;i>at&&!g;i--) if(s[i]==' '&&s[i-1]==' ') g=i;
            while(e>at&&!tvBreak(s,e)) e--;
            if(g) end=g; else if(e>at) end=e; }
        int m=end-at; while(m>0&&s[at+m-1]==' ') m--;
        t->st[t->nl]=(short)at; t->len[t->nl]=(short)m; t->nl++; at=end; }
    return t->nl; }
/* the same number of lines at the narrowest measure that still gives it, so a paragraph's lines come out even and its last word isn't left alone */
static void tvWrap(TvItem*t,int limit){ const int k=tvWrapAt(t,limit); if(k<2) return;
    int word=0,run=0; for(int i=0;;i++){ const TvCh ch=t->s[i]; if(ch&&ch!=' '&&!(run&&tvBreak(t->s,i))) run++; else { if(run>word) word=run; run=(ch&&ch!=' ')?1:0; if(!ch) break; } }   /* the longest piece that can't be broken */
    int best=limit; for(int l=limit-1;l>=word&&tvWrapAt(t,l)==k;l--) best=l;
    tvWrapAt(t,best); }
/* how big it is on this grid and where it would like to go: in proportion, whole rows and whole cells */
static void tvMeasure(TvItem*t){ const int c=g_tvCols,r=g_tvRows;
    if(t->how==TV_BARS){ float w=(float)(int)(t->cols*(float)c/(float)TV_COLS+0.999f), x=(float)(int)(t->col*(float)c/(float)TV_COLS+0.5f);
        if(w<3.f) w=3.f;
        if(w>(float)c) w=(float)c;
        if(x+w>(float)c) x=(float)c-w;
        t->x0=x; t->x1=x+w; t->h=1; t->nl=0; }
    else { int n=tvLenW(t->s),mx=0;
        while(t->size>1&&n*t->size>c) t->size--;   /* a size down on a bigger cell is still the size the game was looking at */
        tvWrap(t,c/t->size);
        for(int i=0;i<t->nl;i++) if(t->len[i]>mx) mx=t->len[i];
        float w=(float)(mx*t->size);
        if(t->how==TV_LEFT){ float x=(float)(int)(t->col*(float)c/(float)TV_COLS+0.5f); t->x0=(x+w>(float)c)?(float)c-w:x; }
        else if(t->how==TV_RIGHT) t->x0=(float)c-w;
        else t->x0=((float)c-w)*0.5f;
        t->x1=t->x0+w; t->h=t->nl*t->size; }
    int y=(int)(t->row*(float)r/(float)TV_ROWS+0.5f);
    if(y+t->h>r) y=r-t->h;
    t->y0=y<0?0:y; t->ax=t->x0; }
/* did the game leave a blank row between these two? Then they'd rather stay apart */
static int tvBlank(const TvItem*a,const TvItem*b){ const TvItem*f=(a->row<=b->row)?a:b,*s=(f==a)?b:a; return s->row>=f->row+(float)f->asked+1.f; }
/* who's in the way of this one where it is now: on the same row two pieces always keep a cell between them, or they'd read as one */
static int tvHit(const int*ord,int n,const TvItem*t,int gaps){
    for(int i=0;i<n;i++){ const TvItem*o=&g_tvItem[ord[i]]; const int g=(gaps&&tvBlank(o,t))?1:0;
        if(o->h>0&&t->y<o->y+o->h+g&&o->y<t->y+t->h+g&&t->x0<o->x1+1.f&&o->x0<t->x1+1.f) return ord[i]; }
    return -1; }
/* the page, 1 if everything found a place */
static int tvLay(const int*ord,int n,int gaps){ const int c=g_tvCols,r=g_tvRows; int fit=1;
    /* down it, by the rows the game asked for: each takes its place, and whatever's in the way pushes it on. A piece anchored on the left tries beside what's in its way first, so a label keeps its bar */
    for(int k=0;k<n;k++){ TvItem*t=&g_tvItem[ord[k]]; const float w=t->x1-t->x0; t->y=t->y0; t->x0=t->ax; t->x1=t->ax+w;
        for(;;){ const int hit=tvHit(ord,k,t,gaps);
            if(hit<0) break;
            const TvItem*o=&g_tvItem[hit];
            if((t->how==TV_LEFT||t->how==TV_BARS)&&!tvBlank(o,t)&&o->x1+1.f+w<=(float)c){ t->x0=o->x1+1.f; t->x1=t->x0+w; continue; }
            t->y++; t->x0=t->ax; t->x1=t->ax+w; } }
    /* and back up it if that ran off the bottom, so the order stays */
    for(int k=n-1;k>=0;k--){ TvItem*t=&g_tvItem[ord[k]];
        if(t->y+t->h>r) t->y=r-t->h;
        if(t->y<0) t->y=0;
        while(t->y>0&&tvHit(ord+k+1,n-k-1,t,gaps)>=0) t->y--;
        if(tvHit(ord+k+1,n-k-1,t,gaps)>=0) fit=0; }
    return fit; }
/* one picture's text, on the grid it has */
static void tvFlushView(int view,const int all){ const int c=g_tvCols; static int ord[TV_ITEMS]; int n=0,maxg=0;
    for(int i=0;i<all;i++){ TvItem*t=&g_tvItem[i]; if(t->view!=view) continue; tvMeasure(t); int k=n++; if(t->group>maxg) maxg=t->group;
        while(k>0&&g_tvItem[ord[k-1]].row>t->row){ ord[k]=ord[k-1]; k--; }
        ord[k]=i; }
    if(!n) return;
    /* with the blank rows the game left, if there's room for them */
    if(!tvLay(ord,n,1)&&!tvLay(ord,n,0)){ static int told=0; if(!told){ told=1; printf("[tv] more text in a frame than the %s reading size has room for: some of it overlaps\n",TV_READ[g_tvRead].name); fflush(stdout); } }
    /* the panels, round where each group's pieces landed */
    for(int g=1;g<=maxg;g++){ float x0=1e9f,x1=-1e9f,y0=1e9f,y1=-1e9f; int any=0;
        for(int i=0;i<all;i++){ const TvItem*t=&g_tvItem[i]; if(t->group!=g||t->view!=view) continue; any=1; if(t->x0<x0) x0=t->x0; if(t->x1>x1) x1=t->x1; if((float)t->y<y0) y0=(float)t->y; if((float)(t->y+t->h)>y1) y1=(float)(t->y+t->h); }
        if(!any) continue;
        { float v[12]; const float cw=8.f*(float)g_tvSx,ch=8.f*(float)g_tvSy; tvRect(v,(float)g_tvX0+(x0-0.5f)*cw,(float)g_tvY0+(y0-0.25f)*ch,(float)g_tvX0+(x1+0.5f)*cw,(float)g_tvY0+(y1+0.25f)*ch); hudTris(v,6,(float)g_tvW,(float)g_tvH,0.f,0.f,0.f,0.62f); } }
    /* drawn in the order the game drew them */
    for(int i=0;i<all;i++){ const TvItem*t=&g_tvItem[i]; TvCh b9[130]; if(t->view!=view) continue;
        if(t->how==TV_BARS){ tvBarDraw(t->x0,(float)t->y,t->x1-t->x0,t->frac,t->r,t->g,t->b); continue; }
        for(int l=0;l<t->nl;l++){ const int m=t->len[l]; memcpy(b9,t->s+t->st[l],(size_t)m*sizeof(TvCh)); b9[m]=0;
            const float x=(t->how==TV_LEFT)?t->x0:(t->how==TV_RIGHT)?(float)(c-m*t->size):((float)(c-m*t->size))*0.5f;
            tvDrawW(x,(float)(t->y+l*t->size),t->size,b9,t->r,t->g,t->b); } } }
static void tvFlush(void){ const int all=g_tvNItem; if(!all) return;
    g_tvNItem=0; tvFlushView(-1,all);
    for(int v=0;v<g_tvNViews&&g_tvNViews>1;v++){ const int wc=g_tvCols,wr=g_tvRows,wx=g_tvX0,wy=g_tvY0; int c0,r0,c,r; tvViewRect(v,&c0,&r0,&c,&r);
        g_tvCols=c; g_tvRows=r; g_tvX0=wx+c0*8*g_tvSx; g_tvY0=wy+r0*8*g_tvSy; tvFlushView(v,all);
        g_tvCols=wc; g_tvRows=wr; g_tvX0=wx; g_tvY0=wy; } }
/* what everything else calls. At the smallest size it's what it always was */
/* (step 26) with a language chosen the page is fitted at the small size too: its words aren't the lengths the game laid out for, and a label that's grown mustn't lie over its bar.
 * TV_FIT is the question anything that draws a panel itself asks: is the text going through the layout, so a panel has to be a group and go where its text goes?
 * (11-06) Bigger letters than the small size, or a language: desk's smaller ones don't need it */
#define TV_FIT (TV_BIGREAD||g_langN)
/* (11-06) the grid's columns over the 35 a game places on: 1, except on a wide grid (E->tune->hudWide), where a column goes where it would in proportion */
#define TV_KX ((float)g_tvCols/(float)TV_COLS)
/* (11-06) and its rows over the 24: 1, except at desk, where a row goes where it would in proportion too */
#define TV_KY ((float)g_tvRows/(float)TV_ROWS)
static void tvText(float col,float row,int size,const char*s,float r,float g,float b){ s=langWord(s); if(TV_FIT||g_tvView>=0) tvQueue(TV_LEFT,col,row,0.f,0.f,size,s,r,g,b); else tvDraw(col*TV_KX,row*TV_KY,size,s,r,g,b); }
/* the same, ending at the grid's right edge, and centred on it */
static void tvTextR(float row,int size,const char*s,float r,float g,float b){ s=langWord(s); if(TV_FIT||g_tvView>=0) tvQueue(TV_RIGHT,0.f,row,0.f,0.f,size,s,r,g,b); else tvDraw((float)g_tvCols-(float)(tvLen(s)*size),row*TV_KY,size,s,r,g,b); }
static void tvTextC(float row,int size,const char*s,float r,float g,float b){ s=langWord(s); if(TV_FIT||g_tvView>=0) tvQueue(TV_CENTRE,0.f,row,0.f,0.f,size,s,r,g,b); else tvDraw(((float)g_tvCols-(float)(tvLen(s)*size))*0.5f,row*TV_KY,size,s,r,g,b); }
static void tvBar(float col,float row,float cols,float frac,float r,float g,float b){ if(TV_FIT||g_tvView>=0) tvQueue(TV_BARS,col,row,cols,frac,1,"",r,g,b); else tvBarDraw(col*TV_KX,row*TV_KY,cols*TV_KX,frac,r,g,b); }
/* (11-06) E->tvPanel: a panel in the grid's cells, behind what's drawn after it, wherever the grid is */
static void apiTvPanel(float col,float row,float cols,float rows,float r,float g,float b,float a){ if(!(cols>0.f&&rows>0.f)) return;
    const float kx=TV_KX,ky=TV_KY,cw=8.f*(float)g_tvSx,ch=8.f*(float)g_tvSy; float v[12];
    tvRect(v,(float)g_tvX0+col*kx*cw,(float)g_tvY0+row*ky*ch,(float)g_tvX0+(col+cols)*kx*cw,(float)g_tvY0+(row+rows)*ky*ch);
    hudTris(v,6,(float)g_tvW,(float)g_tvH,r,g,b,a<0.f?0.f:a>1.f?1.f:a); }
/* (11-06) E->tvTri and E->tvLine: where a column and a row are on the screen, as tvPanel has them, and a line's width across it in pixels whichever way it runs */
static void tvXY(float col,float row,float*x,float*y){ *x=(float)g_tvX0+col*TV_KX*8.f*(float)g_tvSx; *y=(float)g_tvY0+row*TV_KY*8.f*(float)g_tvSy; }
static void apiTvTri(float c0,float r0,float c1,float r1,float c2,float r2,float r,float g,float b,float a){ float v[6];
    tvXY(c0,r0,&v[0],&v[1]); tvXY(c1,r1,&v[2],&v[3]); tvXY(c2,r2,&v[4],&v[5]);
    hudTris(v,3,(float)g_tvW,(float)g_tvH,r,g,b,a<0.f?0.f:a>1.f?1.f:a); }
static void apiTvLine(float c0,float r0,float c1,float r1,float w,float r,float g,float b,float a){ float x0,y0,x1,y1; tvXY(c0,r0,&x0,&y0); tvXY(c1,r1,&x1,&y1);
    const float dx=x1-x0,dy=y1-y0,l=sqrtf(dx*dx+dy*dy); if(!(l>1e-4f)||!(w>0.f)) return;
    const float hw=0.5f*w*TV_KY*8.f*(float)g_tvSy, nx=-dy/l*hw, ny=dx/l*hw;
    float v[12]={x0+nx,y0+ny, x1+nx,y1+ny, x1-nx,y1-ny, x0+nx,y0+ny, x1-nx,y1-ny, x0-nx,y0-ny};
    hudTris(v,6,(float)g_tvW,(float)g_tvH,r,g,b,a<0.f?0.f:a>1.f?1.f:a); }
/* a game's wish for a wide grid, before its hud each frame: the grid's laid out again only when it changes */
static void tvWideSet(int on){ on=on?1:0; if(on==g_tvWide||!g_tvOk) return; g_tvWide=on; tvUse(g_tvRead); }
static void apiTvGrid(int*cols,int*rows){ int c0,r0,c,r; tvViewRect(g_tvView,&c0,&r0,&c,&r); if(cols) *cols=c; if(rows) *rows=r; }
/* (step 26) a page of every script the font has, for looking at: the lab's `words=1` */
static int g_tvWords=0;
static void tvWordsPage(void){
    static const char*const L[]={"THE TV FONT, AND WHAT IT CAN SAY","Français: Où est la bibliothèque ? Ça coûte 5 €","Español: ¿Dónde está el niño? ¡Aquí!","Deutsch: Größe, Straße, Übung, Äpfel",
        "Polski: Zażółć gęślą jaźń","Čeština: Příliš žluťoučký kůň","Türkçe: Işık, çığlık, öğle, şüphe","Íslenska: Þú, það, æði, ör",
        "Ελληνικά: Ξεσκεπάζω την ψυχοφθόρα","Русский: Съешь ещё этих мягких булок","Українська: Їжак, ґедзь і єнот","Српски: Љубав, њива, ђак, џеп",
        "日本語: こんにちは、世界","中文: 你好，世界","한국어: 안녕하세요, 세계"};
    const int n=(int)(sizeof L/sizeof L[0]), wide=g_langWide;
    /* a language of wide glyphs is read on 17 x 12, which holds six lines: the three its glyphs are for, and three of the font's own */
    if(wide){ static const int pick[6]={2,9,8,12,13,14}; static const char*const S[3]={"¿Qué tal? ¡Hola!","Привет, мир","Γειά σου"};
        for(int i=0;i<6;i++) tvText(0.f,(float)(i*4),1,i<3?S[i]:L[pick[i]],0.92f,0.92f,0.9f);
        return; }
    for(int i=0;i<n-3;i++) tvText(0.f,(float)(1+i)+(i?1.f:0.f),1,L[i],i?0.92f:1.f,i?0.92f:0.85f,i?0.9f:0.3f); }
static void apiTvView(int view){ g_tvView=(view<0||g_tvNViews<2)?-1:(view>=g_tvNViews?g_tvNViews-1:view); }
