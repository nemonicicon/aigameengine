/* club_net.h -- online play's wire, as the engine sees it (queue step 29). club_net.c is the wire and says how a session works; engine/net.inc is the engine's side. */
#ifndef CLUB_NET_H
#define CLUB_NET_H

#define NET_PEERS 4
#define NET_SEATS 4
#define NET_ACTS 32

/* one pad, one tick, as it's sent and as every machine plays it. Sticks are sixteen bits, which is all a pad gives, so nothing is lost on the way */
typedef struct { short lx, ly, rx, ry, rt, mdx, mdy; unsigned short held; unsigned char flags; } NetSeat;
enum { NETF_ON = 1, NETF_KB = 2, NETF_FPV = 4, NETF_BACK = 8 };
/* where a session is: gathering its players, loading, loaded and waiting for the word, playing, over */
enum { NET_OFF = 0, NET_GATHER = 1, NET_LOAD = 2, NET_READY = 3, NET_PLAY = 4, NET_OVER = 5 };

int netOpen(const char *spec, const char *game, int nLocal, const char *word, char *why, int cap);
void netPump(void);
void netClose(void);
int netState(void);
int netIsHost(void);
const char *netWhy(void);
int netDelay(void);
int netSeats(void);
int netFirstSeat(void);
int netLocalSeats(void);
int netSeatMine(int seat);
int netSeatOn(int seat, long tick);
int netWaitingOn(void);
int netLobby(int *have, int *want, int *seat);
const char *netHostName(void);
int netPort(void);
int netMyAddresses(char out[][24], int max);
void netHostBegin(unsigned seed);
int netBegun(unsigned *seed);
int netSettled(void);
void netSaveOffer(const char *name, const void *bytes, int n);
int netSaveCount(void);
const char *netSaveName(int i);
const char *netSaveBytes(int i, int *n);
void netReady(unsigned long long sig, unsigned nFiles, int nAct, const unsigned *masks);
int netGo(int *delay, unsigned *masks);
double netGoIn(void);
void netLocal(long tick, const NetSeat *seats);
int netHave(long tick);
void netTake(long tick, NetSeat out[NET_SEATS]);
void netHash(long tick, unsigned long long h);
long netParted(void);
void netWaited(double ms);
void netStats(char *out, int cap);

#endif
