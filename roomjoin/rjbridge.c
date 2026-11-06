// rjbridge.exe - RoomJoin's pipe for a launcher outside Wine (Linux: the launcher is native, the game runs under Proton; Wine's
// named pipes live in its wineserver and a native process can't open them). The launcher starts this program through the same
// Proton, in the game's prefix (= the game's wineserver), with its stdin / stdout as pipes; it connects to
// \\.\pipe\EliteRoomJoin-<pid> of the running game and passes bytes both ways: stdin -> the pipe, the pipe -> stdout. The
// protocol is RoomJoin's own (one JSON object per line), nothing is added or changed.
//   rjbridge.exe [pid]     no pid: every DOW2.exe, the first whose pipe opens
// Ends when the pipe closes (the game ended) or stdin ends (the launcher let go). No pipe: one line
// {"ev":"bridge","error":"no_pipe"} (or "no_game") on stdout and exit code 2.
// Shipped with the RoomJoin add-on (Elite_DLLs/rjbridge.exe); built from this file alone.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600                 // GetNamedPipeServerProcessId (Vista+) with older mingw headers
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MMFind 0.46: the same bridge for MMFind's pipe (launcher rooms) - built as mmbridge.exe with -DPIPE_PREFIX=L"EliteMMFind"
#ifndef PIPE_PREFIX
#define PIPE_PREFIX L"EliteRoomJoin"
#endif
static HANDLE g_pipe, g_out;
static void out(const char *s, DWORD n) { DWORD w; while (n) { if (!WriteFile(g_out, s, n, &w, NULL) || !w) ExitProcess(0); s += w; n -= w; } }
static DWORD WINAPI from_launcher(LPVOID _) {                         // stdin -> the pipe (its own OVERLAPPED: the main thread reads it)
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE), ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    char b[4096]; DWORD n;
    while (ReadFile(in, b, sizeof b, &n, NULL) && n) {
        OVERLAPPED o; memset(&o, 0, sizeof o); o.hEvent = ev; ResetEvent(ev); DWORD w = 0;
        if (!WriteFile(g_pipe, b, n, &w, &o) && (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult(g_pipe, &o, &w, TRUE))) break;
    }
    ExitProcess(0);                                                    // the launcher let go (or the pipe broke)
    return 0;
}
static HANDLE open_pipe(DWORD pid) {
    wchar_t name[64]; _snwprintf(name, 64, L"\\\\.\\pipe\\" PIPE_PREFIX L"-%lu", pid); name[63] = 0;
    for (int i = 0; i < 20; i++) {
        HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            ULONG server = 0;                                          // the game itself serves it, nobody under its name
            if (GetNamedPipeServerProcessId(h, &server) && server != pid) { CloseHandle(h); return INVALID_HANDLE_VALUE; }
            return h;
        }
        if (GetLastError() != ERROR_PIPE_BUSY) return INVALID_HANDLE_VALUE;
        WaitNamedPipeW(name, 200);
    }
    return INVALID_HANDLE_VALUE;
}
int main(int argc, char **argv) {
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD pids[16]; int np = 0;
    if (argc > 1) pids[np++] = strtoul(argv[1], NULL, 10);
    else {
        HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32 e; e.dwSize = sizeof e;
        if (s != INVALID_HANDLE_VALUE) {
            for (BOOL ok = Process32First(s, &e); ok && np < 16; ok = Process32Next(s, &e)) if (!_stricmp(e.szExeFile, "DOW2.exe")) pids[np++] = e.th32ProcessID;
            CloseHandle(s);
        }
    }
    g_pipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < np && g_pipe == INVALID_HANDLE_VALUE; i++) g_pipe = open_pipe(pids[i]);
    if (g_pipe == INVALID_HANDLE_VALUE) {
        const char *m = np ? "{\"ev\":\"bridge\",\"error\":\"no_pipe\"}\n" : "{\"ev\":\"bridge\",\"error\":\"no_game\"}\n";
        out(m, (DWORD)strlen(m)); return 2;
    }
    HANDLE t = CreateThread(NULL, 64 * 1024, from_launcher, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL); if (t) CloseHandle(t);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); char b[4096];
    for (;;) {                                                         // the pipe -> stdout
        OVERLAPPED o; memset(&o, 0, sizeof o); o.hEvent = ev; ResetEvent(ev); DWORD n = 0;
        if (!ReadFile(g_pipe, b, sizeof b, &n, &o) && (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult(g_pipe, &o, &n, TRUE))) break;
        if (n) out(b, n);
    }
    ExitProcess(0);                                                    // the game closed its pipe
}
