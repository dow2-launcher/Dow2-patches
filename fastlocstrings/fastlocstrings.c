// FastLocStrings.dll - releasing a formatted UI string scans the list of all live ones from its start (DoW2: Retribution 3.19).
//
// Localizer.dll keeps every dynamic (formatted) LocString in one vector: CreateDynamicString appends the new entry at the end,
// and when the last reference goes, 0x10004A80 searches the vector FROM THE START for it, swaps the last entry into its place
// and frees it. The HUD formats and drops strings all the time (tooltips, counters, names), while long-lived strings pile up
// at the front, so each release walks past all of them. Measured on the stand (3v3 replay): 5.8% of the game thread.
//
// Fix: the same function, searching from the END (where the short-lived strings are). Entries are distinct pointers, so it finds
// the same entry and leaves the vector exactly as the original does (same swap-with-last), under the same mutex, freeing the
// same two blocks. Entry of 0x10004A80 is redirected with one atomic 8-byte write. UI text only - no sync impact.
#include <windows.h>
#include <stdio.h>
#include <string.h>

static wchar_t g_log[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
#define RVA_RELEASE 0x4A80
// 6A FF 68 <SEH handler> 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 08 53 55 8B 6C 24 20 56 8B F2 57 8D 5E 0C 8B F9 8B CB 89 5C 24 10 FF 15 <Enter>
static const unsigned char HEAD[2] = {0x6A, 0xFF};
static const unsigned char BODY[] = {0x64, 0xA1, 0, 0, 0, 0, 0x50, 0x64, 0x89, 0x25, 0, 0, 0, 0, 0x83, 0xEC, 0x08, 0x53, 0x55, 0x8B, 0x6C,
                                     0x24, 0x20, 0x56, 0x8B, 0xF2, 0x57, 0x8D, 0x5E, 0x0C, 0x8B, 0xF9, 0x8B, 0xCB, 0x89, 0x5C, 0x24, 0x10, 0xFF, 0x15};
// the old loop: mov edx,[eax]; add edx,8; cmp edx,edi; je; add eax,4; cmp eax,ecx; jne
static const unsigned char LOOP[] = {0x8B, 0x10, 0x83, 0xC2, 0x08, 0x3B, 0xD7, 0x74, 0x09, 0x83, 0xC0, 0x04, 0x3B, 0xC1, 0x75, 0xF0};
typedef struct { void **begin, **end, **cap; BYTE mutex[1]; } Vec;              // mutex = Plat::RecursiveMutex at +0xC
typedef void (__thiscall *MutexFn)(void *);
typedef void (__stdcall *FreeFn)(void *pool, void *p);
static MutexFn m_enter, m_leave; static FreeFn mem_free;
static volatile LONG g_rel, g_steps, g_maxlen;

// __fastcall = the original's registers: ecx = string implementation (entry + 8), edx = the vector, one stack arg (pool handle
// pointer), callee pops it
static void __fastcall release_from_end(BYTE *imp, Vec *v, void **pool) {
    m_enter(v->mutex);
    void **b = v->begin, **e = v->end, **p = e;
    LONG n = (LONG)(e - b); if (n > g_maxlen) g_maxlen = n;
    while (p != b) {
        --p;
        if ((BYTE *)*p + 8 == imp) {
            void **ent = (void **)*p;
            *p = e[-1]; v->end = e - 1;
            mem_free(*pool, ent[0]);
            mem_free(*pool, ent);
            break;
        }
    }
    g_rel++; g_steps += (LONG)(e - p);
    m_leave(v->mutex);
}
static int hook(void) {
    BYTE *loc = (BYTE *)GetModuleHandleA("Localizer.dll");
    HMODULE plat = GetModuleHandleA("Platform.dll"), mem = GetModuleHandleA("Memory.dll");
    if (!loc || !plat || !mem) { logmsg("Localizer/Platform/Memory.dll not loaded - not patching"); return 0; }
    m_enter = (MutexFn)GetProcAddress(plat, "?Enter@RecursiveMutex@Plat@@QAEXXZ");
    m_leave = (MutexFn)GetProcAddress(plat, "?Leave@RecursiveMutex@Plat@@QAEXXZ");
    mem_free = (FreeFn)GetProcAddress(mem, "?MemPoolFree@@YGXPAU_HMEMPOOL@@PAX@Z");
    if (!m_enter || !m_leave || !mem_free) { logmsg("imports not found - not patching"); return 0; }
    BYTE *f = loc + RVA_RELEASE;
    if (((DWORD)f & 7) || IsBadReadPtr(f, 0x60) || memcmp(f, HEAD, 2) || f[2] != 0x68 || memcmp(f + 7, BODY, sizeof BODY)
        || memcmp(f + 0x50, LOOP, sizeof LOOP)) { logmsg("Localizer.dll is not the 3.19 one (bytes differ at +%X) - not patching", RVA_RELEASE); return 0; }
    // the mutex call in it must be this import (call [Enter] at +0x2D)
    if (**(void ***)(f + 0x2F) != (void *)m_enter) { logmsg("mutex import differs - not patching"); return 0; }
    LONGLONG oldv = *(volatile LONGLONG *)f, newv; BYTE *nb = (BYTE *)&newv;
    newv = oldv; nb[0] = 0xE9; *(LONG *)(nb + 1) = (LONG)((BYTE *)release_from_end - (f + 5));
    DWORD old; if (!VirtualProtect(f, 8, PAGE_EXECUTE_READWRITE, &old)) { logmsg("VirtualProtect failed"); return 0; }
    LONGLONG was = InterlockedCompareExchange64((LONGLONG *)f, newv, oldv);   // lock cmpxchg8b: no torn instruction for a running thread
    VirtualProtect(f, 8, old, &old); FlushInstructionCache(GetCurrentProcess(), f, 8);
    if (was != oldv) { logmsg("entry changed while patching - not patched"); return 0; }
    logmsg("FastLocStrings 1: string release searches from the end (Localizer %p, function %p)", loc, f);
    return 1;
}
static DWORD WINAPI stats(LPVOID _) {
    for (;;) {
        Sleep(60000);
        LONG r = InterlockedExchange(&g_rel, 0), s = InterlockedExchange(&g_steps, 0);
        logmsg("last minute: %ld releases, %.1f entries looked at per release, list up to %ld", r, r ? (double)s / r : 0.0, g_maxlen);
    }
    return 0;
}
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"FastLocStrings.ini"); wcscat(g_log, L"FastLocStrings.log");
    if (already_loaded("FastLocStrings")) { logmsg("another FastLocStrings copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"FastLocStrings", L"Enabled", 1, ini)) { logmsg("disabled in FastLocStrings.ini"); return TRUE; }
    if (hook() && GetPrivateProfileIntW(L"FastLocStrings", L"Stats", 0, ini)) CloseHandle(CreateThread(NULL, 0, stats, NULL, 0, NULL));
    return TRUE;
}
