/* ---- (11-08) THE NOTE BUS (THE REVA BAND: design/reva_band_gdd.md, section 14) ------------------------------------------------------------------------
 * Timed notes from outside, as a band brain makes them, for a game to play: REVA_NOTES=<port> opens
 * a UDP port on this machine (club_net.c's notesOpen: 127.0.0.1 only). A packet is text, a line each:
 *     now <T>                          the sender's clock as it sent, seconds
 *     n <t> <ch> <note> <vel> <dur>    a note at the sender's time t, on MIDI channel ch (0-15), note and velocity 0-127, lasting dur seconds
 * The sender's clock is kept against the engine's wall clock by the smallest gap ever seen between a `now` and its arrival (the packet that waited least), and a
 * game is handed each note on the bus's own clock (E->noteTime: the wall's, in seconds), early, as the sender scheduled it ahead.
 * ⛔ WHAT'S DRAWN AND HEARD, NEVER THE RULES: nothing here reaches [simhash], and a tick mustn't read it (a tape replays without a band brain). A game reads it in draw.
 * ⛔ EVERYTHING THAT COMES IN IS A STRANGER'S: every number is checked (finite, in range) before it's kept, and a line that isn't one of the two is skipped. */
#include "../club_net.h"
#define NB_MAX 2048
static EngineNote g_nbQ[NB_MAX]; static int g_nbN = 0, g_nbOn = -1;
static double g_nbOff = 1e30;                                         /* the wall's seconds minus the sender's, the smallest seen */
static double nbWall(void){ return wall_ms() / 1000.0; }
static double apiNoteTime(void){ return nbWall(); }
/* (11-08) THE POSE STREAM, on the same port: a solver beside the engine (games/reva_band/solver_live.py: z_band's own drummer and keyboard player, the
 * takes' solver) sends each frame of a player's pose before it's due, a line each:
 *     p <name> <t> <nq> <i qx qy qz qw>... <nt> <i x y z>...     a node's turn (as the pack keeps it) and a node's place, at the sender's time t
 * kept by name, in time order, for engine/perform.inc to play on the body the game named for it (E->perfStream): what a clip's tracks would have said */
#define PS_STREAMS 4
#define PS_FRAMES 900
#define PS_Q 192
#define PS_T 32
typedef struct { double t; short nq, nt; short qi[PS_Q]; float q[PS_Q][4]; short ti[PS_T]; float tv[PS_T][3]; } PsFrame;
typedef struct { char name[16]; PsFrame f[PS_FRAMES]; int n; } PsStream;
static PsStream g_ps[PS_STREAMS]; static int g_psN = 0;
static PsStream *psFind(const char *nm, int make){
    for(int i = 0; i < g_psN; i++) if(!strcmp(g_ps[i].name, nm)) return &g_ps[i];
    if(!make || g_psN >= PS_STREAMS) return NULL; PsStream *s = &g_ps[g_psN++]; memset(s, 0, sizeof *s); snprintf(s->name, sizeof s->name, "%s", nm); return s; }
static void psLine(char *ln, double arrive){
    (void)arrive; char nm[16]; int used = 0;
    if(sscanf(ln, "%15s%n", nm, &used) != 1 || g_nbOff > 1e29) return;
    char *p = ln + used, *e; const double t = strtod(p, &e); if(e == p || !isfinite(t) || fabs(t) > 1e9) return; p = e;
    const long nq = strtol(p, &e, 10); if(e == p || nq < 0 || nq > PS_Q) return; p = e;
    PsFrame F; F.t = t + g_nbOff; F.nq = (short)nq;
    for(long k = 0; k < nq; k++){ const long i = strtol(p, &e, 10); if(e == p || i < 0 || i > 4095) return; p = e; F.qi[k] = (short)i;
        for(int c = 0; c < 4; c++){ const float v = strtof(p, &e); if(e == p || !isfinite(v) || fabsf(v) > 2.f) return; p = e; F.q[k][c] = v; } }
    const long nt = strtol(p, &e, 10); if(e == p || nt < 0 || nt > PS_T) return; p = e; F.nt = (short)nt;
    for(long k = 0; k < nt; k++){ const long i = strtol(p, &e, 10); if(e == p || i < 0 || i > 4095) return; p = e; F.ti[k] = (short)i;
        for(int c = 0; c < 3; c++){ const float v = strtof(p, &e); if(e == p || !isfinite(v) || fabsf(v) > 1e4f) return; p = e; F.tv[k][c] = v; } }
    PsStream *s = psFind(nm, 1); if(!s) return;
    const double now = nbWall(); int drop = 0; while(drop < s->n && s->f[drop].t < now - 1.0) drop++;   /* frames long past go */
    if(drop){ memmove(s->f, s->f + drop, sizeof(PsFrame) * (size_t)(s->n - drop)); s->n -= drop; }
    if(s->n >= PS_FRAMES) return;
    int k = s->n; while(k > 0 && s->f[k - 1].t > F.t){ s->f[k] = s->f[k - 1]; k--; }   /* in time order: the solver's workers finish out of order */
    s->f[k] = F; s->n++; }
static void nbPump(void){
    if(g_nbOn < 0){ const char *e = getenv("REVA_NOTES"); g_nbOn = 0;
        if(e && atoi(e) > 0){ g_nbOn = notesOpen(atoi(e)); printf("[notes] the note bus %s on port %d\n", g_nbOn ? "listening" : "could NOT open", atoi(e)); fflush(stdout); } }
    if(!g_nbOn) return;
    static char buf[65536]; int guard = 0;
    while(guard++ < 64){
        const int n = notesRecv(buf, (int)sizeof buf); if(n <= 0) break;
        const double arrive = nbWall();
        for(char *ln = strtok(buf, "\n"); ln; ln = strtok(NULL, "\n")){
            double t; float dur; int ch, note, vel;
            if(ln[0] == 'p' && ln[1] == ' '){ psLine(ln + 2, arrive); continue; }   /* (11-08) a pose frame */
            if(sscanf(ln, "now %lf", &t) == 1){ if(isfinite(t) && fabs(t) < 1e9){ const double off = arrive - t; if(off < g_nbOff) g_nbOff = off; } continue; }
            if(sscanf(ln, "n %lf %d %d %d %f", &t, &ch, &note, &vel, &dur) == 5){
                if(!isfinite(t) || fabs(t) > 1e9 || !isfinite(dur) || dur < 0.f || dur > 60.f || ch < 0 || ch > 15 || note < 0 || note > 127 || vel < 0 || vel > 127) continue;
                if(g_nbOff > 1e29 || g_nbN >= NB_MAX) continue;               /* no clock yet, or the game isn't reading: dropped */
                EngineNote *q = &g_nbQ[g_nbN++]; q->t = t + g_nbOff; q->dur = dur; q->ch = ch; q->note = note; q->vel = vel; } } }
}
static int apiNotesIn(EngineNote *out, int max){
    nbPump(); if(!out || max <= 0) return 0;
    const int k = g_nbN < max ? g_nbN : max;
    memcpy(out, g_nbQ, sizeof(EngineNote) * (size_t)k);
    memmove(g_nbQ, g_nbQ + k, sizeof(EngineNote) * (size_t)(g_nbN - k)); g_nbN -= k;
    return k;
}
