/* club_save.c -- THE SAVE FILE's key/value store (09-01). Split out of crowd_kms.c 2026-09-30 (stage 3).
 * One text file, key=value per line, loaded at boot, flushed by the club on a record / a mode change /
 * exit; temp + rename so a crash mid-write leaves last night's file. REVA_NOSAVE=1 makes it blind. */
#include "club.h"
#include <dirent.h>
#ifdef _WIN32
#include <direct.h>
#define SAVE_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define SAVE_MKDIR(p) mkdir(p,0755)
#endif
/* save/ is made the first time something's written, if it isn't there: a game played from its own folder hasn't one. It does nothing to a folder that's there */
static void saveDir(void){ static int done=0; if(!done){ done=1; (void)SAVE_MKDIR("save"); } }
#define SAVE_MAX 256
static char  g_svKey[SAVE_MAX][48], g_svVal[SAVE_MAX][96];
static int   g_svN=0, g_svDirty=0, g_svOff=-1;
static const char*SAVE_PATH="save/club.sav";
static int saveOff(void){ if(g_svOff<0) g_svOff=getenv("REVA_NOSAVE")?1:0; return g_svOff; }
static int saveFind(const char*k){ for(int i=0;i<g_svN;i++) if(!strcmp(g_svKey[i],k)) return i; return -1; }
const char*saveGetS(const char*k,const char*def){ int i=saveFind(k); return i<0?def:g_svVal[i]; }
long saveGetI(const char*k,long def){ int i=saveFind(k); return i<0?def:atol(g_svVal[i]); }
void saveSetS(const char*k,const char*v){
    if(saveOff()) return;
    int i=saveFind(k);
    if(i<0){ if(g_svN>=SAVE_MAX){ printf("[save] %d keys -- SAVE_MAX is full, dropping %s\n",SAVE_MAX,k); return; }
             i=g_svN++; snprintf(g_svKey[i],sizeof g_svKey[i],"%s",k); }
    if(strcmp(g_svVal[i],v)){ snprintf(g_svVal[i],sizeof g_svVal[i],"%s",v); g_svDirty=1; } }
void saveSetI(const char*k,long v){ char b[32]; snprintf(b,sizeof b,"%ld",v); saveSetS(k,b); }
void saveLoad(void){
    if(saveOff()){ printf("[save] REVA_NOSAVE: the club remembers nothing tonight\n"); return; }
    FILE*f=fopen(SAVE_PATH,"r"); if(!f){ printf("[save] no %s yet -- first night\n",SAVE_PATH); return; }
    char ln[160]; while(fgets(ln,sizeof ln,f)){
        char*eq=strchr(ln,'='); if(!eq||ln[0]=='#') continue; *eq=0;
        char*v=eq+1; size_t n=strlen(v); while(n&&(v[n-1]=='\n'||v[n-1]=='\r')) v[--n]=0;
        if(g_svN<SAVE_MAX){ snprintf(g_svKey[g_svN],48,"%.47s",ln); snprintf(g_svVal[g_svN],96,"%.95s",v); g_svN++; } }
    fclose(f); printf("[save] %d key(s) from %s\n",g_svN,SAVE_PATH); }
void saveFlush(void){
    if(saveOff()||!g_svDirty) return;
    /* temp + rename: a crash between open and close leaves LAST night's file, not half of
     * tonight's. */
    char tmp[64]; snprintf(tmp,sizeof tmp,"%s.tmp",SAVE_PATH); saveDir();
    FILE*f=fopen(tmp,"w"); if(!f){ printf("[save] cannot write %s\n",tmp); return; }
    fprintf(f,"# THE CLUB -- what it remembers. key=value, one per line.\n");
    for(int i=0;i<g_svN;i++) fprintf(f,"%s=%s\n",g_svKey[i],g_svVal[i]);
    fclose(f); remove(SAVE_PATH); if(rename(tmp,SAVE_PATH)) printf("[save] rename failed\n");
    g_svDirty=0; }

/* ---- step 20 (11-04): A GAME'S OWN SAVE, in named slots. save/<game>_<slot>.txt: a version line at the top, then key=value lines, text a player can read and edit.
 * Everything in is bounds-checked: the slot name is letters, digits, - and _ (31 at most), a key 47 characters, a value 95, SLOT_MAX keys, and a file without a version line that's a
 * number is no save at all. One store for what's being written and one for what was read, so a game can read a slot and write another. REVA_NOSAVE makes both blind. */
#define SLOT_MAX 256
typedef struct { int n,version; char k[SLOT_MAX][48],v[SLOT_MAX][96]; } SlotStore;
static SlotStore g_slotW,g_slotR; static char g_slotPath[192]; static int g_slotOpen=0;
static int slotPathExt(const char*game,const char*slot,const char*ext,char*out,size_t cap){
    char g[32]; int n=0;
    for(const char*p=game?game:"game";*p&&n<31;p++){ char c=*p; if(c>='A'&&c<='Z') c=(char)(c+32);
        if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-') g[n++]=c; else if(c==' '&&n&&g[n-1]!='_') g[n++]='_'; }
    if(!n){ g[n++]='g'; }
    g[n]=0;
    if(!slot||!*slot){ return 0; }
    n=0;
    for(const char*p=slot;*p;p++){ char c=*p; if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')) return 0; if(++n>31) return 0; }
    snprintf(out,cap,"save/%s_%s%s",g,slot,ext); return 1; }
static int slotPath(const char*game,const char*slot,char*out,size_t cap){ return slotPathExt(game,slot,".txt",out,cap); }
static void slotPut(SlotStore*S,const char*k,const char*v){
    if(!k||!v){ return; }
    char kk[48]; int n=0;
    for(const char*p=k;*p&&n<47;p++){ char c=*p; if(c=='='||c=='\n'||c=='\r') continue; kk[n++]=c; } kk[n]=0; if(!n) return;
    int i=-1; for(int j=0;j<S->n;j++) if(!strcmp(S->k[j],kk)){ i=j; break; }
    if(i<0){ if(S->n>=SLOT_MAX){ printf("[save] %d keys is the most a slot holds, dropping %s\n",SLOT_MAX,kk); return; } i=S->n++; snprintf(S->k[i],sizeof S->k[i],"%s",kk); }
    n=0; for(const char*p=v;*p&&n<95;p++){ char c=*p; if(c=='\n'||c=='\r') c=' '; S->v[i][n++]=c; } S->v[i][n]=0; }
void slotBegin(const char*game,const char*slot,int version){
    g_slotW.n=0; g_slotW.version=version<1?1:version; g_slotOpen=slotPath(game,slot,g_slotPath,sizeof g_slotPath);
    if(!g_slotOpen) printf("[save] slot '%s': a name is letters, digits, - and _, 31 at most\n",slot?slot:""); }
void slotSet(const char*k,const char*v){ if(g_slotOpen) slotPut(&g_slotW,k,v); }
static int g_slotNetOn,g_slotNetHost;   /* a session's copy of the host's saves: below, with slotLoad */
int slotEnd(void){
    if(!g_slotOpen){ return 0; }
    g_slotOpen=0;
    if(g_slotNetOn){   /* (step 29) in a session the save goes into every machine's copy of the host's, and only the host's file is written */
        static char t[40001]; int n=snprintf(t,sizeof t,"version %d\n# a save: key=value, one a line, yours to edit\n",g_slotW.version);
        for(int i=0;i<g_slotW.n&&n<(int)sizeof t-160;i++) n+=snprintf(t+n,sizeof t-(size_t)n,"%s=%s\n",g_slotW.k[i],g_slotW.v[i]);
        slotNetPut(g_slotPath,t,n);
        if(!g_slotNetHost) return 1; }
    if(saveOff()) return 0;
    char tmp[200]; snprintf(tmp,sizeof tmp,"%s.tmp",g_slotPath); saveDir();
    FILE*f=fopen(tmp,"w"); if(!f){ printf("[save] cannot write %s\n",tmp); return 0; }
    fprintf(f,"version %d\n# a save: key=value, one a line, yours to edit\n",g_slotW.version);
    for(int i=0;i<g_slotW.n;i++) fprintf(f,"%s=%s\n",g_slotW.k[i],g_slotW.v[i]);
    fclose(f); remove(g_slotPath); if(rename(tmp,g_slotPath)){ printf("[save] rename failed for %s\n",g_slotPath); return 0; }
    return 1; }
/* ---- step 29 (11-05): A SESSION'S SAVES ARE THE HOST'S. What a game reads from a save differs from one machine to the next, and a game that reads it into its rules would
 * part from the others at the first tick. So when players gather, the host's slots for this game are handed to everyone before anything's loaded (engine/net.inc), and for
 * as long as the session lasts every machine reads those, from memory, and none reads its own. A save made in the session goes into that copy on every machine, so a later
 * load sees it everywhere; only the host writes it to a file, so nobody's own save is written over by somebody else's game. */
#define SLOTNET_MAX 16
#define SLOTNET_BYTES 40000
static struct { char path[64]; char*d; int n; } g_slotNet[SLOTNET_MAX]; static int g_slotNetN=0,g_slotNetOn=0,g_slotNetHost=0;
void slotNetBegin(int host){ for(int i=0;i<g_slotNetN;i++){ free(g_slotNet[i].d); g_slotNet[i].d=NULL; } g_slotNetN=0; g_slotNetOn=1; g_slotNetHost=host; }
void slotNetEnd(void){ g_slotNetOn=0; }
void slotNetPut(const char*path,const char*bytes,int n){
    if(!path||!path[0]||n<0||n>SLOTNET_BYTES) return;
    int i=-1; for(int j=0;j<g_slotNetN;j++) if(!strcmp(g_slotNet[j].path,path)){ i=j; break; }
    if(i<0){ if(g_slotNetN>=SLOTNET_MAX){ printf("[save] a session holds %d slots at most, and %s isn't one of them\n",SLOTNET_MAX,path); return; } i=g_slotNetN++; snprintf(g_slotNet[i].path,sizeof g_slotNet[i].path,"%s",path); g_slotNet[i].d=NULL; }
    char*d=(char*)malloc((size_t)n+1); if(!d) return;
    if(n) memcpy(d,bytes,(size_t)n);
    d[n]=0; free(g_slotNet[i].d); g_slotNet[i].d=d; g_slotNet[i].n=n; }
/* the host's slots for this game, each handed to `offer` by its path. How many */
int slotNetOffer(const char*game,void(*offer)(const char*path,const void*bytes,int n)){
    char p[192]; if(saveOff()||!slotPath(game,"x",p,sizeof p)) return 0;
    size_t pl=strlen(p); if(pl<10) return 0;
    p[pl-5]=0;   /* "save/<game>_x.txt" without its "x.txt" */
    const char*pre=p+5; const size_t prel=strlen(pre); char names[SLOTNET_MAX][64]; int nn=0;
    DIR*d=opendir("save"); if(!d) return 0;
    for(struct dirent*e;(e=readdir(d))&&nn<SLOTNET_MAX;){ const size_t l=strlen(e->d_name); if(l>prel+4&&l<58&&!strncmp(e->d_name,pre,prel)&&(!strcmp(e->d_name+l-4,".txt")||!strcmp(e->d_name+l-4,".bin"))){ memcpy(names[nn],"save/",5); memcpy(names[nn]+5,e->d_name,l+1); nn++; } }
    closedir(d);
    for(int i=1;i<nn;i++) for(int j=i;j>0&&strcmp(names[j-1],names[j])>0;j--){ char t[64]; memcpy(t,names[j],64); memcpy(names[j],names[j-1],64); memcpy(names[j-1],t,64); }   /* a folder's own order is the disk's: by name, so every host offers them the same way */
    int sent=0;
    for(int i=0;i<nn;i++){ FILE*f=fopen(names[i],"rb"); if(!f) continue; static char buf[SLOTNET_BYTES+1]; const int n=(int)fread(buf,1,SLOTNET_BYTES+1,f); fclose(f);
        if(n>SLOTNET_BYTES){ printf("[save] %s is too long to hand to the others, so nobody reads it in this session\n",names[i]); continue; }
        offer(names[i],buf,n); sent++; }
    return sent; }
/* a save's text, a line at a time, from a file or from memory: the same checks either way */
static int slotParse(const char*p,const char*text,int n){
    int at=0,ver=0,first=1; char ln[256];
    while(at<n){ int l=0; while(at<n&&text[at]!='\n'){ if(l<255) ln[l++]=text[at]; at++; } if(at<n) at++; ln[l]=0; while(l&&(ln[l-1]=='\r')) ln[--l]=0;
        if(first){ first=0; if(sscanf(ln,"version %d",&ver)!=1||ver<1||ver>1000000000){ printf("[save] %s: no version line, so no save\n",p); return 0; } continue; }
        if(ln[0]=='#') continue;
        char*eq=strchr(ln,'='); if(!eq) continue;
        *eq=0; slotPut(&g_slotR,ln,eq+1); }
    if(first){ printf("[save] %s: no version line, so no save\n",p); return 0; }
    g_slotR.version=ver; return ver; }
int slotLoad(const char*game,const char*slot){
    g_slotR.n=0; g_slotR.version=0; char p[192]; if(!slotPath(game,slot,p,sizeof p)) return 0;
    if(g_slotNetOn){ for(int i=0;i<g_slotNetN;i++) if(!strcmp(g_slotNet[i].path,p)&&g_slotNet[i].d) return slotParse(p,g_slotNet[i].d,g_slotNet[i].n); return 0; }   /* in a session: the host's, or none */
    if(saveOff()) return 0;
    FILE*f=fopen(p,"rb"); if(!f) return 0;
    static char buf[SLOTNET_BYTES+1]; const int n=(int)fread(buf,1,SLOTNET_BYTES,f); fclose(f);
    return slotParse(p,buf,n); }
const char* slotGet(const char*k,const char*def){ if(!k) return def; for(int i=0;i<g_slotR.n;i++) if(!strcmp(g_slotR.k[i],k)) return g_slotR.v[i]; return def; }
/* ---- (11-12) A GAME'S OWN BYTES, beside its text slots (the Godot proof of concept's step 6: the skate game's memory card keeps clips, and a clip is the controls of every
 * tick). save/<game>_<slot>.bin, the slot named as a text slot's is; written whole to a temp file and renamed; read back no further than the game's buffer, and -1 when the
 * file is longer than that. A session's are the host's, as the text slots are: handed over at the start (slotNetOffer offers both kinds, SLOTNET_BYTES a file at most), read
 * from that copy, put in it by a save on every machine and written to a file by the host's alone. REVA_NOSAVE makes them blind */
int slotBytesSave(const char*game,const char*slot,const void*bytes,int n){
    char p[192]; if(!bytes||n<0||!slotPathExt(game,slot,".bin",p,sizeof p)){ printf("[save] bytes for slot '%s': a name is letters, digits, - and _, 31 at most\n",slot?slot:""); return 0; }
    if(g_slotNetOn){
        if(n>SLOTNET_BYTES) printf("[save] %s is %d bytes, and a session keeps %d a slot at most: the others won't have it\n",p,n,SLOTNET_BYTES);
        else slotNetPut(p,(const char*)bytes,n);
        if(!g_slotNetHost) return 1; }
    if(saveOff()) return 0;
    char tmp[200]; snprintf(tmp,sizeof tmp,"%s.tmp",p); saveDir();
    FILE*f=fopen(tmp,"wb"); if(!f){ printf("[save] cannot write %s\n",tmp); return 0; }
    const size_t w=n?fwrite(bytes,1,(size_t)n,f):0; fclose(f);
    if(w!=(size_t)n){ printf("[save] %s: the disk took %d of %d bytes\n",tmp,(int)w,n); remove(tmp); return 0; }
    remove(p); if(rename(tmp,p)){ printf("[save] rename failed for %s\n",p); return 0; }
    return 1; }
int slotBytesLoad(const char*game,const char*slot,void*buf,int cap){
    char p[192]; if(!buf||cap<0||!slotPathExt(game,slot,".bin",p,sizeof p)) return 0;
    if(g_slotNetOn){ for(int i=0;i<g_slotNetN;i++) if(!strcmp(g_slotNet[i].path,p)&&g_slotNet[i].d){ if(g_slotNet[i].n>cap) return -1; if(g_slotNet[i].n) memcpy(buf,g_slotNet[i].d,(size_t)g_slotNet[i].n); return g_slotNet[i].n; } return 0; }
    if(saveOff()) return 0;
    FILE*f=fopen(p,"rb"); if(!f) return 0;
    const int n=(int)fread(buf,1,(size_t)cap,f); const int more=fgetc(f)!=EOF; fclose(f);
    return more?-1:n; }
