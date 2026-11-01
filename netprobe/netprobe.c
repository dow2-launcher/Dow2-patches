// NetProbe.dll - EXPERIMENT (01.11.2026): what passes from a player's computer to our game servers. No hooks, nothing in the
// game is touched: once per game start, 15 s after it, a background thread talks to the probe service on each of our servers
// (gameserver/probe_server.py), writes NetProbe.log and sends that log to us (the same place "Send logs" of the launcher
// sends to), so it works in vanilla DoW2 too: there it comes in the stand-alone package (winmm.dll + the dow2-patches folder).
//
// Why: players in Russia and Ukraine lose the TCP game connection to our servers after ~16-30 KB (DPI on the way), while it
// works through a VPN. This shows per server and port whether a TCP download gets through or stalls (and where), whether UDP
// passes both ways (echo and a burst from the server) and whether a TCP request split in two (the simplest anti-DPI trick)
// changes anything. ~1-2 minutes, ~5 MB, once per game start.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static const struct { const char *name, *ip; } SERVERS[] = {
    {"Poland", "15.220.68.107"}, {"Germany", "116.202.106.150"}, {"Moscow", "83.217.192.34"},
};
static const int PORTS[] = {47110, 3478, 27015};
#define TCP_SIZE (256 * 1024)

static wchar_t g_log[MAX_PATH];
static int g_vanilla, g_package;          // vanilla game (no Elite in exe/command line); loaded from the dow2-patches folder
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

static void put32(unsigned char *p, unsigned v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

// TCP: ask for 256 KB, count what comes; a stall = no byte for 6 s. split: the 8-byte request in two parts 300 ms apart
static void tcp_test(const char *name, const char *ip, int port, int split) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = inet_addr(ip);
    DWORD t0 = GetTickCount(), to = 6000; int got = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);                                   // connect with a 5 s limit
    connect(s, (struct sockaddr *)&a, sizeof a);
    fd_set w; FD_ZERO(&w); FD_SET(s, &w); struct timeval tv = {5, 0};
    int ok = select(0, NULL, &w, NULL, &tv) == 1;
    nb = 0; ioctlsocket(s, FIONBIO, &nb);
    if (!ok) { logmsg("%-8s tcp/%-5d %s: no connection in 5 s", name, port, split ? "split" : "plain"); closesocket(s); return; }
    DWORD tc = GetTickCount() - t0;
    unsigned char req[8] = {'P', 'R', 'B', '1'}; put32(req + 4, TCP_SIZE);
    if (split) { send(s, (const char *)req, 1, 0); Sleep(300); send(s, (const char *)req + 1, 7, 0); }
    else send(s, (const char *)req, 8, 0);
    static char buf[65536];
    for (;;) { int n = recv(s, buf, sizeof buf, 0); if (n <= 0) break; got += n; if (got >= TCP_SIZE) break; }
    DWORD tt = GetTickCount() - t0;
    if (got >= TCP_SIZE) logmsg("%-8s tcp/%-5d %s: OK %d KB in %.1f s (connect %lu ms)", name, port, split ? "split" : "plain", got / 1024, tt / 1000.0, tc);
    else logmsg("%-8s tcp/%-5d %s: STALL at %d KB of %d (connect %lu ms, %.1f s)", name, port, split ? "split" : "plain", got / 1024, TCP_SIZE / 1024, tc, tt / 1000.0);
    closesocket(s);
}

// UDP: a cookie, 40 echoes of 1000 bytes at 25/s (loss and rtt), then a burst of 200 x 1200 bytes from the server
static void udp_test(const char *name, const char *ip, int port) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = inet_addr(ip);
    DWORD to = 1500; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
    static char buf[2048]; unsigned char cookie[8]; int have = 0;
    for (int i = 0; i < 3 && !have; i++) {
        sendto(s, "PU0", 3, 0, (struct sockaddr *)&a, sizeof a);
        int n = recv(s, buf, sizeof buf, 0);
        if (n >= 11 && !memcmp(buf, "PC0", 3)) { memcpy(cookie, buf + 3, 8); have = 1; }
    }
    if (!have) { logmsg("%-8s udp/%-5d: no answer at all", name, port); closesocket(s); return; }
    int echoed = 0; DWORD rtt[40]; int nr = 0;
    to = 300; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
    for (int i = 0; i < 40; i++) {
        char p[1003] = {'P', 'U', '1'}; *(int *)(p + 3) = i; DWORD t = GetTickCount();
        sendto(s, p, sizeof p, 0, (struct sockaddr *)&a, sizeof a);
        int n = recv(s, buf, sizeof buf, 0);
        if (n == (int)sizeof p && *(int *)(buf + 3) == i) { echoed++; rtt[nr++] = GetTickCount() - t; }
        Sleep(40);
    }
    for (int i = 1; i < nr; i++) for (int j = i; j > 0 && rtt[j] < rtt[j - 1]; j--) { DWORD x = rtt[j]; rtt[j] = rtt[j - 1]; rtt[j - 1] = x; }
    unsigned char req[15] = {'P', 'U', '2'}; memcpy(req + 3, cookie, 8); req[11] = 0; req[12] = 200; req[13] = 1200 >> 8; req[14] = 1200 & 255;
    while (recv(s, buf, sizeof buf, 0) > 0) {}                                   // late echoes out of the way
    sendto(s, (const char *)req, sizeof req, 0, (struct sockaddr *)&a, sizeof a);
    to = 3000; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
    int burst = 0, last = -1;
    for (;;) { int n = recv(s, buf, sizeof buf, 0); if (n <= 0) break; if (n >= 5 && !memcmp(buf, "PD2", 3)) { burst++; last = ((unsigned char)buf[3] << 8) | (unsigned char)buf[4]; } }
    logmsg("%-8s udp/%-5d: echo %d/40 (rtt %lu ms), burst %d/200 (%d KB, last #%d)", name, port, echoed, nr ? rtt[nr / 2] : 0, burst, burst * 1200 / 1024, last);
    closesocket(s);
}

static unsigned long long my_steam(void) {   // Steam writes the active account here
    DWORD v = 0, sz = sizeof v; HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"ActiveUser", NULL, NULL, (BYTE *)&v, &sz); RegCloseKey(k); }
    return v ? 76561197960265728ULL + v : 0;
}
// the log of this run to /stats/api/logs (rts, then the mirrors) - like the launcher's "Send logs", one small text
static void upload(long from) {
    FILE *f = _wfopen(g_log, L"rb"); if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f) - from; if (n <= 0 || n > 65536) { fclose(f); return; }
    static char body[70000]; int L = snprintf(body, 2048, "NetProbe 2 · %s · %s · start, steam=%llu\n\n=== NetProbe.log (%ld bytes) ===\n",
                                                g_vanilla ? "vanilla" : "Elite", g_package ? "package" : "launcher", my_steam(), n);
    fseek(f, from, SEEK_SET); L += (int)fread(body + L, 1, n, f); fclose(f);
    const wchar_t *hosts[] = { L"rts.fintebtc.de", L"dow2.fintebtc.de", L"dow2ru.fintebtc.de" };
    const wchar_t *path = g_vanilla ? L"/stats/api/logs?who=netprobe-vanilla" : L"/stats/api/logs?who=netprobe-elite";
    for (int i = 0; i < 3; i++) {
        HINTERNET s = WinHttpOpen(L"NetProbe/2", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0); if (!s) continue;
        WinHttpSetTimeouts(s, 8000, 8000, 15000, 15000);
        HINTERNET c = WinHttpConnect(s, hosts[i], INTERNET_DEFAULT_HTTPS_PORT, 0);
        HINTERNET r = c ? WinHttpOpenRequest(c, L"POST", path, NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
        DWORD code = 0, sz = sizeof code;
        if (r && WinHttpSendRequest(r, L"Content-Type: text/plain; charset=utf-8\r\n", (DWORD)-1, body, L, L, 0) && WinHttpReceiveResponse(r, NULL))
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
        if (r) WinHttpCloseHandle(r); if (c) WinHttpCloseHandle(c); WinHttpCloseHandle(s);
        if (code == 200) { logmsg("sent to %ls", hosts[i]); return; }
    }
    logmsg("could not send the result (no server answered) - send NetProbe.log by hand");
}

static DWORD WINAPI worker(LPVOID _) {
    Sleep(15000);                                                                // the game starts first
    WSADATA wd; if (WSAStartup(MAKEWORD(2, 2), &wd)) { logmsg("no winsock"); return 0; }
    long from = 0; { FILE *f = _wfopen(g_log, L"rb"); if (f) { fseek(f, 0, SEEK_END); from = ftell(f); fclose(f); } }
    logmsg("NetProbe 2: what passes to our game servers (experiment, %s, %s)", g_vanilla ? "vanilla" : "Elite", g_package ? "package" : "launcher");
    for (int i = 0; i < (int)(sizeof SERVERS / sizeof *SERVERS); i++) {
        for (int p = 0; p < (int)(sizeof PORTS / sizeof *PORTS); p++) {
            tcp_test(SERVERS[i].name, SERVERS[i].ip, PORTS[p], 0);
            udp_test(SERVERS[i].name, SERVERS[i].ip, PORTS[p]);
        }
        tcp_test(SERVERS[i].name, SERVERS[i].ip, PORTS[0], 1);
    }
    logmsg("NetProbe done");
    WSACleanup();
    upload(from);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t low[2 * MAX_PATH + 64]; _snwprintf(low, sizeof low / sizeof *low - 1, L"%ls", g_log); low[sizeof low / sizeof *low - 1] = 0;
    _wcslwr(low); g_package = wcsstr(low, L"\\dow2-patches\\") != NULL;
    GetModuleFileNameW(NULL, low, MAX_PATH); wcsncat(low, L" ", 2); wcsncat(low, GetCommandLineW(), MAX_PATH); _wcslwr(low);
    g_vanilla = !wcsstr(low, L"elite");          // Elite 3.x runs Elite.exe, older ones DOW2.exe -modname Elite
    wcscat(g_log, L"NetProbe.log");
    char n[64]; wsprintfA(n, "Local\\dow2patch_NetProbe_%lu", GetCurrentProcessId());   // one copy per game
    HANDLE m = CreateMutexA(NULL, FALSE, n); if (m && GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
