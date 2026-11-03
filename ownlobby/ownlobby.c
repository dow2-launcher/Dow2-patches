// OwnLobby.dll - EXPERIMENT (owner 02.11.2026: the automatch fully on our own servers). The game's lobby service (RLink: plain
// HTTPS GETs to dow2r-lobby.reliclink.com, curl + OpenSSL inside DOW2.exe 3.19) goes to OUR lobby server (dow2-ladder repo,
// lobby/own_lobby.py on rts.fintebtc.de:47443). It hosts the private rooms (the fast automatch makes those) and their matches
// itself - the room's relay is our game server, the result goes to our ladder - and passes everything else on to Relic as it is
// (login, chat, friends, Relic's custom games), so with this DLL the game still sees Relic's world.
//
// 1. Trust. The game trusts exactly one root, "RelicLink Root CA" (a PEM string in .rdata 0x010A8628). RLink's config filler
//    0x4C8C90 (one call, from 0x4C8E78) puts {count 1, certs[0] = that PEM} into the config; we detour the call and add OUR CA as
//    certs[1] (count 2; the parser 0x542C80 reads every entry). Relic stays trusted: if our server is away, the game goes straight
//    to Relic as without the add-on. The CA is read once when RLink starts - so this DLL must be loaded at process start
//    (the winmm.dll loader: ~50 ms); loaded later (Elite's injector, ~9 s) it does nothing at all and says so in its log.
// 2. Connect. DOW2.exe's own import of connect: port 443 to an address of Relic's lobby service -> our server, as long as our
//    server answers (probed every 20 s). Nothing else is touched. Settings: OwnLobby.ini [OwnLobby] Servers=host:port;host:port
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "ca_pem.h"

#define VERSION        "2"
#define RL_MANAGER     0x01335014      // the RLink manager (NULL until RLink starts)
#define RELIC_PEM      0x010A8628      // "RelicLink Root CA"
#define CFG_FILL       0x004C8C90      // RLink config filler (ESI = config): count +8, certs +0xC
#define CFG_CALL       0x004C8E78      // its one call (E8 rel32), in the RLink start 0x4C8D00
#define CFG_STORE      0x004C8CDE      // mov [esi+0xC], 0x010A8628 inside the filler (checked, not changed)
// 2 (03.11.2026): Moscow first - the mirror (83.217.192.34, TCP as is to rts) for players in Russia, whose streams to Europe stop
// after ~33 KB (DPI): with OwnLobby 1 their lobby calls hung on rts and Find a Game did not open without a VPN
#define DEFAULT_SERVERS L"83.217.192.34:47443;rts.fintebtc.de:47443"
#define STREAM_NEED    60000          // a server is used only if its stream check (port + 2: 64 KB on connect) brings this much
#define LOBBY_HOST     "dow2r-lobby.reliclink.com"

typedef int (WSAAPI *ConnectFn)(SOCKET, const struct sockaddr *, int);
static ConnectFn g_prev;
static wchar_t g_dir[MAX_PATH];
static CRITICAL_SECTION g_lk;
static struct { char host[96]; unsigned short port; DWORD checked; int ok; unsigned long ip; } g_srv[4]; static int g_nsrv;
static int g_streamCheck = 1;               // OwnLobby.ini [OwnLobby] StreamCheck=0: a connect is enough (as OwnLobby 1)
static volatile unsigned long g_target; static volatile unsigned short g_targetPort;   // the server that answered last (0 = none)
static unsigned long g_relic[24]; static int g_nrelic;                                  // Relic's lobby addresses
static HANDLE g_wake;

static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsOwnLobby.log", g_dir);
    EnterCriticalSection(&g_lk);
    FILE *f = _wfopen(p, L"a");
    if (f) {
        SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d.%02d %02d:%02d:%02d.%03d ", t.wDay, t.wMonth, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
    }
    LeaveCriticalSection(&g_lk);
}

// ---------------- 1. our CA next to Relic's ----------------
// cdecl-or-stdcall agnostic: the caller's return address is put aside, the filler runs on the caller's own arguments, EAX and
// ESI, then certs[1] = ours if the filler did what we know (count 1, certs[0] = Relic's)
void *g_cfgRet, *g_cfgOrig = (void *)CFG_FILL; const char *g_ourPem = CA_PEM; volatile int g_cfgDone;
__asm__(".text\n.globl _ol_cfg_stub\n_ol_cfg_stub:\n"
        "  popl _g_cfgRet\n"
        "  call *_g_cfgOrig\n"
        "  cmpl $1, 0x8(%esi)\n"
        "  jne 1f\n"
        "  cmpl $0x010A8628, 0xC(%esi)\n"
        "  jne 1f\n"
        "  movl _g_ourPem, %ecx\n"
        "  movl %ecx, 0x10(%esi)\n"
        "  movl $2, 0x8(%esi)\n"
        "  movl $1, _g_cfgDone\n"
        "1:\n"
        "  pushl _g_cfgRet\n"
        "  ret\n");
void ol_cfg_stub(void);

static int add_our_ca(void) {
    const unsigned char *call = (const unsigned char *)CFG_CALL, *store = (const unsigned char *)CFG_STORE;
    static const unsigned char want_store[] = {0xC7, 0x46, 0x0C, 0x28, 0x86, 0x0A, 0x01, 0x83, 0x46, 0x08, 0x01};
    if (IsBadReadPtr(call, 5) || IsBadReadPtr(store, sizeof want_store) || IsBadReadPtr((void *)RELIC_PEM, 32)) return 0;
    if (call[0] != 0xE8 || (unsigned)(CFG_CALL + 5 + *(int *)(call + 1)) != CFG_FILL) return 0;
    if (memcmp(store, want_store, sizeof want_store) || memcmp((void *)RELIC_PEM, "-----BEGIN CERTIFICATE-----", 27)) return 0;
    int rel = (int)((unsigned)ol_cfg_stub - (CFG_CALL + 5)); DWORD o;
    if (!VirtualProtect((void *)(CFG_CALL + 1), 4, PAGE_EXECUTE_READWRITE, &o)) return 0;
    *(int *)(CFG_CALL + 1) = rel; VirtualProtect((void *)(CFG_CALL + 1), 4, o, &o);
    FlushInstructionCache(GetCurrentProcess(), (void *)CFG_CALL, 5);
    return 1;
}

// ---------------- 2. Relic's lobby connections to our server ----------------
static int is_relic(unsigned long ip) { for (int i = 0; i < g_nrelic; i++) if (g_relic[i] == ip) return 1; return 0; }
static void add_relic(unsigned long ip) { if (ip && ip != INADDR_NONE && !is_relic(ip) && g_nrelic < 24) g_relic[g_nrelic++] = ip; }

static int WSAAPI connect_hook(SOCKET s, const struct sockaddr *name, int len) {
    if (name && name->sa_family == AF_INET && len >= (int)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)name;
        if (ntohs(a->sin_port) == 443) {
            unsigned long t = g_target; unsigned short tp = g_targetPort;
            if (is_relic(a->sin_addr.s_addr)) {
                if (t && g_cfgDone) {
                    struct sockaddr_in b = *a; b.sin_addr.s_addr = t; b.sin_port = htons(tp);
                    static DWORD said; if (GetTickCount() - said > 60000) { said = GetTickCount(); char ip[20]; strcpy(ip, inet_ntoa(a->sin_addr));
                        struct in_addr x; x.s_addr = t; logmsg("lobby: Relic %s:443 -> ours %s:%u", ip, inet_ntoa(x), tp); }
                    return g_prev(s, (struct sockaddr *)&b, sizeof b);
                }
                static DWORD said2; if (GetTickCount() - said2 > 60000) { said2 = GetTickCount(); logmsg("lobby: %s - straight to Relic", !g_cfgDone ? "our CA is not in (loaded too late)" : "none of our lobby servers answers"); }
            } else SetEvent(g_wake);                         // a 443 we don't know: maybe Relic moved - look the name up again
        }
    }
    return g_prev(s, name, len);
}
static int hook_exe_connect(void) {        // DOW2.exe's own import of ws2_32 connect (curl is linked into the exe); chains to what is there
    unsigned char *b = (unsigned char *)GetModuleHandleA(NULL); IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; int n = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(b + dir.VirtualAddress); imp->Name; imp++) {
        if (lstrcmpiA((const char *)(b + imp->Name), "ws2_32.dll")) continue;
        IMAGE_THUNK_DATA *ot = (IMAGE_THUNK_DATA *)(b + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(b + imp->FirstThunk);
        for (; ot->u1.AddressOfData; ot++, t++) {
            int match = IMAGE_SNAP_BY_ORDINAL(ot->u1.Ordinal) ? IMAGE_ORDINAL(ot->u1.Ordinal) == 4
                      : !strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(b + ot->u1.AddressOfData))->Name, "connect");
            if (!match) continue;
            DWORD o; g_prev = (ConnectFn)t->u1.Function; VirtualProtect(&t->u1.Function, 4, PAGE_READWRITE, &o);
            t->u1.Function = (DWORD)connect_hook; VirtualProtect(&t->u1.Function, 4, o, &o); n++;
        }
    }
    return n;
}

// a TCP connect within ms: 1 = answers
static int probe(unsigned long ip, unsigned short port, int ms) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (s == INVALID_SOCKET) return 0;
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = ip; a.sin_port = htons(port);
    int ok = 0;
    if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) ok = 1;   // the real ws2_32 connect: our hook is only on the exe's import
    else if (WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set w, e; FD_ZERO(&w); FD_ZERO(&e); FD_SET(s, &w); FD_SET(s, &e); struct timeval tv = {ms / 1000, (ms % 1000) * 1000};
        if (select(0, NULL, &w, &e, &tv) > 0 && FD_ISSET(s, &w)) ok = 1;
    }
    closesocket(s); return ok;
}
// the stream check (OwnLobby 2): connect to port + 2 of the lobby server, which sends 64 KB and closes; bytes that came in ms
static int stream_bytes(unsigned long ip, unsigned short port, int ms) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); if (s == INVALID_SOCKET) return -1;
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_addr.s_addr = ip; a.sin_port = htons(port);
    int got = -1; DWORD t0 = GetTickCount();
    if (connect(s, (struct sockaddr *)&a, sizeof a) == 0 || WSAGetLastError() == WSAEWOULDBLOCK) {
        fd_set w, e; FD_ZERO(&w); FD_ZERO(&e); FD_SET(s, &w); FD_SET(s, &e); struct timeval tv = {2, 500000};
        if (select(0, NULL, &w, &e, &tv) > 0 && FD_ISSET(s, &w)) {
            got = 0; char buf[8192];
            while (got < 65536) {
                int left = ms - (int)(GetTickCount() - t0); if (left <= 0) break;
                fd_set r; FD_ZERO(&r); FD_SET(s, &r); struct timeval tr = {left / 1000, (left % 1000) * 1000};
                if (select(0, &r, NULL, NULL, &tr) <= 0) break;
                int n = recv(s, buf, sizeof buf, 0); if (n <= 0) break;
                got += n;
            }
        }
    }
    closesocket(s); return got;
}
static unsigned long resolve(const char *host) {
    unsigned long ip = inet_addr(host); if (ip != INADDR_NONE) return ip;
    struct hostent *h = gethostbyname(host); return h && h->h_addr_list[0] ? *(unsigned long *)h->h_addr_list[0] : 0;
}
static DWORD WINAPI watcher(LPVOID _) {
    WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd);
    DWORD lastRelic = 0; unsigned long was = 1;
    for (;;) {
        if (!lastRelic || GetTickCount() - lastRelic > 300000) {           // Relic's lobby addresses (the game looks the same name up)
            lastRelic = GetTickCount();
            const char *names[] = {LOBBY_HOST, "test" LOBBY_HOST};
            for (int k = 0; k < 2; k++) { struct hostent *h = gethostbyname(names[k]);
                if (h) for (int i = 0; h->h_addr_list[i]; i++) add_relic(*(unsigned long *)h->h_addr_list[i]); }
        }
        unsigned long t = 0; unsigned short tp = 0; int which = -1;
        for (int i = 0; i < g_nsrv && !t; i++) {
            unsigned long ip = resolve(g_srv[i].host);
            if (!ip || !probe(ip, g_srv[i].port, 2500)) { g_srv[i].checked = 0; continue; }
            if (g_streamCheck) {                         // the stream: again every 5 min while it passes, every 60 s while it does not
                DWORD now = GetTickCount();
                if (!g_srv[i].checked || ip != g_srv[i].ip || now - g_srv[i].checked > (g_srv[i].ok ? 300000u : 60000u)) {
                    int got = stream_bytes(ip, (unsigned short)(g_srv[i].port + 2), 4000), ok = got >= STREAM_NEED;
                    if (ok != g_srv[i].ok || !g_srv[i].checked) logmsg("lobby server %s:%u: stream check %d of 65536 bytes in 4 s - %s", g_srv[i].host, g_srv[i].port, got,
                                                                     ok ? "usable" : got < 0 ? "no answer, not used" : "the stream stops (DPI?), not used");
                    g_srv[i].checked = now ? now : 1; g_srv[i].ok = ok; g_srv[i].ip = ip;
                }
                if (!g_srv[i].ok) continue;
            }
            t = ip; tp = g_srv[i].port; which = i;
        }
        g_targetPort = tp; g_target = t;
        if (t != was) { was = t; if (t) logmsg("lobby server %s:%u answers", g_srv[which].host, tp); else logmsg("no lobby server of ours answers - the game talks to Relic directly"); }
        WaitForSingleObject(g_wake, 20000);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID _) {
    if (r != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(NULL, exe, MAX_PATH); wchar_t *e = wcsrchr(exe, L'\\');
    if (!e || _wcsicmp(e + 1, L"DOW2.exe")) return TRUE;                 // only the game itself
    if (GetModuleHandleW(L"OwnLobby.dll") != h && GetModuleHandleW(L"OwnLobby.dll")) return TRUE;   // a second copy: the first one works
    static int once; if (once++) return TRUE;
    InitializeCriticalSection(&g_lk);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0;
    FILETIME c, x, k, u, now; GetProcessTimes(GetCurrentProcess(), &c, &x, &k, &u); GetSystemTimeAsFileTime(&now);
    unsigned ms = (unsigned)((((ULONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime) - ((ULONGLONG)c.dwHighDateTime << 32 | c.dwLowDateTime)) / 10000);
    if (IsBadReadPtr((void *)RL_MANAGER, 4) || *(void **)RL_MANAGER) {
        logmsg("OwnLobby " VERSION ": loaded %u ms after the start, RLink has started already - doing nothing (the early loader winmm.dll is not there or not loaded)", ms);
        return TRUE;
    }
    if (!add_our_ca()) { logmsg("OwnLobby " VERSION ": not the DOW2.exe 3.19 we know (RLink config) - doing nothing"); return TRUE; }
    wchar_t ini[MAX_PATH], v[512]; _snwprintf(ini, MAX_PATH, L"%lsOwnLobby.ini", g_dir);
    GetPrivateProfileStringW(L"OwnLobby", L"Servers", DEFAULT_SERVERS, v, 512, ini);
    char a[512]; WideCharToMultiByte(CP_ACP, 0, v, -1, a, sizeof a, NULL, NULL);
    for (char *t = strtok(a, ";, "); t && g_nsrv < 4; t = strtok(NULL, ";, ")) {
        char *p = strrchr(t, ':'); int port = 47443; if (p) { *p = 0; port = atoi(p + 1); }
        if (*t && port > 0 && port < 65536) { snprintf(g_srv[g_nsrv].host, sizeof g_srv[0].host, "%s", t); g_srv[g_nsrv++].port = (unsigned short)port; }
    }
    g_streamCheck = GetPrivateProfileIntW(L"OwnLobby", L"StreamCheck", 1, ini);
    static const char *known[] = {"100.56.91.69", "184.192.75.54", "44.194.141.226"};     // dow2r-lobby.reliclink.com, 02.11.2026
    for (int i = 0; i < 3; i++) add_relic(inet_addr(known[i]));
    int n = hook_exe_connect();
    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    CloseHandle(CreateThread(NULL, 0, watcher, NULL, 0, NULL));
    logmsg("OwnLobby " VERSION ": loaded %u ms after the start (RLink not started yet) - our CA added next to Relic's, connect hooked in %d import(s), %d server(s)%s", ms, n, g_nsrv, g_streamCheck ? ", each used only if its stream comes through" : ", StreamCheck=0");
    return TRUE;
}
