/* club_audio.c -- THE AUDIO MIXER. Split out of crowd_kms.c 2026-09-30 (stage 2 of the split).
 * Self-contained: SDL audio, its own bank and voices, nothing of the club's but the six calls in club.h. */
#include "club.h"
#define STB_VORBIS_NO_PUSHDATA_API
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#include "third_party/stb_vorbis.c"
#pragma GCC diagnostic pop
#undef L
#undef C
#undef R
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
#define SFX_BANK 512
#define SFX_VOICES 32
#ifndef SFX_RATE
#define SFX_RATE 44100
#endif
typedef struct { char name[32]; float*pcm; int n; int missing; } SfxClip;
typedef struct { int clip; int pos; float gain,pan; int loop,on,spatial; float p[3]; int gen,bus; float rate,frac; int wait; } SfxVoice;   /* rate (11-05): how fast it plays, 1 as recorded; frac, where it is between two samples */
static SfxClip g_sfx[SFX_BANK]; static int g_nSfx=0;
static SfxVoice g_voice[SFX_VOICES]; static int g_voiceGen=1;
static SDL_AudioDeviceID g_mixDev=0;
static float g_lstE[3]={0,0,0}, g_lstR[3]={1,0,0};
static FILE*g_mixDump=NULL; static long g_mixDumpFrames=0;
static float g_sfxMaster=1.f; static int g_sfxDbg=0, g_sfxPlays=0;
/* ---- BUSES AND STREAMED MUSIC (11-03, engine plan Phase 6). A thread of its own reads the music a little at a time into a ring of two seconds, and the mixer only ever reads the ring, so the disk can't reach the sound or a frame. */
static struct { float now,want,rate; } g_bus[BUS_N]={{1.f,1.f,0.f},{1.f,1.f,0.f},{1.f,1.f,0.f},{1.f,1.f,0.f}};
#define MUS_CAP (SFX_RATE*2*2)
static SDL_atomic_t g_musOut;
/* (11-12) TWO DECKS, for a crossfade (the Godot proof of concept's step 8: their soundtrack crosses one song into the next): each deck is a thread
 * reading its file into its own ring, and the mixer sums them, each at its own fade, a sample at a time. musicPlay cuts to a track on the deck
 * that's playing, as it always did; musicCross brings one in on the other deck while the playing one goes out, and a deck that's gone silent is
 * let go at the next call. len and out are the track's length and how much of it has gone out, in the mixer's frames, for musicLeft */
typedef struct { SDL_Thread*th; SDL_atomic_t run,wr,rd,out,len; float*ring; char path[320]; int loop; float gain,fade,fadeTo,fadeStep; } MusDeck;
static MusDeck g_mus[2]; static int g_musCur=0;
/* (11-09) THE MIXER'S OWN CLOCK: when its last buffer began, smoothed (a callback's own moment jitters by milliseconds), and how long a buffer is.
 * sfxPlayIn counts a voice's wait in samples from where the NEXT buffer begins, so a sound starts at its moment whatever the frame rate */
static double g_cbAt=0.0, g_cbPeriod=0.0;
static long g_mixFrames=0; static int g_stGen[256]; static long g_stAt[256]; static int g_stN=0, g_sfxSync=-1;   /* (11-09) REVA_SFXSYNC=1: the sample each voice started on */
static double sfxClock(void){ return (double)SDL_GetPerformanceCounter()/(double)SDL_GetPerformanceFrequency(); }
static void sfxCallback(void*ud,Uint8*stream,int len){
    (void)ud; int frames=len/4; short*out=(short*)stream;
    { const double nw=sfxClock(), per=(double)frames/(double)SFX_RATE; if(g_cbAt<=0.0||fabs(nw-(g_cbAt+per))>0.1) g_cbAt=nw; else { const double pr=g_cbAt+per; g_cbAt=pr+0.1*(nw-pr); } g_cbPeriod=per; }
    static float acc[8192*2]; if(frames>8192) frames=8192;
    memset(acc,0,(size_t)frames*2*sizeof(float));
    for(int b=0;b<BUS_N;b++){ float d=g_bus[b].want-g_bus[b].now,st=g_bus[b].rate*(float)frames/(float)SFX_RATE; g_bus[b].now=(fabsf(d)<=st||st<=0.f)?g_bus[b].want:g_bus[b].now+(d>0.f?st:-st); }
    for(int d=0;d<2;d++){ MusDeck*M=&g_mus[d]; if(!M->ring||!SDL_AtomicGet(&M->run)) continue;
        int rd=SDL_AtomicGet(&M->rd),wr=SDL_AtomicGet(&M->wr),m=(wr-rd)/2; const float g=M->gain*g_bus[BUS_MUSIC].now*g_sfxMaster; if(m>frames) m=frames;
        for(int i=0;i<m;i++){
            if(M->fade<M->fadeTo){ M->fade+=M->fadeStep; if(M->fade>M->fadeTo) M->fade=M->fadeTo; } else if(M->fade>M->fadeTo){ M->fade-=M->fadeStep; if(M->fade<M->fadeTo) M->fade=M->fadeTo; }
            const float gf=g*M->fade; acc[i*2]+=M->ring[(unsigned)(rd+i*2)%MUS_CAP]*gf; acc[i*2+1]+=M->ring[(unsigned)(rd+i*2+1)%MUS_CAP]*gf; }
        SDL_AtomicSet(&M->rd,rd+m*2); SDL_AtomicAdd(&M->out,m); if(d==g_musCur) SDL_AtomicAdd(&g_musOut,m); }
    for(int v=0;v<SFX_VOICES;v++){ SfxVoice*V=&g_voice[v]; if(!V->on) continue;
        SfxClip*C=&g_sfx[V->clip]; if(!C->pcm){ V->on=0; continue; }
        float g=V->gain*g_sfxMaster*g_bus[V->bus].now, pan=V->pan;
        if(V->spatial){ float dx=V->p[0]-g_lstE[0],dy=V->p[1]-g_lstE[1],dz=V->p[2]-g_lstE[2]; float d2=dx*dx+dy*dy+dz*dz;
            g*=1.f/(1.f+d2*0.012f);                         /* about -6 dB at 9 units, -20 dB at 29 */
            float dl=sqrtf(d2)+1e-3f; pan=(dx*g_lstR[0]+dz*g_lstR[2])/dl*0.8f; }
        if(pan>1.f)pan=1.f;
        if(pan<-1.f)pan=-1.f;
        float gl=g*sqrtf(0.5f*(1.f-pan)), gr=g*sqrtf(0.5f*(1.f+pan));
        int p=V->pos, i0=0;
        if(V->wait>0){ if(V->wait>=frames){ V->wait-=frames; continue; } i0=V->wait; V->wait=0; }
        if(g_sfxSync>0&&p==0&&V->frac==0.f&&g_stN<256){ g_stGen[g_stN]=V->gen; g_stAt[g_stN]=g_mixFrames+i0; g_stN++; }   /* (11-09) not its moment yet: it starts part-way through a buffer */
        if(V->rate==1.f||!(V->rate>0.f)){ for(int i=i0;i<frames;i++){ if(p>=C->n){ if(V->loop) p=0; else { V->on=0; break; } }
            float s=C->pcm[p++]; acc[i*2]+=s*gl; acc[i*2+1]+=s*gr; } }
        else { float fr=V->frac; const float rt=V->rate;   /* (11-05) played faster or slower: between two samples, a straight line */
            for(int i=i0;i<frames;i++){ if(p>=C->n){ if(V->loop) p=0; else { V->on=0; break; } }
                const int q=p+1<C->n?p+1:(V->loop?0:p); const float s=C->pcm[p]*(1.f-fr)+C->pcm[q]*fr; acc[i*2]+=s*gl; acc[i*2+1]+=s*gr;
                fr+=rt; while(fr>=1.f){ fr-=1.f; p++; if(p>=C->n){ if(V->loop) p=0; else break; } } }
            V->frac=fr; }
        V->pos=p; }
    for(int i=0;i<frames*2;i++){ float s=acc[i]; s=s/(1.f+fabsf(s)*0.35f); if(s>1.f)s=1.f; if(s<-1.f)s=-1.f; out[i]=(short)(s*32767.f); }
    g_mixFrames+=frames;
    if(g_mixDump){ fwrite(out,4,(size_t)frames,g_mixDump); g_mixDumpFrames+=frames; fflush(g_mixDump); } }   /* flushed: a harness KILLS the engine */
/* a clip into the bank by name: the mixer's float mono 44.1k, kept (and freed) by the bank */
static int sfxBankPut(const char*name,float*pcm,int n){
    int slot=-1; for(int i=0;i<g_nSfx;i++) if(!strcmp(g_sfx[i].name,name)){ slot=i; break; }
    if(slot<0){ if(g_nSfx>=SFX_BANK){ printf("[sfx] bank FULL (%d) -- %s not loaded\n",SFX_BANK,name); free(pcm); return -1; } slot=g_nSfx++; }
    if(g_mixDev) SDL_LockAudioDevice(g_mixDev);
    if(g_sfx[slot].pcm) free(g_sfx[slot].pcm);
    snprintf(g_sfx[slot].name,32,"%s",name); g_sfx[slot].pcm=pcm; g_sfx[slot].n=n; g_sfx[slot].missing=0;
    if(g_mixDev) SDL_UnlockAudioDevice(g_mixDev);
    return slot; }
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
    return sfxBankPut(name,pcm,n); }
static int sfxLoad(const char*name){
    for(int i=0;i<g_nSfx;i++) if(!strcmp(g_sfx[i].name,name)) return g_sfx[i].missing?-1:i;
    { char own[256]; snprintf(own,sizeof own,"sounds/%s",name); int n=0; float*pcm=sfxDecodeFile(own,&n);   /* (11-12) a game's own sounds/, a wav or an ogg, as engine.h always said */
      if(pcm&&n>0){ const int s=sfxBankPut(name,pcm,n); if(s>=0&&g_sfxDbg) printf("[sfx] loaded %s from sounds/: %d samples\n",name,n); return s; }
      free(pcm); }
    char path[256]; snprintf(path,sizeof path,"scenes/sfx/%s.wav",name);
    SDL_AudioSpec spec; Uint8*buf=NULL; Uint32 len=0;
    clubOpened(path);
    if(!SDL_LoadWAV(path,&spec,&buf,&len)){
        printf("[sfx] '%s' MISSING (sounds/%s.wav or .ogg, or %s) -- foley.py / foley_game.py make the club's\n",name,name,path);
        if(g_nSfx<SFX_BANK){ snprintf(g_sfx[g_nSfx].name,32,"%s",name); g_sfx[g_nSfx].missing=1; g_nSfx++; }
        return -1; }
    int slot=sfxBankFromWav(name,buf,len,&spec); SDL_FreeWAV(buf);
    if(slot>=0&&g_sfxDbg) printf("[sfx] loaded %s: %d samples\n",name,g_sfx[slot].n);
    return slot; }
void sfxPreload(const char*name){ sfxLoad(name); }
static double g_playWait=0.0;   /* (11-09) seconds from now the next voice starts (sfxPlayIn): read under the device's lock, where the callback can't be mid-buffer */
int sfxPlaySlot(int slot,float gain,const float*pos,int loop){
    if(slot<0||!g_mixDev) return -1;
    int v=-1; for(int i=0;i<SFX_VOICES;i++) if(!g_voice[i].on){ v=i; break; }
    /* (11-09) no voice free: the one furthest through its sound goes, but never a long one (a song's sung voices, minutes long, were always the furthest) */
    if(v<0){ int best=-1; for(int i=0;i<SFX_VOICES;i++){ if(g_voice[i].loop||g_sfx[g_voice[i].clip].n>30*SFX_RATE) continue; if(best<0||g_voice[i].pos>g_voice[best].pos) best=i; } v=best<0?0:best; }
    SDL_LockAudioDevice(g_mixDev);
    SfxVoice*V=&g_voice[v]; V->clip=slot; V->pos=0; V->gain=gain; V->pan=0.f; V->loop=loop; V->on=1; V->bus=BUS_SFX; V->rate=1.f; V->frac=0.f; V->wait=g_playWait>0.0?(int)((sfxClock()+g_playWait-(g_cbAt+g_cbPeriod))*(double)SFX_RATE):0; if(V->wait<0) V->wait=0;
    V->spatial=(pos!=NULL); if(pos){ V->p[0]=pos[0]; V->p[1]=pos[1]; V->p[2]=pos[2]; }
    V->gen=g_voiceGen++;
    SDL_UnlockAudioDevice(g_mixDev);
    g_sfxPlays++;
    if(g_sfxDbg){ printf("[sfx] play %s gain %.2f%s\n",g_sfx[slot].name,gain,pos?" (spatial)":""); fflush(stdout); }
    return (v<<16)|(V->gen&0xffff); }
/* (10-22, reva) the outside face door 11 needs: a synthesised clip into the bank, a voice turned while it plays,
 * the master volume, whether the device is open */
int sfxBankPCM(const char*name,float*pcm,int n){
    int slot=-1; for(int i=0;i<g_nSfx;i++) if(!strcmp(g_sfx[i].name,name)){ slot=i; break; }
    if(slot<0) for(int i=0;i<g_nSfx;i++) if(!g_sfx[i].name[0]){ slot=i; break; }   /* a place something's given back */
    if(slot<0){ if(g_nSfx>=SFX_BANK){ free(pcm); return -1; } slot=g_nSfx++; }
    if(g_mixDev) SDL_LockAudioDevice(g_mixDev);
    if(g_sfx[slot].pcm) free(g_sfx[slot].pcm);
    snprintf(g_sfx[slot].name,32,"%s",name); g_sfx[slot].pcm=pcm; g_sfx[slot].n=n; g_sfx[slot].missing=0;
    if(g_mixDev) SDL_UnlockAudioDevice(g_mixDev);
    return slot; }
void sfxBankFree(int slot){ if(slot<0||slot>=g_nSfx) return; if(g_mixDev) SDL_LockAudioDevice(g_mixDev);
    for(int v=0;v<SFX_VOICES;v++) if(g_voice[v].on&&g_voice[v].clip==slot) g_voice[v].on=0;
    free(g_sfx[slot].pcm); g_sfx[slot].pcm=NULL; g_sfx[slot].n=0; g_sfx[slot].name[0]=0; g_sfx[slot].missing=0; if(g_mixDev) SDL_UnlockAudioDevice(g_mixDev); }
/* a voice's line: all of it, in the mixer's form. Any thread, so it reads the file itself and touches nothing of the mixer's */
static float* sfxDecodeBytes(const unsigned char*b,size_t len,const char*path,int*nOut){ int ch=0,rate=0,frames=0; float*mix=NULL; const size_t ln=strlen(path);
    if(ln>4&&!strcmp(path+ln-4,".ogg")){ short*o=NULL; const int fr=stb_vorbis_decode_memory(b,(int)len,&ch,&rate,&o); if(fr<=0||!o||ch<1){ free(o); return NULL; } frames=fr; mix=(float*)malloc((size_t)frames*sizeof(float)); for(int i=0;i<frames;i++){ float s=0.f; for(int c=0;c<ch;c++) s+=(float)o[i*ch+c]/32768.f; mix[i]=s/(float)ch; } free(o); }
    else { if(len<12||memcmp(b,"RIFF",4)||memcmp(b+8,"WAVE",4)) return NULL; size_t at=12; int bits=0,fmt=0; const unsigned char*data=NULL; size_t dataN=0;
        while(at+8<=len){ const unsigned n=b[at+4]|b[at+5]<<8|b[at+6]<<16|(unsigned)b[at+7]<<24; const unsigned char*c=b+at+8; size_t have=len-at-8; if(n<have) have=n;
            if(!memcmp(b+at,"fmt ",4)&&have>=16){ fmt=c[0]|c[1]<<8; ch=c[2]; rate=c[4]|c[5]<<8|c[6]<<16; bits=c[14]; } else if(!memcmp(b+at,"data",4)){ data=c; dataN=have; break; } at+=8+(size_t)n+(n&1u); }
        const int bytes=bits/8; if(!data||ch<1||ch>8||rate<1||!((fmt==1&&bits==16)||(fmt==3&&bits==32)||(fmt==1&&bits==8))) return NULL; frames=(int)(dataN/(size_t)(bytes*ch)); if(frames<1) return NULL; mix=(float*)malloc((size_t)frames*sizeof(float));
        for(int i=0;i<frames;i++){ float s=0.f; for(int c=0;c<ch;c++){ const unsigned char*q=data+(size_t)(i*ch+c)*(size_t)bytes; if(bits==16){ short v; memcpy(&v,q,2); s+=(float)v/32768.f; } else if(bits==32){ float v; memcpy(&v,q,4); s+=v; } else s+=((float)q[0]-128.f)/128.f; } mix[i]=s/(float)ch; } }
    if(rate==SFX_RATE){ *nOut=frames; return mix; }
    const double step=(double)rate/(double)SFX_RATE; const int m=(int)((double)frames/step); float*o=(float*)malloc((size_t)(m>0?m:1)*sizeof(float)); for(int i=0;i<m;i++){ const double x=(double)i*step; const int k=(int)x; const float u=(float)(x-(double)k); o[i]=k+1<frames?mix[k]*(1.f-u)+mix[k+1]*u:mix[frames-1]; } free(mix); *nOut=m; return o; }
float* sfxDecodeFile(const char*path,int*n){ char p[256]; snprintf(p,sizeof p,"%s",path); FILE*f=fopen(p,"rb"); if(!f){ snprintf(p,sizeof p,"%s.wav",path); f=fopen(p,"rb"); } if(!f){ snprintf(p,sizeof p,"%s.ogg",path); f=fopen(p,"rb"); } if(!f) return NULL;
    clubOpened(p); fseek(f,0,SEEK_END); const long len=ftell(f); fseek(f,0,SEEK_SET); unsigned char*b=len>0?(unsigned char*)malloc((size_t)len):NULL; if(!b||fread(b,1,(size_t)len,f)!=(size_t)len){ free(b); fclose(f); return NULL; } fclose(f);
    float*o=sfxDecodeBytes(b,(size_t)len,p,n); free(b); return o; }
void sfxVoiceGain(int id,float g){ if(id<0||!g_mixDev) return; int v=id>>16; if(v<0||v>=SFX_VOICES) return;
    SDL_LockAudioDevice(g_mixDev); if((g_voice[v].gen&0xffff)==(id&0xffff)) g_voice[v].gain=g; SDL_UnlockAudioDevice(g_mixDev); }
/* (11-05) a playing voice's speed: 1 as recorded, 2 an octave up. REVA_NOPITCH=1 holds every voice at 1, for a control */
void sfxVoicePitch(int id,float r){ static int off=-1; if(off<0) off=getenv("REVA_NOPITCH")?1:0;
    if(off||id<0||!g_mixDev) return;
    const int v=id>>16; if(v<0||v>=SFX_VOICES) return;
    if(!(r>0.f)) r=1.f;
    r=r<0.25f?0.25f:r>4.f?4.f:r;
    SDL_LockAudioDevice(g_mixDev); if((g_voice[v].gen&0xffff)==(id&0xffff)) g_voice[v].rate=r; SDL_UnlockAudioDevice(g_mixDev); }
void sfxSetMaster(float m){ g_sfxMaster=m; }
int sfxReady(void){ return g_mixDev!=0; }
int sfxPlay(const char*name,float gain,const float*pos,int loop){ return sfxPlaySlot(sfxLoad(name),gain,pos,loop); }
void sfxStop(int id){ if(id<0||!g_mixDev) return; int v=id>>16; if(v<0||v>=SFX_VOICES) return;
    SDL_LockAudioDevice(g_mixDev); if((g_voice[v].gen&0xffff)==(id&0xffff)) g_voice[v].on=0; SDL_UnlockAudioDevice(g_mixDev); }
/* (11-12) a playing voice moved: its world position from now on, its pan and distance worked out from the listener each buffer */
void sfxVoicePlace(int id,const float*p){ if(id<0||!g_mixDev||!p) return; int v=id>>16; if(v<0||v>=SFX_VOICES) return;
    SDL_LockAudioDevice(g_mixDev); if((g_voice[v].gen&0xffff)==(id&0xffff)){ g_voice[v].p[0]=p[0]; g_voice[v].p[1]=p[1]; g_voice[v].p[2]=p[2]; g_voice[v].spatial=1; } SDL_UnlockAudioDevice(g_mixDev); }
float sfxLength(const char*name){ const int s=sfxLoad(name); return s<0?-1.f:(float)g_sfx[s].n/(float)SFX_RATE; }
void sfxListener(const float*eye,const float*ctr){
    float fx=ctr[0]-eye[0], fz=ctr[2]-eye[2], fl=sqrtf(fx*fx+fz*fz); if(fl<1e-4f) return;
    if(g_mixDev) SDL_LockAudioDevice(g_mixDev);
    g_lstE[0]=eye[0]; g_lstE[1]=eye[1]; g_lstE[2]=eye[2];
    g_lstR[0]=fz/fl; g_lstR[1]=0.f; g_lstR[2]=-fx/fl;      /* right = forward x up */
    if(g_mixDev) SDL_UnlockAudioDevice(g_mixDev); }
int sfxPlayBus(int bus,const char*name,float gain,const float*pos,int loop){ int id=sfxPlaySlot(sfxLoad(name),gain,pos,loop); if(id>=0&&bus>=0&&bus<BUS_N) g_voice[id>>16].bus=bus; return id; }
/* (11-09) a sound that starts `seconds` from now, to the sample (the live band's notes, known ahead: games/reva_band) */
int sfxPlayIn(int bus,const char*name,float gain,float seconds){ g_playWait=seconds>0.f?(double)seconds:0.0; const double want=sfxClock()+g_playWait; int id=sfxPlaySlot(sfxLoad(name),gain,NULL,0); g_playWait=0.0;
    if(g_sfxSync<0) g_sfxSync=getenv("REVA_SFXSYNC")?1:0;
    if(g_sfxSync>0&&id>=0&&g_mixDev){ SDL_LockAudioDevice(g_mixDev); int n=g_stN; int gg[256]; long aa[256]; memcpy(gg,g_stGen,sizeof gg); memcpy(aa,g_stAt,sizeof aa); g_stN=0; SDL_UnlockAudioDevice(g_mixDev);
        printf("[sfxin] %d %.6f\n",g_voice[id>>16].gen,want); for(int i=0;i<n;i++) printf("[sfxst] %d %ld\n",gg[i],aa[i]); fflush(stdout); }
    if(id>=0&&bus>=0&&bus<BUS_N) g_voice[id>>16].bus=bus; return id; }
void sfxBus(int bus,float gain,float fadeSeconds){ if(bus<0||bus>=BUS_N) return; g_bus[bus].want=gain; g_bus[bus].rate=fadeSeconds>0.f?fabsf(gain-g_bus[bus].now)/fadeSeconds:0.f; }
/* the music's thread: read a block, turn it into the mixer's rate and two channels, and wait while the ring is full */
static int musThread(void*ud){ MusDeck*M=(MusDeck*)ud; stb_vorbis*ov=NULL; FILE*wf=NULL; int ch=2,rate=SFX_RATE; long wavAt=0,wavLen=0,wavLeft=0;
    size_t ln=strlen(M->path);
    if(ln>4&&!strcmp(M->path+ln-4,".ogg")){ int err=0; ov=stb_vorbis_open_filename(M->path,&err,NULL); if(!ov){ printf("[music] can't read %s (%d)\n",M->path,err); fflush(stdout); SDL_AtomicSet(&M->run,0); return 0; }
        stb_vorbis_info vi=stb_vorbis_get_info(ov); ch=vi.channels; rate=(int)vi.sample_rate;
        SDL_AtomicSet(&M->len,(int)((double)stb_vorbis_stream_length_in_samples(ov)*(double)SFX_RATE/(double)(rate>0?rate:SFX_RATE))); }
    else { wf=fopen(M->path,"rb"); unsigned char h[12]; if(!wf||fread(h,1,12,wf)!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4)){ printf("[music] can't read %s: not a WAV\n",M->path); fflush(stdout); if(wf) fclose(wf); SDL_AtomicSet(&M->run,0); return 0; }
        int bits=0; for(;;){ unsigned char c[8]; if(fread(c,1,8,wf)!=8) break; unsigned n=c[4]|c[5]<<8|c[6]<<16|(unsigned)c[7]<<24;
            if(!memcmp(c,"fmt ",4)){ unsigned char fm[16]; if(fread(fm,1,16,wf)!=16) break; ch=fm[2]; rate=fm[4]|fm[5]<<8|fm[6]<<16; bits=fm[14]; if(n>16) fseek(wf,(long)(n-16),SEEK_CUR); }
            else if(!memcmp(c,"data",4)){ wavAt=ftell(wf); wavLen=(long)n; break; }
            else fseek(wf,(long)(n+(n&1)),SEEK_CUR); }
        if(bits!=16||!wavLen||ch<1||ch>2){ printf("[music] %s: 16-bit WAV, one or two channels, please\n",M->path); fflush(stdout); fclose(wf); SDL_AtomicSet(&M->run,0); return 0; }
        wavLeft=wavLen; SDL_AtomicSet(&M->len,(int)((double)(wavLen/(2*ch))*(double)SFX_RATE/(double)(rate>0?rate:SFX_RATE))); }
    clubOpened(M->path);
    printf("[music] %s, %d channels at %d Hz\n",M->path,ch,rate); fflush(stdout);
    float src[2048*2]; short raw[2048*2]; double step=(double)rate/(double)SFX_RATE,ph=0.0; float pv[2]={0.f,0.f};
    while(SDL_AtomicGet(&M->run)){
        int wr=SDL_AtomicGet(&M->wr),rd=SDL_AtomicGet(&M->rd);
        if(MUS_CAP-(wr-rd)<(int)(2048.0/step+2.0)*2+4){ SDL_Delay(15); continue; }
        int n=0;
        if(ov) n=stb_vorbis_get_samples_float_interleaved(ov,2,src,2048*2);
        else { long fr=wavLeft/(2*ch); if(fr>2048) fr=2048; if(fr>0){ n=(int)fread(raw,(size_t)(2*ch),(size_t)fr,wf); wavLeft-=(long)n*2*ch; for(int i=0;i<n;i++){ src[i*2]=raw[i*ch]/32768.f; src[i*2+1]=raw[i*ch+(ch-1)]/32768.f; } } }
        if(n<=0){ if(!M->loop) { SDL_Delay(20); continue; } if(ov) stb_vorbis_seek_start(ov); else { fseek(wf,wavAt,SEEK_SET); wavLeft=wavLen; } continue; }
        for(int i=0;i<n;i++){ while(ph<=1.0){ float t=(float)ph; int w2=SDL_AtomicGet(&M->wr);
                M->ring[(unsigned)w2%MUS_CAP]=pv[0]+(src[i*2]-pv[0])*t; M->ring[(unsigned)(w2+1)%MUS_CAP]=pv[1]+(src[i*2+1]-pv[1])*t; SDL_AtomicSet(&M->wr,w2+2); ph+=step; }
            ph-=1.0; pv[0]=src[i*2]; pv[1]=src[i*2+1]; } }
    if(ov) stb_vorbis_close(ov);
    if(wf) fclose(wf);
    return 0; }
static void musHalt(MusDeck*M){ if(!M->th) return; SDL_AtomicSet(&M->run,0); SDL_WaitThread(M->th,NULL); M->th=NULL; }
/* a deck from the start of a file, at a fade going to fadeTo over seconds (0: at once) */
static int musStart(MusDeck*M,const char*path,float gain,int loop,float fade,float fadeTo,float seconds){
    musHalt(M);
    if(!M->ring) M->ring=(float*)malloc(MUS_CAP*sizeof(float));
    if(!M->ring) return 0;
    snprintf(M->path,sizeof M->path,"%s",path); M->loop=loop; M->gain=gain;
    if(g_mixDev) SDL_LockAudioDevice(g_mixDev);
    M->fade=fade; M->fadeTo=fadeTo; M->fadeStep=seconds>0.f?1.f/(seconds*(float)SFX_RATE):1.f;
    SDL_AtomicSet(&M->wr,0); SDL_AtomicSet(&M->rd,0); SDL_AtomicSet(&M->out,0); SDL_AtomicSet(&M->len,0);
    if(g_mixDev) SDL_UnlockAudioDevice(g_mixDev);
    SDL_AtomicSet(&M->run,1);
    M->th=SDL_CreateThread(musThread,"music",M);
    return M->th!=NULL; }
/* the deck that's gone out, let go once it's silent */
static void musReap(void){ MusDeck*M=&g_mus[1-g_musCur]; if(M->th&&M->fade<=0.f&&M->fadeTo<=0.f) musHalt(M); }
int musicPlay(const char*path,float gain,int loop){ if(!g_mixDev) return 0;
    musHalt(&g_mus[1-g_musCur]);   /* a cut: the other deck stops too */
    SDL_AtomicSet(&g_musOut,0); sfxBus(BUS_MUSIC,1.f,0.f);
    return musStart(&g_mus[g_musCur],path,gain,loop,1.f,1.f,0.f); }
int musicCross(const char*path,float gain,int loop,float seconds){ if(!g_mixDev) return 0;
    MusDeck*was=&g_mus[g_musCur];
    if(was->th&&SDL_AtomicGet(&was->run)){ SDL_LockAudioDevice(g_mixDev); was->fadeTo=0.f; was->fadeStep=seconds>0.f?1.f/(seconds*(float)SFX_RATE):1.f; SDL_UnlockAudioDevice(g_mixDev); }
    else musHalt(was);
    g_musCur=1-g_musCur;
    SDL_AtomicSet(&g_musOut,0); sfxBus(BUS_MUSIC,1.f,0.f);
    return musStart(&g_mus[g_musCur],path,gain,loop,0.f,1.f,seconds); }
float musicLeft(void){ musReap(); MusDeck*M=&g_mus[g_musCur];
    if(!M->th||!SDL_AtomicGet(&M->run)||M->loop) return -1.f;
    const int len=SDL_AtomicGet(&M->len); if(len<=0) return -1.f;   /* not open yet */
    const int left=len-SDL_AtomicGet(&M->out); return left>0?(float)left/(float)SFX_RATE:0.f; }
double musicSeconds(void){ return (double)SDL_AtomicGet(&g_musOut)/(double)SFX_RATE; }
void musicStop(float fadeSeconds){ if(fadeSeconds>0.f){ sfxBus(BUS_MUSIC,0.f,fadeSeconds); return; } musHalt(&g_mus[0]); musHalt(&g_mus[1]); }
void sfxInit(void){
    if(g_mixDev) return;   /* idempotent: door 11 opens the mixer before the club does (reva 10-12) */
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
