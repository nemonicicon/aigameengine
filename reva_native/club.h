/* club.h -- what the club's translation units share. 2026-09-30: the split, stage 1 and 2.
 * ⛔ Everything in crowd_kms.c is static and always was; a section leaves only when its outside
 * face is this small. The receipt for every stage: REVA_SIMHASH lines and the lab frame byte-identical
 * to the run before the move (design/platform_tiers.md, "the split"). */
#ifndef CLUB_H
#define CLUB_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>

/* THE CLUB'S OWN MATH (11-02) is club_math.h: one answer on every machine. A game gets that header through engine.h and nothing else of this one (step 20) */
#include "club_math.h"

/* club_math.c: column-major 4x4, the same math as crowd_bench */
typedef float M4[16];
void m4_mul(M4 r,const M4 a,const M4 b);
void m4_trs(M4 m,const float*t,const float*q,const float*s);
void m4_persp(M4 m,float fovyDeg,float aspect,float zn,float zf);
void m4_lookat(M4 m,float ex,float ey,float ez,float cx,float cy,float cz);

void clubOpened(const char*path);   /* crowd_kms.c: REVA_OPENLOG, every file the engine reads */
/* club_audio.c: THE AUDIO MIXER (09-11). REVA_NOAUDIO, REVA_SFXDBG, REVA_AUDIODUMP as before. */
int sfxBankFromWav(const char*name,Uint8*buf,Uint32 len,SDL_AudioSpec*spec);
int sfxPlaySlot(int slot,float gain,const float*pos,int loop);
int sfxPlay(const char*name,float gain,const float*pos,int loop);
void sfxPreload(const char*name);                    /* in the bank now, so its first play doesn't read the disk in the middle of a frame */
void sfxStop(int id);
void sfxListener(const float*eye,const float*ctr);
void sfxInit(void);
/* (10-22, reva) door 11 (lighthouse.c) synthesises its own sounds and turns them while they play */
#define SFX_RATE 44100
float* sfxDecodeFile(const char*path,int*n);          /* a WAV or an Ogg, whole, as mono float at SFX_RATE. malloc'd; NULL if it can't be read. Any thread. A name with no such file tries .wav and .ogg */
void sfxBankFree(int slot);                          /* its memory goes, and anything playing it stops */
int sfxBankPCM(const char*name,float*pcm,int n);     /* takes pcm (malloc'd, mono float at SFX_RATE); the slot */
void sfxVoiceGain(int id,float g);                   /* a playing voice's gain (the id sfxPlay* returned) */
void sfxSetMaster(float m);                          /* the master volume, 0..1 */
int sfxReady(void);                                  /* is the device open */
/* (11-03) every voice is on a bus, and a bus's gain can fade: the music comes down under a voice and nothing else moves. Music is streamed off the disk by a thread of its own, so it can't make a frame late. Ogg Vorbis or 16-bit WAV, any rate. None of this is in the sim */
enum { BUS_SFX, BUS_MUSIC, BUS_VOICE, BUS_UI, BUS_N };
int sfxPlayBus(int bus,const char*name,float gain,const float*pos,int loop);
void sfxBus(int bus,float gain,float fadeSeconds);
int musicPlay(const char*path,float gain,int loop);  /* 1 if it opened. Takes over from whatever was playing */
void musicStop(float fadeSeconds);
double musicSeconds(void);                           /* how much of the music has really gone out, in seconds, since it started. Any thread */
/* club_save.c: THE SAVE FILE (09-01). REVA_NOSAVE=1 makes the bench blind to it. */
const char* saveGetS(const char*k,const char*def);
long saveGetI(const char*k,long def);
void saveSetS(const char*k,const char*v);
void saveSetI(const char*k,long v);
void saveLoad(void);
void saveFlush(void);
/* (step 20, 11-04) a game's own save, in named slots: save/<game>_<slot>.txt, text, a version line then key=value lines. E->saveBegin..E->loadGet in engine.h are these */
void slotBegin(const char*game,const char*slot,int version);
void slotSet(const char*k,const char*v);
int  slotEnd(void);
int  slotLoad(const char*game,const char*slot);
const char* slotGet(const char*k,const char*def);
/* (step 29) a session's saves are the host's: handed over when players gather, read from memory by every machine while it lasts (club_save.c says why) */
void slotNetBegin(int host); void slotNetEnd(void); void slotNetPut(const char*path,const char*bytes,int n);
int  slotNetOffer(const char*game,void(*offer)(const char*path,const void*bytes,int n));
#endif
