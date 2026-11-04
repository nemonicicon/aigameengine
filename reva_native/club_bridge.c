/* club_bridge.c -- the bridge's wire: one client on a local TCP port, whole lines each way, and nothing here ever blocks a frame (engine plan, Phase 5). What the lines mean is engine/bridge.inc.
 * Its own file because winsock has to come before windows.h, and SDL brings that in everywhere else. It only ever listens on 127.0.0.1. */
#ifdef _WIN32
#include <winsock2.h>
typedef int socklen_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSESOCK close
#endif
#include <stdio.h>
#include <string.h>

static long g_ls = -1, g_cl = -1;
static char g_in[8192];
static int g_nIn = 0;

static void nonblock(long s) {
#ifdef _WIN32
    u_long on = 1; ioctlsocket((SOCKET)s, FIONBIO, &on);
#else
    fcntl((int)s, F_SETFL, fcntl((int)s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

int bridgeOpen(int port) {
#ifdef _WIN32
    WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) return 0;
#endif
    long s = (long)socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
    int yes = 1; setsockopt((int)s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind((int)s, (struct sockaddr *)&a, sizeof a) || listen((int)s, 1)) { CLOSESOCK((int)s); return 0; }
    nonblock(s); g_ls = s; return 1;
}

/* one whole line from the client, if there's one. 1 if it filled out */
int bridgeLine(char *out, int cap) {
    if (g_ls < 0) return 0;
    if (g_cl < 0) { long c = (long)accept((int)g_ls, NULL, NULL); if (c >= 0) { nonblock(c); g_cl = c; g_nIn = 0; } }
    if (g_cl >= 0) {
        int n = (int)recv((int)g_cl, g_in + g_nIn, (int)sizeof g_in - 1 - g_nIn, 0);
        if (n > 0) g_nIn += n;
        else if (n == 0) { CLOSESOCK((int)g_cl); g_cl = -1; g_nIn = 0; return 0; }   /* the client went away, and the next one can connect */
    }
    g_in[g_nIn] = 0;
    char *nl = strchr(g_in, '\n'); if (!nl) { if (g_nIn >= (int)sizeof g_in - 1) g_nIn = 0; return 0; }
    int len = (int)(nl - g_in); if (len >= cap) len = cap - 1;
    memcpy(out, g_in, (size_t)len); out[len] = 0; if (len > 0 && out[len - 1] == '\r') out[len - 1] = 0;
    int rest = g_nIn - (int)(nl + 1 - g_in); memmove(g_in, nl + 1, (size_t)rest); g_nIn = rest;
    return 1;
}

void bridgeSend(const char *s) {
    if (g_cl < 0) return;
    int n = (int)strlen(s);
    if (send((int)g_cl, s, n, 0) < 0 || send((int)g_cl, "\n", 1, 0) < 0) { CLOSESOCK((int)g_cl); g_cl = -1; g_nIn = 0; }
}
