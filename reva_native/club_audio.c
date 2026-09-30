/* club_audio.c -- THE AUDIO MIXER. Split out of crowd_kms.c 2026-09-30 (stage 2 of the split).
 * Self-contained: SDL audio, its own bank and voices, nothing of the club's but the six calls in club.h. */
#include "club.h"
/* ================= THE AUDIO MIXER (09-11) =========================================
 * The audit ranked "a runtime audio mixer" sixth: the engine had ZERO sound effects outside
 * a scene and played one queued wav; no door made a sound; Act V is entirely sound design.
 * One SDL device (44.1 kHz stereo s16), 32 voices mixed in float with a soft knee, a wav
 * bank loaded by NAME from scenes/sfx (foley.py / foley_game.py fill it), per-voice gain,
 * pan, loop, and an optional world position: distance attenuation and pan against the
 * listener the lens publishes every frame. The scene track is a voice on the same device
 * now (it used to open its own). A set can carry `sound <name> x y z [gain] [loop]` for an
 * ambience. ctl sfx=<name>[,<name>...] test-fires from the lab.
 *   REVA_NOAUDIO=1 opens nothing. REVA_SFXDBG=1 prints every play. ⭐ REVA_AUDIODUMP=<path>
 *   writes the mixed output as raw s16 stereo 44.1k -- the RECEIPT: audio cannot be seen in
 *   a PPM, so anim_bench/audiocheck.py reads the dump and counts the bursts. */
#define SFX_BANK 64
#define SFX_VOICES 32
#define SFX_RATE 44100
typedef struct { char name[32]; float*pcm; int n; int missing; } SfxClip;
typedef struct { int clip; int pos; float gain,pan; int loop,on,spatial; float p[3]; int gen; } SfxVoice;
static SfxClip g_sfx[SFX_BANK]; static int g_nSfx=0;
static SfxVoice g_voice[SFX_VOICES]; static int g_voiceGen=1;
static SDL_AudioDeviceID g_mixDev=0;
static float g_lstE[3]={0,0,0}, g_lstR[3]={1,0,0};
static FILE*g_mixDump=NULL; static long g_mixDumpFrames=0;
static float g_sfxMaster=1.f; static int g_sfxDbg=0, g_sfxPlays=0;
static void sfxCallback(void*ud,Uint8*stream,int len){
    (void)ud; int frames=len/4; short*out=(short*)stream;
    static float acc[8192*2]; if(frames>8192) frames=8192;
    memset(acc,0,(size_t)frames*2*sizeof(float));
    for(int v=0;v<SFX_VOICES;v++){ SfxVoice*V=&g_voice[v]; if(!V->on) continue;
        SfxClip*C=&g_sfx[V->clip]; if(!C->pcm){ V->on=0; continue; }
        float g=V->gain*g_sfxMaster, pan=V->pan;
        if(V->spatial){ float dx=V->p[0]-g_lstE[0],dy=V->p[1]-g_lstE[1],dz=V->p[2]-g_lstE[2]; float d2=dx*dx+dy*dy+dz*dz;
            g*=1.f/(1.f+d2*0.012f);                         /* about -6 dB at 9 units, -20 dB at 29 */
            float dl=sqrtf(d2)+1e-3f; pan=(dx*g_lstR[0]+dz*g_lstR[2])/dl*0.8f; }
        if(pan>1.f)pan=1.f; if(pan<-1.f)pan=-1.f;
        float gl=g*sqrtf(0.5f*(1.f-pan)), gr=g*sqrtf(0.5f*(1.f+pan));
        int p=V->pos;
        for(int i=0;i<frames;i++){ if(p>=C->n){ if(V->loop) p=0; else { V->on=0; break; } }
            float s=C->pcm[p++]; acc[i*2]+=s*gl; acc[i*2+1]+=s*gr; }
        V->pos=p; }
    for(int i=0;i<frames*2;i++){ float s=acc[i]; s=s/(1.f+fabsf(s)*0.35f); if(s>1.f)s=1.f; if(s<-1.f)s=-1.f; out[i]=(short)(s*32767.f); }
    if(g_mixDump){ fwrite(out,4,(size_t)frames,g_mixDump); g_mixDumpFrames+=frames; fflush(g_mixDump); } }   /* flushed: a harness KILLS the engine */
/* convert any wav to the mixer's float mono 44.1k; returns the bank slot or -1 */
int sfxBankFromWav(const char*name,Uint8*buf,Uint32 len,SDL_AudioSpec*spec){
    SDL_AudioCVT cvt;
    int r=SDL_BuildAudioCVT(&cvt,spec->format,spec->channels,spec->freq,AUDIO_F32SYS,1,SFX_RATE);
    if(r<0){ printf("[sfx] %s: cannot convert (%s)\n",name,SDL_GetError()); return -1; }
    Uint8*work=(Uint8*)malloc((size_t)len*(r?cvt.len_mult:1)+16); memcpy(work,buf,len);
    int n;
    if(r){ cvt.buf=work; cvt.len=(int)len; SDL_ConvertAudio(&cvt); n=cvt.len_cvt/4; }
    else n=(int)len/4;
    float*pcm=(float*)malloc((size_t)n*sizeof(float)); memcpy(pcm,work,(size_t)n*sizeof(float)); free(work);
    int slot=-1; for(int i=0;i<g_nSfx;i++) if(!strcmp(g_sfx[i].name,name)){ slot=i; break; }
    if(slot<0){ if(g_nSfx>=SFX_BANK){ printf("[sfx] bank FULL (%d) -- %s not loaded\n",SFX_BANK,name); free(pcm); return -1; } slot=g_nSfx++; }
    if(g_mixDev) SDL_LockAudioDevice(g_mixDev);
    if(g_sfx[slot].pcm) free(g_sfx[slot].pcm);
    snprintf(g_sfx[slot].name,32,"%s",name); g_sfx[slot].pcm=pcm; g_sfx[slot].n=n; g_sfx[slot].missing=0;
    if(g_mixDev) SDL_UnlockAudioDevice(g_mixDev);
    return slot; }
static int sfxLoad(const char*name){
    for(int i=0;i<g_nSfx;i++) if(!strcmp(g_sfx[i].name,name)) return g_sfx[i].missing?-1:i;
    char path[256]; snprintf(path,sizeof path,"scenes/sfx/%s.wav",name);
    SDL_AudioSpec spec; Uint8*buf=NULL; Uint32 len=0;
    if(!SDL_LoadWAV(path,&spec,&buf,&len)){
        printf("[sfx] '%s' MISSING (%s) -- foley.py / foley_game.py make it\n",name,path);
        if(g_nSfx<SFX_BANK){ snprintf(g_sfx[g_nSfx].name,32,"%s",name); g_sfx[g_nSfx].missing=1; g_nSfx++; }
        return -1; }
    int slot=sfxBankFromWav(name,buf,len,&spec); SDL_FreeWAV(buf);
    if(slot>=0&&g_sfxDbg) printf("[sfx] loaded %s: %d samples\n",name,g_sfx[slot].n);
    return slot; }
int sfxPlaySlot(int slot,float gain,const float*pos,int loop){
    if(slot<0||!g_mixDev) return -1;
    int v=-1; for(int i=0;i<SFX_VOICES;i++) if(!g_voice[i].on){ v=i; break; }
    if(v<0){ int best=0; for(int i=1;i<SFX_VOICES;i++) if(!g_voice[i].loop&&g_voice[i].pos>g_voice[best].pos) best=i; v=best; }
    SDL_LockAudioDevice(g_mixDev);
    SfxVoice*V=&g_voice[v]; V->clip=slot; V->pos=0; V->gain=gain; V->pan=0.f; V->loop=loop; V->on=1;
    V->spatial=(pos!=NULL); if(pos){ V->p[0]=pos[0]; V->p[1]=pos[1]; V->p[2]=pos[2]; }
    V->gen=g_voiceGen++;
    SDL_UnlockAudioDevice(g_mixDev);
    g_sfxPlays++;
    if(g_sfxDbg){ printf("[sfx] play %s gain %.2f%s\n",g_sfx[slot].name,gain,pos?" (spatial)":""); fflush(stdout); }
    return (v<<16)|(V->gen&0xffff); }
int sfxPlay(const char*name,float gain,const float*pos,int loop){ return sfxPlaySlot(sfxLoad(name),gain,pos,loop); }
void sfxStop(int id){ if(id<0||!g_mixDev) return; int v=id>>16; if(v<0||v>=SFX_VOICES) return;
    SDL_LockAudioDevice(g_mixDev); if((g_voice[v].gen&0xffff)==(id&0xffff)) g_voice[v].on=0; SDL_UnlockAudioDevice(g_mixDev); }
void sfxListener(const float*eye,const float*ctr){
    float fx=ctr[0]-eye[0], fz=ctr[2]-eye[2], fl=sqrtf(fx*fx+fz*fz); if(fl<1e-4f) return;
    if(g_mixDev) SDL_LockAudioDevice(g_mixDev);
    g_lstE[0]=eye[0]; g_lstE[1]=eye[1]; g_lstE[2]=eye[2];
    g_lstR[0]=fz/fl; g_lstR[1]=0.f; g_lstR[2]=-fx/fl;      /* right = forward x up */
    if(g_mixDev) SDL_UnlockAudioDevice(g_mixDev); }
void sfxInit(void){
    if(getenv("REVA_NOAUDIO")){ printf("[sfx] REVA_NOAUDIO: no mixer\n"); return; }
    { const char*e=getenv("REVA_SFXDBG"); g_sfxDbg=(e&&e[0]!='0')?1:0; }
    /* [pi] the KMS build never calls SDL_Init: the mixer owns its subsystem (refcounted, a no-op on SDL video builds) */
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0){ printf("[sfx] SDL audio init FAILED: %s -- silent\n",SDL_GetError()); return; }
    SDL_AudioSpec want, have; memset(&want,0,sizeof want);
    want.freq=SFX_RATE; want.format=AUDIO_S16SYS; want.channels=2; want.samples=1024; want.callback=sfxCallback;
    g_mixDev=SDL_OpenAudioDevice(NULL,0,&want,&have,0);
    if(!g_mixDev){ printf("[sfx] mixer device FAILED: %s -- silent\n",SDL_GetError()); return; }
    { const char*d=getenv("REVA_AUDIODUMP"); if(d&&d[0]){ g_mixDump=fopen(d,"wb"); if(!g_mixDump) printf("[sfx] dump FAILED to open %s\n",d); } }
    SDL_PauseAudioDevice(g_mixDev,0);
    printf("[sfx] mixer %d Hz stereo, %d voices%s\n",have.freq,SFX_VOICES,g_mixDump?" (dumping raw s16)":""); fflush(stdout); }
