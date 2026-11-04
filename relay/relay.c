// Relay 17-22 (02-04.11.2026) - DoW2 Retribution / Elite: online games through OUR game servers. One source, two add-ons
// (docs/ADDON_RULES.md: one feature - one DLL - one tick), two separate ticks in the launcher (owner 02.11.2026: the old
// Fallback / Mode / Backup / UDP / Server controls were "убогие" - two clear ticks instead, the server is always automatic):
//
//   Relay.dll         (relay_r2, "Use our servers for games")         - a game goes STRAIGHT through our server (our battle
//                     server hosts the session, "DWR2") whenever everyone in it reaches our servers: that is known for our
//                     automatch (the matchmaker has every player's ping to every node and picks the best node for all:
//                     MMFind sets DOW2_RELAY_USE=1 + DOW2_RELAY_HOST), and for a game whose address already is ours (our
//                     lobby server's rooms). A custom lobby can't know who will come, so it stays a Relic game.
//   RelayForward.dll  (relay_fwd, "Use forwarding servers in all games") - every other online game (Relic's) goes through
//                     our server nearest to the player, which passes it on to the Relic relay the game wanted ("DWR3") -
//                     for players whose way to Relic (ports 27000-27099) is cut or slow.
//   Both on: straight through ours when possible, otherwise the nearest forwarding server. One on: only its behaviour.
//   Both off: as without the add-on (Relic directly).
//
// How the two DLLs share the work: each sets its flag in the process environment at load (DOW2_RELAY_OWN=1 /
// DOW2_RELAY_FWD=1); the first one loaded installs the connect hook and the ping loop for both (a named mutex) and reads
// both flags at every connect - so one hook, never two, and each tick works alone. Built with -DRELAY_FWD for the second.
//
// Transport: UDP ONLY (owner 02.11.2026; in Russia TCP to our servers gets cut on the way, UDP passes - NetProbe). The game
// connects to a local port, a thread carries the bytes to the node's udp_gateway (UDP 47100, gameserver/udp_gateway.py),
// with our own delivery (numbers, acks, resends). No UDP answer from the node: a forwarded game goes straight to Relic
// (the bytes of the game, without our preamble); a game on our server tries our other nodes (they hand guests on to the
// node that has the session).
// Nodes: the built-in three (Poland 15.220.68.107, Germany 116.202.106.150, Moscow 83.217.192.34) + Relay.ini [Relay] Nodes=
// (MMFind writes it from the ladder: /ladder/api/relays and the "relays" field of mm/join|poll; re-read within 5 s).
// Every node is pinged over UDP (PING/PONG of udp_gateway, best of 3) at start, every 5 minutes and when Nodes= changes;
// "nearest" = the lowest delay. DOW2_RELAY_PING "ip:ms,..." (-1 = no answer) goes to the matchmaker only with Relay.dll on.
// Removed in 17: Mode, Fallback (compatible / strict), Backup, UDP=, Hosts= (the hand pick), TCP to our servers, the
// "game closed" windows of the strict mode.
// Relay 22 (04.11.2026): the relay chain. A native game (our lobby server's room: the game connects to one of our nodes itself)
// whose node this player can't reach - no UDP answer, or the stream check of Relay 18 failed (DPI) - plays through the
// player's own nearest usable node: preamble "DWR4" + IPv4 + port (BE) of the room's node, then the game's stream; that node
// passes it on to the room's node over our UDP tunnel between the nodes (battle_relay.py, du_client.py). Player -> his node ->
// the session's node, never more. Before, such a player could not join at all (UDP to the node, then TCP to it, then nothing).
// The pings offered to the matchmaker start with "0.0.0.22:22" - "this player can chain": the ladder may then put the match on a
// node he doesn't reach (the lowest worst delay of all players, a chain = ping to his node + node -> node).
// Relay 21 (03.11.2026): no global lock on the hot path. Relay 20 took one critical section and walked 64 slots on EVERY send /
// WSASend of every socket of the process (the IAT of all modules is hooked) and on every connect - all the game's network
// threads queued on it (players in Russia: "BattleServerRelay::Process - 177..325 ms between calls" with the add-on). Now a
// socket is ours only when its preamble waits (marked lock-free at the redirect); every other send passes with one read.
// Also: only TCP connects to 27000-27099 / our battle port are looked at (a UDP socket's connect is never redirected), and
// NetProbe.dll is never hooked (its TCP 27015 test to our nodes went into our UDP tunnel as if it were the game - stand 03.11).
// Relay 20 (03.11.2026): copies of the unacknowledged packets inside each UDP data packet (see DATA2 below; agreed in the SYN,
// an old gateway = no copies), and the native connections of our lobby server's rooms (the game connects to one of our nodes
// on the battle port itself) over UDP too - before they went to the node over TCP, which DPI cuts in Russia after ~33 KB;
// TCP to that node stays the fallback when its udp_gateway does not answer.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#ifdef RELAY_FWD
#define ME "RelayForward"
#define MY_FLAG "DOW2_RELAY_FWD"
#else
#define ME "Relay"
#define MY_FLAG "DOW2_RELAY_OWN"
#endif

typedef int (WSAAPI *ConnectFn)(SOCKET, const struct sockaddr *, int);
typedef int (WSAAPI *SendFn)(SOCKET, const char *, int, int);
typedef int (WSAAPI *RecvFn)(SOCKET, char *, int, int);
typedef int (WSAAPI *WSASendFn)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);
static ConnectFn g_connectOrig; static SendFn g_sendOrig; static WSASendFn g_wsaSendOrig; static RecvFn g_recvOrig;
static int g_port = 47100;                                       // our nodes: udp_gateway (UDP) in front of battle_relay
// local sockets whose preamble is not sent yet. Relay 21: written only under g_pk (a redirected connect - rare), READ without
// any lock: g_npend = filled slots; 0 (nearly always: a slot lives from the redirect to the game's first send, milliseconds)
// = every send / WSASend / connect of the process passes the hook with one memory read, no lock, no loop
static struct { volatile SOCKET s; unsigned char pre[11]; int len; } g_pend[64]; static CRITICAL_SECTION g_pk;
static volatile LONG g_npend;
static int pend_find(SOCKET s) {                                // lock-free: the slot of s, -1 = not ours
    if (!g_npend || !s) return -1;
    for (int i = 0; i < 64; i++) if (g_pend[i].s == s) return i;
    return -1;
}
static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH]; static CRITICAL_SECTION g_lk;
static struct in_addr g_hosts[8]; static int g_nHosts; static volatile LONG g_ms[8] = {-2, -2, -2, -2, -2, -2, -2, -2};   // -2 = not measured yet

static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsRelay.log", g_dir);
    EnterCriticalSection(&g_lk); FILE *f = _wfopen(p, L"a");
    if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
             va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_lk);
}
static int flag(const char *name) { char v[4] = ""; return GetEnvironmentVariableA(name, v, sizeof v) && v[0] == '1'; }

static int patch_imports(void *real, void *hook) {               // every loaded module's IAT entries of ws2_32/wsock32 connect
    HMODULE mods[512]; DWORD need = 0, n = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return 0;
    for (DWORD m = 0; m < need / sizeof(HMODULE) && m < 512; m++) {
        unsigned char *b = (unsigned char *)mods[m]; IMAGE_DOS_HEADER *d = (IMAGE_DOS_HEADER *)b;
        char nm[64];                                              // Relay 21: NetProbe measures the player's real way (TCP 27015 to our
        if (GetModuleBaseNameA(GetCurrentProcess(), mods[m], nm, sizeof nm) && !lstrcmpiA(nm, "NetProbe.dll")) continue;   // nodes) - never ours
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

static FILETIME g_iniTime;
static int merge_nodes(void) {
    wchar_t w[512]; GetPrivateProfileStringW(L"Relay", L"Nodes", L"", w, 512, g_ini);
    char hs[512]; WideCharToMultiByte(CP_ACP, 0, w, -1, hs, sizeof hs, NULL, NULL);
    int added = 0; char *ctx = NULL;
    for (char *t = strtok_s(hs, ";, ", &ctx); t && g_nHosts < 8; t = strtok_s(NULL, ";, ", &ctx)) {
        struct in_addr ip; ip.s_addr = inet_addr(t); if (ip.s_addr == INADDR_NONE || ip.s_addr == 0) continue;
        int have = 0; for (int i = 0; i < g_nHosts; i++) if (g_hosts[i].s_addr == ip.s_addr) have = 1;
        if (have) continue;
        g_hosts[g_nHosts] = ip; MemoryBarrier(); g_nHosts++; added++;
    }
    return added;
}
static int ini_changed(void) {
    WIN32_FILE_ATTRIBUTE_DATA a; if (!GetFileAttributesExW(g_ini, GetFileExInfoStandard, &a)) return 0;
    if (CompareFileTime(&a.ftLastWriteTime, &g_iniTime) == 0) return 0;
    g_iniTime = a.ftLastWriteTime; return 1;
}
// ---------- UDP to our nodes (protocol: gameserver/udp_gateway.py) ----------
// Relay 20 (03.11.2026): redundancy. SYN carries our version 2; a gateway that knows it answers SYNACK + 2, and then both sides
// send DATA2 instead of DATA: the packet also carries copies of the previous packets not acknowledged yet (the newest 2 that
// fit), so a single lost packet (or two in a row) is filled by the next one without waiting for a resend. An old gateway
// answers a bare SYNACK -> the old DATA, no copies (old Relay 18/19 keep working with a new gateway the same way).
//   DATA2: "DU" 8 cid | seq u32 | ack u32 | sack u32 | k u8 | k x (seq u32, len u16, bytes) | the packet's own bytes
//   ACK2:  "DU" 9 cid | ack u32 | sack u32 | k u8 | k x (seq u32, len u16, bytes)  - an ACK with copies (k >= 1; none = ACK)
// The acks of the other way's packets are carriers too (a game sends turns and gets ticks ~10 times a second each way, so
// the next packet in the same direction comes about twice as soon). An ACK2 is answered only when a copy in it filled
// something (a plain DATA always is), so two sides never bounce acks.
// The copies only go while the whole datagram stays within U_BUDGET (game turns are small: they nearly always fit; a big
// transfer carries none and is resent as before).
enum { U_SYN = 1, U_SYNACK, U_DATA, U_ACK, U_FIN, U_PING, U_PONG, U_DATA2, U_ACK2 };
#define U_MAXP 1200
#define U_WIN 512
#define U_VER 2
#define U_COPIES 2
#define U_BUDGET 1240
typedef struct { unsigned char d[U_MAXP]; int n; DWORD first, last; int sends; } UPkt;
static volatile LONG g_tunSeed;
static void u_hdr(unsigned char *b, int t, DWORD cid) { b[0] = 'D'; b[1] = 'U'; b[2] = (unsigned char)t; memcpy(b + 3, &cid, 4); }
static DWORD new_cid(void) { return GetTickCount() ^ (GetCurrentThreadId() << 16) ^ (DWORD)InterlockedIncrement(&g_tunSeed) * 2654435761u; }

static SOCKET udp_to(struct in_addr ip) {
    SOCKET u = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); if (u == INVALID_SOCKET) return u;
    struct sockaddr_in sa = {0}; sa.sin_family = AF_INET; sa.sin_addr = ip; sa.sin_port = htons((unsigned short)g_port);
    g_connectOrig(u, (struct sockaddr *)&sa, sizeof sa); return u;
}
static int udp_wait(SOCKET u, unsigned char *pk, int cap, int ms) {
    fd_set r; FD_ZERO(&r); FD_SET(u, &r); struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    return select(0, &r, NULL, NULL, &tv) > 0 ? recv(u, (char *)pk, cap, 0) : -1;
}
static int udp_rtt(struct in_addr ip, int ms) {                 // one PING/PONG: the delay in ms, -1 = no answer
    SOCKET u = udp_to(ip); if (u == INVALID_SOCKET) return -1;
    DWORD cid = new_cid(); unsigned char pk[64]; LARGE_INTEGER f, a, b; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&a);
    u_hdr(pk, U_PING, cid); memcpy(pk + 7, &a.QuadPart, 8); send(u, (char *)pk, 15, 0);
    int r = -1; DWORD until = GetTickCount() + ms;
    while ((int)(until - GetTickCount()) > 0) {
        int n = udp_wait(u, pk, sizeof pk, (int)(until - GetTickCount()));
        if (n < 0) break;
        if (n >= 7 && pk[0] == 'D' && pk[1] == 'U' && pk[2] == U_PONG && !memcmp(pk + 3, &cid, 4)) {
            QueryPerformanceCounter(&b); r = (int)((b.QuadPart - a.QuadPart) * 1000 / f.QuadPart); break; }
    }
    closesocket(u); return r;
}
static int udp_open(struct in_addr ip, SOCKET *pu, DWORD *pcid) {   // the handshake: SYN every 250 ms, 3 s in all; the version agreed, 0 = none
    SOCKET u = udp_to(ip); if (u == INVALID_SOCKET) return 0;
    DWORD cid = new_cid(); unsigned char pk[64];
    for (int i = 0; i < 12; i++) {
        u_hdr(pk, U_SYN, cid); pk[7] = U_VER; send(u, (char *)pk, 8, 0);
        DWORD until = GetTickCount() + 250;
        while ((int)(until - GetTickCount()) > 0) {
            int n = udp_wait(u, pk, sizeof pk, (int)(until - GetTickCount()));
            if (n < 0) break;
            if (n >= 7 && pk[0] == 'D' && pk[2] == U_SYNACK && !memcmp(pk + 3, &cid, 4)) {   // an old gateway: no version byte = 1
                *pu = u; *pcid = cid; return n >= 8 && pk[7] >= 2 ? 2 : 1; }
        }
    }
    closesocket(u); return 0;
}

// ---------- which node ----------
static int node_of(struct in_addr ip) { for (int i = 0; i < g_nHosts; i++) if (g_hosts[i].s_addr == ip.s_addr) return i; return -1; }
// Relay 18 (03.11.2026): a node counts only if a STREAM gets through, not just a short ping. NetProbe (2-3.11): for most
// players in Russia the way to Poland/Germany answers a connect or a ping in ~50 ms but stops after ~33 KB (DPI) - Relay 17
// ranked such a node "nearest" and the game went to a dead server. So after the ping: 40 packets x 1000 bytes (40 KB, past
// the cut), paced; the gateway answers each with a short PONG (never more bytes than came in). Usable = 30+ answers and 3+
// of the last 6 (the ones past ~33 KB). The node keeps its ping time when it passes, else -1 (not used, not offered).
#define BURST_N 40
#define BURST_LEN 1000
static int udp_burst(struct in_addr ip, int *tail) {
    SOCKET u = udp_to(ip); if (u == INVALID_SOCKET) return 0;
    DWORD cid = new_cid(); static unsigned char pk[BURST_LEN + 16]; unsigned char got[BURST_N] = {0}, in[64];
    memset(pk, 0x5a, sizeof pk); u_hdr(pk, U_PING, cid);
    timeBeginPeriod(1);
    for (int n = 0; n < BURST_N; n++) {
        DWORD seq = n | 0xB0000000u; memcpy(pk + 7, &seq, 4); send(u, (char *)pk, 7 + BURST_LEN, 0);
        if (n % 4 == 3) Sleep(2);                                       // ~1 MB/s: a stream, not a flood
        for (int r; (r = udp_wait(u, in, sizeof in, 0)) >= 11; )        // answers that are already here
            if (in[0] == 'D' && in[2] == U_PONG && !memcmp(in + 3, &cid, 4)) { DWORD q; memcpy(&q, in + 7, 4); if ((q >> 28) == 0xB && (q & 0xff) < BURST_N) got[q & 0xff] = 1; }
    }
    timeEndPeriod(1);
    DWORD until = GetTickCount() + 1500;
    while ((int)(until - GetTickCount()) > 0) {
        int r = udp_wait(u, in, sizeof in, (int)(until - GetTickCount())); if (r < 0) break;
        if (r >= 11 && in[0] == 'D' && in[2] == U_PONG && !memcmp(in + 3, &cid, 4)) { DWORD q; memcpy(&q, in + 7, 4); if ((q >> 28) == 0xB && (q & 0xff) < BURST_N) got[q & 0xff] = 1; }
    }
    closesocket(u);
    int n = 0, t = 0; for (int i = 0; i < BURST_N; i++) { n += got[i]; if (i >= BURST_N - 6) t += got[i]; }
    *tail = t; return n;
}
static void measure(int i) {                                    // best of 3 pings, then the stream check (Relay 18)
    int best = -1;
    for (int k = 0; k < 3; k++) { int r = udp_rtt(g_hosts[i], 1500); if (r >= 0 && (best < 0 || r < best)) best = r; Sleep(50); }
    if (best >= 0) {
        int tail = 0, got = udp_burst(g_hosts[i], &tail);
        if (got < 30 || tail < 3) {
            logmsg("relay: %s answers a ping in %d ms but a stream does not get through (%d of %d answers, %d of the last 6) - not used",
                   inet_ntoa(g_hosts[i]), best, got, BURST_N, tail);
            best = -1;
        }
    }
    g_ms[i] = best;
}
static int nearest(DWORD skip) {                                // the lowest delay of the nodes that answer, -1 = none
    for (int i = 0; i < g_nHosts; i++) if (g_ms[i] == -2) measure(i);   // asked before the first ping round ended
    int best = -1;
    for (int i = 0; i < g_nHosts; i++) if (!(skip >> i & 1) && g_ms[i] >= 0 && (best < 0 || g_ms[i] < g_ms[best])) best = i;
    return best;
}
static int own_node(void) {                                     // a game on our server: the matchmaker's pick, else the nearest
    char hv[32] = ""; struct in_addr want; want.s_addr = INADDR_NONE;
    if (GetEnvironmentVariableA("DOW2_RELAY_HOST", hv, sizeof hv) && hv[0]) want.s_addr = inet_addr(hv);
    if (want.s_addr != INADDR_NONE) {
        int i = node_of(want);
        if (i >= 0 && g_ms[i] >= 0) return i;
        if (i < 0 && udp_rtt(want, 1500) >= 0 && g_nHosts < 8) { g_hosts[g_nHosts] = want; g_ms[g_nHosts] = 1; MemoryBarrier(); return g_nHosts++; }
        logmsg("relay: the matchmaker picked %s, it does not answer over UDP - the nearest of ours", hv);
    }
    return nearest(0);
}

static DWORD WINAPI ping_loop(LPVOID _) {
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); char last[256] = "";
    for (;;) {
        char env[256]; int L = 0; env[0] = 0;
        for (int i = 0; i < g_nHosts && L < 200; i++) {
            measure(i);
            int ms = g_ms[i] == 0 ? 1 : (int)g_ms[i];           // the matchmaker reads 0 as "no answer" (a local VPN once faked 0 ms over TCP)
            L += snprintf(env + L, sizeof env - L, "%s%s:%d", L ? "," : "", inet_ntoa(g_hosts[i]), ms);
        }
        // Relay 22: "0.0.0.22:22" first (MMFind passes it on as one more ping, the ladder reads it as "can chain"; old ladders ignore it)
        char offer[300]; snprintf(offer, sizeof offer, "0.0.0.22:22%s%s", env[0] ? "," : "", env);
        SetEnvironmentVariableA("DOW2_RELAY_PING", flag("DOW2_RELAY_OWN") ? offer : NULL);   // only "use our servers" offers us to the matchmaker
        if (strcmp(env, last)) { logmsg("relay: UDP ping %s", env); strcpy(last, env); }
        for (int k = 0; k < 60; k++) {                          // every 5 minutes, at once when MMFind wrote new Nodes=
            Sleep(5000);
            if (ini_changed()) { int a = merge_nodes(); if (a) { logmsg("relay: %d node(s) from the ladder added, %d to ping", a, g_nHosts); break; } }
        }
    }
    return 0;
}

// ---------- one game connection: local port <-> UDP ----------
// own: 1 = a game on our server (other nodes may take it), 2 = native (Relay 20): the game itself connects to one of our
// nodes on the battle port (our lobby server made it the room's relay) - no preamble, only that node: over UDP, and when its
// udp_gateway does not answer, TCP straight to the node as the game wanted
typedef struct { SOCKET lst; struct sockaddr_in relic; int node, own, plen; } TunArg;
static void pipe_relic(SOCKET g, SOCKET t, int skip) {         // forwarding failed: the game's bytes straight to Relic, our preamble dropped
    char buf[8192];
    for (;;) {
        fd_set r; FD_ZERO(&r); FD_SET(g, &r); FD_SET(t, &r); struct timeval tv = {60, 0};
        if (select(0, &r, NULL, NULL, &tv) <= 0) break;
        if (FD_ISSET(g, &r)) {
            int n = g_recvOrig(g, buf, sizeof buf, 0); if (n <= 0) break;
            int o = skip < n ? skip : n; skip -= o;
            if (n > o && g_sendOrig(t, buf + o, n - o, 0) != n - o) break;
        }
        if (FD_ISSET(t, &r)) { int n = g_recvOrig(t, buf, sizeof buf, 0); if (n <= 0 || g_sendOrig(g, buf, n, 0) != n) break; }
    }
}
static void udp_carry(SOCKET g, SOCKET u, DWORD cid, int ver, const unsigned char *pre, int plen);
// Relay 22: a native game whose node (x) this player can't reach - through his own nearest usable node, "DWR4" + x's address.
// 1 = carried (the game is over), 0 = no other node of ours answers (the nodes in *tried are skipped, each one tried is added)
static int native_chain(SOCKET g, TunArg *ta, DWORD *tried) {
    int x = ta->node; unsigned char pre[10]; SOCKET u; DWORD cid; int ver; DWORD t0 = GetTickCount();
    memcpy(pre, "DWR4", 4); memcpy(pre + 4, &g_hosts[x], 4); memcpy(pre + 8, &ta->relic.sin_port, 2);   // sin_port: already big-endian
    for (int i = nearest(*tried); i >= 0; i = nearest(*tried)) {
        *tried |= 1u << i;
        if ((ver = udp_open(g_hosts[i], &u, &cid))) {
            char xs[20]; snprintf(xs, sizeof xs, "%s", inet_ntoa(g_hosts[x]));
            logmsg("udp: connection %08lX to %s, chain on to the room's node %s (handshake %lu ms, %s)", cid, inet_ntoa(g_hosts[i]), xs,
                   GetTickCount() - t0, ver >= 2 ? "with copies" : "old gateway, no copies");
            udp_carry(g, u, cid, ver, pre, sizeof pre); return 1;
        }
        g_ms[i] = -1;
        logmsg("udp: %s does not answer over UDP", inet_ntoa(g_hosts[i]));
    }
    return 0;
}
static DWORD WINAPI tunnel(LPVOID pv) {
    TunArg ta = *(TunArg *)pv; free(pv);
    fd_set r; FD_ZERO(&r); FD_SET(ta.lst, &r); struct timeval tv = {30, 0};
    SOCKET g = select(0, &r, NULL, NULL, &tv) > 0 ? accept(ta.lst, NULL, NULL) : INVALID_SOCKET;
    closesocket(ta.lst);
    if (g == INVALID_SOCKET) { logmsg("udp: the game did not come to the local port"); return 0; }
    int one = 1; setsockopt(g, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
    u_long nb = 0; ioctlsocket(g, FIONBIO, &nb);
    SOCKET u; DWORD cid; DWORD tried = 0; DWORD t0 = GetTickCount(); int ver;
    if (ta.own == 2) {                                          // Relay 22: native - the room's node if it is usable, else a chain
        int x = ta.node;
        if (g_ms[x] == -2) measure(x);
        if (g_ms[x] < 0) {
            logmsg("udp: the room's node %s is not usable from here (no answer or the stream does not get through) - through another of our nodes",
                   inet_ntoa(g_hosts[x]));
            tried |= 1u << x;
            if (native_chain(g, &ta, &tried)) return 0;
            tried &= ~(1u << x);                                // no other node either: the room's node itself after all
        }
    }
    for (int i = ta.node; i >= 0; i = ta.own == 1 ? nearest(tried) : -1) {
        tried |= 1u << i;
        if ((ver = udp_open(g_hosts[i], &u, &cid))) {
            logmsg("udp: connection %08lX to %s%s (handshake %lu ms, %s)", cid, inet_ntoa(g_hosts[i]), ta.own == 2 ? ", native" : "",
                   GetTickCount() - t0, ver >= 2 ? "with copies" : "old gateway, no copies");
            udp_carry(g, u, cid, ver, NULL, 0); return 0;
        }
        g_ms[i] = -1;                                           // until the next ping round
        logmsg("udp: %s does not answer over UDP", inet_ntoa(g_hosts[i]));
        if (ta.own == 2 && !(tried & ~(1u << i)) && native_chain(g, &ta, &tried)) return 0;   // Relay 22: native, its node silent - a chain
    }
    if (ta.own != 1) {                                          // a Relic game: straight to Relic, as without the add-on; native: TCP to the node
        SOCKET t = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (g_connectOrig(t, (struct sockaddr *)&ta.relic, sizeof ta.relic) == 0) {
            logmsg(ta.own == 2 ? "udp: %s:%u over TCP as the game wanted (no UDP answer)" : "relay: no forwarding server answers - %s:%u straight to Relic",
                   inet_ntoa(ta.relic.sin_addr), ntohs(ta.relic.sin_port));
            setsockopt(t, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one); pipe_relic(g, t, ta.plen);
        } else logmsg("relay: %s does not take the connection either (%d)", inet_ntoa(ta.relic.sin_addr), WSAGetLastError());
        closesocket(t);
    } else logmsg("udp: none of our servers answers over UDP - this game can't connect");
    closesocket(g); return 0;
}
// copies of the newest unacknowledged packets below number `below` into pk + o while the datagram stays within budget (its own
// bytes `own` counted); returns the new end, k = how many
static int u_copies(unsigned char *pk, int o, UPkt *snd, DWORD sbase, DWORD below, int own, int *k) {
    *k = 0;
    for (DWORD p = below; p-- > sbase && below - p <= 16 && *k < U_COPIES; ) {
        UPkt *x = &snd[p % U_WIN]; if (x->n <= 0) continue;     // acknowledged out of order already
        if (o + 6 + x->n + own > U_BUDGET) continue;
        unsigned short L = (unsigned short)x->n; memcpy(pk + o, &p, 4); memcpy(pk + o + 4, &L, 2); memcpy(pk + o + 6, x->d, x->n);
        o += 6 + x->n; (*k)++;
    }
    return o;
}
// one packet with data: DATA (version 1) or DATA2 with copies of the newest unacknowledged ones before it (version 2)
static int u_build(unsigned char *pk, DWORD cid, int ver, UPkt *snd, DWORD sbase, DWORD sq, DWORD rnext, DWORD sk, unsigned *copies) {
    UPkt *q = &snd[sq % U_WIN];
    u_hdr(pk, ver >= 2 ? U_DATA2 : U_DATA, cid); memcpy(pk + 7, &sq, 4); memcpy(pk + 11, &rnext, 4); memcpy(pk + 15, &sk, 4);
    if (ver < 2) { memcpy(pk + 19, q->d, q->n); return 19 + q->n; }
    int k, o = u_copies(pk, 20, snd, sbase, sq, q->n, &k);
    pk[19] = (unsigned char)k; *copies += k;
    memcpy(pk + o, q->d, q->n); return o + q->n;
}
// an ACK; version 2: an ACK2 with copies of the newest unacknowledged packets when there are any
static int u_ackpk(unsigned char *pk, DWORD cid, int ver, UPkt *snd, DWORD sbase, DWORD snext, DWORD rnext, DWORD sk, unsigned *copies) {
    u_hdr(pk, U_ACK, cid); memcpy(pk + 7, &rnext, 4); memcpy(pk + 11, &sk, 4);
    if (ver < 2) return 15;
    int k, o = u_copies(pk, 16, snd, sbase, snext, 0, &k);
    if (!k) return 15;
    pk[2] = U_ACK2; pk[15] = (unsigned char)k; *copies += k; return o;
}
static void udp_carry(SOCKET g, SOCKET u, DWORD cid, int ver, const unsigned char *pre, int plen) {
    unsigned char pk[U_BUDGET + 64], ak[U_BUDGET + 64];
    UPkt *snd = calloc(U_WIN, sizeof(UPkt)); UPkt *rcv = calloc(U_WIN, sizeof(UPkt));   // ring buffers by packet number % U_WIN
    char *rhave = calloc(U_WIN, 1);
    DWORD snext = 0, sbase = 0, rnext = 0, lastIn = GetTickCount(), lastOut = 0, lastAck = (DWORD)-1; int dup = 0;
    double srtt = 150, rttvar = 50; unsigned resent = 0, sentn = 0, gotn = 0, gapn = 0, copies = 0, saved = 0; int gameOpen = 1;
    #define SACK() ({ DWORD m_ = 0; for (int i_ = 0; i_ < 32; i_++) if (rhave[(rnext + 1 + i_) % U_WIN]) m_ |= 1u << i_; m_; })
    if (plen > 0 && plen <= U_MAXP) {                            // Relay 22: our preamble (DWR4) is the stream's first packet
        UPkt *q = &snd[0]; memcpy(q->d, pre, plen); q->n = plen; q->first = q->last = GetTickCount(); q->sends = 1;
        int len = u_build(pk, cid, ver, snd, sbase, snext, rnext, 0, &copies); send(u, (char *)pk, len, 0); snext++; sentn++; lastOut = q->first;
    }
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
                int len = u_build(pk, cid, ver, snd, sbase, snext, rnext, SACK(), &copies);
                send(u, (char *)pk, len, 0); snext++; sentn++; lastOut = now;
            }
        }
        if (sel > 0 && FD_ISSET(u, &rs)) {                       // server -> game
            int n = recv(u, (char *)pk, sizeof pk, 0);
            if (n >= 7 && pk[0] == 'D' && pk[1] == 'U' && !memcmp(pk + 3, &cid, 4)) {
                lastIn = now; int t = pk[2]; DWORD ack = 0, sack = 0;
                if (t == U_FIN) { logmsg("udp: %08lX closed by the server", cid); break; }
                if ((t == U_DATA && n >= 19) || (t == U_DATA2 && n >= 20) || (t == U_ACK && n >= 15) || (t == U_ACK2 && n >= 16)) {
                    int o = t == U_ACK || t == U_ACK2 ? 7 : 11; memcpy(&ack, pk + o, 4); memcpy(&sack, pk + o + 4, 4);
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
                struct { DWORD seq; unsigned char *p; int len; } in[U_COPIES + 2]; int nin = 0, ncopy = 0;
                if (t == U_DATA && n >= 19) { memcpy(&in[0].seq, pk + 7, 4); in[0].p = pk + 19; in[0].len = n - 19; nin = 1; }
                if ((t == U_DATA2 && n >= 20) || (t == U_ACK2 && n >= 16)) {   // the copies first (older numbers), then the packet's own bytes
                    int k = pk[t == U_ACK2 ? 15 : 19], o = t == U_ACK2 ? 16 : 20;
                    int bad = 0;
                    for (int i = 0; i < k; i++) {                    // more copies than we keep: skipped, but the offset follows them
                        unsigned short L; if (o + 6 > n) { bad = 1; break; } memcpy(&L, pk + o + 4, 2); if (L > U_MAXP || o + 6 + L > n) { bad = 1; break; }
                        if (nin < U_COPIES + 1) { memcpy(&in[nin].seq, pk + o, 4); in[nin].p = pk + o + 6; in[nin].len = L; nin++; }
                        o += 6 + L;
                    }
                    ncopy = nin;
                    for (int a = 0, b = ncopy - 1; a < b; a++, b--) { __typeof__(in[0]) x = in[a]; in[a] = in[b]; in[b] = x; }   // oldest first
                    if (t == U_DATA2 && !bad && n - o <= U_MAXP) { memcpy(&in[nin].seq, pk + 7, 4); in[nin].p = pk + o; in[nin].len = n - o; nin++; }
                }
                int took = 0;
                for (int j = 0; j < nin; j++) {
                    DWORD seq = in[j].seq; int len = in[j].len;
                    if (seq >= rnext && seq - rnext < U_WIN && !rhave[seq % U_WIN]) {
                        UPkt *q = &rcv[seq % U_WIN]; memcpy(q->d, in[j].p, len); q->n = len; rhave[seq % U_WIN] = 1; took++;
                        if (j < ncopy) saved++;                      // its own packet was lost (or is late): the copy filled it
                        else if (seq != rnext) gapn++;
                        while (rhave[rnext % U_WIN]) {
                            UPkt *x = &rcv[rnext % U_WIN]; rhave[rnext % U_WIN] = 0;
                            for (int off = 0; off < x->n; ) { int w = g_sendOrig(g, (char *)x->d + off, x->n - off, 0); if (w <= 0) { gameOpen = 0; break; } off += w; }
                            rnext++; gotn++;
                        }
                    }
                }
                if (t == U_DATA || t == U_DATA2 || (t == U_ACK2 && took)) {   // an ACK2 only when its copy filled something
                    int len = u_ackpk(ak, cid, ver, snd, sbase, snext, rnext, SACK(), &copies); send(u, (char *)ak, len, 0); lastOut = now;
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
                int len = u_build(pk, cid, ver, snd, sbase, sq, rnext, SACK(), &copies);
                send(u, (char *)pk, len, 0); q->last = now; q->sends++; resent++; lastOut = now;
            }
        }
        while (sbase < snext && snd[sbase % U_WIN].n <= 0) { snd[sbase % U_WIN].n = 0; sbase++; }   // acked out of order earlier
        if (!gameOpen && sbase == snext) break;                  // the game closed and everything got there
        if (now - lastOut > 1000) { int len = u_ackpk(ak, cid, ver, snd, sbase, snext, rnext, SACK(), &copies); send(u, (char *)ak, len, 0); lastOut = now; }
    }
    for (int i = 0; i < 3; i++) { u_hdr(pk, U_FIN, cid); send(u, (char *)pk, 7, 0); }
    logmsg("udp: %08lX over: sent %u, got %u, sent again %u, out of order in %u, round trip %d ms; copies sent %u, lost ones filled by a copy %u",
           cid, sentn, gotn, resent, gapn, (int)srtt, copies, saved);
    closesocket(u); closesocket(g); free(snd); free(rcv); free(rhave);
}
static int tunnel_port(int node, int own, int plen, const struct sockaddr_in *relic) {   // a local port for one game connection
    SOCKET l = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (l == INVALID_SOCKET) return 0;
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); int al = sizeof a;
    if (bind(l, (struct sockaddr *)&a, sizeof a) || listen(l, 1) || getsockname(l, (struct sockaddr *)&a, &al)) { closesocket(l); return 0; }
    TunArg *ta = malloc(sizeof *ta); ta->lst = l; ta->relic = *relic; ta->node = node; ta->own = own; ta->plen = plen;
    HANDLE th = CreateThread(NULL, 0, tunnel, ta, 0, NULL); if (!th) { free(ta); closesocket(l); return 0; }
    CloseHandle(th); return ntohs(a.sin_port);
}

// ---------- the hooks ----------
// Relay 19 (release gate 03.11.2026): OwnLobby.dll (loaded first by winmm.dll) hooks DOW2.exe's own import of connect; patch_imports
// only takes entries that still point at ws2_32's connect, so the exe's entry - the one the game's battleserverrelay connects use -
// was never ours: with OwnLobby on, Relay did nothing for the game (stands: a classic automatch with own_server true played on
// Relic's relay, no "relay:" line). Now the exe's entry is hooked whatever is there and chains to it (exe_connect_hook -> g_exePrev).
static ConnectFn g_exePrev;
static int relay_connect(ConnectFn pass, SOCKET sock, const struct sockaddr *name, int len);
static int WSAAPI connect_hook(SOCKET sock, const struct sockaddr *name, int len) { return relay_connect(g_connectOrig, sock, name, len); }
static int WSAAPI exe_connect_hook(SOCKET sock, const struct sockaddr *name, int len) { return relay_connect(g_exePrev, sock, name, len); }
static int relay_connect(ConnectFn pass, SOCKET sock, const struct sockaddr *name, int len) {
    if (pend_find(sock) >= 0) {                                    // a reused socket handle must not get a stale preamble (lock only then)
        EnterCriticalSection(&g_pk);
        for (int i = 0; i < 64; i++) if (g_pend[i].s == sock) { g_pend[i].s = 0; InterlockedDecrement(&g_npend); }
        LeaveCriticalSection(&g_pk);
    }
    if (!name || name->sa_family != AF_INET || len < (int)sizeof(struct sockaddr_in)) return pass(sock, name, len);
    const struct sockaddr_in *a = (const struct sockaddr_in *)name; unsigned short p = ntohs(a->sin_port);
    // Relay 21: only the game's TCP connections to Relic's relays (27000-27099) and to our nodes' battle port are ours; anything
    // else - the chat and API of the launcher / ladder / our lobby server (443, 47443), Relic's lobby over HTTPS, a UDP socket
    // that connect()s (Steam's networking, a ping) - goes on untouched, address and all
    if (p != g_port && (p < 27000 || p > 27099)) return pass(sock, name, len);
    { int ty = 0, tl = sizeof ty; if (getsockopt(sock, SOL_SOCKET, SO_TYPE, (char *)&ty, &tl) || ty != SOCK_STREAM) return pass(sock, name, len); }
    if (p == g_port && node_of(a->sin_addr) >= 0) {              // Relay 20: native - our lobby server gave one of our nodes as the room's relay
        int lp = tunnel_port(node_of(a->sin_addr), 2, 0, a);       // that node over UDP, TCP to it when UDP does not answer; no preamble
        if (!lp) { logmsg("relay: the UDP tunnel did not start - %s:%u over TCP", inet_ntoa(a->sin_addr), p); return pass(sock, name, len); }
        logmsg("relay: %s:%u (native: our lobby server's room) -> over UDP", inet_ntoa(a->sin_addr), p);
        struct sockaddr_in b = *a; b.sin_addr.s_addr = htonl(INADDR_LOOPBACK); b.sin_port = htons((unsigned short)lp);
        return pass(sock, (const struct sockaddr *)&b, sizeof b);
    }
    if (p < 27000 || p > 27099) return pass(sock, name, len);   // not Relic's battleserverrelay
    int own = flag("DOW2_RELAY_OWN"), fwd = flag("DOW2_RELAY_FWD"), automatch = flag("DOW2_RELAY_USE");
    char ip[32]; snprintf(ip, sizeof ip, "%s", inet_ntoa(a->sin_addr));
    int node = node_of(a->sin_addr), straight = 0;
    // the game asks for one of OUR addresses as if it were Relic's relay (our lobby server's rooms; seen 01.11 after a match on
    // ours): our server in any case - its battle port, never the address itself. In our automatch: the matchmaker's node.
    if (node >= 0) { straight = 1; if (own && automatch) { int m = own_node(); if (m >= 0) node = m; } }
    else if (own && automatch) { node = own_node(); straight = node >= 0; if (!straight) logmsg("relay: none of ours answers over UDP - %s:%u not on our server", ip, p); }
    if (!straight) {
        node = fwd ? nearest(0) : -1;
        if (node < 0) {
            static DWORD said; if (GetTickCount() - said > 60000) { said = GetTickCount();
                logmsg(fwd ? "relay: no forwarding server answers - %s:%u straight to Relic" : "relay: %s:%u on Relic's servers%s", ip, p,
                       own && !fwd ? " (not our automatch; forwarding is off)" : ""); }
            return pass(sock, name, len);
        }
    }
    int players = 0; char ev[8];
    if (GetEnvironmentVariableA("DOW2_LOBBY_PLAYERS", ev, sizeof ev)) players = atoi(ev);
    if (players < 0 || players > 16) players = 0;
    int plen = straight ? 11 : 10;
    int lp = tunnel_port(node, straight, plen, a);
    if (!lp) { logmsg("relay: the UDP tunnel did not start - %s:%u straight to Relic", ip, p); return pass(sock, name, len); }
    EnterCriticalSection(&g_pk);
    int k = -1; for (int i = 0; i < 64; i++) if (!g_pend[i].s) { k = i; break; }
    if (k >= 0) { memcpy(g_pend[k].pre, straight ? "DWR2" : "DWR3", 4); memcpy(g_pend[k].pre + 4, &a->sin_addr, 4);
                  g_pend[k].pre[8] = (unsigned char)(p >> 8); g_pend[k].pre[9] = (unsigned char)p; g_pend[k].pre[10] = (unsigned char)players; g_pend[k].len = plen;
                  MemoryBarrier(); g_pend[k].s = sock; InterlockedIncrement(&g_npend); }   // the bytes first, then the socket: a lock-free reader never sees half
    LeaveCriticalSection(&g_pk);
    if (k < 0) logmsg("relay: too many connections at once - the preamble is missing on this one");
    logmsg("relay: %s:%u -> ours %s over UDP (%s)", ip, p, inet_ntoa(g_hosts[node]),
           straight ? "straight: the game is on our server" : "forwarding: a Relic game, our server passes it on");
    struct sockaddr_in b = *a; b.sin_addr.s_addr = htonl(INADDR_LOOPBACK); b.sin_port = htons((unsigned short)lp);
    return pass(sock, (const struct sockaddr *)&b, sizeof b);
}

static void send_preamble(SOCKET s) {                           // before the first game bytes on a redirected socket
    if (pend_find(s) < 0) return;                                 // Relay 21: not a redirected socket - no lock (Relay 20 took g_pk on every send)
    unsigned char pre[11]; int have = 0, plen = 10;
    EnterCriticalSection(&g_pk);
    for (int i = 0; i < 64; i++) if (g_pend[i].s == s) { memcpy(pre, g_pend[i].pre, 11); plen = g_pend[i].len; g_pend[i].s = 0; InterlockedDecrement(&g_npend); have = 1; break; }
    LeaveCriticalSection(&g_pk);
    if (!have) return;
    for (int off = 0, tries = 0; off < plen; ) {                  // the socket is fresh and local: the bytes go at once
        int n = g_sendOrig(s, (const char *)pre + off, plen - off, 0);
        if (n > 0) { off += n; continue; }
        if (WSAGetLastError() != WSAEWOULDBLOCK || ++tries > 200) { logmsg("relay: preamble not sent (%d)", WSAGetLastError()); return; }
        Sleep(10);
    }
}
static int WSAAPI send_hook(SOCKET s, const char *buf, int len, int flags) { send_preamble(s); return g_sendOrig(s, buf, len, flags); }
static int WSAAPI wsasend_hook(SOCKET s, LPWSABUF b, DWORD n, LPDWORD sent, DWORD fl, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cr) {
    send_preamble(s); return g_wsaSendOrig(s, b, n, sent, fl, ov, cr);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    char mx[64]; wsprintfA(mx, "Local\\dow2patch_%s_%lu", ME, GetCurrentProcessId());
    CreateMutexA(NULL, FALSE, mx); if (GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
    SetEnvironmentVariableA(MY_FLAG, "1");                      // this tick is on (read at every connect by whichever DLL hooks)
#ifndef RELAY_FWD
    SetEnvironmentVariableA("DOW2_RELAY", "9c");                // for MMFind: we may go on our server; the matchmaker may mix us with Relic players
#endif
    wsprintfA(mx, "Local\\dow2patch_RelayCore_%lu", GetCurrentProcessId());
    CreateMutexA(NULL, FALSE, mx); if (GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;   // the other one carries the connections
    InitializeCriticalSection(&g_lk); InitializeCriticalSection(&g_pk);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0;
    _snwprintf(g_ini, MAX_PATH, L"%lsRelay.ini", g_dir);
    { wchar_t p[MAX_PATH], o[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsRelay.log", g_dir); _snwprintf(o, MAX_PATH, L"%lsRelay.log.old", g_dir);
      WIN32_FILE_ATTRIBUTE_DATA a; if (GetFileAttributesExW(p, GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > (1u << 20))) MoveFileExW(p, o, MOVEFILE_REPLACE_EXISTING); }
    g_port = GetPrivateProfileIntW(L"Relay", L"Port", 47100, g_ini);
    const char *bh[] = {"15.220.68.107", "116.202.106.150", "83.217.192.34"};   // Poland (AWS), Germany (Hetzner), Moscow (Timeweb)
    for (int i = 0; i < 3; i++) g_hosts[g_nHosts++].s_addr = inet_addr(bh[i]);
    merge_nodes(); ini_changed();
    HMODULE ws = GetModuleHandleA("ws2_32.dll"); if (!ws) ws = LoadLibraryA("ws2_32.dll");
    void *real = ws ? (void *)GetProcAddress(ws, "connect") : NULL; if (!real) return TRUE;
    g_connectOrig = (ConnectFn)real;
    g_sendOrig = (SendFn)GetProcAddress(ws, "send"); g_wsaSendOrig = (WSASendFn)GetProcAddress(ws, "WSASend"); g_recvOrig = (RecvFn)GetProcAddress(ws, "recv");
    int n = patch_imports(real, (void *)connect_hook);
    int nx = 0;                                                   // DOW2.exe's own import, if another add-on (OwnLobby) hooked it first
    { unsigned char *b = (unsigned char *)GetModuleHandleA(NULL); IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
      IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
      for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(b + dir.VirtualAddress); dir.VirtualAddress && imp->Name; imp++) {
          if (lstrcmpiA((const char *)(b + imp->Name), "ws2_32.dll")) continue;
          IMAGE_THUNK_DATA *ot = (IMAGE_THUNK_DATA *)(b + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
          IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(b + imp->FirstThunk);
          for (; ot->u1.AddressOfData; ot++, t++) {
              int match = IMAGE_SNAP_BY_ORDINAL(ot->u1.Ordinal) ? IMAGE_ORDINAL(ot->u1.Ordinal) == 4
                        : !strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(b + ot->u1.AddressOfData))->Name, "connect");
              void *cur = (void *)t->u1.Function;
              if (!match || cur == (void *)connect_hook || cur == (void *)exe_connect_hook || cur == real) continue;
              DWORD o; g_exePrev = (ConnectFn)cur; VirtualProtect(&t->u1.Function, 4, PAGE_READWRITE, &o);
              t->u1.Function = (DWORD)exe_connect_hook; VirtualProtect(&t->u1.Function, 4, o, &o); nx++;
          }
      } }
    n += nx;
    int ns = g_sendOrig ? patch_imports((void *)g_sendOrig, (void *)send_hook) : 0;
    int nw = g_wsaSendOrig ? patch_imports((void *)g_wsaSendOrig, (void *)wsasend_hook) : 0;
    logmsg("Relay 22 start (%s.dll carries the connections; our servers: %s, forwarding: %s - each tick is read at connect time; "
           "UDP only, %d node(s)), connect hooked in %d import(s)%s, send %d, WSASend %d%s", ME,
           flag("DOW2_RELAY_OWN") ? "on" : "off (yet)", flag("DOW2_RELAY_FWD") ? "on" : "off (yet)", g_nHosts, n, nx ? " (the game's own one chained after another add-on)" : "", ns, nw,
           n ? "" : " - another add-on (automatch) already routes the relay, staying idle");
    if (n) CloseHandle(CreateThread(NULL, 0, ping_loop, NULL, 0, NULL));
    return TRUE;
}
