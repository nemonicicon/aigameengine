/* engine/tvgrown.inc -- THE TV FONT ITSELF, AND IT GROWN TO A TYPEFACE'S SIZE (queue step 28, 11-05).
 * The typeface that ships with the engine (11-04) is the one the Pi's console letters already used. That's this font, and it's the only
 * one: nothing of anyone else's ships. So the text that used to be set in a typeface read off the machine, film's captions (engine/captions.inc) and the old start
 * menu (a menu scene), is the TV font four times its size, 32 x 32 a glyph, its stairs rounded off (the EPX rule, twice) and its edge softened by a pixel, as an alpha
 * picture a renderer can scale. A run can still name a font of its own: REVA_CAPFONT=<a .ttf> for captions, REVA_LHFONT=system for the menu, as each says.
 * The font's two sheets are included here, once, ahead of everything that draws text: engine/tvtext.inc is the grid they're usually drawn on. */
#include "tvfont.inc"
#include "tvfont2.inc"
typedef unsigned short TvCh;   /* a letter: its code point (engine/tvtext.inc says how a string becomes these) */
/* which glyph a code point is: 0 to 95 the first sheet's, then the second's. -1 for one the font hasn't got */
static int tvGlyphOf(TvCh c){ if(c>=32&&c<128) return (int)c-32;
    int lo=0,hi=TV_MORE-1; while(lo<=hi){ const int m=(lo+hi)/2; if(TV_MORE_CP[m]==c) return 96+m; if(TV_MORE_CP[m]<c) lo=m+1; else hi=m-1; }
    return -1; }
#define TVG 32   /* a grown glyph's side */
/* glyph g, grown, as 32 x 32 of alpha */
__attribute__((unused)) static void tvGrown(int g,unsigned char*o){
    unsigned char a[8][8],b[16][16],c[32][32];
    for(int y=0;y<8;y++) for(int x=0;x<8;x++) a[y][x]=(g<96)?(unsigned char)(TV_FONT[(g/16)*8+y][(g%16)*9+x]=='#'):(unsigned char)((TV_MORE_BITS[g-96][y]>>(7-x))&1);
    /* EPX: a pixel becomes four, and a corner of it takes its two neighbours' colour where they agree and the ones across from them don't: a stair becomes a slope */
#define TV_EPX(src,n,dst) for(int y=0;y<(n);y++) for(int x=0;x<(n);x++){ const int P=src[y][x],A=y>0?src[y-1][x]:0,D=y<(n)-1?src[y+1][x]:0,C=x>0?src[y][x-1]:0,B=x<(n)-1?src[y][x+1]:0; \
        dst[y*2][x*2]=(unsigned char)((C==A&&C!=D&&A!=B)?A:P); dst[y*2][x*2+1]=(unsigned char)((A==B&&A!=C&&B!=D)?B:P); \
        dst[y*2+1][x*2]=(unsigned char)((D==C&&D!=B&&C!=A)?C:P); dst[y*2+1][x*2+1]=(unsigned char)((B==D&&B!=A&&D!=C)?D:P); }
    TV_EPX(a,8,b)
    TV_EPX(b,16,c)
#undef TV_EPX
    for(int y=0;y<TVG;y++) for(int x=0;x<TVG;x++){ int s=0;
        for(int j=-1;j<=1;j++) for(int i=-1;i<=1;i++){ const int yy=y+j,xx=x+i; if(yy>=0&&yy<TVG&&xx>=0&&xx<TVG&&c[yy][xx]) s+=(i?1:2)*(j?1:2); }
        o[y*TVG+x]=(unsigned char)(s*255/16); } }
