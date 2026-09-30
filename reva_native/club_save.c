/* club_save.c -- THE SAVE FILE's key/value store (09-01). Split out of crowd_kms.c 2026-09-30 (stage 3).
 * One text file, key=value per line, loaded at boot, flushed by the club on a record / a mode change /
 * exit; temp + rename so a crash mid-write leaves last night's file. REVA_NOSAVE=1 makes it blind. */
#include "club.h"
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
        if(g_svN<SAVE_MAX){ snprintf(g_svKey[g_svN],48,"%s",ln); snprintf(g_svVal[g_svN],96,"%s",v); g_svN++; } }
    fclose(f); printf("[save] %d key(s) from %s\n",g_svN,SAVE_PATH); }
void saveFlush(void){
    if(saveOff()||!g_svDirty) return;
    /* temp + rename: a crash between open and close leaves LAST night's file, not half of
     * tonight's. */
    char tmp[64]; snprintf(tmp,sizeof tmp,"%s.tmp",SAVE_PATH);
    FILE*f=fopen(tmp,"w"); if(!f){ printf("[save] cannot write %s\n",tmp); return; }
    fprintf(f,"# THE CLUB -- what it remembers. key=value, one per line.\n");
    for(int i=0;i<g_svN;i++) fprintf(f,"%s=%s\n",g_svKey[i],g_svVal[i]);
    fclose(f); remove(SAVE_PATH); if(rename(tmp,SAVE_PATH)) printf("[save] rename failed\n");
    g_svDirty=0; }
