// SpeechStreakFix.dll - a rare crash of DoW2: Retribution 3.19 when a unit gets a kill (unit speech, kill streak).
//
// The unit speech keeps, per entity, how many kills it has made ("Killing_Streak" of sound:speechtuning_*.lua): a sorted
// vector map { Entity*, count } in the speech event listener (+0x24 begin, +0x28 end). On every kill FUN_00B8FB80 does
//     ebp = end; equal_range(begin, end, &killer);            // 0x00B8FC0F call 0x00815E30
//     if (first != second) ebp = first;                         // 0x00B8FC18 cmp / je / mov ebp,eax
//     if (ebp) count = [ebp+4];                                 // 0x00B8FC20 test / je / 0x00B8FC24 mov eax,[ebp+4]
//     map[killer] = count + 1;
// For an entity that has no kills yet (and a non-empty map) it reads end->count: 4 bytes PAST the vector's buffer. Mostly
// that is the next heap cell (the new killer then starts from garbage + 1), but when the buffer is the last cell of a
// committed page and the next page is not committed, the game crashes: Access Violation at 0x00B8FC24 (TvangeS, 11.11.2026:
// begin 0x84B22FF8, end = capacity = 0x84B23000, read 0x84B23004).
//
// Fix: 4 bytes at 0x00B8FC1C, "je +2; mov ebp,eax" -> "mov ebp,eax; je 0x00B8FC2D": a killer not in the map counts from 0
// and nothing is read; a killer in the map reads its own entry as before. EBP is not used after 0x00B8FC2D until it is
// cleared at 0x00B8FC45. One aligned 4-byte store. Speech only (which line a unit says after N kills) - nothing of the
// simulation is touched, no sync impact. All 13 bytes around it are checked first; another build is left alone.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define CMP_AT   0x00B8FC18
#define PATCH_AT 0x00B8FC1C
#define SKIP_TO  0x00B8FC2D
#define READ_AT  0x00B8FC20
static const unsigned char ORIG[13] = {0x3B, 0x44, 0x24, 0x14,   // cmp eax,[esp+14h]   (first == second ?)
                                       0x74, 0x02, 0x8B, 0xE8,   // je +2 ; mov ebp,eax   <- patched
                                       0x85, 0xED, 0x74, 0x09,   // test ebp,ebp ; je 0x00B8FC2D
                                       0x8B};                    // mov eax,[ebp+4]
static wchar_t g_log[MAX_PATH];

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static int site_ok(const unsigned char *t) {
    if (IsBadReadPtr(t, sizeof ORIG)) { logmsg("no code at %08X (not DOW2.exe 3.19?) - not patching", CMP_AT); return 0; }
    if (memcmp(t, ORIG, sizeof ORIG)) {
        char hex[3 * sizeof ORIG + 1]; for (unsigned i = 0; i < sizeof ORIG; i++) sprintf(hex + 3 * i, "%02X ", t[i]);
        logmsg("DOW2.exe is not 3.19 or already changed (bytes at %08X: %s) - not patching", CMP_AT, hex); return 0;
    }
    return 1;
}
#ifndef STAND_PROBE
static const unsigned char NEWB[4] = {0x8B, 0xE8, 0x74, 0x0D};  // mov ebp,eax ; je 0x00B8FC2D
static void install(void) {
    unsigned char *t = (unsigned char *)CMP_AT;
    if (!site_ok(t)) return;
    DWORD old; if (!VirtualProtect((void *)PATCH_AT, 4, PAGE_EXECUTE_READWRITE, &old)) { logmsg("VirtualProtect failed (%lu)", GetLastError()); return; }
    LONG want; memcpy(&want, NEWB, 4);
    InterlockedExchange((LONG *)PATCH_AT, want);   // aligned: a thread on this code sees either the old or the new 4 bytes
    VirtualProtect((void *)PATCH_AT, 4, old, &old); FlushInstructionCache(GetCurrentProcess(), (void *)PATCH_AT, 4);
    logmsg("SpeechStreakFix 1: kill-streak speech no longer reads past its list (patched %08X: %02X %02X %02X %02X)", PATCH_AT,
           t[4], t[5], t[6], t[7]);
}
#else
// STAND ONLY (built with -DSTAND_PROBE, not shipped): the same logic in a stub that also counts both ways and records which
// entity got the kill (its blueprint file), logged every 10 s from its own thread.
static volatile LONG g_found, g_new, g_past;
typedef void *(__stdcall *PbgFn)(void *ent);
typedef const char *(__thiscall *NameFn)(void *pbg);
static PbgFn pbg_of; static NameFn pbg_name;
typedef struct { void *key; int kind; char name[96]; } Rec;      // kind 0 known, 1 new on empty list, 2 new on a non-empty list
static Rec g_ring[256]; static volatile LONG g_head;
static void __cdecl record(void **map, void *key) {             // game thread, at the kill
    int kind = 1;
    if (map[0] != map[1]) { kind = 2; for (void **e = map[0]; e != map[1]; e += 2) if (e[0] == key) { kind = 0; break; } }
    Rec *r = &g_ring[g_head & 255]; r->key = key; r->kind = kind; r->name[0] = 0;
    void *pbg = pbg_of ? pbg_of(key) : NULL; const char *n = pbg && pbg_name ? pbg_name(pbg) : NULL;
    if (n) lstrcpynA(r->name, n, sizeof r->name);
    InterlockedIncrement(&g_head);
}
static void install(void) {
    unsigned char *t = (unsigned char *)CMP_AT;
    if (!site_ok(t)) return;
    HMODULE se = GetModuleHandleA("SimEngine.dll");
    if (se) { pbg_of = (PbgFn)GetProcAddress(se, "?GetPropertyBagGroup@@YGPBVPropertyBagGroup@@PBVEntity@@@Z");
              pbg_name = (NameFn)GetProcAddress(se, "?GetFilename@PropertyBagGroup@@QBEPBDXZ"); }
    unsigned char *s = VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE), *p = s;
    #define B(x) (*p++ = (unsigned char)(x))
    #define D(x) (*(DWORD *)p = (DWORD)(x), p += 4)
    B(0x60);                                             // pushad
    B(0xFF); B(0x74); B(0x24); B(0x54);                  // push [esp+54h]  (the killer: [esp+34h] before pushad)
    B(0x56);                                             // push esi        (the map)
    B(0xE8); D((DWORD)record - (DWORD)(p + 4));          // call record
    B(0x83); B(0xC4); B(0x08);                           // add esp,8
    B(0x61);                                             // popad
    B(0x3B); B(0x44); B(0x24); B(0x14);                  // cmp eax,[esp+14h]
    B(0x74); B(0x0E);                                    // je new
    B(0x8B); B(0xE8);                                    // mov ebp,eax
    B(0xF0); B(0xFF); B(0x05); D(&g_found);              // lock inc [g_found]
    B(0xE9); D(READ_AT - (DWORD)(p + 4));                // jmp 0x00B8FC20 (test ebp / read)
    B(0xF0); B(0xFF); B(0x05); D(&g_new);                // new: lock inc [g_new]
    B(0x8B); B(0x0E); B(0x3B); B(0x4E); B(0x04);         // mov ecx,[esi] ; cmp ecx,[esi+4]  (esi = the map; ecx is free here)
    B(0x74); B(0x07);                                    // je skip (empty list: the original read nothing either)
    B(0xF0); B(0xFF); B(0x05); D(&g_past);               // lock inc [g_past]  = the original read past the list here
    B(0xE9); D(SKIP_TO - (DWORD)(p + 4));                // skip: jmp 0x00B8FC2D (no read)
    LONGLONG oldv = *(volatile LONGLONG *)t, newv; unsigned char *nb = (unsigned char *)&newv;
    newv = oldv; nb[0] = 0xE9; *(LONG *)(nb + 1) = (LONG)(s - (t + 5)); nb[5] = nb[6] = nb[7] = 0x90;
    DWORD old; VirtualProtect(t, 8, PAGE_EXECUTE_READWRITE, &old);
    InterlockedCompareExchange64((LONGLONG *)t, newv, oldv);
    VirtualProtect(t, 8, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 8);
    logmsg("SpeechStreakFix PROBE: %08X -> stub %p (counts known / new killers; names %p %p)", CMP_AT, s, pbg_of, pbg_name);
}
static DWORD WINAPI stats(LPVOID _) {
    static const char *KIND[3] = {"known", "new (list empty)", "NEW ON A NON-EMPTY LIST (original reads past it)"};
    LONG done = 0;
    for (;;) {
        Sleep(10000);
        for (; done < g_head; done++) { Rec *r = &g_ring[done & 255]; logmsg("PROBE kill by %p %s: %s", r->key, r->name, KIND[r->kind]); }
        logmsg("PROBE totals: known %ld, new %ld, new on a non-empty list %ld", (LONG)g_found, (LONG)g_new, (LONG)g_past);
    }
    return 0;
}
#endif
// one copy per game process: a patch can come both from Elite_DLLs (launcher) and from dow2-patches\ (winmm loader)
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"SpeechStreakFix.ini"); wcscat(g_log, L"SpeechStreakFix.log");
    if (already_loaded("SpeechStreakFix")) { logmsg("another SpeechStreakFix copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"SpeechStreakFix", L"Enabled", 1, ini)) { logmsg("disabled in SpeechStreakFix.ini"); return TRUE; }
    install();
#ifdef STAND_PROBE
    CloseHandle(CreateThread(NULL, 0, stats, NULL, 0, NULL));
#endif
    return TRUE;
}
