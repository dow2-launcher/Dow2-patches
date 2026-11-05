// wine_smoke.c - the real Relay.dll under Wine: this program loads Relay.dll from its own directory (Relay.ini there:
// [Relay] Nodes=<the test gateway>), connects TCP to <gateway>:47100 as the game does for a native room of our lobby server
// (Relay.dll redirects it into its UDP tunnel), and plays a model player for <secs>: hello, a turn every 100 ms, reads the
// server's ticks. Prints one line: ticks seen, own turns seen, turn latency p50/p99/max. Used by wine_smoke.sh.
// i686-w64-mingw32-gcc -O2 -o wine_smoke.exe wine_smoke.c -lws2_32
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double now_s(void) { static LARGE_INTEGER f; LARGE_INTEGER c; if (!f.QuadPart) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c.QuadPart / f.QuadPart; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return x < y ? -1 : x > y; }
static int send_all(SOCKET s, const unsigned char *b, int n) { while (n > 0) { int w = send(s, (const char *)b, n, 0); if (w <= 0) return -1; b += w; n -= w; } return 0; }
static int frame(SOCKET s, const unsigned char *m, int n) { unsigned char h[2] = {(unsigned char)(n >> 8), (unsigned char)n}; return send_all(s, h, 2) || send_all(s, m, n); }

int main(int argc, char **argv) {
    if (argc < 5) { printf("usage: wine_smoke <gateway ip> <sid> <player no> <secs>\n"); return 2; }
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    if (!LoadLibraryA("Relay.dll")) { printf("no Relay.dll (%lu)\n", GetLastError()); return 1; }
    Sleep(500);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = inet_addr(argv[1]); a.sin_port = htons(47100);
    if (connect(s, (struct sockaddr *)&a, sizeof a)) { printf("connect failed %d\n", WSAGetLastError()); return 1; }
    unsigned long sid = strtoul(argv[2], NULL, 10); int me = atoi(argv[3]); double secs = atof(argv[4]);
    unsigned char m[64] = {0x15}; memcpy(m + 1, &sid, 4); m[9] = (unsigned char)(0x40 + me);   // hello: sid, 4 x 00, id (8), flag
    frame(s, m, 18);
    unsigned char t0[30] = {0x09, 0xfe, 24, 0}; frame(s, t0, 28);                                  // a first turn: joins now
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    static unsigned char in[1 << 16]; int have = 0, ticks = 0, idx = 0, nl = 0; static double lat[4096];
    double start = now_s(), next = start, end = start + secs; unsigned turn = 1;
    while (now_s() < end + 1.0) {
        double t = now_s();
        if (t >= next && t < end) {                                                             // a turn: 09 fe len(24) <idx? seq time>
            unsigned char tm[28] = {0x09, 0xfe, 24, 0}; memcpy(tm + 4, &me, 4); memcpy(tm + 8, &turn, 4); memcpy(tm + 12, &t, 8);
            frame(s, tm, 28); turn++; next += 0.1;
        }
        int n = recv(s, (char *)in + have, sizeof in - have, 0);
        if (n == 0) break;
        if (n > 0) have += n;
        int o = 0;
        while (have - o >= 2) {
            int L = in[o] << 8 | in[o + 1]; if (have - o < 2 + L) break;
            unsigned char *f = in + o + 2;
            if (f[0] == 0x15 && !idx) idx = -1;
            if (f[0] == 0x0a && L >= 3) {
                ticks++; int k = f[2], p = 3;
                for (int i = 0; i < k && p + 3 <= L; i++) {
                    int dl = f[p + 1] | f[p + 2] << 8; unsigned char *d = f + p + 3;
                    int who; memcpy(&who, d, 4);
                    if (dl >= 16 && who == me) { double ts; memcpy(&ts, d + 8, 8); if (ts > start + 3 && nl < 4096) lat[nl++] = now_s() - ts; }
                    p += 3 + dl;
                }
            }
            o += 2 + L;
        }
        if (o) { memmove(in, in + o, have - o); have -= o; }
        Sleep(1);
    }
    closesocket(s);
    qsort(lat, nl, sizeof lat[0], cmpd);
    printf("player %d: ticks %d, own turns seen %d of %u, latency ms p50 %.1f p99 %.1f max %.1f\n", me, ticks, nl, turn - 1,
           nl ? lat[nl / 2] * 1000 : -1, nl ? lat[nl * 99 / 100] * 1000 : -1, nl ? lat[nl - 1] * 1000 : -1);
    Sleep(3000);                                                                               // Relay.dll writes its "over" line
    return 0;
}
