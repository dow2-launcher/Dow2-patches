// rjpipe.exe - a test client of RoomJoin's pipe (RoomJoin 9+), what the launcher does, from a console (also under Wine:
// the same prefix = the same wineserver as the game).
//   rjpipe <pid|auto> <seconds> [line | sleep:<ms>]...
// auto = the first DOW2.exe. Every line is sent as it is (a JSON command, "\n" added); sleep:<ms> waits. Everything the game
// sends is printed with the time since the start, until <seconds> are over or the game closes the pipe.
//   rjpipe auto 3 "{\"cmd\":\"status\"}"
//   rjpipe auto 40 "{\"cmd\":\"join\",\"room\":123,\"name\":\"test\"}"
//   rjpipe auto 20 "{\"cmd\":\"join\",\"room\":123,\"name\":\"t\"}" sleep:3000 "{\"cmd\":\"cancel\"}"
// Build: i686-w64-mingw32-gcc -O2 -s -o rjpipe.exe rjpipe.c   (make build/rjpipe.exe)
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static DWORD find_game(void) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32 e; e.dwSize = sizeof e; DWORD pid = 0;
    if (s == INVALID_HANDLE_VALUE) return 0;
    for (BOOL ok = Process32First(s, &e); ok; ok = Process32Next(s, &e)) if (!_stricmp(e.szExeFile, "DOW2.exe")) { pid = e.th32ProcessID; break; }
    CloseHandle(s); return pid;
}
static DWORD t0;
static void pump(HANDLE h, DWORD ms) {                                // print what came, for ms
    DWORD end = GetTickCount() + ms; static char buf[4096]; static int n;
    while ((int)(end - GetTickCount()) > 0) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) { printf("[%6lu ms] the pipe closed (error %lu)\n", GetTickCount() - t0, GetLastError()); fflush(stdout); exit(0); }
        if (!avail) { Sleep(20); continue; }
        DWORD r = 0; if (avail > sizeof buf - 1 - n) avail = sizeof buf - 1 - n;
        if (!ReadFile(h, buf + n, avail, &r, NULL)) { printf("[%6lu ms] read failed %lu\n", GetTickCount() - t0, GetLastError()); exit(0); }
        n += r; buf[n] = 0;
        char *nl;
        while ((nl = strchr(buf, '\n'))) { *nl = 0; printf("[%6lu ms] <- %s\n", GetTickCount() - t0, buf); fflush(stdout); memmove(buf, nl + 1, strlen(nl + 1) + 1); n = (int)strlen(buf); }
    }
}
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: rjpipe <pid|auto> <seconds> [line | sleep:<ms>]...\n"); return 2; }
    t0 = GetTickCount();
    DWORD pid = strcmp(argv[1], "auto") ? strtoul(argv[1], NULL, 10) : find_game();
    if (!pid) { printf("no DOW2.exe\n"); return 1; }
    char name[64]; snprintf(name, sizeof name, "\\\\.\\pipe\\EliteRoomJoin-%lu", pid);
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 50 && h == INVALID_HANDLE_VALUE; i++) {
        h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) { DWORD e = GetLastError(); if (e == ERROR_PIPE_BUSY) WaitNamedPipeA(name, 200); else { printf("cannot open %s (error %lu): no RoomJoin 9 in that game\n", name, e); return 1; } }
    }
    if (h == INVALID_HANDLE_VALUE) { printf("%s busy\n", name); return 1; }
    printf("[%6lu ms] connected to %s\n", GetTickCount() - t0, name); fflush(stdout);
    pump(h, 300);
    for (int i = 3; i < argc; i++) {
        if (!strncmp(argv[i], "sleep:", 6)) { pump(h, strtoul(argv[i] + 6, NULL, 10)); continue; }
        char line[2048]; snprintf(line, sizeof line, "%s\n", argv[i]);
        DWORD w; if (!WriteFile(h, line, (DWORD)strlen(line), &w, NULL)) { printf("write failed %lu\n", GetLastError()); return 1; }
        printf("[%6lu ms] -> %s\n", GetTickCount() - t0, argv[i]); fflush(stdout);
        pump(h, 100);
    }
    DWORD total = strtoul(argv[2], NULL, 10) * 1000, el = GetTickCount() - t0;
    if (total > el) pump(h, total - el);
    CloseHandle(h); return 0;
}
