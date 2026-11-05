/* club_net.c -- ONLINE PLAY'S WIRE (queue step 29, 11-05). Its own file for the reason club_bridge.c is: winsock has to come before windows.h.
 *
 * WHAT A SESSION IS. The engine's rules are a function of the dice and of every pad's state, tick by tick: that's what a tape is, and why one replays on any machine
 * to the same hashes. So two machines play the same game by agreeing the dice and then giving each other their pads, a tick at a time. Nothing of the world is ever
 * sent, only pads: a session costs the same whether the game has two bodies or two thousand, and a Pi and a PC can share one because they work out the same world.
 *
 * LOCKSTEP, WITH A DELAY. What a player's pad does at tick T is played, on every machine including their own, at tick T+delay. A machine runs tick T only when it holds
 * every pad's state for T, and waits when it doesn't: that wait is the whole of what happens when the wire is slow, a picture held for a moment, never a different game.
 * The delay is chosen once, when the session starts, from how long the wire takes, and it doesn't move after: no automatic cuts.
 * ⛔ NOT ROLLBACK. Rollback plays ahead on a guess and re-runs the ticks it guessed wrong, which needs the whole world saved and put back, and every re-run tick costs a
 * tick again. This engine's world isn't in one place to save, and a Pi's field hasn't a second tick in it. engine_plan.md says so under step 29.
 *
 * THE WIRE. UDP, one port, a star: every player talks to the host and the host passes pads on. A packet of pads carries every tick the other end hasn't said it has,
 * so a lost packet costs nothing but the next one's arrival. The few things that must arrive (the start, the saves, who's left) go in a small in-order channel, one at a
 * time, sent again until it's answered. Every tick's hash of the rules is compared as it runs: two machines that part are told the tick, at once.
 *
 * ⛔ EVERYTHING THAT COMES IN IS A STRANGER'S. Every length, count, seat, tick and index in a packet is checked against what this end knows before it's used, a packet
 * from an address that isn't the session's is dropped unread, and all a peer can ever put into the game is the state of its own pads. anim_bench/netcheck.py throws
 * broken packets at a running session under the compiler's memory checkers. ⛔ Nothing here is secret or signed: the word that keeps a stranger out of a session is a
 * lock against accidents, and anyone on the path can read or forge a packet. It's for friends, not for a prize.
 * ⛔ Addresses are IPv4, the host's port has to be reachable (the same network, or a port opened on a router), and there's no matchmaking: none of that is built. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK close
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "club_net.h"

#define NET_MAGIC 0x314E5652u   /* "RVN1" */
#define NET_PROTO 1
#define NET_RING 512            /* ticks of pads kept a peer: eight and a half seconds, far past the longest wait */
#define NET_MTU 1200
#define NET_REL_MAX 1000
#define NET_SAVES 16
#define NET_SAVE_MAX 40000

enum { T_HELLO = 1, T_WELCOME, T_REFUSE, T_LOBBY, T_REL, T_ACK, T_INPUT, T_PING, T_PONG, T_LAST };
enum { K_BEGIN = 1, K_SAVE, K_READY, K_GO, K_DROP, K_BYE };
#define SEAT_BYTES 17

typedef struct RelMsg { struct RelMsg *next; unsigned short seq; int len; unsigned char d[NET_REL_MAX + 8]; } RelMsg;
typedef struct {
    int used; long goneAt;                      /* goneAt > 0: its pads are unplugged from that tick on */
    struct sockaddr_in addr;
    int firstSeat, nSeats;
    long have;                                  /* its pads are held here for every tick up to this one */
    long ackOf[NET_PEERS];                      /* what it says it holds of each peer's pads */
    NetSeat in[NET_RING][NET_SEATS]; long inTick[NET_RING];
    double heard, sentAt, pingAt, waitSince;    /* waitSince: when the host last had this player's pads for the tick it's on */
    RelMsg *relHead, *relTail; unsigned short relNext, relWant; double relSentAt;
    int ready; unsigned long long sig; unsigned nFiles;
    unsigned binds[NET_ACTS][NET_SEATS]; unsigned char bindSet[NET_ACTS][NET_SEATS];
    double rtt; int rttN; unsigned nonce;       /* the shortest there-and-back measured, and how many times it's been measured */
    double clockOff;                            /* its clock less this machine's, from that shortest trip: how a moment on the host's clock is found on a player's */
    long hashTick, hashDone; unsigned long long hash;   /* the newest hash it's sent, and the last tick of its that was held against ours */
    long tick;                                  /* the last tick it says it ran */
    long leaving; char leaveWhy[64];            /* it's said goodbye, and its last pads are for this tick: it's gone once they're all here */
} Peer;
typedef struct { char name[48]; int n, got; unsigned char *d; } NetSave;
static struct {
    int state, host, me; long sock; unsigned session, word; char game[24];
    Peer p[NET_PEERS];
    int seatsWant, myLocal;
    unsigned seed; int begun, go, delay, hashEvery, nAct;
    unsigned binds[NET_ACTS][NET_SEATS];
    NetSave save[NET_SAVES]; int nSave, saveChunks, saveGot;
    char why[128];
    struct sockaddr_in hostAddr; char hostName[96]; int port;
    double helloAt, lobbyAt, openAt, lastPump, timeoutS, joinS, readyAt, startLocal, beganAt, loadS;
    int lobbyHave, lobbyWant, lobbySeat;
    struct { long tick; unsigned long long h; } hash[256];
    long myTick, parted, lastLocal, waitSeat;
    NetSeat direct[NET_SEATS];                  /* my own pads as they are now, for when the session's over */
    /* a slow wire, for testing: REVA_NETLAG ms, REVA_NETJITTER ms, REVA_NETLOSS and REVA_NETDUP percent */
    double lag, jitter; int loss, dup; unsigned long long dice;
    struct { double at; struct sockaddr_in to; int n; unsigned char d[NET_MTU + 64]; } q[256]; int nq;
    unsigned long sent, got, resent, refusedPk, lost, held; double waited, worstWait, bytesOut, bytesIn;
    int nobye, breakAt, said;
    int over;                                   /* nobody else is in it any more: what was sent is played out, and from there this machine's alone */
} N;

/* ---- small things ---- */
static double netMs(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6; }
static unsigned netDice(void) { N.dice ^= N.dice << 13; N.dice ^= N.dice >> 7; N.dice ^= N.dice << 17; return (unsigned)(N.dice >> 16); }
static unsigned netFnv(const char *s) { unsigned h = 2166136261u; for (; s && *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; } return h; }
static void netSay(const char *fmt, ...) { va_list a; va_start(a, fmt); printf("[net] "); vprintf(fmt, a); printf("\n"); va_end(a); fflush(stdout); }
static int sameAddr(const struct sockaddr_in *a, const struct sockaddr_in *b) { return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port; }
static const char *addrText(const struct sockaddr_in *a, char *out, int cap) { const unsigned char *b = (const unsigned char *)&a->sin_addr.s_addr; snprintf(out, (size_t)cap, "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], (unsigned)ntohs(a->sin_port)); return out; }

/* a packet being written, and one being read: a read past the end marks it bad and gives zeros, so a short packet can't be read off the end of */
typedef struct { unsigned char *p; int n, cap, bad; } Wr;
typedef struct { const unsigned char *p; int n, at, bad; } Rd;
static void w8(Wr *w, unsigned v) { if (w->n + 1 > w->cap) { w->bad = 1; return; } w->p[w->n++] = (unsigned char)v; }
static void w16(Wr *w, unsigned v) { w8(w, v & 255u); w8(w, (v >> 8) & 255u); }
static void w32(Wr *w, unsigned v) { w16(w, v & 65535u); w16(w, (v >> 16) & 65535u); }
static void w64(Wr *w, unsigned long long v) { w32(w, (unsigned)(v & 0xffffffffu)); w32(w, (unsigned)(v >> 32)); }
static void wbytes(Wr *w, const void *d, int n) { if (n < 0 || w->n + n > w->cap) { w->bad = 1; return; } memcpy(w->p + w->n, d, (size_t)n); w->n += n; }
static unsigned r8(Rd *r) { if (r->at + 1 > r->n) { r->bad = 1; return 0; } return r->p[r->at++]; }
static unsigned r16(Rd *r) { unsigned a = r8(r); return a | (r8(r) << 8); }
static unsigned r32(Rd *r) { unsigned a = r16(r); return a | (r16(r) << 16); }
static unsigned long long r64(Rd *r) { unsigned long long a = r32(r); return a | ((unsigned long long)r32(r) << 32); }
static const unsigned char *rbytes(Rd *r, int n) { if (n < 0 || r->at + n > r->n) { r->bad = 1; return NULL; } const unsigned char *p = r->p + r->at; r->at += n; return p; }
static void wSeat(Wr *w, const NetSeat *s) { w16(w, (unsigned short)s->lx); w16(w, (unsigned short)s->ly); w16(w, (unsigned short)s->rx); w16(w, (unsigned short)s->ry); w16(w, (unsigned short)s->rt); w16(w, (unsigned short)s->mdx); w16(w, (unsigned short)s->mdy); w16(w, s->held); w8(w, s->flags); }
static void rSeat(Rd *r, NetSeat *s) { s->lx = (short)r16(r); s->ly = (short)r16(r); s->rx = (short)r16(r); s->ry = (short)r16(r); s->rt = (short)r16(r); s->mdx = (short)r16(r); s->mdy = (short)r16(r);
    s->held = (unsigned short)(r16(r) & 0x7fu); s->flags = (unsigned char)((r8(r) & (NETF_KB | NETF_FPV | NETF_BACK)) | NETF_ON); }   /* only the buttons there are, and a pad that's sent is a pad that's plugged in */

/* ---- sending: straight out, or through the slow wire a test asks for ---- */
static void rawSend(const struct sockaddr_in *to, const unsigned char *d, int n) { if (N.sock >= 0) { N.bytesOut += (double)n; sendto((int)N.sock, (const char *)d, n, 0, (const struct sockaddr *)to, sizeof *to); N.sent++; } }
static void netSend(const struct sockaddr_in *to, const unsigned char *d, int n) {
    if (n <= 0 || n > NET_MTU) return;
    if (N.loss > 0 && (int)(netDice() % 100u) < N.loss) { N.lost++; return; }
    const int copies = (N.dup > 0 && (int)(netDice() % 100u) < N.dup) ? 2 : 1;
    for (int c = 0; c < copies; c++) {
        if ((N.lag > 0.0 || N.jitter > 0.0) && N.nq < 256) { N.q[N.nq].at = netMs() + N.lag + (N.jitter > 0.0 ? (double)(netDice() % 1000u) * 0.001 * N.jitter : 0.0); N.q[N.nq].to = *to; N.q[N.nq].n = n; memcpy(N.q[N.nq].d, d, (size_t)n); N.nq++; }
        else rawSend(to, d, n); } }
static void netFlushSlow(double now) { for (int i = 0; i < N.nq;) { if (N.q[i].at <= now) { rawSend(&N.q[i].to, N.q[i].d, N.q[i].n); N.q[i] = N.q[--N.nq]; } else i++; } }
static void head(Wr *w, unsigned char *buf, int type) { w->p = buf; w->n = 0; w->cap = NET_MTU; w->bad = 0; w32(w, NET_MAGIC); w32(w, N.session); w8(w, (unsigned)type); w8(w, (unsigned)(N.me < 0 ? 255 : N.me)); w16(w, 0); }
static void post(Wr *w, const struct sockaddr_in *to) { if (w->bad || w->n < 12) return; const int len = w->n - 12; w->p[10] = (unsigned char)(len & 255); w->p[11] = (unsigned char)(len >> 8); netSend(to, w->p, w->n); }

/* ---- the in-order channel: one at a time, sent again until it's answered ---- */
static void relPush(Peer *P, int kind, const unsigned char *d, int n) {
    if (!P->used || n < 0 || n > NET_REL_MAX) return;
    RelMsg *m = (RelMsg *)calloc(1, sizeof *m); if (!m) return;
    m->seq = P->relNext++; m->len = n + 1; m->d[0] = (unsigned char)kind; if (n) memcpy(m->d + 1, d, (size_t)n);
    if (P->relTail) P->relTail->next = m; else { P->relHead = m; P->relSentAt = 0.0; }
    P->relTail = m; }
static void relSendHead(Peer *P, double now) { RelMsg *m = P->relHead; if (!m) return; unsigned char b[NET_MTU]; Wr w; head(&w, b, T_REL); w16(&w, m->seq); wbytes(&w, m->d, m->len); post(&w, &P->addr); P->relSentAt = now; }
static void relFree(Peer *P) { while (P->relHead) { RelMsg *m = P->relHead; P->relHead = m->next; free(m); } P->relTail = NULL; }
static void relAcked(Peer *P, unsigned seq) { RelMsg *m = P->relHead; if (!m || m->seq != (unsigned short)seq) return; P->relHead = m->next; if (!P->relHead) P->relTail = NULL; free(m); P->relSentAt = 0.0; }

/* ---- who's here ---- */
static int seatsTaken(void) { int n = 0; for (int i = 0; i < NET_PEERS; i++) if (N.p[i].used && N.p[i].goneAt == 0) n += N.p[i].nSeats; return n; }
static int othersLeft(void) { int n = 0; for (int i = 0; i < NET_PEERS; i++) if (i != N.me && N.p[i].used && N.p[i].goneAt == 0) n++; return n; }
/* it's over for this machine. In the middle of a game nothing stops: every pad it was sent is still played, each machine that was in it is unplugged from the first tick
 * this one hasn't its pads for, and this machine's own pads go on arriving the same few ticks late, so its tape is one game from the first tick to the last */
static void netOver(const char *why) {
    if (N.over || N.state == NET_OVER || N.state == NET_OFF) return;
    snprintf(N.why, sizeof N.why, "%s", why); N.over = 1; netSay("the session's over: %s", N.why);
    if (N.state != NET_PLAY) { N.state = NET_OVER; return; }
    for (int i = 0; i < NET_PEERS; i++) { Peer *P = &N.p[i]; if (i != N.me && P->used && !P->goneAt) P->goneAt = P->have + 1 < 1 ? 1 : P->have + 1; } }
/* goodbye, and the last tick this machine has sent pads for: the other end plays up to there before it counts this machine gone */
static void sendBye(Peer *P, const char *why) { unsigned char d[100]; Wr w = {d, 0, (int)sizeof d, 0}; char t[64]; memset(t, 0, sizeof t); snprintf(t, sizeof t, "%s", why ? why : "");
    w32(&w, (unsigned)((N.state == NET_PLAY && N.me >= 0) ? N.p[N.me].have : 0)); wbytes(&w, t, 64); relPush(P, K_BYE, d, w.n); }
/* the host says a player's gone: its pads are unplugged from the first tick the host hasn't its pads for, and everyone else is told that tick */
static void hostDrop(int c, const char *why) {
    Peer *P = &N.p[c]; if (!N.host || c <= 0 || c >= NET_PEERS || !P->used || P->goneAt) return;
    const long X = P->have + 1 < 1 ? 1 : P->have + 1; P->goneAt = X;
    netSay("player %d's machine is gone (%s): its pad%s unplugged from tick %ld", P->firstSeat + 1, why, P->nSeats > 1 ? "s are" : " is", X);
    for (int i = 1; i < NET_PEERS; i++) if (i != c && N.p[i].used && N.p[i].goneAt == 0) { unsigned char d[8]; Wr w = {d, 0, 8, 0}; w8(&w, (unsigned)c); w32(&w, (unsigned)X); relPush(&N.p[i], K_DROP, d, w.n); }
    if (N.state >= NET_LOAD && !othersLeft()) netOver("everyone else has left"); }
/* a machine that's said goodbye, or gone quiet, is gone */
static void peerGone(int i, const char *why) { Peer *P = &N.p[i]; P->leaving = 0; if (N.host) hostDrop(i, why); else { char m[128]; snprintf(m, sizeof m, "the host: %.100s", why); netOver(m); } }

/* ---- pads: kept by tick ---- */
static const NetSeat NEUTRAL = {0, 0, 0, 0, 0, 0, 0, 0, NETF_ON};
static void padsStart(void) { for (int i = 0; i < NET_PEERS; i++) { Peer *P = &N.p[i]; if (!P->used) continue; P->have = N.delay;
        for (long t = 1; t <= N.delay; t++) { P->inTick[t % NET_RING] = t; for (int s = 0; s < NET_SEATS; s++) P->in[t % NET_RING][s] = NEUTRAL; }
        for (int q = 0; q < NET_PEERS; q++) { P->ackOf[q] = N.delay; }
        P->hashTick = P->hashDone = 0; } }
static void padsKeep(Peer *P, long t, const NetSeat *s) { if (t <= P->have || t > P->have + NET_RING / 2) return; const int k = (int)(t % NET_RING); P->inTick[k] = t; memcpy(P->in[k], s, sizeof(NetSeat) * (size_t)P->nSeats);
    while (P->inTick[(P->have + 1) % NET_RING] == P->have + 1) P->have++; }
/* the pads the other end hasn't said it has, oldest first, as many as fit */
static void wBlock(Wr *w, int q, long from, int room) {
    Peer *Q = &N.p[q]; long to = Q->have; if (Q->goneAt && to > Q->goneAt - 1) to = Q->goneAt - 1;
    int count = (to >= from) ? (int)(to - from + 1) : 0; const int rec = Q->nSeats * SEAT_BYTES; if (count > room / (rec > 0 ? rec : 1)) count = room / (rec > 0 ? rec : 1); if (count > 60) count = 60; if (count < 0) count = 0;
    w8(w, (unsigned)q); w8(w, (unsigned)Q->nSeats); w8(w, (unsigned)count); w8(w, 0); w32(w, (unsigned)from);
    for (int i = 0; i < count; i++) { const int k = (int)((from + i) % NET_RING); for (int s = 0; s < Q->nSeats; s++) wSeat(w, &Q->in[k][s]); } }
static void sendPads(int dest, double now) {
    Peer *D = &N.p[dest]; if (!D->used || dest == N.me) return; unsigned char b[NET_MTU]; Wr w; head(&w, b, T_INPUT);
    for (int q = 0; q < NET_PEERS; q++) w32(&w, (unsigned)(N.p[q].used ? (N.p[q].have > 0x7ffffff0L ? 0x7ffffff0L : N.p[q].have) : 0));
    long ht = 0; unsigned long long hh = 0; if (N.hashEvery > 0 && N.myTick >= N.hashEvery) { ht = N.myTick - N.myTick % N.hashEvery; if (N.hash[ht & 255].tick == ht) hh = N.hash[ht & 255].h; else ht = 0; }
    w32(&w, (unsigned)ht); w64(&w, hh); w32(&w, (unsigned)N.myTick);
    int blocks = 0, which[NET_PEERS];
    if (N.host) { for (int q = 0; q < NET_PEERS; q++) if (q != dest && N.p[q].used) which[blocks++] = q; } else which[blocks++] = N.me;
    w8(&w, (unsigned)blocks);
    for (int i = 0; i < blocks; i++) { const int q = which[i]; const int room = (NET_MTU - w.n - 8 * (blocks - i)) / (blocks - i); wBlock(&w, q, D->ackOf[q] + 1, room > 0 ? room : 0); }
    post(&w, &D->addr); D->sentAt = now; }

/* ---- what comes in ---- */
static void gotPads(int from, Rd *r) {
    Peer *F = &N.p[from]; long ack[NET_PEERS]; for (int q = 0; q < NET_PEERS; q++) ack[q] = (long)r32(r);
    const long ht = (long)r32(r); const unsigned long long hh = r64(r); const long tk = (long)r32(r); int blocks = (int)r8(r); if (r->bad || blocks > NET_PEERS) return;
    for (int q = 0; q < NET_PEERS; q++) if (N.p[q].used && ack[q] > F->ackOf[q]) F->ackOf[q] = ack[q] > N.p[q].have ? N.p[q].have : ack[q];   /* nobody has what hasn't been sent */
    if (tk > F->tick && tk < F->tick + 100000) F->tick = tk;
    if (ht > F->hashDone && ht > F->hashTick && ht <= F->tick) { F->hashTick = ht; F->hash = hh; }
    for (int i = 0; i < blocks; i++) { const int q = (int)r8(r), ns = (int)r8(r), count = (int)r8(r); (void)r8(r); const long first = (long)r32(r); if (r->bad || ns < 1 || ns > NET_SEATS) return;
        const int mine = N.host ? (q == from) : (q != N.me && q >= 0 && q < NET_PEERS && N.p[q].used);   /* a player sends its own pads and no one else's; the host sends everyone's */
        const int ok = mine && q >= 0 && q < NET_PEERS && ns == N.p[q].nSeats && ns >= 1 && ns <= NET_SEATS && first >= 1 && first < 0x7ff00000L;
        for (int k = 0; k < count; k++) { NetSeat s[NET_SEATS]; memset(s, 0, sizeof s); for (int j = 0; j < ns && j < NET_SEATS; j++) rSeat(r, &s[j]); if (r->bad) return;
            if (ok && N.p[q].goneAt == 0) padsKeep(&N.p[q], first + k, s);
            else if (ok && first + k < N.p[q].goneAt) padsKeep(&N.p[q], first + k, s); } } }
static void gotRel(int from, int kind, Rd *r) {
    Peer *F = &N.p[from];
    if (kind == K_BEGIN && !N.host && from == 0 && !N.begun) {
        N.seed = r32(r); N.seatsWant = (int)r8(r); const int np = (int)r8(r); N.nSave = (int)r8(r); N.saveChunks = (int)r16(r); if (r->bad || np > NET_PEERS || N.nSave > NET_SAVES) { netOver("the host's start made no sense"); return; }
        for (int i = 0; i < NET_PEERS; i++) if (i != 0 && i != N.me) N.p[i].used = 0;
        for (int i = 0; i < np; i++) { const int id = (int)r8(r), fs = (int)r8(r), ns = (int)r8(r); if (r->bad || id < 0 || id >= NET_PEERS || ns < 1 || fs < 0 || fs + ns > NET_SEATS) { netOver("the host's start made no sense"); return; }
            N.p[id].used = 1; N.p[id].firstSeat = fs; N.p[id].nSeats = ns; N.p[id].goneAt = 0; }
        if (!N.p[N.me].used) { netOver("the host's start left this machine out"); return; }
        N.begun = 1; N.saveGot = 0; if (N.saveChunks == 0) N.state = NET_LOAD;
    } else if (kind == K_SAVE && !N.host && from == 0 && N.begun && N.state == NET_GATHER) {
        const int fi = (int)r8(r), total = (int)r32(r), off = (int)r32(r), n = (int)r16(r); const unsigned char *nm = rbytes(r, 48), *d = rbytes(r, n);
        if (r->bad || fi < 0 || fi >= N.nSave || total < 0 || total > NET_SAVE_MAX || off < 0 || n < 0 || off + n > total) return;
        NetSave *S = &N.save[fi]; if (!S->d) { S->d = (unsigned char *)calloc(1, (size_t)total + 1); S->n = total; memcpy(S->name, nm, 47); S->name[47] = 0; }
        if (S->d && S->n == total) { memcpy(S->d + off, d, (size_t)n); S->got += n; }
        if (++N.saveGot >= N.saveChunks) N.state = NET_LOAD;
    } else if (kind == K_READY && N.host && from > 0) {
        F->sig = r64(r); F->nFiles = r32(r); const int nb = (int)r16(r); if (r->bad) return;
        for (int i = 0; i < nb; i++) { const int seat = (int)r8(r), act = (int)r8(r); const unsigned mask = r32(r) & 0x7fu; if (r->bad) return;
            if (act >= 0 && act < NET_ACTS && seat >= F->firstSeat && seat < F->firstSeat + F->nSeats && seat < NET_SEATS) { F->binds[act][seat] = mask; F->bindSet[act][seat] = 1; } }   /* a player can move its own buttons, and nobody else's */
        F->ready = 1;
    } else if (kind == K_GO && !N.host && from == 0 && !N.go) {
        N.delay = (int)r8(r); N.hashEvery = (int)r8(r); const int na = (int)r8(r); const unsigned startAt = r32(r); if (r->bad || N.delay < 1 || N.delay > 30 || na > NET_ACTS) { netOver("the host's go made no sense"); return; }
        { const double now = netMs(); double in = (double)(int)(startAt - (unsigned)(long long)(now + N.p[0].clockOff)); if (!(in >= 0.0)) in = 0.0; if (in > 3000.0) in = 3000.0; N.startLocal = now + in; }   /* the host's moment, on this machine's clock */
        if (N.hashEvery < 1) N.hashEvery = 20;
        for (int a = 0; a < na; a++) for (int s = 0; s < NET_SEATS; s++) N.binds[a][s] = r32(r) & 0x7fu;
        if (r->bad) { netOver("the host's go made no sense"); return; } N.nAct = na; N.go = 1;
    } else if (kind == K_DROP && !N.host && from == 0) {
        const int c = (int)r8(r); const long X = (long)r32(r); if (r->bad || c < 0 || c >= NET_PEERS || X < 1) return;
        if (c == N.me) { netOver("the host let this machine go"); return; }
        if (N.p[c].used && !N.p[c].goneAt) { N.p[c].goneAt = X; netSay("player %d's machine is gone: its pad%s unplugged from tick %ld", N.p[c].firstSeat + 1, N.p[c].nSeats > 1 ? "s are" : " is", X); }
    } else if (kind == K_BYE) {
        const long last = (long)r32(r); const unsigned char *t = rbytes(r, 64); char why[65]; if (r->bad) return; memcpy(why, t, 64); why[64] = 0; for (char *c = why; *c; c++) if (*c < 32 || *c > 126) *c = '?';
        if (!why[0]) snprintf(why, sizeof why, "it said goodbye");
        if (N.state == NET_PLAY && !N.over && !F->goneAt && last > F->have && last <= F->have + NET_RING / 2) { F->leaving = last; snprintf(F->leaveWhy, sizeof F->leaveWhy, "%.63s", why); }   /* its last pads are still on their way */
        else peerGone(from, why); } }
static void gotPacket(const struct sockaddr_in *src, const unsigned char *d, int n, double now) {
    Rd r = {d, n, 0, 0}; if (n < 12 || n > NET_MTU) { N.refusedPk++; return; }
    const unsigned magic = r32(&r), session = r32(&r); const int type = (int)r8(&r), from = (int)r8(&r), len = (int)r16(&r);
    if (magic != NET_MAGIC || len != n - 12 || type < 1 || type >= T_LAST) { N.refusedPk++; return; }
    N.got++;
    if (N.host) {
        if (type == T_HELLO) {
            const unsigned proto = r16(&r); const int nl = (int)r8(&r); (void)r8(&r); const unsigned word = r32(&r), nonce = r32(&r); const unsigned char *g = rbytes(&r, 24); if (r.bad) return;
            char game[25]; memcpy(game, g, 24); game[24] = 0; const char *no = NULL; int id = -1;
            for (int i = 1; i < NET_PEERS; i++) if (N.p[i].used && sameAddr(&N.p[i].addr, src) && N.p[i].nonce == nonce) id = i;
            if (id < 0) {
                if (proto != NET_PROTO) no = "a different version of the engine"; else if (strncmp(game, N.game, 23)) no = "a different game"; else if (word != N.word) no = "the wrong word";
                else if (N.state != NET_GATHER || N.begun) no = "the game has started"; else if (nl < 1 || nl > NET_SEATS || seatsTaken() + nl > N.seatsWant) no = "no room";
                if (!no) { for (int i = 1; i < NET_PEERS && id < 0; i++) if (!N.p[i].used) id = i; if (id < 0) no = "no room"; }
                if (no) { unsigned char b[NET_MTU]; Wr w; head(&w, b, T_REFUSE); w32(&w, nonce); char t[64]; memset(t, 0, sizeof t); snprintf(t, sizeof t, "%s", no); wbytes(&w, t, 64); post(&w, src); return; }
                Peer *P = &N.p[id]; memset(P, 0, sizeof *P); P->used = 1; P->addr = *src; P->nonce = nonce; P->nSeats = nl; P->firstSeat = seatsTaken() - nl; P->heard = now; P->rtt = -1.0;
                char at[32]; netSay("player %d has joined from %s (%d of %d)", P->firstSeat + 1, addrText(src, at, sizeof at), seatsTaken(), N.seatsWant); }
            Peer *P = &N.p[id]; P->heard = now; unsigned char b[NET_MTU]; Wr w; head(&w, b, T_WELCOME); w32(&w, nonce); w8(&w, (unsigned)id); w8(&w, (unsigned)P->firstSeat); w8(&w, (unsigned)P->nSeats); w8(&w, (unsigned)N.seatsWant); post(&w, src);
            return; }
        if (from < 1 || from >= NET_PEERS || !N.p[from].used || session != N.session || !sameAddr(&N.p[from].addr, src)) { N.refusedPk++; return; }   /* not one of this session's players, at their address */
    } else {
        if (!sameAddr(&N.hostAddr, src)) { N.refusedPk++; return; }
        if (type == T_WELCOME || type == T_REFUSE) {
            const unsigned nonce = r32(&r); if (r.bad || nonce != N.p[0].nonce || N.me >= 0) return;
            if (type == T_REFUSE) { const unsigned char *t = rbytes(&r, 64); if (r.bad) return; char why[65], m[128]; memcpy(why, t, 64); why[64] = 0; for (char *c = why; *c; c++) if (*c < 32 || *c > 126) *c = '?'; snprintf(m, sizeof m, "the host said no: %s", why); netOver(m); return; }
            const int id = (int)r8(&r), fs = (int)r8(&r), ns = (int)r8(&r), want = (int)r8(&r); if (r.bad || id < 1 || id >= NET_PEERS || ns != N.myLocal || fs < 0 || fs + ns > NET_SEATS) return;
            N.me = id; N.session = session; N.seatsWant = want; Peer *P = &N.p[id]; memset(P, 0, sizeof *P); P->used = 1; P->firstSeat = fs; P->nSeats = ns; N.p[0].heard = now; N.lobbySeat = fs;
            netSay("joined as player %d", fs + 1); return; }
        if (N.me < 0 || session != N.session || from != 0) { N.refusedPk++; return; } }
    Peer *F = &N.p[from]; F->heard = now;
    if (type == T_LOBBY && !N.host) { N.lobbyHave = (int)r8(&r); N.lobbyWant = (int)r8(&r); }
    else if (type == T_PING) { const unsigned t = r32(&r); if (r.bad) return; unsigned char b[NET_MTU]; Wr w; head(&w, b, T_PONG); w32(&w, t); w32(&w, (unsigned)(long long)now); post(&w, &F->addr); }
    else if (type == T_PONG) { const unsigned t = r32(&r), theirs = r32(&r); if (r.bad) return; const double rt = (double)((unsigned)(long long)now - t);
        if (rt >= 0.0 && rt < 5000.0) { F->rttN++;
            if (F->rtt < 0.0 || rt < F->rtt) { F->rtt = rt; F->clockOff = (double)(int)(theirs - (unsigned)(long long)now) + rt * 0.5; }   /* the quickest trip is the one least held up, so its halfway point is the best guess at when the other end answered */
            else F->rtt = F->rtt * 0.98 + rt * 0.02; } }
    else if (type == T_ACK) { const unsigned seq = r16(&r); if (!r.bad) relAcked(F, seq); }
    else if (type == T_REL) { const unsigned seq = r16(&r); const int kind = (int)r8(&r); if (r.bad) return;
        const short ahead = (short)(seq - F->relWant);
        if (ahead <= 0) { unsigned char b[NET_MTU]; Wr w; head(&w, b, T_ACK); w16(&w, seq); post(&w, &F->addr); }   /* this one or an older one: say it's here, again if need be */
        if (ahead == 0) { F->relWant++; gotRel(from, kind, &r); } }
    else if (type == T_INPUT && N.state == NET_PLAY && !N.over) gotPads(from, &r); }

/* ---- the engine's side ---- */
int netState(void) { return N.over ? NET_OVER : N.state; }
int netIsHost(void) { return N.host; }
const char *netWhy(void) { return N.why; }
int netDelay(void) { return N.delay; }
int netSeats(void) { int n = 0; for (int i = 0; i < NET_PEERS; i++) if (N.p[i].used) { const int e = N.p[i].firstSeat + N.p[i].nSeats; if (e > n) n = e; } return n; }
int netFirstSeat(void) { return (N.me >= 0 && N.p[N.me].used) ? N.p[N.me].firstSeat : 0; }
int netLocalSeats(void) { return N.myLocal; }
int netSeatMine(int seat) { return N.me >= 0 && N.p[N.me].used && seat >= N.p[N.me].firstSeat && seat < N.p[N.me].firstSeat + N.p[N.me].nSeats; }
int netSeatOn(int seat, long tick) { for (int i = 0; i < NET_PEERS; i++) { const Peer *P = &N.p[i]; if (P->used && seat >= P->firstSeat && seat < P->firstSeat + P->nSeats) return !(P->goneAt && tick >= P->goneAt); } return 0; }
int netWaitingOn(void) { return (int)N.waitSeat; }
int netLobby(int *have, int *want, int *seat) { if (N.host) { *have = seatsTaken(); *want = N.seatsWant; *seat = 0; } else { *have = N.lobbyHave; *want = N.lobbyWant ? N.lobbyWant : N.seatsWant; *seat = N.me >= 0 ? N.lobbySeat : -1; } return N.state == NET_GATHER; }
const char *netHostName(void) { return N.hostName; }
int netPort(void) { return N.port; }
/* this machine's own addresses, for the host's waiting screen: what to tell the others */
int netMyAddresses(char out[][24], int max) {
    int n = 0;
#ifndef _WIN32
    /* off Windows, from the network cards themselves: a Pi's own name is 127.0.1.1 to itself */
    { struct ifaddrs *list = NULL; if (getifaddrs(&list) == 0) { for (struct ifaddrs *a = list; a && n < max; a = a->ifa_next) { if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
              const unsigned char *b = (const unsigned char *)&((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr; if (b[0] == 127) continue; snprintf(out[n++], 24, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]); }
          freeifaddrs(list); } if (n) return n; }
#endif
    char nm[256];
    if (gethostname(nm, sizeof nm) == 0) { struct addrinfo hints, *res = NULL; memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(nm, NULL, &hints, &res) == 0) { for (struct addrinfo *a = res; a && n < max; a = a->ai_next) { const unsigned char *b = (const unsigned char *)&((struct sockaddr_in *)a->ai_addr)->sin_addr.s_addr; if (b[0] == 127) continue; snprintf(out[n++], 24, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]); } freeaddrinfo(res); } }
    return n; }
/* "host", "host:7777", "host:7777:3" (how many pads the session has), or "join:address", "join:address:7777". 1 if the socket's open */
int netOpen(const char *spec, const char *game, int nLocal, const char *word, char *why, int cap) {
    memset(&N, 0, sizeof N); N.sock = -1; N.me = -1; N.state = NET_OFF; N.hashEvery = 20; N.timeoutS = 10.0; N.joinS = 20.0; N.loadS = 120.0; N.waitSeat = -1;
    char s[160]; snprintf(s, sizeof s, "%s", spec ? spec : ""); char *part[4] = {s, NULL, NULL, NULL}; int np = 1; for (char *c = s; *c && np < 4; c++) if (*c == ':') { *c = 0; part[np++] = c + 1; }
    const int hosting = !strcmp(part[0], "host"), joining = !strcmp(part[0], "join");
    if (!hosting && !joining) { snprintf(why, (size_t)cap, "REVA_NET is host[:port[:pads]] or join:address[:port], and this says %s", spec ? spec : "nothing"); return 0; }
    N.port = 7777; N.seatsWant = 2;
    if (hosting) { if (part[1] && atoi(part[1]) > 0) N.port = atoi(part[1]); if (part[2] && atoi(part[2]) > 0) N.seatsWant = atoi(part[2]); }
    else { if (!part[1] || !part[1][0]) { snprintf(why, (size_t)cap, "join needs an address: join:192.168.1.20"); return 0; } snprintf(N.hostName, sizeof N.hostName, "%s", part[1]); if (part[2] && atoi(part[2]) > 0) N.port = atoi(part[2]); }
    if (N.port < 1 || N.port > 65535) { snprintf(why, (size_t)cap, "a port is 1 to 65535"); return 0; }
    if (nLocal < 1) nLocal = 1;
    if (nLocal > NET_SEATS) nLocal = NET_SEATS;
    if (N.seatsWant > NET_SEATS) N.seatsWant = NET_SEATS;
    if (N.seatsWant < nLocal + (hosting ? 1 : 0)) N.seatsWant = nLocal + 1 > NET_SEATS ? NET_SEATS : nLocal + 1;
    N.myLocal = nLocal; snprintf(N.game, sizeof N.game, "%s", game ? game : ""); N.word = netFnv(word ? word : "");
    { const char *e; N.dice = 0x9E3779B97F4A7C15ull ^ (unsigned long long)time(NULL) ^ ((unsigned long long)(size_t)&N << 20) ^ (unsigned long long)(netMs() * 1000.0);
      if ((e = getenv("REVA_NETSEED")) && e[0]) N.dice = 0x9E3779B97F4A7C15ull * (unsigned long long)(atol(e) + 1);
      if ((e = getenv("REVA_NETLAG")) && e[0]) N.lag = atof(e);
      if ((e = getenv("REVA_NETJITTER")) && e[0]) N.jitter = atof(e);
      if ((e = getenv("REVA_NETLOSS")) && e[0]) N.loss = atoi(e);
      if ((e = getenv("REVA_NETDUP")) && e[0]) N.dup = atoi(e);
      if ((e = getenv("REVA_NETTIMEOUT")) && atof(e) > 0.0) N.timeoutS = atof(e);
      if ((e = getenv("REVA_NETJOINWAIT")) && atof(e) > 0.0) N.joinS = atof(e);
      if ((e = getenv("REVA_NETLOADWAIT")) && atof(e) > 0.0) N.loadS = atof(e);
      if ((e = getenv("REVA_NETNOBYE")) && e[0] == '1') N.nobye = 1;
      if ((e = getenv("REVA_NETBREAK")) && atoi(e) > 0) N.breakAt = atoi(e);
      if (N.loss < 0) N.loss = 0;
      if (N.loss > 95) N.loss = 95;
      if (N.lag < 0.0) N.lag = 0.0;
      if (N.jitter < 0.0) N.jitter = 0.0; }
#ifdef _WIN32
    { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) { snprintf(why, (size_t)cap, "this machine has no network to open"); return 0; } }
#endif
    long sk = (long)socket(AF_INET, SOCK_DGRAM, 0); if (sk < 0) { snprintf(why, (size_t)cap, "can't make a socket"); return 0; }
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons((unsigned short)(hosting ? N.port : 0));
    if (bind((int)sk, (struct sockaddr *)&a, sizeof a)) { CLOSESOCK((int)sk); if (hosting) snprintf(why, (size_t)cap, "port %d is in use on this machine", N.port); else snprintf(why, (size_t)cap, "can't open a port to join from"); return 0; }
#ifdef _WIN32
    { u_long on = 1; ioctlsocket((SOCKET)sk, FIONBIO, &on); }
    { DWORD off = 0, ret = 0; WSAIoctl((SOCKET)sk, _WSAIOW(IOC_VENDOR, 12), &off, sizeof off, NULL, 0, &ret, NULL, NULL); }   /* SIO_UDP_CONNRESET off: Windows otherwise fails the next read when a packet was sent to a port nobody's on yet */
#else
    fcntl((int)sk, F_SETFL, fcntl((int)sk, F_GETFL, 0) | O_NONBLOCK);
#endif
    N.sock = sk; N.host = hosting; const double now = netMs(); N.openAt = N.lastPump = now;
    if (hosting) { N.me = 0; N.session = (netDice() << 16) ^ netDice() ^ 1u; Peer *P = &N.p[0]; P->used = 1; P->firstSeat = 0; P->nSeats = nLocal; P->rtt = 0.0;
        netSay("hosting %s on port %d for %d pads, %d of them here", N.game, N.port, N.seatsWant, nLocal); }
    else { struct addrinfo hints, *res = NULL; memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM; char ps[16]; snprintf(ps, sizeof ps, "%d", N.port);
        if (getaddrinfo(N.hostName, ps, &hints, &res) != 0 || !res) { CLOSESOCK((int)sk); N.sock = -1; snprintf(why, (size_t)cap, "there's no machine called %s", N.hostName); return 0; }
        memcpy(&N.hostAddr, res->ai_addr, sizeof N.hostAddr); freeaddrinfo(res);
        Peer *P = &N.p[0]; P->used = 1; P->addr = N.hostAddr; P->nonce = (netDice() << 16) ^ netDice() ^ 1u; P->heard = now; P->rtt = -1.0;
        char at[32]; netSay("joining %s at %s with %d pad%s", N.game, addrText(&N.hostAddr, at, sizeof at), nLocal, nLocal > 1 ? "s" : ""); }
    N.state = NET_GATHER; return 1; }

/* everything that's arrived, and everything that's due. Call it often: nothing here waits */
void netPump(void) {
    if (N.sock < 0 || N.state == NET_OFF) return;
    const double now = netMs();
    if (now - N.lastPump > 400.0) { const double gap = now - N.lastPump; for (int i = 0; i < NET_PEERS; i++) N.p[i].heard += gap; }   /* this machine wasn't listening (it was loading): nobody's late for that */
    N.lastPump = now;
    for (int k = 0; k < 512; k++) { unsigned char d[NET_MTU + 64]; struct sockaddr_in src; socklen_t sl = sizeof src; memset(&src, 0, sizeof src);
        const int n = (int)recvfrom((int)N.sock, (char *)d, (int)sizeof d, 0, (struct sockaddr *)&src, &sl); if (n <= 0) break;
        N.bytesIn += (double)n;
        if (N.state != NET_OVER) gotPacket(&src, d, n, now); }
    netFlushSlow(now);
    for (int i = 0; i < NET_PEERS; i++) { Peer *P = &N.p[i]; if (i != N.me && P->used && !P->goneAt && P->leaving && P->have >= P->leaving) peerGone(i, P->leaveWhy); }
    if (N.state == NET_OVER || N.over) { for (int i = 0; i < NET_PEERS; i++) { Peer *P = &N.p[i]; if (i != N.me && P->used && P->relHead && now - P->relSentAt > 80.0) relSendHead(P, now); } return; }   /* a goodbye still gets through */
    if (N.state == NET_GATHER) {
        if (!N.host && N.me < 0) { if (now - N.helloAt > 250.0) { N.helloAt = now; unsigned char b[NET_MTU]; Wr w; head(&w, b, T_HELLO); w16(&w, NET_PROTO); w8(&w, (unsigned)N.myLocal); w8(&w, 0); w32(&w, N.word); w32(&w, N.p[0].nonce); char g[24]; memset(g, 0, sizeof g); snprintf(g, sizeof g, "%s", N.game); wbytes(&w, g, 24); post(&w, &N.hostAddr); }
            if (now - N.openAt > N.joinS * 1000.0) { char m[192]; snprintf(m, sizeof m, "nobody answered at %s, port %d", N.hostName, N.port); netOver(m); return; } }
        if (N.host && now - N.lobbyAt > 250.0) { N.lobbyAt = now; for (int i = 1; i < NET_PEERS; i++) if (N.p[i].used) { unsigned char b[NET_MTU]; Wr w; head(&w, b, T_LOBBY); w8(&w, (unsigned)seatsTaken()); w8(&w, (unsigned)N.seatsWant); post(&w, &N.p[i].addr); } } }
    for (int i = 0; i < NET_PEERS; i++) { Peer *P = &N.p[i]; if (i == N.me || !P->used || P->goneAt || (!N.host && i != 0)) continue;   /* a player talks to the host, and to nobody else: the others' pads come through it */
        if (P->relHead && now - P->relSentAt > (P->rtt > 30.0 ? P->rtt * 2.0 : 60.0)) { if (P->relSentAt > 0.0) N.resent++; relSendHead(P, now); }
        if (N.state < NET_PLAY && (N.me >= 0) && now - P->pingAt > (N.state == NET_READY ? 50.0 : 200.0)) { P->pingAt = now; unsigned char b[NET_MTU]; Wr w; head(&w, b, T_PING); w32(&w, (unsigned)(long long)now); post(&w, &P->addr); }
        if (N.state == NET_PLAY && now - P->sentAt > 33.0) sendPads(i, now);
        const double quiet = (P->leaving ? 1.5 : N.state == NET_PLAY ? N.timeoutS : (N.state == NET_GATHER ? 8.0 : 180.0)) * 1000.0;   /* a machine that's said goodbye has no reason to be slow about it */
        /* ⛔ A PLAYER IS THERE WHILE ITS PADS KEEP COMING, NOT WHILE IT KEEPS TALKING. A player that joins and then sends anything but pads would otherwise hold every machine
         * at one tick for as long as it liked. So the host counts how long it's been the one being waited for, and that's what runs out. One that's ahead of the host is never
         * blamed for a wait that's somebody else's */
        if (N.host && N.state == NET_PLAY) { if (P->have >= N.myTick + 1) P->waitSince = now; else if (now - P->waitSince > quiet) { hostDrop(i, P->leaving ? P->leaveWhy : "its pads stopped coming"); continue; } }
        if (N.me >= 0 && now - P->heard > quiet) { if (N.host) { if (N.state == NET_GATHER) { netSay("player %d stopped answering, and its place is free again", P->firstSeat + 1); relFree(P); P->used = 0; int fs = N.p[0].nSeats; for (int j = 1; j < NET_PEERS; j++) if (N.p[j].used) { N.p[j].firstSeat = fs; fs += N.p[j].nSeats; } }
                                                              else hostDrop(i, P->leaving ? P->leaveWhy : "nothing heard from it"); }
                                                 else { netOver(P->leaving ? "the host left" : "nothing heard from the host"); return; } } }
    /* a player that joined and then never took the start: eight seconds, and the game goes ahead without it */
    if (N.host && N.state == NET_LOAD && N.begun && now - N.beganAt > 8000.0) for (int i = 1; i < NET_PEERS; i++) if (N.p[i].used && !N.p[i].goneAt && N.p[i].relHead) { relFree(&N.p[i]); hostDrop(i, "it never took the start"); }
    /* the host, with everyone ready: are they all playing the same game, and how long is the wire */
    if (N.host && N.state == NET_READY && !N.go) { int all = 1;
        /* a player that joined and never finishes loading: the host waits two minutes past its own (REVA_NETLOADWAIT), and then the game goes ahead without it */
        if (now - N.readyAt > N.loadS * 1000.0) for (int i = 1; i < NET_PEERS; i++) if (N.p[i].used && !N.p[i].goneAt && !N.p[i].ready) { sendBye(&N.p[i], "it never finished loading"); hostDrop(i, "it never finished loading"); }
        /* everyone's ready, and the wire to each has been measured a few times (or two seconds have gone by): the delay is chosen once, so it's chosen on something */
        for (int i = 1; i < NET_PEERS; i++) if (N.p[i].used && !N.p[i].goneAt && (!N.p[i].ready || (N.p[i].rttN < 4 && now - N.readyAt < 2000.0))) all = 0;
        if (all && N.p[0].ready) { double worst = 0.0, second = 0.0;
            for (int i = 1; i < NET_PEERS; i++) { Peer *P = &N.p[i]; if (!P->used || P->goneAt) continue;
                if (P->sig != N.p[0].sig || P->nFiles != N.p[0].nFiles) { netSay("player %d's machine has loaded different files (%u of them, against %u here)", P->firstSeat + 1, P->nFiles, N.p[0].nFiles); sendBye(P, "its files aren't the host's: the two have different versions of the game"); P->goneAt = 1; continue; }
                const double h = (P->rtt > 0.0 ? P->rtt : 0.0) * 0.5; if (h > worst) { second = worst; worst = h; } else if (h > second) second = h; }
            const double path = worst + second, tick = 1000.0 / 60.0; int d = (int)((path + 4.0 + path * 0.25) / tick) + 2; const char *e = getenv("REVA_NETDELAY"); if (e && atoi(e) > 0) d = atoi(e);
            if (d < 1) d = 1;
            if (d > 15) d = 15;   /* a quarter of a second: past that it isn't a game, and no player's word about the wire can ask for more */
            N.delay = d;
            for (int a = 0; a < NET_ACTS; a++) for (int s = 0; s < NET_SEATS; s++) { N.binds[a][s] = N.p[0].binds[a][s]; for (int i = 1; i < NET_PEERS; i++) if (N.p[i].used && !N.p[i].goneAt && N.p[i].bindSet[a][s]) N.binds[a][s] = N.p[i].binds[a][s]; }
            /* every machine's clock starts at one moment, far enough off for the word to reach the furthest: they then tick side by side, and nobody spends the game waiting on a machine that began a little late */
            double lead = worst * 6.0 + 150.0; if (lead > 1500.0) lead = 1500.0; N.startLocal = now + lead;
            unsigned char dd[NET_REL_MAX]; Wr w = {dd, 0, (int)sizeof dd, 0}; w8(&w, (unsigned)N.delay); w8(&w, (unsigned)N.hashEvery); w8(&w, (unsigned)N.nAct); w32(&w, (unsigned)(long long)N.startLocal); for (int a = 0; a < N.nAct; a++) for (int s = 0; s < NET_SEATS; s++) w32(&w, N.binds[a][s]);
            for (int i = 1; i < NET_PEERS; i++) if (N.p[i].used && !N.p[i].goneAt) relPush(&N.p[i], K_GO, dd, w.n);
            N.go = 1; netSay("everyone's loaded: the wire is %.0f ms there and back at its longest, so a button is played %d ticks after it's pressed", path * 2.0, N.delay);
            if (!othersLeft()) netOver("nobody else had the same game"); } } }

/* the host: everyone's here (or the host won't wait for more). The dice, who sits where, and the host's saves go to everyone */
void netHostBegin(unsigned seed) {
    if (!N.host || N.begun) return;
    N.seed = seed; N.begun = 1; N.beganAt = netMs();
    int fs = 0, np = 0; for (int i = 0; i < NET_PEERS; i++) if (N.p[i].used) { N.p[i].firstSeat = fs; fs += N.p[i].nSeats; np++; }
    int chunks = 0; for (int i = 0; i < N.nSave; i++) chunks += N.save[i].n ? (N.save[i].n + 899) / 900 : 1;
    unsigned char d[NET_REL_MAX]; Wr w = {d, 0, (int)sizeof d, 0}; w32(&w, seed); w8(&w, (unsigned)fs); w8(&w, (unsigned)np); w8(&w, (unsigned)N.nSave); w16(&w, (unsigned)chunks);
    for (int i = 0; i < NET_PEERS; i++) if (N.p[i].used) { w8(&w, (unsigned)i); w8(&w, (unsigned)N.p[i].firstSeat); w8(&w, (unsigned)N.p[i].nSeats); }
    for (int c = 1; c < NET_PEERS; c++) { if (!N.p[c].used) continue; relPush(&N.p[c], K_BEGIN, d, w.n);
        for (int i = 0; i < N.nSave; i++) { NetSave *S = &N.save[i]; int off = 0; do { const int n = S->n - off > 900 ? 900 : S->n - off; unsigned char e[NET_REL_MAX]; Wr x = {e, 0, (int)sizeof e, 0};
                w8(&x, (unsigned)i); w32(&x, (unsigned)S->n); w32(&x, (unsigned)off); w16(&x, (unsigned)n); char nm[48]; memset(nm, 0, sizeof nm); snprintf(nm, sizeof nm, "%s", S->name); wbytes(&x, nm, 48); if (n) wbytes(&x, S->d + off, n);
                relPush(&N.p[c], K_SAVE, e, x.n); off += n; } while (off < S->n); } }
    N.seatsWant = fs; N.state = NET_LOAD; netSay("starting with %d pads on %d machines, seed %u, %d save%s handed over", fs, np, seed, N.nSave, N.nSave == 1 ? "" : "s"); }
int netBegun(unsigned *seed) { if (N.state < NET_LOAD || N.state == NET_OVER || !N.begun) return 0; if (seed) *seed = N.seed; return 1; }
/* the host's saves: offered before the start, and read by name after it on every machine */
void netSaveOffer(const char *name, const void *bytes, int n) { if (!N.host || N.begun || N.nSave >= NET_SAVES || n < 0 || n > NET_SAVE_MAX) return; NetSave *S = &N.save[N.nSave++]; snprintf(S->name, sizeof S->name, "%s", name); S->n = S->got = n; S->d = (unsigned char *)calloc(1, (size_t)n + 1); if (S->d && n) memcpy(S->d, bytes, (size_t)n); }
int netSaveCount(void) { return N.nSave; }
const char *netSaveName(int i) { return (i >= 0 && i < N.nSave) ? N.save[i].name : ""; }
const char *netSaveBytes(int i, int *n) { if (i < 0 || i >= N.nSave || !N.save[i].d) { if (n) *n = 0; return ""; } if (n) *n = N.save[i].n; return (const char *)N.save[i].d; }

/* loaded: what this machine read (a hash of its files, and how many), how many actions the game has, and where this machine's own players have their buttons */
void netReady(unsigned long long sig, unsigned nFiles, int nAct, const unsigned *masks /* [nAct][my local pads] */) {
    if (N.state != NET_LOAD) return;
    if (nAct > NET_ACTS) nAct = NET_ACTS;
    if (nAct < 0) nAct = 0;
    Peer *M = &N.p[N.me]; N.nAct = nAct; M->sig = sig; M->nFiles = nFiles; M->ready = 1;
    for (int a = 0; a < nAct; a++) for (int j = 0; j < N.myLocal; j++) { const int seat = M->firstSeat + j; if (seat < NET_SEATS) { M->binds[a][seat] = masks[a * NET_SEATS + j] & 0x7fu; M->bindSet[a][seat] = 1; } }
    if (!N.host) { unsigned char d[NET_REL_MAX]; Wr w = {d, 0, (int)sizeof d, 0}; w64(&w, sig); w32(&w, nFiles); w16(&w, (unsigned)(nAct * N.myLocal));
        for (int a = 0; a < nAct; a++) for (int j = 0; j < N.myLocal; j++) { w8(&w, (unsigned)(M->firstSeat + j)); w8(&w, (unsigned)a); w32(&w, M->binds[a][M->firstSeat + j < NET_SEATS ? M->firstSeat + j : 0]); }
        relPush(&N.p[0], K_READY, d, w.n); }
    else for (int a = 0; a < nAct; a++) for (int s = 0; s < NET_SEATS; s++) if (!M->bindSet[a][s]) M->binds[a][s] = masks[a * NET_SEATS + s] & 0x7fu;   /* the host's own table is what a pad nobody's moved is on */
    N.readyAt = netMs(); N.state = NET_READY; }
/* nothing that has to arrive is still on its way: the host waits for this after the start, before it goes off to load */
/* how many ms until every machine starts, once the host's said go: under 0 before that */
double netGoIn(void) { return (N.state == NET_READY && N.go) ? N.startLocal - netMs() : -1.0; }
int netSettled(void) { for (int i = 0; i < NET_PEERS; i++) if (i != N.me && N.p[i].used && !N.p[i].goneAt && N.p[i].relHead) return 0; return 1; }
/* 1 once the host says go: the delay, and every pad's buttons. The session's playing from here */
int netGo(int *delay, unsigned *masks /* [NET_ACTS][NET_SEATS] */) {
    if (N.state != NET_READY || !N.go || netMs() < N.startLocal) return 0;
    if (delay) *delay = N.delay;
    if (masks) memcpy(masks, N.binds, sizeof N.binds);
    padsStart(); const double now = netMs(); for (int i = 0; i < NET_PEERS; i++) N.p[i].heard = N.p[i].waitSince = now;
    N.myTick = 0; N.lastLocal = N.delay; N.state = NET_PLAY; return 1; }

/* this tick's pads from this machine, to be played at tick + delay */
void netLocal(long tick, const NetSeat *seats) {
    for (int j = 0; j < N.myLocal && j < NET_SEATS; j++) { N.direct[j] = seats[j]; N.direct[j].flags |= NETF_ON; }
    if (N.state != NET_PLAY) return;
    const long t = tick + N.delay; Peer *M = &N.p[N.me]; if (t != N.lastLocal + 1) return;   /* once a tick, in order */
    NetSeat s[NET_SEATS]; memset(s, 0, sizeof s); for (int j = 0; j < M->nSeats && j < NET_SEATS; j++) s[j] = N.direct[j];
    padsKeep(M, t, s); N.lastLocal = t;
    if (N.over) return;
    const double now = netMs(); for (int i = 0; i < NET_PEERS; i++) if (i != N.me && N.p[i].used && !N.p[i].goneAt && (N.host || i == 0)) sendPads(i, now); }
/* does this machine hold every pad for this tick */
int netHave(long tick) {
    if (N.state != NET_PLAY) return 1;
    for (int i = 0; i < NET_PEERS; i++) { const Peer *P = &N.p[i]; if (!P->used) continue; if (P->goneAt && tick >= P->goneAt) continue; if (P->have < tick) { N.waitSeat = P->firstSeat; return 0; } }
    N.waitSeat = -1; return 1; }
/* every pad for this tick. A pad nobody holds, or whose machine has gone, is unplugged */
void netTake(long tick, NetSeat out[NET_SEATS]) {
    memset(out, 0, sizeof(NetSeat) * NET_SEATS);
    if (N.state != NET_PLAY) { if (N.me >= 0 && N.p[N.me].used) for (int j = 0; j < N.myLocal; j++) { const int s = N.p[N.me].firstSeat + j; if (s < NET_SEATS) out[s] = N.direct[j]; } return; }   /* it's over: this machine's own pads, as they are */
    for (int i = 0; i < NET_PEERS; i++) { const Peer *P = &N.p[i]; if (!P->used || (P->goneAt && tick >= P->goneAt)) continue; const int k = (int)(tick % NET_RING);
        if (P->inTick[k] != tick) continue;
        for (int j = 0; j < P->nSeats; j++) if (P->firstSeat + j < NET_SEATS) out[P->firstSeat + j] = P->in[k][j]; }
    /* REVA_NETBREAK=<tick>, for the check that has to be able to fail: this machine alone plays that tick with pad 1 pushed and pressed, so its game parts from the others' */
    if (N.breakAt && tick == N.breakAt) { out[0].lx = 32767; out[0].ly = 32767; out[0].held ^= 1; netSay("REVA_NETBREAK: this machine alone plays tick %ld with a pad nobody was sent", tick); }
    N.myTick = tick; }
/* this tick's hash of the rules, held against everyone else's as theirs come in */
void netHash(long tick, unsigned long long h) {
    if (N.state != NET_PLAY) return;
    N.hash[tick & 255].tick = tick; N.hash[tick & 255].h = h;
    if (N.over) return;
    for (int i = 0; i < NET_PEERS; i++) { Peer *P = &N.p[i]; if (i == N.me || !P->used || P->goneAt || P->hashTick <= P->hashDone || P->hashTick > tick) continue;
        const long t = P->hashTick; P->hashDone = t;   /* held against ours once */
        if (N.hash[t & 255].tick != t || N.hash[t & 255].h == P->hash) continue;
        N.parted = t;
        if (N.host) netSay("\xe2\x9b\x94 player %d's game parted from this one by tick %ld: the same pads gave two different games (%016llx here, %016llx there). That machine is out of the session", P->firstSeat + 1, t, N.hash[t & 255].h, P->hash);
        else netSay("\xe2\x9b\x94 this machine's game parted from the host's by tick %ld: the same pads gave two different games (%016llx here, %016llx there). It can't go on in the session", t, N.hash[t & 255].h, P->hash);
        char w[64]; snprintf(w, sizeof w, "its game parted from the host's by tick %ld", t);   /* the other end is told the tick too */
        if (N.host) { sendBye(P, w); hostDrop(i, "its game parted from this one"); } else { sendBye(P, w); netOver("this machine's game parted from the host's"); return; } } }
long netParted(void) { return N.parted; }
/* a tick was held for the wire, this long past when it was due */
void netWaited(double ms) { N.held++; N.waited += ms; if (ms > N.worstWait) N.worstWait = ms; }
void netStats(char *out, int cap) { snprintf(out, (size_t)cap, "tick %ld, played %d ticks behind the pad, %lu ticks held for the wire (%.0f ms in all, %.0f at the longest), %lu packets out (%.0f KB) and %lu in (%.0f KB), %lu refused unread, %lu sent again", N.myTick, N.delay, N.held, N.waited, N.worstWait, N.sent, N.bytesOut / 1024.0, N.got, N.bytesIn / 1024.0, N.refusedPk, N.resent); }
/* leaving: the others are told, and what they haven't yet said they have is given a moment to arrive */
void netClose(void) {
    if (N.sock < 0) return;
    if (N.state != NET_OFF && !N.nobye) { const double t0 = netMs();
        if (N.state != NET_OVER && !N.over) for (int i = 0; i < NET_PEERS; i++) if (i != N.me && N.p[i].used && !N.p[i].goneAt && (N.host || i == 0)) sendBye(&N.p[i], "it left");
        for (;;) { netPump(); int owed = 0; const double now = netMs(); for (int i = 0; i < NET_PEERS; i++) { Peer *P = &N.p[i]; if (i == N.me || !P->used || (!N.host && i != 0)) continue; if (P->relHead) owed = 1;
                if (N.state != NET_PLAY || N.over || P->goneAt) continue;
                int behind = 0;   /* the pads this end owes it: its own, and the host owes it everyone else's */
                for (int q = 0; q < NET_PEERS; q++) { const Peer *Q = &N.p[q]; if (q == i || !Q->used || (!N.host && q != N.me)) continue; long top = Q->have; if (Q->goneAt && top > Q->goneAt - 1) top = Q->goneAt - 1; if (P->ackOf[q] < top) behind = 1; }
                if (behind) { owed = 1; if (now - P->sentAt > 15.0) sendPads(i, now); } }
            if (!owed || now - t0 > 1500.0) break;
#ifdef _WIN32
            Sleep(2);
#else
            usleep(2000);
#endif
        } netFlushSlow(netMs() + 1e9); }
    for (int i = 0; i < NET_PEERS; i++) relFree(&N.p[i]);
    for (int i = 0; i < NET_SAVES; i++) { free(N.save[i].d); N.save[i].d = NULL; }
    CLOSESOCK((int)N.sock); N.sock = -1; N.state = NET_OFF; }
