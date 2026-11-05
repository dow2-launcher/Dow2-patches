// du_harness.c - the UDP tunnel of Relay.dll on Linux, for the network tests (relay/test/README.md).
// Built 32-bit (-m32: DWORD / long are 32 bits as on Windows) from the very same du_tunnel.h as Relay.dll 23, or with
// -DOLD22 from the delivery of Relay 22 (cut out of git by mk_old22.sh) to show the old behaviour.
// Usage: du_harness <local tcp port> <gateway ip> <gateway port> [syn version, default 2]
// It listens on 127.0.0.1:<port> for ONE connection of the "game" (the test's game model), opens DU to the gateway (SYN every
// 250 ms, 3 s), then carries the bytes both ways until either side ends; the Relay.log lines go to stderr.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

typedef int SOCKET;
typedef unsigned long DWORD;
typedef long LONG;
#define INVALID_SOCKET (-1)
#define closesocket close
#define DU_NFDS(a, b) ((a) > (b) ? (a) + 1 : (b) + 1)
#define g_recvOrig recv
#define g_sendOrig send

static double t0;
static void logmsg(const char *fmt, ...) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    fprintf(stderr, "%9.3f ", t.tv_sec + t.tv_nsec / 1e9 - t0);
    va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); fputc('\n', stderr); fflush(stderr);
}

#ifdef OLD22
static DWORD GetTickCount(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (DWORD)(t.tv_sec * 1000 + t.tv_nsec / 1000000); }
#include "old22_tunnel.h"
#else
#include "../du_tunnel.h"
#endif

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: du_harness <tcp port> <gateway ip> <gateway port> [version]\n"); return 2; }
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); t0 = t.tv_sec + t.tv_nsec / 1e9;
    int ver_want = argc > 4 ? atoi(argv[4]) : 2;
    int l = socket(AF_INET, SOCK_STREAM, 0), one = 1; setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = htons(atoi(argv[1]));
    if (bind(l, (struct sockaddr *)&a, sizeof a) || listen(l, 1)) { perror("bind"); return 1; }
    int g = accept(l, NULL, NULL); close(l);
    setsockopt(g, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    int u = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in s = {0}; s.sin_family = AF_INET; s.sin_addr.s_addr = inet_addr(argv[2]); s.sin_port = htons(atoi(argv[3]));
#ifndef OLD22
    int rb = 256 * 1024; setsockopt(u, SOL_SOCKET, SO_RCVBUF, &rb, sizeof rb);   // as relay.c udp_to (Relay 23)
#endif
    connect(u, (struct sockaddr *)&s, sizeof s);
    DWORD cid = 0; { FILE *r = fopen("/dev/urandom", "rb"); if (!r || fread(&cid, 4, 1, r) != 1) cid = getpid() * 2654435761u; if (r) fclose(r); }
    unsigned char pk[64]; int ver = 0;
    for (int i = 0; i < 12 && !ver; i++) {                       // the handshake as relay.c udp_open
        u_hdr(pk, U_SYN, cid); pk[7] = (unsigned char)ver_want; send(u, pk, 8, 0);
        fd_set r; FD_ZERO(&r); FD_SET(u, &r); struct timeval tv = {0, 250000};
        while (select(u + 1, &r, NULL, NULL, &tv) > 0) {
            int n = recv(u, pk, sizeof pk, 0);
            if (n >= 7 && pk[0] == 'D' && pk[2] == U_SYNACK && !memcmp(pk + 3, &cid, 4)) { ver = n >= 8 && pk[7] >= 2 ? 2 : 1; break; }
            FD_ZERO(&r); FD_SET(u, &r);
        }
    }
    if (!ver) { logmsg("udp: no SYNACK"); return 1; }
    logmsg("udp: connection %08lX (%s)", cid, ver >= 2 ? "with copies" : "old gateway, no copies");
    udp_carry(g, u, cid, ver, NULL, 0);
    return 0;
}
