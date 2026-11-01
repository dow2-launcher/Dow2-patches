// Relay.dll - DoW2 Retribution / Elite: online matches on OUR battle servers instead of Relic's (AWS Virginia).
// Our server (battle_relay.py) does what Relic's battleserverrelay does, so only players with this add-on can play together.
// Every online session opens a TCP connection to Relic's battleserverrelay (ports 27000-27099); we send those connects to
// our battle server (port 47100) with a preamble "DWR1"/"DWR2" + IPv4 + port (BE) in front of the game's own data.
// Servers (Relay 10, 30.10.2026): Warsaw (AWS 15.220.68.107), then Germany (Hetzner 116.202.106.150).
// Elite_DLLs\Relay.ini [Relay] (optional): Hosts=ip;ip (the launcher's Server pick; set = always this one, even over the
// matchmaker's choice - our servers pass a guest on to the one that has the match), Port=47100
//
// Switches (the launcher writes them into Relay.ini):
//   Backup=1   - after Warsaw try Germany, for players whose network doesn't reach Warsaw.
//   Fallback=1 - compatible (default): our server only for an automatch where everyone has the add-on (MMFind sets
//                DOW2_RELAY_USE=1 from the matchmaker), every other game on Relic's servers as without the add-on, so
//                players without it can join our lobbies; none of ours answers -> Relic too.
//   Fallback=0 - compatibility off: every online game on our servers; the matchmaker pairs such a player only with players
//                who have our servers on (since 30.10.2026), a custom lobby with Relic players can't play (error, game closes).
//
// Relay 15 (01.11.2026, owner: «несколько новых галочек на все случаи»):
//   Mode=compat|strict|bridge (the launcher's list; without it Fallback=1/0 means compat/strict as before)
//     bridge - "every game through our server, with anybody": an automatch where everyone has the add-on is hosted on our
//              server as in compat; every other online game still plays on Relic, but the connection goes through our
//              server nearest to the player (lowest delay of Poland, Germany, Moscow; or the server picked by hand), which
//              passes it on to the Relic relay the game wanted ("DWR3"). For players whose own way to Relic is cut or slow.
//              None of ours answers -> straight to Relic.
//   UDP=1 - the connection to our server goes in UDP packets with our own delivery (udp_gateway.py on the servers) instead
//           of TCP, which DPI cuts after ~16-30 KB on the way to foreign hosting. Every connection that goes to our server
//           (any mode): the game connects to a local port of ours, a thread carries the bytes; no UDP answer in 3 s ->
//           the same thread carries them over plain TCP, the game notices nothing.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

typedef int (WSAAPI *ConnectFn)(SOCKET, const struct sockaddr *, int);
static ConnectFn g_connectOrig; static struct in_addr g_relayIp; static volatile int g_relayOn;
static int g_port = 47100;                                       // our battle server's port
static int g_backup = 1, g_fallback = 1, g_handHosts, g_bridge, g_udp;   // g_handHosts: the player picked the server (launcher: Server)                         // Relay.ini switches, see the top
typedef int (WSAAPI *SendFn)(SOCKET, const char *, int, int);
typedef int (WSAAPI *RecvFn)(SOCKET, char *, int, int);
static RecvFn g_recvOrig; static SOCKET g_ours[64]; static void relay_mixed(void);
typedef int (WSAAPI *WSASendFn)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
static SendFn g_sendOrig; static WSASendFn g_wsaSendOrig;
static struct { SOCKET s; unsigned char pre[11]; int len; } g_pend[64]; static CRITICAL_SECTION g_pk;   // redirected sockets, preamble not sent yet
static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH]; static CRITICAL_SECTION g_lk;

static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsRelay.log", g_dir);
    EnterCriticalSection(&g_lk); FILE *f = _wfopen(p, L"a");
    if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
             va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_lk);
}

static int patch_imports(void *real, void *hook) {               // every loaded module's IAT entries of ws2_32/wsock32 connect
    HMODULE mods[512]; DWORD need = 0, n = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return 0;
    for (DWORD m = 0; m < need / sizeof(HMODULE) && m < 512; m++) {
        unsigned char *b = (unsigned char *)mods[m]; IMAGE_DOS_HEADER *d = (IMAGE_DOS_HEADER *)b;
        if (IsBadReadPtr(d, sizeof *d) || d->e_magic != IMAGE_DOS_SIGNATURE) continue;
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + d->e_lfanew);
        IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress) continue;
        for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(b + dir.VirtualAddress); imp->Name; imp++) {
            const char *dll = (const char *)(b + imp->Name);
            if (lstrcmpiA(dll, "ws2_32.dll") && lstrcmpiA(dll, "wsock32.dll")) continue;
            for (IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(b + imp->FirstThunk); t->u1.Function; t++)
                if ((void *)t->u1.Function == real) {
                    DWORD o; VirtualProtect(&t->u1.Function, 4, PAGE_READWRITE, &o); t->u1.Function = (DWORD)hook; VirtualProtect(&t->u1.Function, 4, o, &o); n++;
                }
        }
    }
    return (int)n;
}

static int tcp_ok(struct in_addr ip, unsigned short port, int ms) {   // TCP connect within ms (through the original connect)
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (s == INVALID_SOCKET) return 0;
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr = ip; a.sin_port = htons(port);
    g_connectOrig(s, (struct sockaddr *)&a, sizeof a);
    fd_set w, e; FD_ZERO(&w); FD_SET(s, &w); FD_ZERO(&e); FD_SET(s, &e); struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    int ok = select(0, NULL, &w, &e, &tv) > 0 && FD_ISSET(s, &w) && !FD_ISSET(s, &e);
    closesocket(s); return ok;
}

static int tcp_ok(struct in_addr ip, unsigned short port, int ms);
static struct in_addr g_bridgeHosts[4]; static int g_nBridge; static volatile LONG g_bridgeMs[4] = {-1, -1, -1, -1};   // Relay 15 bridge
static struct in_addr g_hosts[8]; static int g_nHosts; static volatile DWORD g_okAt; static volatile LONG g_dying;

// The error is shown by a separate process (PowerShell's message box) and the game closes at once: a message box of
// the game itself stayed under the fullscreen picture, and the game looked frozen (owner 30.10.2026: «должна была просто
// ошибку показать»). The text goes to PowerShell as -EncodedCommand (base64 of UTF-16LE), so no quoting can break it.
static void b64(const unsigned char *in, int n, wchar_t *out) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        unsigned v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = t[v >> 18 & 63]; out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
}
static void show_apart(const wchar_t *text) {
    static wchar_t script[4096], cmd[12000];
    int n = _snwprintf(script, 4000, L"Add-Type -AssemblyName PresentationFramework;[System.Windows.MessageBox]::Show('");
    for (const wchar_t *c = text; *c && n < 3900; c++) { if (*c == L'\'') script[n++] = L'\''; script[n++] = *c; }
    n += _snwprintf(script + n, 4090 - n, L"','Elite: use our game servers',0,16) | Out-Null");
    int k = _snwprintf(cmd, 200, L"powershell.exe -NoProfile -WindowStyle Hidden -EncodedCommand ");
    b64((const unsigned char *)script, n * 2, cmd + k);
    STARTUPINFOW si = {sizeof si}; PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    else logmsg("could not start the message (%lu)", GetLastError());
}

static void die_with(const char *why, const wchar_t *text) {    // tell the player and leave the game
    if (InterlockedExchange(&g_dying, 1)) { Sleep(INFINITE); }
    logmsg("FATAL: %s - closing the game", why);
    show_apart(text);
    TerminateProcess(GetCurrentProcess(), 0x52454C59);          // 'RELY'
}
static void relay_fail(const char *why) {
    die_with(why,
        L"Не удалось подключиться к нашему игровому серверу. Игра закрыта.\n\n"
        L"У вас включено «Использовать свои серверы» с выключенным «Режимом совместимости»: так игра идёт ТОЛЬКО через наш сервер.\n"
        L"Выберите в лаунчере другой сервер, включите «Режим совместимости» или снимите галочку «Использовать свои серверы».\n"
        L"Чтобы помочь найти причину — нажмите в лаунчере «Отправить логи».\n\n"
        L"Could not connect to our game server. The game was closed.\n"
        L"\"Use our game servers\" is on with \"Compatibility mode\" off: the game goes ONLY through our server. "
        L"Pick another server, turn \"Compatibility mode\" on or untick \"Use our game servers\" in the launcher.");
}
static void relay_mixed(void) {
    die_with("this lobby plays on Relic's servers (its host does not use our servers)",
        L"Это лобби на серверах Relic (у его хоста нет «Использовать свои серверы»), а у вас выключен «Режим совместимости» — "
        L"зайти в него нельзя. Игра закрыта.\n\n"
        L"Чтобы заходить в любые лобби, включите в лаунчере «Режим совместимости» под «Использовать свои серверы».\n\n"
        L"This lobby is on Relic's servers (its host does not use our servers) and your \"Compatibility mode\" is off - "
        L"you can't join it. The game was closed.\n"
        L"To join any lobby, turn \"Compatibility mode\" on under \"Use our game servers\" in the launcher.");
}

// Relay 9: ping all our servers (TCP connect time, best of 3) at start and every 5 minutes -> DOW2_RELAY_PING "ip:ms,..."
// (-1 = no answer). MMFind sends it with the automatch search; the matchmaker picks the server best for everyone of the
// match (the lowest worst ping) and MMFind sets DOW2_RELAY_HOST to it -> this match connects there.
static int tcp_rtt(struct in_addr ip, unsigned short port, int ms) {
    LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    if (!tcp_ok(ip, port, ms)) return -1;
    QueryPerformanceCounter(&b); return (int)((b.QuadPart - a.QuadPart) * 1000 / f.QuadPart);
}
static DWORD WINAPI ping_loop(LPVOID _) {
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); char last[256] = "";
    for (;;) {
        char env[256]; int L = 0; env[0] = 0;
        for (int i = 0; i < g_nHosts && L < 200; i++) {
            int best = -1;
            for (int k = 0; k < 3; k++) { int r = tcp_rtt(g_hosts[i], (unsigned short)g_port, 2000); if (r >= 0 && (best < 0 || r < best)) best = r; Sleep(100); }
            L += snprintf(env + L, sizeof env - L, "%s%s:%d", L ? "," : "", inet_ntoa(g_hosts[i]), best);
        }
        for (int i = 0; g_bridge && i < g_nBridge; i++) {         // bridge: Moscow too (the nearest one carries Relic games)
            int j = -1; for (int k = 0; k < g_nHosts; k++) if (g_hosts[k].s_addr == g_bridgeHosts[i].s_addr) j = k;
            int best = -1;
            if (j >= 0) { char *c = strstr(env, inet_ntoa(g_bridgeHosts[i])); if (c) { c = strchr(c, ':'); if (c) best = atoi(c + 1); } }
            else for (int k = 0; k < 3; k++) { int r = tcp_rtt(g_bridgeHosts[i], (unsigned short)g_port, 2000); if (r >= 0 && (best < 0 || r < best)) best = r; Sleep(100); }
            g_bridgeMs[i] = best;
        }
        if (g_bridge) { char bl[128] = ""; int bL = 0; for (int i = 0; i < g_nBridge; i++) bL += snprintf(bl + bL, sizeof bl - bL, "%s%s:%ld", bL ? "," : "", inet_ntoa(g_bridgeHosts[i]), g_bridgeMs[i]);
                        static char lastb[128]; if (strcmp(bl, lastb)) { logmsg("relay: bridge servers %s", bl); strcpy(lastb, bl); } }
        SetEnvironmentVariableA("DOW2_RELAY_PING", env);
        if (strcmp(env, last)) { logmsg("relay: ping %s", env); strcpy(last, env); }
        Sleep(300000);
    }
    return 0;
}

static int pick_relay(void) {                                   // first host of the list that answers; checked at most once a minute
    char hv[32] = ""; struct in_addr want; want.s_addr = INADDR_NONE;   // the matchmaker's choice for this match comes first
    if (!g_handHosts && GetEnvironmentVariableA("DOW2_RELAY_HOST", hv, sizeof hv) && hv[0]) want.s_addr = inet_addr(hv);   // the player's own pick wins
    if (want.s_addr != INADDR_NONE) {
        if (g_okAt && want.s_addr == g_relayIp.s_addr && GetTickCount() - g_okAt < 60000) return 1;
        if (tcp_ok(want, (unsigned short)g_port, 3000)) {
            if (want.s_addr != g_relayIp.s_addr) logmsg("relay: the matchmaker picked %s (best ping for everyone)", hv);
            g_relayIp = want; g_okAt = GetTickCount(); return 1;
        }
        static DWORD said; if (GetTickCount() - said > 60000) { said = GetTickCount(); logmsg("relay: the matchmaker picked %s, it does not answer - the first one that does", hv); }
    } else if (g_okAt && GetTickCount() - g_okAt < 60000) return 1;
    for (int i = 0; i < g_nHosts; i++)
        if (tcp_ok(g_hosts[i], (unsigned short)g_port, 4000)) { g_relayIp = g_hosts[i]; g_okAt = GetTickCount(); return 1; }
    return 0;
}


// ---------- Relay 15: the UDP tunnel (protocol: gameserver/udp_gateway.py) ----------
enum { U_SYN = 1, U_SYNACK, U_DATA, U_ACK, U_FIN };
#define U_MAXP 1200
#define U_WIN 512
typedef struct { struct in_addr ip; SOCKET lst; } TunArg;
typedef struct { unsigned char d[U_MAXP]; int n; DWORD first, last; int sends; } UPkt;
static DWORD g_tunSeed;
static void u_hdr(unsigned char *b, int t, DWORD cid) { b[0] = 'D'; b[1] = 'U'; b[2] = (unsigned char)t; memcpy(b + 3, &cid, 4); }
static void pipe_tcp(SOCKET a, SOCKET b) {                      // UDP did not answer: the same bytes over TCP
    char buf[8192];
    for (;;) {
        fd_set r; FD_ZERO(&r); FD_SET(a, &r); FD_SET(b, &r); struct timeval tv = {30, 0};
        if (select(0, &r, NULL, NULL, &tv) <= 0) break;
        if (FD_ISSET(a, &r)) { int n = g_recvOrig(a, buf, sizeof buf, 0); if (n <= 0 || g_sendOrig(b, buf, n, 0) != n) break; }
        if (FD_ISSET(b, &r)) { int n = g_recvOrig(b, buf, sizeof buf, 0); if (n <= 0 || g_sendOrig(a, buf, n, 0) != n) break; }
    }
}
static DWORD WINAPI tunnel(LPVOID pv) {
    TunArg ta = *(TunArg *)pv; free(pv);
    fd_set r; FD_ZERO(&r); FD_SET(ta.lst, &r); struct timeval tv = {30, 0};
    SOCKET g = select(0, &r, NULL, NULL, &tv) > 0 ? accept(ta.lst, NULL, NULL) : INVALID_SOCKET;
    closesocket(ta.lst);
    if (g == INVALID_SOCKET) { logmsg("udp: the game did not come to the local port"); return 0; }
    int one = 1; setsockopt(g, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
    u_long nb = 0; ioctlsocket(g, FIONBIO, &nb);
    SOCKET u = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in sa = {0}; sa.sin_family = AF_INET; sa.sin_addr = ta.ip; sa.sin_port = htons((unsigned short)g_port);
    g_connectOrig(u, (struct sockaddr *)&sa, sizeof sa);
    DWORD cid = GetTickCount() ^ (GetCurrentThreadId() << 16) ^ (DWORD)InterlockedIncrement((LONG *)&g_tunSeed) * 2654435761u;
    unsigned char pk[U_MAXP + 32]; int ok = 0; DWORD t0 = GetTickCount();
    for (int i = 0; i < 12 && !ok; i++) {                       // SYN every 250 ms, 3 s in all
        u_hdr(pk, U_SYN, cid); pk[7] = 1; send(u, (char *)pk, 8, 0);
        fd_set ur; FD_ZERO(&ur); FD_SET(u, &ur); struct timeval t2 = {0, 250000};
        if (select(0, &ur, NULL, NULL, &t2) > 0) { int n = recv(u, (char *)pk, sizeof pk, 0); if (n >= 7 && pk[0] == 'D' && pk[2] == U_SYNACK) ok = 1; }
    }
    if (!ok) {
        closesocket(u);
        logmsg("udp: %s does not answer over UDP - this connection goes over TCP", inet_ntoa(ta.ip));
        SOCKET t = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (g_connectOrig(t, (struct sockaddr *)&sa, sizeof sa) == 0) { setsockopt(t, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one); pipe_tcp(g, t); }
        else logmsg("udp: TCP to %s failed too (%d)", inet_ntoa(ta.ip), WSAGetLastError());
        closesocket(t); closesocket(g); return 0;
    }
    logmsg("udp: connection %08lX to %s over UDP (handshake %lu ms)", cid, inet_ntoa(ta.ip), GetTickCount() - t0);
    UPkt *snd = calloc(U_WIN, sizeof(UPkt)); UPkt *rcv = calloc(U_WIN, sizeof(UPkt));   // ring buffers by packet number % U_WIN
    char *rhave = calloc(U_WIN, 1);
    DWORD snext = 0, sbase = 0, rnext = 0, lastIn = GetTickCount(), lastOut = 0, lastAck = (DWORD)-1; int dup = 0;
    double srtt = 150, rttvar = 50; unsigned resent = 0, sentn = 0, gotn = 0, gapn = 0; int gameOpen = 1;
    #define SACK() ({ DWORD m_ = 0; for (int i_ = 0; i_ < 32; i_++) if (rhave[(rnext + 1 + i_) % U_WIN]) m_ |= 1u << i_; m_; })
    for (;;) {
        DWORD now = GetTickCount();
        if (now - lastIn > 30000) { logmsg("udp: %08lX - nothing from the server for 30 s", cid); break; }
        fd_set rs; FD_ZERO(&rs); FD_SET(u, &rs); if (gameOpen && snext - sbase < U_WIN) FD_SET(g, &rs);
        struct timeval t3 = {0, 20000};
        int sel = select(0, &rs, NULL, NULL, &t3); now = GetTickCount();
        if (sel > 0 && FD_ISSET(g, &rs)) {                       // game -> server
            UPkt *q = &snd[snext % U_WIN];
            int n = g_recvOrig(g, (char *)q->d, U_MAXP, 0);
            if (n <= 0) { gameOpen = 0; }
            else {
                q->n = n; q->first = q->last = now; q->sends = 1;
                u_hdr(pk, U_DATA, cid); DWORD sk = SACK(); memcpy(pk + 7, &snext, 4); memcpy(pk + 11, &rnext, 4); memcpy(pk + 15, &sk, 4);
                memcpy(pk + 19, q->d, n); send(u, (char *)pk, 19 + n, 0); snext++; sentn++; lastOut = now;
            }
        }
        if (sel > 0 && FD_ISSET(u, &rs)) {                       // server -> game
            int n = recv(u, (char *)pk, sizeof pk, 0);
            if (n >= 7 && pk[0] == 'D' && pk[1] == 'U' && !memcmp(pk + 3, &cid, 4)) {
                lastIn = now; int t = pk[2]; DWORD ack = 0, sack = 0;
                if (t == U_FIN) { logmsg("udp: %08lX closed by the server", cid); break; }
                if ((t == U_DATA && n >= 19) || (t == U_ACK && n >= 15)) {
                    int o = t == U_DATA ? 11 : 7; memcpy(&ack, pk + o, 4); memcpy(&sack, pk + o + 4, 4);
                    while (sbase < ack && sbase < snext) {           // acknowledged in order
                        UPkt *q = &snd[sbase % U_WIN];
                        if (q->sends == 1) { double rt = now - q->first; rttvar = 0.75 * rttvar + 0.25 * fabs(srtt - rt); srtt = 0.875 * srtt + 0.125 * rt; }
                        q->n = 0; sbase++;
                    }
                    for (int i = 0; i < 32; i++) if (sack >> i & 1) {   // arrived out of order: negative length = acknowledged
                        DWORD sq = ack + 1 + i; if (sq >= sbase && sq < snext && snd[sq % U_WIN].n > 0) snd[sq % U_WIN].n = -snd[sq % U_WIN].n; }
                    if (ack == lastAck && sack && sbase < snext && snd[sbase % U_WIN].n > 0) { if (dup < 2) dup++; } else dup = 0;   // 2 = send the first one again now
                    lastAck = ack;
                }
                if (t == U_DATA && n >= 19) {
                    DWORD seq; memcpy(&seq, pk + 7, 4); int len = n - 19;
                    if (seq >= rnext && seq - rnext < U_WIN && !rhave[seq % U_WIN]) {
                        UPkt *q = &rcv[seq % U_WIN]; memcpy(q->d, pk + 19, len); q->n = len; rhave[seq % U_WIN] = 1;
                        if (seq != rnext) gapn++;
                        while (rhave[rnext % U_WIN]) {
                            UPkt *x = &rcv[rnext % U_WIN]; rhave[rnext % U_WIN] = 0;
                            for (int off = 0; off < x->n; ) { int w = g_sendOrig(g, (char *)x->d + off, x->n - off, 0); if (w <= 0) { gameOpen = 0; break; } off += w; }
                            rnext++; gotn++;
                        }
                    }
                    unsigned char a[15]; u_hdr(a, U_ACK, cid); DWORD sk = SACK(); memcpy(a + 7, &rnext, 4); memcpy(a + 11, &sk, 4);
                    send(u, (char *)a, 15, 0); lastOut = now;
                }
            }
        }
        // resend what is overdue (RTO from the measured round trip, doubled per resend) and once on two duplicate acks
        double rto = srtt + 4 * rttvar; if (rto < 60) rto = 60; if (rto > 1000) rto = 1000;
        for (DWORD sq = sbase; sq < snext && sq < sbase + 64; sq++) {
            UPkt *q = &snd[sq % U_WIN]; if (q->n <= 0) continue;
            int mult = q->sends > 4 ? 8 : 1 << (q->sends - 1);
            if ((now - q->last > rto * mult) || (dup == 2 && sq == sbase)) {
                if (dup == 2) dup = 3;
                u_hdr(pk, U_DATA, cid); DWORD sk = SACK(); memcpy(pk + 7, &sq, 4); memcpy(pk + 11, &rnext, 4); memcpy(pk + 15, &sk, 4);
                memcpy(pk + 19, q->d, q->n); send(u, (char *)pk, 19 + q->n, 0); q->last = now; q->sends++; resent++; lastOut = now;
            }
        }
        while (sbase < snext && snd[sbase % U_WIN].n <= 0) { snd[sbase % U_WIN].n = 0; sbase++; }   // acked out of order earlier
        if (!gameOpen && sbase == snext) break;                  // the game closed and everything got there
        if (now - lastOut > 1000) { unsigned char a[15]; u_hdr(a, U_ACK, cid); DWORD sk = SACK(); memcpy(a + 7, &rnext, 4); memcpy(a + 11, &sk, 4); send(u, (char *)a, 15, 0); lastOut = now; }
    }
    for (int i = 0; i < 3; i++) { u_hdr(pk, U_FIN, cid); send(u, (char *)pk, 7, 0); }
    logmsg("udp: %08lX over: sent %u, got %u, sent again %u, out of order in %u, round trip %d ms", cid, sentn, gotn, resent, gapn, (int)srtt);
    closesocket(u); closesocket(g); free(snd); free(rcv); free(rhave);
    return 0;
}
// a local port for one game connection to our server ip; the thread carries it over UDP (TCP when UDP fails)
static int tunnel_port(struct in_addr ip) {
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (l == INVALID_SOCKET) return 0;
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); int al = sizeof a;
    if (bind(l, (struct sockaddr *)&a, sizeof a) || listen(l, 1) || getsockname(l, (struct sockaddr *)&a, &al)) { closesocket(l); return 0; }
    TunArg *ta = malloc(sizeof *ta); ta->ip = ip; ta->lst = l;
    HANDLE th = CreateThread(NULL, 0, tunnel, ta, 0, NULL); if (!th) { free(ta); closesocket(l); return 0; }
    CloseHandle(th); return ntohs(a.sin_port);
}

// bridge: our server nearest to the player (the lowest delay the ping loop measured; the one picked by hand wins)
static int pick_bridge(struct in_addr *out) {
    int best = -1;
    for (int i = 0; i < g_nBridge; i++) if (g_bridgeMs[i] > 0 && (best < 0 || g_bridgeMs[i] < g_bridgeMs[best])) best = i;
    if (best < 0) for (int i = 0; i < g_nBridge; i++) if (tcp_ok(g_bridgeHosts[i], (unsigned short)g_port, 3000)) { best = i; break; }
    if (best < 0) return 0;
    *out = g_bridgeHosts[best]; return 1;
}

static int WSAAPI connect_hook(SOCKET sock, const struct sockaddr *name, int len) {
    if (g_relayOn && name && name->sa_family == AF_INET && len >= (int)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)name; unsigned short p = ntohs(a->sin_port);
        if (p >= 27000 && p <= 27099) {                           // Relic's battleserverrelay (any of their servers)
            char ip[32]; snprintf(ip, sizeof ip, "%s", inet_ntoa(a->sin_addr));
            char use[4] = "";
            int bridge = 0, toOurs = 0;
            // Relay 15: the game asks for one of OUR addresses as if it were Relic's relay (seen 01.11: 15.220.68.107:27015 after a
            // match on our server) - that is our server in any mode: its battle port, never the address itself (other services there)
            { const char *ours[] = {"15.220.68.107", "116.202.106.150", "83.217.192.34"};
              for (int i = 0; i < 3; i++) if (a->sin_addr.s_addr == inet_addr(ours[i])) toOurs = 1; }
            int automatch = GetEnvironmentVariableA("DOW2_RELAY_USE", use, sizeof use) && use[0] == '1';
            if (toOurs && automatch) toOurs = 0;                  // an automatch: the matchmaker's server (no extra hop)
            if (toOurs) { g_relayIp = a->sin_addr; g_okAt = GetTickCount(); }
            if (!toOurs && !automatch && g_fallback) {
                struct in_addr bh;
                if (!g_bridge || !pick_bridge(&bh)) {
                    static DWORD said2; if (GetTickCount() - said2 > 60000) { said2 = GetTickCount(); logmsg(g_bridge ? "relay: none of ours answers - %s:%u straight to Relic" : "relay: %s:%u on Relic's servers (not an automatch where everyone has the add-on)", ip, p); }
                    return g_connectOrig(sock, name, len);
                }
                bridge = 1; g_relayIp = bh;                       // Relay 15 bridge: a Relic game, carried by our server
            }
            if (!bridge && !toOurs && !pick_relay()) {
                if (!g_fallback) relay_fail("our relay does not answer");
                static DWORD said; if (GetTickCount() - said > 60000) { said = GetTickCount(); logmsg("relay: none of ours answers - %s:%u on Relic's servers, as without the add-on", ip, p); }
                return g_connectOrig(sock, name, len);
            }
            int players = 0; char ev[8];
            if (GetEnvironmentVariableA("DOW2_LOBBY_PLAYERS", ev, sizeof ev)) players = atoi(ev);
            if (players < 0 || players > 16) players = 0;
            EnterCriticalSection(&g_pk);
            int k = -1; for (int i = 0; i < 64; i++) if (g_pend[i].s == sock || (k < 0 && !g_pend[i].s)) { k = i; if (g_pend[i].s == sock) break; }
            if (k >= 0) { g_pend[k].s = sock; memcpy(g_pend[k].pre, bridge ? "DWR3" : g_fallback ? "DWR2" : "DWR1", 4); memcpy(g_pend[k].pre + 4, &a->sin_addr, 4);
                          g_pend[k].pre[8] = (unsigned char)(p >> 8); g_pend[k].pre[9] = (unsigned char)p;
                          g_pend[k].pre[10] = (unsigned char)players; g_pend[k].len = !bridge && g_fallback ? 11 : 10; }
            LeaveCriticalSection(&g_pk);
            if (k < 0) relay_fail("too many relay connections");
            EnterCriticalSection(&g_pk); for (int i = 0; i < 64; i++) if (!g_ours[i] || g_ours[i] == sock) { g_ours[i] = sock; break; } LeaveCriticalSection(&g_pk);
            struct sockaddr_in b = *a; b.sin_addr = g_relayIp; b.sin_port = htons((unsigned short)g_port);
            char via[40] = "";
            if (g_udp) {                                          // Relay 15: to a local port of ours, a thread carries it over UDP
                int lp = tunnel_port(g_relayIp);
                if (lp) { b.sin_addr.s_addr = htonl(INADDR_LOOPBACK); b.sin_port = htons((unsigned short)lp); strcpy(via, " over UDP"); }
                else strcpy(via, " (UDP tunnel failed to start - TCP)");
            }
            logmsg("relay: %s:%u -> ours %s:%d%s%s", ip, p, inet_ntoa(g_relayIp), g_port, via,
                   bridge ? " (bridge: a Relic game, our server passes it on)" : g_fallback ? " (compatible: a lobby hosted elsewhere is passed on)" : "");
            return g_connectOrig(sock, (const struct sockaddr *)&b, sizeof b);
        }
    }
    EnterCriticalSection(&g_pk);                                  // a reused socket handle must not get a stale preamble
    for (int i = 0; i < 64; i++) { if (g_pend[i].s == sock) g_pend[i].s = 0; if (g_ours[i] == sock) g_ours[i] = 0; }
    LeaveCriticalSection(&g_pk);
    return g_connectOrig(sock, name, len);
}

static int send_preamble(SOCKET s) {                           // before the first game bytes on a redirected socket
    unsigned char pre[11]; int have = 0, plen = 10;
    EnterCriticalSection(&g_pk);
    for (int i = 0; i < 64; i++) if (g_pend[i].s == s) { memcpy(pre, g_pend[i].pre, 11); plen = g_pend[i].len; g_pend[i].s = 0; have = 1; break; }
    LeaveCriticalSection(&g_pk);
    if (!have) return 1;
    for (int off = 0, tries = 0; off < plen; ) {                  // the socket is fresh: the bytes go at once; wait a little if not
        int n = g_sendOrig(s, (const char *)pre + off, plen - off, 0);
        if (n > 0) { off += n; continue; }
        if (WSAGetLastError() != WSAEWOULDBLOCK || ++tries > 200) { logmsg("relay: preamble not sent (%d)", WSAGetLastError()); relay_fail("our relay does not take the connection"); }
        Sleep(10);
    }
    return 1;
}

static int WSAAPI recv_hook(SOCKET s, char *buf, int len, int flags) {
    int n = g_recvOrig(s, buf, len, flags);
    if (n >= 12 && !memcmp(buf, "DWRERR:mixed", 12)) {
        int ours = 0; EnterCriticalSection(&g_pk); for (int i = 0; i < 64; i++) if (g_ours[i] == s) ours = 1; LeaveCriticalSection(&g_pk);
        if (ours) relay_mixed();
    }
    return n;
}

static int WSAAPI send_hook(SOCKET s, const char *buf, int len, int flags) {
    send_preamble(s); return g_sendOrig(s, buf, len, flags);
}

static int WSAAPI wsasend_hook(SOCKET s, LPWSABUF b, DWORD n, LPDWORD sent, DWORD fl, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
    send_preamble(s); return g_wsaSendOrig(s, b, n, sent, fl, ov, cr);
}

static void load_hosts(void) {                                  // from DllMain: IP addresses only (no name lookups under the loader lock)
    wchar_t hosts[256]; GetPrivateProfileStringW(L"Relay", L"Hosts", L"15.220.68.107", hosts, 256, g_ini);   // Warsaw (AWS)
    char hs[256]; WideCharToMultiByte(CP_ACP, 0, hosts, -1, hs, sizeof hs, NULL, NULL);
    for (char *t = strtok(hs, ";, "); t && g_nHosts < 8; t = strtok(NULL, ";, ")) {
        struct in_addr ip; ip.s_addr = inet_addr(t); if (ip.s_addr != INADDR_NONE) g_hosts[g_nHosts++] = ip;
    }
    if (!g_nHosts) { g_hosts[0].s_addr = inet_addr("15.220.68.107"); g_nHosts = 1; }
    wchar_t own[8]; GetPrivateProfileStringW(L"Relay", L"Hosts", L"", own, 8, g_ini); g_handHosts = own[0] != 0;
    if (!own[0] && g_backup)                                    // Germany (Hetzner) after Warsaw
        g_hosts[g_nHosts++].s_addr = inet_addr("116.202.106.150");
    g_relayIp = g_hosts[0]; g_relayOn = 1;                      // from the very first connect: ours, never Relic directly
    if (g_handHosts) for (int i = 0; i < g_nHosts && g_nBridge < 4; i++) g_bridgeHosts[g_nBridge++] = g_hosts[i];   // bridge: the hand pick
    else { const char *bh[] = {"15.220.68.107", "116.202.106.150", "83.217.192.34"}; for (int i = 0; i < 3; i++) g_bridgeHosts[g_nBridge++].s_addr = inet_addr(bh[i]); }
}

static DWORD WINAPI first_check(LPVOID _) {                     // early warning in the log; the hard check is at connect time
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    logmsg(pick_relay() ? "relay: all online games on our battle server %s" : "relay: ours (%s) does not answer yet", inet_ntoa(g_relayIp));
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_lk);
    char mx[64]; wsprintfA(mx, "Local\\dow2patch_Relay_%lu", GetCurrentProcessId());
    CreateMutexA(NULL, FALSE, mx); if (GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0;
    _snwprintf(g_ini, MAX_PATH, L"%lsRelay.ini", g_dir);
    g_port = GetPrivateProfileIntW(L"Relay", L"Port", 47100, g_ini); InitializeCriticalSection(&g_pk);
    g_backup = GetPrivateProfileIntW(L"Relay", L"Backup", 1, g_ini);
    g_fallback = GetPrivateProfileIntW(L"Relay", L"Fallback", 1, g_ini);   // "Compatibility mode" sub-tick in the launcher (on by default)
    { wchar_t m[16]; GetPrivateProfileStringW(L"Relay", L"Mode", L"", m, 16, g_ini);   // Relay 15: the launcher's list of modes
      if (!_wcsicmp(m, L"strict")) g_fallback = 0; else if (!_wcsicmp(m, L"compat")) g_fallback = 1; else if (!_wcsicmp(m, L"bridge")) { g_fallback = 1; g_bridge = 1; } }
    g_udp = GetPrivateProfileIntW(L"Relay", L"UDP", 0, g_ini);
    HMODULE ws = GetModuleHandleA("ws2_32.dll"); if (!ws) ws = LoadLibraryA("ws2_32.dll");
    void *real = ws ? (void *)GetProcAddress(ws, "connect") : NULL; if (!real) return TRUE;
    g_connectOrig = (ConnectFn)real;
    int n = patch_imports(real, (void *)connect_hook);
    g_sendOrig = (SendFn)GetProcAddress(ws, "send"); g_wsaSendOrig = (WSASendFn)GetProcAddress(ws, "WSASend");
    int ns = g_sendOrig ? patch_imports((void *)g_sendOrig, (void *)send_hook) : 0;
    int nw = g_wsaSendOrig ? patch_imports((void *)g_wsaSendOrig, (void *)wsasend_hook) : 0;
    g_recvOrig = (RecvFn)GetProcAddress(ws, "recv"); int nr = g_recvOrig ? patch_imports((void *)g_recvOrig, (void *)recv_hook) : 0;
    { wchar_t p[MAX_PATH], o[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsRelay.log", g_dir); _snwprintf(o, MAX_PATH, L"%lsRelay.log.old", g_dir);
      WIN32_FILE_ATTRIBUTE_DATA a; if (GetFileAttributesExW(p, GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > (1u << 20))) MoveFileExW(p, o, MOVEFILE_REPLACE_EXISTING); }
    load_hosts();
    SetEnvironmentVariableA("DOW2_RELAY", g_fallback ? "9c" : "9");   // for MMFind: the matchmaker may mix us with Relic players
    logmsg("Relay 15 start (%d server(s), mode %s%s), connect hooked in %d import(s), send %d, WSASend %d, recv %d%s", g_nHosts,
           g_bridge ? "bridge" : g_fallback ? "compatible" : "strict", g_udp ? ", UDP to our server" : "", n, ns, nw, nr, n ? "" : " - another add-on (automatch) already routes the relay, staying idle");
    if (n && g_relayOn) { CloseHandle(CreateThread(NULL, 0, first_check, NULL, 0, NULL)); CloseHandle(CreateThread(NULL, 0, ping_loop, NULL, 0, NULL)); }
    return TRUE;
}
