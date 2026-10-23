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

/* club_math.c: column-major 4x4, the same math as crowd_bench */
typedef float M4[16];
void m4_mul(M4 r,const M4 a,const M4 b);
void m4_trs(M4 m,const float*t,const float*q,const float*s);
void m4_persp(M4 m,float fovyDeg,float aspect,float zn,float zf);
void m4_lookat(M4 m,float ex,float ey,float ez,float cx,float cy,float cz);

/* club_audio.c: THE AUDIO MIXER (09-11). REVA_NOAUDIO, REVA_SFXDBG, REVA_AUDIODUMP as before. */
int sfxBankFromWav(const char*name,Uint8*buf,Uint32 len,SDL_AudioSpec*spec);
int sfxPlaySlot(int slot,float gain,const float*pos,int loop);
int sfxPlay(const char*name,float gain,const float*pos,int loop);
void sfxStop(int id);
void sfxListener(const float*eye,const float*ctr);
void sfxInit(void);
/* (10-22, reva) door 11 (lighthouse.c) synthesises its own sounds and turns them while they play */
#define SFX_RATE 44100
int sfxBankPCM(const char*name,float*pcm,int n);     /* takes pcm (malloc'd, mono float at SFX_RATE); the slot */
void sfxVoiceGain(int id,float g);                   /* a playing voice's gain (the id sfxPlay* returned) */
void sfxSetMaster(float m);                          /* the master volume, 0..1 */
int sfxReady(void);                                  /* is the device open */
/* club_save.c: THE SAVE FILE (09-01). REVA_NOSAVE=1 makes the bench blind to it. */
const char* saveGetS(const char*k,const char*def);
long saveGetI(const char*k,long def);
void saveSetS(const char*k,const char*v);
void saveSetI(const char*k,long v);
void saveLoad(void);
void saveFlush(void);
#endif
