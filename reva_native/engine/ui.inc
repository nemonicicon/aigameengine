/* ---- UI (11-03, engine plan Phase 6) ---------------------------------------------------------------------------------------------------------------------------------------
 * A list of choices on the TV's grid, for a game's own menus, and the pause menu's CONTROLS screen, which moves what a game's actions are on. A game's tick asks uiMove which way the list went and whether something was chosen, and its hud draws it with uiList: the same split as everything else, so a menu replays too. */
static int apiUiMove(int player,int*sel,int n){ static int hold[4]; if(player<0||player>3||n<1) return -1;
    const EnginePad*p=&g_pads[player]; int dir=p->y>0.5f?-1:p->y<-0.5f?1:0;
    if(!dir) hold[player]=0;
    else { int h=hold[player]++; if(h==0||(h>18&&h%6==0)) *sel=(*sel+dir+n)%n; }
    return (p->pressed&PAD_A)?*sel:-1; }
#define UI_TXT (TV_COLS*3+8)   /* a line of a menu, in bytes: a letter past ASCII is up to three (step 26) */
static void apiUiList(float row,const char*const*items,int n,int sel){
    for(int i=0;i<n;i++){ char t[UI_TXT]; snprintf(t,sizeof t,i==sel?"> %.90s <":"%.90s",langWord(items[i]));
        if(i==sel) tvTextC(row+(float)i*2.f,1,t,1.f,0.92f,0.4f); else tvTextC(row+(float)i*2.f,1,t,0.7f,0.75f,0.8f); } }
/* ---- A FULLER MENU (queue step 10). A game says what's on it, in order, in tick, every tick: uiBegin, then uiButton, uiToggle, uiSlider, uiChoice, uiText and the rest, then uiEnd. Each answers there and then what the player just did, and the values are the game's own, so there's nothing to keep in step. Where the menu is, its EngineUi, is the game's too, so it's in the receipt and a replay does the same.
 * It's made for a pad on a tube: the stick moves between rows and changes a value, A picks, B goes back, LB and RB change tabs, and there's no pointer. What it draws is kept from the tick and drawn with the HUD, so hud has nothing to do for it, and tick's the only place a menu's asked about. A long one scrolls so that the row in use can be seen. It's all on the TV's grid, one string a row wherever it can be, so it fits itself to the player's reading size like everything else (engine/tvtext.inc). */
#define UI_MAXROW 128     /* in one menu: past that, a row isn't there and it says so once */
#define UI_MAXDRAW 320    /* in one frame */
typedef struct { char s[UI_TXT],v[UI_TXT]; float col,row,cols,frac,r,g,b,top; int kind,wide,mark,view; } UiDraw;   /* kind: 0 a row of text, 1 a bar, 2 the panel behind a menu. top: its menu's first row */
static UiDraw g_uiDraw[UI_MAXDRAW]; static int g_uiND=0;
enum { UIR_TEXT, UIR_BAR, UIR_KEYS };
typedef struct { char t[UI_TXT],v[UI_TXT]; float vy,frac; int kind,focus,off; } UiRow;
static struct { EngineUi*u; int player; float col,row,cols,rows,vy,pinned; int nr,nf,on,dx,dy,d0,view; unsigned pressed; unsigned long long off; UiRow r[UI_MAXROW]; } g_ui;
static float g_uiColF[3]={1.f,0.92f,0.4f},g_uiColN[3]={0.7f,0.75f,0.8f},g_uiColO[3]={0.34f,0.37f,0.42f};
static char g_uiSnd[3][24];
static const char g_uiKeys[4][11]={"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ.-'!","0123456789"};
static void uiSay(const char*what){ static const char*said[6]; static int n=0; for(int i=0;i<n;i++) if(!strcmp(said[i],what)) return;   /* once each. They're all string literals */
    if(n<6) said[n++]=what;
    printf("[ui] %s\n",what); fflush(stdout); }
static void uiSound(int which){ if(g_uiSnd[which][0]) sfxPlay(g_uiSnd[which],0.5f,NULL,0); }
static void apiUiColors(float fr,float fg,float fb,float nr,float ng,float nb,float or9,float og,float ob){ g_uiColF[0]=fr; g_uiColF[1]=fg; g_uiColF[2]=fb; g_uiColN[0]=nr; g_uiColN[1]=ng; g_uiColN[2]=nb; g_uiColO[0]=or9; g_uiColO[1]=og; g_uiColO[2]=ob; }
static void apiUiSounds(const char*move,const char*pick,const char*back){ const char*a[3]={move,pick,back};
    for(int i=0;i<3;i++){ snprintf(g_uiSnd[i],sizeof g_uiSnd[i],"%s",a[i]?a[i]:""); if(g_uiSnd[i][0]) sfxPreload(g_uiSnd[i]); } }
/* once a tick, before the game's: last tick's menus go. A tick that's only ticking still says them over, so it's the last one that's seen */
static void uiFrame(void){ g_uiND=0; g_ui.u=NULL; }
static void apiUiEnd(void);
/* the stick in whole steps: one at once, and if it's held, one every 6 ticks after 18 more */
static int uiStep(int d,int*hold){ if(!d){ *hold=0; return 0; } const int h=(*hold)++; return (h==0||(h>18&&h%6==0))?d:0; }
static void apiUiBegin(EngineUi*u,int player,float col,float row,float cols,float rows){ if(g_ui.u) apiUiEnd(); if(!u){ uiSay("uiBegin with no EngineUi"); return; }
    g_ui.d0=g_uiND; g_ui.view=-1; g_ui.u=u; g_ui.player=player; g_ui.col=col; g_ui.row=row; g_ui.cols=cols; g_ui.rows=rows; g_ui.vy=0.f; g_ui.pinned=0.f; g_ui.nr=0; g_ui.nf=0; g_ui.on=1; g_ui.off=0;
    float x=0.f,y=0.f; unsigned pr=0u;
    if(player>=0&&player<4){ const EnginePad*p=&g_pads[player]; x=p->x; y=p->y; pr=p->pressed; }
    else for(int k=0;k<4;k++){ const EnginePad*p=&g_pads[k]; if(!p->connected) continue; if(fabsf(p->x)>fabsf(x)) x=p->x; if(fabsf(p->y)>fabsf(y)) y=p->y; pr|=p->pressed; }   /* anyone's */
    g_ui.pressed=pr; g_ui.dx=uiStep(x>0.5f?1:x<-0.5f?-1:0,&u->holdX); g_ui.dy=uiStep(y>0.5f?-1:y<-0.5f?1:0,&u->holdY);
    if(!u->editing&&u->n>0){ if(u->sel>=u->n) u->sel=u->n-1; if(u->sel<0) u->sel=0;
        const int was=u->sel,d=g_ui.dy?g_ui.dy:1;
        if(g_ui.dy||(u->sel<64&&((u->off>>u->sel)&1u))) for(int t=0;t<u->n;t++){ u->sel=(u->sel+d+u->n)%u->n; if(u->sel>=64||!((u->off>>u->sel)&1u)) break; }   /* round the ends, and past what can't be used */
        if(u->sel!=was) uiSound(0); } }
static UiRow* uiAdd(int kind,int focusable,float h,int*me){ if(!g_ui.u){ uiSay("a menu's row outside uiBegin and uiEnd"); return NULL; } if(g_ui.nr>=UI_MAXROW){ uiSay("more rows than a menu holds: they aren't there"); return NULL; }
    UiRow*R=&g_ui.r[g_ui.nr++]; memset(R,0,sizeof *R); R->kind=kind; R->vy=g_ui.vy; g_ui.vy+=h; *me=-1;
    if(focusable){ const int i=g_ui.nf++; *me=i; R->off=!g_ui.on; if(R->off&&i<64) g_ui.off|=1ull<<i; R->focus=(i==g_ui.u->sel)&&!R->off&&!g_ui.u->editing; }
    else R->off=!g_ui.on;
    return R; }
static int uiTook(const UiRow*R,unsigned button){ if(R->focus&&(g_ui.pressed&button)){ g_ui.pressed&=~button; return 1; } return 0; }
static void apiUiView(int view){ g_ui.view=(view<0||g_tvNViews<2)?-1:(view>=g_tvNViews?g_tvNViews-1:view); }
static void apiUiLabel(const char*text){ int me; UiRow*R=uiAdd(UIR_TEXT,0,2.f,&me); if(R) snprintf(R->t,sizeof R->t,"%s",text?text:""); }
static void apiUiGap(float rows){ if(g_ui.u) g_ui.vy+=rows; }
static void apiUiEnable(int on){ g_ui.on=on!=0; }
static int apiUiButton(const char*text){ int me; UiRow*R=uiAdd(UIR_TEXT,1,2.f,&me); if(!R) return 0; snprintf(R->t,sizeof R->t,"%s",text?text:""); if(uiTook(R,PAD_A)){ uiSound(1); return 1; } return 0; }
static int apiUiToggle(const char*text,int*on){ int me; UiRow*R=uiAdd(UIR_TEXT,1,2.f,&me); if(!R||!on) return 0; snprintf(R->t,sizeof R->t,"%s",text?text:""); int ch=0;
    if(uiTook(R,PAD_A)||(R->focus&&g_ui.dx)){ *on=!*on; ch=1; uiSound(1); }
    snprintf(R->v,sizeof R->v,"%s",*on?"ON":"OFF"); return ch; }
static int apiUiSlider(const char*text,float*value,float lo,float hi,float step){ int me; UiRow*R=uiAdd(UIR_BAR,1,2.f,&me); if(!R||!value) return 0; snprintf(R->t,sizeof R->t,"%s",text?text:""); int ch=0;
    if(R->focus&&g_ui.dx&&step>0.f){ const float was=*value; float v=lo+floorf((*value-lo)/step+(float)g_ui.dx+0.5f)*step; if(v<lo) v=lo; if(v>hi) v=hi; *value=v; if(v!=was){ ch=1; uiSound(0); } }
    R->frac=hi>lo?(*value-lo)/(hi-lo):0.f; return ch; }
static int apiUiChoice(const char*text,const char*const*items,int n,int*choice){ int me; UiRow*R=uiAdd(UIR_TEXT,1,2.f,&me); if(!R||!choice||!items||n<1) return 0; snprintf(R->t,sizeof R->t,"%s",text?text:""); int ch=0,d=g_ui.dx;
    if(uiTook(R,PAD_A)) d=1;
    if(R->focus&&d){ *choice=(*choice+d+n)%n; ch=1; uiSound(0); }
    if(*choice<0||*choice>=n) *choice=0;
    snprintf(R->v,sizeof R->v,R->focus?"< %.20s >":"%.20s",items[*choice]); return ch; }
/* a word, with a pad: A opens a keyboard on the grid, the stick picks, A types, B rubs out, and DONE or START ends it. 1 when it's ended. While it's up, it's all the menu shows */
static int apiUiText(const char*text,char*buf,int cap){ int me; UiRow*R=uiAdd(UIR_TEXT,1,2.f,&me); if(!R||!buf||cap<1) return 0; EngineUi*u=g_ui.u; snprintf(R->t,sizeof R->t,"%s",text?text:"");
    if(uiTook(R,PAD_A)){ u->editing=me+1; u->kx=0; u->ky=0; uiSound(1); }
    if(u->editing==me+1){ const int len=(int)strlen(buf); int done=0;
        if(g_ui.dx){ u->kx=(u->kx+g_ui.dx+10)%10; uiSound(0); }
        if(g_ui.dy){ u->ky=(u->ky+g_ui.dy+5)%5; uiSound(0); }
        if(g_ui.pressed&PAD_A){ char c=0;
            if(u->ky<4) c=g_uiKeys[u->ky][u->kx];
            else if(u->kx<4) c=' ';
            else if(u->kx<7){ if(len>0) buf[len-1]=0; uiSound(2); }
            else done=1;
            if(c&&len<cap-1){ buf[len]=c; buf[len+1]=0; uiSound(1); } }
        if(g_ui.pressed&PAD_B){ if(len>0) buf[len-1]=0; uiSound(2); }
        if(g_ui.pressed&PAD_START) done=1;
        g_ui.pressed=0u;
        if(done){ u->editing=0; uiSound(1); }
        else { int k9; uiAdd(UIR_KEYS,0,0.f,&k9); }
        snprintf(R->v,sizeof R->v,"%.24s%s",buf,done?"":"_"); return done; }
    snprintf(R->v,sizeof R->v,"%.24s",buf); return 0; }
static UiDraw* uiPut(const char*s,float col,float row,const float*c){ if(g_uiND>=UI_MAXDRAW){ uiSay("more on the screen than a frame's menus hold"); static UiDraw spare; return &spare; } UiDraw*D=&g_uiDraw[g_uiND++]; memset(D,0,sizeof *D); snprintf(D->s,sizeof D->s,"%s",s); D->col=col; D->row=row; D->r=c[0]; D->g=c[1]; D->b=c[2]; D->view=g_ui.view; D->top=g_ui.row; return D; }
/* tabs, first in a menu: LB and RB go between them, and it starts again at its first row. Which one it's on is in the EngineUi */
static int apiUiTabs(const char*const*names,int n){ EngineUi*u=g_ui.u; if(!u||!names||n<1) return 0;
    if(!u->editing&&(g_ui.pressed&(PAD_LB|PAD_RB))){ u->tab=(u->tab+((g_ui.pressed&PAD_RB)?1:-1)+n)%n; u->sel=0; u->top=0.f; g_ui.nf=0; uiSound(0); g_ui.pressed&=~(unsigned)(PAD_LB|PAD_RB); }
    if(u->tab<0||u->tab>=n) u->tab=0;
    if(!u->editing){ char t[UI_TXT]; int at=0; for(int k=0;k<n&&at<UI_TXT-40;k++){ const int w=snprintf(t+at,sizeof t-(size_t)at,k==u->tab?"[%.36s]  ":"%.36s  ",langWord(names[k])); if(w<0) break; at+=w; }
        uiPut(t,g_ui.col+2.f,g_ui.row,g_uiColF); }
    g_ui.pinned=2.f; return u->tab; }
static int apiUiBack(void){ if(g_ui.u&&!g_ui.u->editing&&(g_ui.pressed&PAD_B)){ g_ui.pressed&=~(unsigned)PAD_B; uiSound(2); return 1; } return 0; }
/* a label, a group of spaces and a value, to this width. One string, because a group of spaces is where a line breaks if it really won't fit */
static void uiLine(char*o,const char*l,const char*v,int wide){ int n=0; l=langWord(l); v=langWord(v);   /* (step 26) in the language chosen, and its gap counted in letters, not bytes */
    for(const char*p=l;*p&&n<UI_TXT-2;p++) o[n++]=*p;
    if(*v){ int gap=wide-tvLen(l)-tvLen(v); if(gap<2) gap=2; for(int i=0;i<gap&&n<UI_TXT-2;i++) o[n++]=' '; for(const char*p=v;*p&&n<UI_TXT-2;p++) o[n++]=*p; }
    o[n]=0; }
/* the keyboard: one string a row, so it fits at any reading size, with the key in use in brackets */
static void uiKeysDraw(const EngineUi*u,const UiRow*F,float x,float y,int wide){ char t[UI_TXT]; { UiDraw*D=uiPut(F->t,x,y,g_uiColF); snprintf(D->v,sizeof D->v,"%s",F->v); D->wide=wide; }
    for(int ky=0;ky<4;ky++){ int n=0; for(int kx=0;kx<10;kx++){ const int on=u->ky==ky&&u->kx==kx; if(on) t[n++]='['; t[n++]=g_uiKeys[ky][kx]; if(on) t[n++]=']'; } t[n]=0; uiPut(t,x,y+2.f+(float)ky*2.f,u->ky==ky?g_uiColF:g_uiColN); }
    const int w=u->ky==4?(u->kx<4?0:u->kx<7?1:2):-1; snprintf(t,sizeof t,"%s %s %s",w==0?"[SPACE]":"SPACE",w==1?"[RUB]":"RUB",w==2?"[DONE]":"DONE"); uiPut(t,x,y+10.f,w>=0?g_uiColF:g_uiColN); }
/* a dark back for the whole menu, so it reads over any picture. It goes in ahead of what the menu's already put */
static void uiPanel(float col,float row,float cols,float rows){ if(g_uiND>=UI_MAXDRAW) return; const int at=g_ui.d0; memmove(&g_uiDraw[at+1],&g_uiDraw[at],(size_t)(g_uiND-at)*sizeof(UiDraw)); g_uiND++; UiDraw*D=&g_uiDraw[at]; memset(D,0,sizeof *D); D->kind=2; D->col=col; D->row=row; D->cols=cols; D->frac=rows; D->view=g_ui.view; D->top=row; }
static void uiPanelDraw(const UiDraw*D){ const float kx=(float)g_tvCols/(float)TV_COLS,ky=(float)g_tvRows/(float)TV_ROWS,cw=8.f*(float)g_tvSx,ch=8.f*(float)g_tvSy; float v[12];
    tvRect(v,(float)g_tvX0+(D->col*kx-0.5f)*cw,(float)g_tvY0+(D->row*ky-0.5f)*ch,(float)g_tvX0+((D->col+D->cols)*kx+0.5f)*cw,(float)g_tvY0+((D->row+D->frac)*ky+0.5f)*ch);
    hudTris(v,6,(float)g_tvW,(float)g_tvH,0.f,0.f,0.f,0.62f); }
/* the menu's said: scroll to the row in use, and keep what's to be drawn */
static void apiUiEnd(void){ EngineUi*u=g_ui.u; if(!u) return; g_ui.u=NULL; u->n=g_ui.nf; u->off=g_ui.off; if(u->sel>=u->n) u->sel=u->n>0?u->n-1:0;
    const float x0=g_ui.col,cols=g_ui.cols,view=g_ui.rows-g_ui.pinned,y0=g_ui.row+g_ui.pinned; const int wide=(int)cols-2>0?(int)cols-2:1;
    if(u->editing){ int keys=0; for(int i=1;i<g_ui.nr;i++) if(g_ui.r[i].kind==UIR_KEYS){ uiPanel(x0,g_ui.row,cols,12.f); uiKeysDraw(u,&g_ui.r[i-1],x0+2.f,g_ui.row,wide); keys=1; break; }
        if(keys) return;
        u->editing=0; }   /* the game stopped saying the row it was typing in */
    float vyF=0.f,total=0.f; int any=0; for(int i=0;i<g_ui.nr;i++){ const UiRow*R=&g_ui.r[i]; if(R->kind==UIR_KEYS) continue; if(R->vy+2.f>total) total=R->vy+2.f; if(R->focus){ vyF=R->vy; any=1; } }
    if(any){ if(vyF<u->top) u->top=vyF; if(vyF+2.f>u->top+view) u->top=vyF+2.f-view; }
    if(u->top>total-view) u->top=total-view;
    if(u->top<0.f) u->top=0.f;
    uiPanel(x0,g_ui.row,cols,g_ui.pinned+(total<view?total:view));
    for(int i=0;i<g_ui.nr;i++){ const UiRow*R=&g_ui.r[i]; const float*c=R->off?g_uiColO:R->focus?g_uiColF:g_uiColN;
        if(R->kind==UIR_KEYS||R->vy<u->top-0.01f||R->vy+1.f>u->top+view+0.01f) continue;
        const float y=y0+R->vy-u->top;
        { UiDraw*D=uiPut(R->t,R->focus?x0:x0+2.f,y,c); snprintf(D->v,sizeof D->v,"%s",R->v); D->wide=wide; D->mark=R->focus; }
        if(R->kind==UIR_BAR){ UiDraw*D=uiPut("",x0+cols*0.55f,y,c); D->kind=1; D->cols=cols*0.42f; D->frac=R->frac; } }
    if(u->top>0.01f) uiPut("^",x0+cols-1.f,y0,g_uiColN);
    if(u->top+view<total-0.01f) uiPut("V",x0+cols-1.f,y0+view-2.f,g_uiColN); }
/* with the HUD: what the tick's menus said. A row's put together here, because here's where the grid it's on is known, and the rules never see it. At a bigger reading size the layout moves rows about, so a menu's a group and tvFlush puts its panel round them */
static void uiFlush(void){ int group=0; g_tvGroup=0;
    for(int i=0;i<g_uiND;i++){ const UiDraw*D=&g_uiDraw[i]; g_tvView=D->view;
        /* (11-06) at the desk reading size a menu's rows are rows of its letters, down from its first: it starts where the game put it and keeps its size in letters,
         * as at the small size, instead of spreading its rows over the taller grid. Its width stays in proportion. Only the drawing: the tick's rows and scrolling are the same */
        const float ky=(TV_FIT||D->view>=0)?1.f:TV_KY, row=(ky!=1.f)?D->top+(D->row-D->top)/ky:D->row;
        if(D->kind==2){ if(TV_FIT||D->view>=0) g_tvGroup=++group; else { UiDraw P=*D; if(ky!=1.f) P.frac=D->frac/ky; g_tvGroup=0; uiPanelDraw(&P); } continue; }
        if(D->kind==1){ tvBar(D->col,row,D->cols,D->frac,D->r,D->g,D->b); continue; }
        int c0,r0,c9,r9; tvViewRect(D->view,&c0,&r0,&c9,&r9); char t[UI_TXT],f[UI_TXT+4]; uiLine(t,D->s,D->v,D->wide*c9/TV_COLS);
        if(D->mark){ f[0]='>'; f[1]=' '; memcpy(f+2,t,strlen(t)+1); tvText(D->col,row,1,f,D->r,D->g,D->b); } else tvText(D->col,row,1,t,D->r,D->g,D->b); }
    g_tvGroup=0; g_tvView=-1; }
/* the CONTROLS screen: every action the game named, and what it's on. A picks one, and the next button pressed is its button, for everyone. It's written to save/controls.txt straight away, one line an action */
static int g_ctlOn=0,g_ctlSel=0,g_ctlWait=0;
static const char* ctlButtons(unsigned m,char*b,size_t cap){ static const struct { unsigned m; const char*n; } B[]={{PAD_A,"A"},{PAD_B,"B"},{PAD_X,"X"},{PAD_Y,"Y"},{PAD_LB,"LB"},{PAD_RB,"RB"},{PAD_START,"START"}}; size_t at=0; b[0]=0;
    for(unsigned i=0;i<sizeof B/sizeof B[0];i++) if(m&B[i].m){ int w=snprintf(b+at,cap-at,"%s%s",at?" ":"",B[i].n); if(w<0||(size_t)w>=cap-at) break; at+=(size_t)w; }
    return b; }
static void ctlSave(void){ if(getenv("REVA_NOSAVE")||getenv("REVA_REPLAY")) return;
#ifdef _WIN32
    _mkdir("save");
#else
    mkdir("save",0777);
#endif
    FILE*f=fopen("save/controls.txt","w"); if(!f) return;
    fprintf(f,"# what this game's actions are on. Change it here or in the pause menu's CONTROLS. A line can start p1 to p4 for one player.\n");
    for(int i=0;i<g_nAct;i++){ char b[40]; for(char*q=(char*)ctlButtons(g_binds[i].mask[0],b,sizeof b);*q;q++) *q=(char)(*q>='A'&&*q<='Z'?*q+32:*q); fprintf(f,"%s %s\n",g_binds[i].name,b); }
    fclose(f); }
/* one tick of the screen, on pad 0, in place of the pause menu's own */
static void ctlTick(void){ const unsigned pr=g_pads[0].pressed; static int pu=0,pd=0; int up=g_ly<-0.55f,dn=g_ly>0.55f;
    if(g_ctlWait){ unsigned m=pr&(PAD_A|PAD_B|PAD_X|PAD_Y|PAD_LB|PAD_RB);
        if(m&&g_ctlSel<g_nAct){ for(int k=0;k<4;k++) g_binds[g_ctlSel].mask[k]=m; g_ctlWait=0; ctlSave(); } }
    else { if(g_nAct>0){ if(up&&!pu) g_ctlSel=(g_ctlSel+g_nAct-1)%g_nAct; if(dn&&!pd) g_ctlSel=(g_ctlSel+1)%g_nAct; }
        if((pr&PAD_A)&&g_nAct>0) g_ctlWait=1;
        if(pr&PAD_B) g_ctlOn=0; }
    pu=up; pd=dn; }
static void ctlDraw(void){ tvTextC(4,2,"CONTROLS",0.49f,0.98f,1.f);
    if(!g_nAct) tvTextC(11,1,"THIS GAME NAMES NONE",0.7f,0.75f,0.8f);
    for(int i=0;i<g_nAct&&i<7;i++){ char b[40],t[TV_COLS+1]; ctlButtons(g_binds[i].mask[0],b,sizeof b);
        if(i==g_ctlSel&&g_ctlWait) snprintf(t,sizeof t,"%.14s  PRESS A BUTTON",g_binds[i].name); else snprintf(t,sizeof t,i==g_ctlSel?"> %.14s  %.14s <":"%.14s  %.14s",g_binds[i].name,b);
        for(char*q=t;*q;q++) if(*q>='a'&&*q<='z') *q=(char)(*q-32);
        if(i==g_ctlSel) tvTextC(8.f+(float)i*2.f,1,t,1.f,0.92f,0.4f); else tvTextC(8.f+(float)i*2.f,1,t,0.7f,0.75f,0.8f); }
    tvTextC(21,1,"STICK PICK  A CHANGE  B BACK",0.22f,1.f,0.54f); }
