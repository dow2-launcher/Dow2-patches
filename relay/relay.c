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
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdarg.h>

typedef int (WSAAPI *ConnectFn)(SOCKET, const struct sockaddr *, int);
static ConnectFn g_connectOrig; static struct in_addr g_relayIp; static volatile int g_relayOn;
static int g_port = 47100;                                       // our battle server's port
static int g_backup = 1, g_fallback = 1, g_handHosts;   // g_handHosts: the player picked the server (launcher: Server)                         // Relay.ini switches, see the top
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

static int WSAAPI connect_hook(SOCKET sock, const struct sockaddr *name, int len) {
    if (g_relayOn && name && name->sa_family == AF_INET && len >= (int)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)name; unsigned short p = ntohs(a->sin_port);
        if (p >= 27000 && p <= 27099) {                           // Relic's battleserverrelay (any of their servers)
            char ip[32]; snprintf(ip, sizeof ip, "%s", inet_ntoa(a->sin_addr));
            char use[4] = "";
            if (g_fallback && !(GetEnvironmentVariableA("DOW2_RELAY_USE", use, sizeof use) && use[0] == '1')) {
                static DWORD said2; if (GetTickCount() - said2 > 60000) { said2 = GetTickCount(); logmsg("relay: %s:%u on Relic's servers (not an automatch where everyone has the add-on)", ip, p); }
                return g_connectOrig(sock, name, len);
            }
            if (!pick_relay()) {
                if (!g_fallback) relay_fail("our relay does not answer");
                static DWORD said; if (GetTickCount() - said > 60000) { said = GetTickCount(); logmsg("relay: none of ours answers - %s:%u on Relic's servers, as without the add-on", ip, p); }
                return g_connectOrig(sock, name, len);
            }
            int players = 0; char ev[8];
            if (GetEnvironmentVariableA("DOW2_LOBBY_PLAYERS", ev, sizeof ev)) players = atoi(ev);
            if (players < 0 || players > 16) players = 0;
            EnterCriticalSection(&g_pk);
            int k = -1; for (int i = 0; i < 64; i++) if (g_pend[i].s == sock || (k < 0 && !g_pend[i].s)) { k = i; if (g_pend[i].s == sock) break; }
            if (k >= 0) { g_pend[k].s = sock; memcpy(g_pend[k].pre, g_fallback ? "DWR2" : "DWR1", 4); memcpy(g_pend[k].pre + 4, &a->sin_addr, 4);
                          g_pend[k].pre[8] = (unsigned char)(p >> 8); g_pend[k].pre[9] = (unsigned char)p;
                          g_pend[k].pre[10] = (unsigned char)players; g_pend[k].len = g_fallback ? 11 : 10; }
            LeaveCriticalSection(&g_pk);
            if (k < 0) relay_fail("too many relay connections");
            EnterCriticalSection(&g_pk); for (int i = 0; i < 64; i++) if (!g_ours[i] || g_ours[i] == sock) { g_ours[i] = sock; break; } LeaveCriticalSection(&g_pk);
            struct sockaddr_in b = *a; b.sin_addr = g_relayIp; b.sin_port = htons((unsigned short)g_port);
            logmsg("relay: %s:%u -> ours %s:%d%s", ip, p, inet_ntoa(g_relayIp), g_port, g_fallback ? " (compatible: a lobby hosted elsewhere is passed on)" : "");
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
    logmsg("Relay 14 start (%d server(s), Relic fallback %s), connect hooked in %d import(s), send %d, WSASend %d, recv %d%s", g_nHosts, g_fallback ? "on" : "off", n, ns, nw, nr, n ? "" : " - another add-on (automatch) already routes the relay, staying idle");
    if (n && g_relayOn) { CloseHandle(CreateThread(NULL, 0, first_check, NULL, 0, NULL)); CloseHandle(CreateThread(NULL, 0, ping_loop, NULL, 0, NULL)); }
    return TRUE;
}
