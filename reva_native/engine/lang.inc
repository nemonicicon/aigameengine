/* ---- ANOTHER LANGUAGE'S WORDS (queue step 26, 11-05) -----------------------------------------------------------------------------------------------------------------------
 * jacob, 11-04: text beyond ASCII, "and a way to swap a game's words for another language's", with "capability for the larger scripts too". Built and left off: a game is in
 * the words it was written in until something asks for another language, and then nothing else about it changes.
 *
 * A LANGUAGE IS A FILE, lang/<code>.txt beside the game, in UTF-8:
 *     # a comment
 *     name Español                       what it calls itself, for a menu
 *     wide cjk16                         (only a language that needs them) its big glyphs are in data/cjk16.tvw: skate_src/author/fontbake.py makes one
 *     "PRESS A TO PLAY" = "PULSA A PARA JUGAR"
 *     "SCORE %d" = "PUNTOS %d"
 * The left side is the game's own text, exactly as it's written in the game, which is the key: nothing is numbered and nothing has to be declared. A line the file hasn't got
 * stays as the game wrote it. Inside the quotes \" is a quote, \\ a backslash and \n a new line.
 *
 * HOW A GAME'S WORDS CHANGE. Every string that reaches the TV's text (E->tvText and its kin, a menu's labels, the engine's own menus) is looked up whole, so a fixed label
 * needs nothing done to it. A string a game builds with numbers in it is looked up before it's built: snprintf(b, sizeof b, E->word("SCORE %d"), score).
 * E->language("es") reads the file and remembers the choice (save/language.txt); E->language("") goes back to the game's own words. REVA_LANG=<code> says it for one run.
 *
 * THE BIGGER LETTERS. The TV font has accented Latin, Greek and Cyrillic in its own hand (engine/tvfont2.inc), one cell a letter like any other. Chinese, Japanese and Korean
 * need more dots than a cell has at the small reading size, so a language with a `wide` line is read at the large one (17 x 12, engine/tvtext.inc): its glyphs are 16 x 16
 * dots in one cell there, and the fitting that moves a game's 35 x 24 onto bigger letters does the rest. The glyphs aren't the engine's: nothing ships but the TV font.
 * ⛔ What's drawn, never the rules: nothing here is in [simhash]. A game mustn't decide anything by what a string says after E->word. */
#define LANG_MAX 2048
typedef struct { const char*k,*v; unsigned h; } LangPair;
static LangPair g_lang[LANG_MAX]; static short g_langAt[LANG_MAX*2]; static int g_langN=0,g_langWide=0;
static char g_langCode[16]="",g_langName[48]="",g_langWideName[48]=""; static char*g_langBuf=NULL;
static unsigned langHash(const char*s){ unsigned h=2166136261u; while(*s){ h^=(unsigned char)*s++; h*=16777619u; } return h; }
/* this text in the language chosen, or the text itself */
static const char* langWord(const char*s){
    if(!g_langN||!s||!s[0]) return s;
    const unsigned h=langHash(s); unsigned k=h&(LANG_MAX*2-1);
    for(int q=0;q<LANG_MAX*2;q++,k=(k+1)&(LANG_MAX*2-1)){ const int i=g_langAt[k]; if(i<0) return s; if(g_lang[i].h==h&&!strcmp(g_lang[i].k,s)) return g_lang[i].v; }
    return s; }
/* a quoted string in place: its escapes undone, the closing quote made its end. Where the text after it starts, or NULL if it isn't one */
static char* langQuoted(char*p,char**out){
    while(*p==' '||*p=='\t') p++;
    if(*p!='"') return NULL;
    char*w=++p; *out=w;
    for(;*p&&*p!='"';p++){ if(*p=='\\'&&p[1]){ p++; *w++=(*p=='n')?'\n':*p; } else *w++=*p; }
    if(*p!='"') return NULL;
    *w=0; return p+1; }
/* ⛔ (step 28, found by the fuzzer) A TRANSLATION IS USED AS THE GAME'S OWN FORMAT: snprintf(b, n, E->word("SCORE %d"), score). So a language file that answered
 * "SCORE %d" with "%s" had the engine read a number as an address, and one with %n had it write. A line is kept only if its %-directives are exactly the game's own,
 * one for one and in order; text with none on either side (a label, "50% OFF") passes as text. This writes a string's directives out one a line; 0 if there are too many to hold */
static int langFmt(const char*s,char*o,size_t cap){ size_t n=0;
    while(*s){ if(*s!='%'){ s++; continue; }
        if(s[1]=='%'){ s+=2; continue; }
        const char*e=s+1; while(*e&&strchr("-+ #0123456789.*hlLzjt",*e)) e++;
        if(*e&&strchr("diouxXeEfFgGaAcspn",*e)){ if(n+(size_t)(e-s)+3>cap) return 0; for(const char*c=s;c<=e;c++) o[n++]=*c; o[n++]='\n'; s=e+1; }
        else s++; }
    o[n]=0; return 1; }
static void tvWideDrop(void); static int tvWideLoad(const char*name);   /* engine/tvtext.inc */
static void langClear(void){ free(g_langBuf); g_langBuf=NULL; g_langN=0; g_langCode[0]=0; g_langName[0]=0; g_langWideName[0]=0; g_langWide=0; for(int i=0;i<LANG_MAX*2;i++) g_langAt[i]=-1; tvWideDrop(); }
/* read lang/<code>.txt. 1 if it's the language now; 0 leaves the game in its own words, and says why */
static int langLoad(const char*code){
    langClear();
    if(!code||!code[0]) return 1;
    for(const char*c=code;*c;c++) if(!((*c>='a'&&*c<='z')||(*c>='A'&&*c<='Z')||(*c>='0'&&*c<='9')||*c=='-'||*c=='_')||c-code>14){ printf("[lang] '%s' isn't a language's name: letters, digits, - and _, 15 at most\n",code); return 0; }
    char path[64]; snprintf(path,sizeof path,"lang/%s.txt",code);
    FILE*f=fopen(path,"rb"); if(!f){ printf("[lang] no %s: the game stays in its own words\n",path); fflush(stdout); return 0; }
    clubOpened(path);
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    if(n<0||n>(1<<20)){ fclose(f); printf("[lang] %s is over a megabyte: not read\n",path); return 0; }
    g_langBuf=(char*)malloc((size_t)n+2); if(!g_langBuf){ fclose(f); return 0; }
    n=(long)fread(g_langBuf,1,(size_t)n,f); fclose(f); g_langBuf[n]='\n'; g_langBuf[n+1]=0;
    char*p=g_langBuf; if(n>=3&&(unsigned char)p[0]==0xEF&&(unsigned char)p[1]==0xBB&&(unsigned char)p[2]==0xBF) p+=3;   /* a byte-order mark some editors put first */
    int line=0,bad=0;
    while(*p){ char*e=strchr(p,'\n'); if(!e) break; *e=0; if(e>p&&e[-1]=='\r') e[-1]=0; line++;
        char*q=p; while(*q==' '||*q=='\t') q++;
        if(!strncmp(q,"name ",5)) snprintf(g_langName,sizeof g_langName,"%s",q+5);
        else if(!strncmp(q,"wide ",5)){ sscanf(q+5,"%47s",g_langWideName); }
        else if(*q=='"'){ char*k=NULL,*v=NULL; char*r=langQuoted(q,&k);
            if(r){ while(*r==' '||*r=='\t') r++; if(*r=='=') r=langQuoted(r+1,&v); else r=NULL; }
            if(!r){ if(bad++<4) printf("[lang] %s line %d isn't `\"text\" = \"text\"`: left out\n",path,line); }
            else if(g_langN>=LANG_MAX){ if(bad++<4) printf("[lang] %s has more than %d lines: the rest are left out\n",path,LANG_MAX); }
            else { char fk[200],fv[200]; if(!langFmt(k,fk,sizeof fk)||!langFmt(v,fv,sizeof fv)||strcmp(fk,fv)){ if(bad++<4) printf("[lang] %s line %d: its %% directives aren't the game's own, one for one: left out\n",path,line); r=NULL; } }
            if(!r||g_langN>=LANG_MAX){ }
            else { LangPair*L=&g_lang[g_langN]; L->k=k; L->v=v; L->h=langHash(k); unsigned a=L->h&(LANG_MAX*2-1); while(g_langAt[a]>=0) a=(a+1)&(LANG_MAX*2-1); g_langAt[a]=(short)g_langN++; } }
        p=e+1; }
    snprintf(g_langCode,sizeof g_langCode,"%s",code);
    if(g_langWideName[0]) g_langWide=tvWideLoad(g_langWideName);
    printf("[lang] %s (%s): %d lines%s\n",code,g_langName[0]?g_langName:"no name",g_langN,g_langWideName[0]?(g_langWide?", with its big glyphs: read at the large size":", and its big glyphs aren't here: they'll show as ?"):""); fflush(stdout);
    return 1; }
