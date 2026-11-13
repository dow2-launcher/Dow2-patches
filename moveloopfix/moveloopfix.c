// MoveLoopFix.dll - the game freezes for good (then runs out of memory) when a melee unit ends up exactly on the centre of the
// enemy unit it walks to (DoW2: Retribution 3.19.10320, the same DOW2.exe for vanilla and Elite).
//
// The bug (engine, Relic): the per-unit movement step 0x009141A0 (thiscall-like, the mover in ebp = its one stack argument,
// ret 4) runs a state machine while the time left of the tick ([esp+0x54], float) is > 0:
//     2 follow the path   - the unit is already at the path's end: no time spent
//     4 reached?          - EntityUtil::IsWithinDistShifted(self, target, 0) is false when both centres are bit-equal
//     0 new path          - PathHigh::FindPath from cell X to the same cell X "succeeds"
//     2 ...               - for ever: the time left never drops, every pass allocates, ~15 MB/s, then out of memory.
// Found on fastbench_1113 (repro hang_s5_12da6e: an ork slugga's last step at tick 3323 lands bit-exactly on a banshee's
// centre with 0.00057 s of the tick left). Any melee unit can hit it - players and computers alike - but it needs the two
// centres to be bit-equal, so it is extremely rare (whole human games: closest approach to a target centre 0.57 m).
//
// The guard: at the loop's exit test (0x009144F3: xorps xmm0,xmm0 / comiss xmm0,[esp+0x54] / jae 0x0091451A) count the
// passes of one call in which the time left is bit-equal to the previous pass. After 256 such passes in a row the time left
// is set to 0, so the loop leaves by its own normal "time used up" exit (0x0091451A). The count is reset at the start of
// every call of 0x009141A0 (second patch, the function's first 6 bytes), so it never adds up across calls or ticks. A normal
// step makes a handful of passes at most, so in any game that does not hit the bug nothing in the simulation changes (whole
// human replays: their own sync hashes on every tick, the guard never fired). Multiplayer: the guard changes the sim only
// where a game without it would hang for ever; a player without it freezes there anyway.
// The counters are a small table indexed by the mover's address (no shared state between movers if the engine ever steps
// them on several threads; two movers in one slot only reset each other's count = the guard fires later, never earlier).
//
// Both places are checked byte for byte first (and the code around them); any other exe build, or code already changed by
// someone else, leaves the game untouched and says so in MoveLoopFix.log. Rules: docs/ADDON_RULES.md (own DLL, own tick).
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define VERSION   "1"
#define FN_STEP   0x009141A0u   // per-unit movement step (xorps xmm1,xmm1 / sub esp,50h / push ebx / push ebp / mov ebp,[esp+5Ch])
#define AT_EXIT   0x009144F3u   // the loop's exit test
#define CONT_AT   0x009144FDu   // after the exit test: time left > 0
#define EXIT_AT   0x0091451Au   // the loop's own exit
#define G_WORLD   0x013356A4u   // +0x178 sim tick (one tick = 0.1 s of game time; as Marks / MatchTimer)
#define LIMIT     256

// the step's first 24 bytes (0x009141A0..0x009141B7) and the exit test with the code around it (0x009144E5..0x0091451C)
static const unsigned char ORIG_STEP[24] = {
    0x0F, 0x57, 0xC9, 0x83, 0xEC, 0x50,             // xorps xmm1,xmm1 ; sub esp,50h        <- replaced (jmp + nop)
    0x53, 0x55, 0x8B, 0x6C, 0x24, 0x5C,             // push ebx ; push ebp ; mov ebp,[esp+5Ch]
    0x0F, 0x2F, 0x4D, 0x50, 0x56, 0x57,             // comiss xmm1,[ebp+50h] ; push esi ; push edi
    0x0F, 0x87, 0x62, 0x03, 0x00, 0x00};            // ja 0x0091451A
static const unsigned char ORIG_EXIT[56] = {
    0x3B, 0x85, 0xC4, 0x00, 0x00, 0x00, 0x74, 0x06, // cmp eax,[ebp+0C4h] ; je 0x009144F3
    0x89, 0x85, 0xC4, 0x00, 0x00, 0x00,             // mov [ebp+0C4h],eax
    0x0F, 0x57, 0xC0, 0x0F, 0x2F, 0x44, 0x24, 0x54, // xorps xmm0,xmm0 ; comiss xmm0,[esp+54h]   <- replaced (jmp + nop)
    0x73, 0x1D,                                     // jae 0x0091451A
    0x38, 0x5C, 0x24, 0x43, 0x0F, 0x84, 0x59, 0xFD, 0xFF, 0xFF,   // cmp [esp+43h],bl ; je 0x00914260
    0x8B, 0x85, 0xC4, 0x00, 0x00, 0x00, 0x3B, 0xC3, 0x74, 0x09,   // mov eax,[ebp+0C4h] ; cmp eax,ebx ; je 0x0091451A
    0x83, 0xF8, 0x05, 0x0F, 0x85, 0x46, 0xFD, 0xFF, 0xFF,         // cmp eax,5 ; jne 0x00914260
    0x32, 0xC0, 0x5F};                              // 0x0091451A: xor al,al ; pop edi

DWORD mlf_tab[256 * 4];                          // per slot: mover, time-left bits of the last pass, count, -
volatile LONG mlf_fired;                         // times the guard cut the loop
DWORD mlf_ring[16 * 4];                          // per fire: sim tick, mover, time-left bits, -
DWORD mlf_world = G_WORLD, mlf_step_cont = FN_STEP + 6, mlf_cont = CONT_AT, mlf_exit = EXIT_AT;

// Entry: forget the count of this mover (a new call = a new count). Then the 6 replaced bytes and back.
// Exit test: no calls, only eax/ecx/edx (saved); the flags are set again by the replaced comiss.
__asm__(".intel_syntax noprefix\n"
        ".globl _mlf_cave_entry\n_mlf_cave_entry:\n"
        "push eax\npush ecx\n"
        "mov eax, dword ptr [esp + 12]\n"                 // the mover (the step's argument)
        "mov ecx, eax\nshr ecx, 4\nand ecx, 255\nshl ecx, 4\n"
        "cmp eax, dword ptr [_mlf_tab + ecx]\njne 1f\n"
        "mov dword ptr [_mlf_tab + ecx], 0\nmov dword ptr [_mlf_tab + ecx + 8], 0\n"
        "1:\npop ecx\npop eax\n"
        "xorps xmm1, xmm1\nsub esp, 0x50\n"               // the replaced instructions
        "jmp dword ptr [_mlf_step_cont]\n"

        ".globl _mlf_cave_exit\n_mlf_cave_exit:\n"
        "push eax\npush ecx\npush edx\n"
        "mov eax, dword ptr [esp + 12 + 0x54]\n"          // time left (bits)
        "mov ecx, ebp\nshr ecx, 4\nand ecx, 255\nshl ecx, 4\n"
        "lea edx, [_mlf_tab + ecx]\n"
        "cmp ebp, dword ptr [edx]\njne 2f\n"
        "cmp eax, dword ptr [edx + 4]\njne 2f\n"
        "inc dword ptr [edx + 8]\n"
        "cmp dword ptr [edx + 8], 256\njb 3f\n"
        "mov dword ptr [esp + 12 + 0x54], 0\n"            // time left = 0 -> the loop's own exit
        "mov dword ptr [edx + 8], 0\n"
        "mov edx, 1\nlock xadd dword ptr [_mlf_fired], edx\n"   // edx = this fire's number - 1
        "and edx, 15\nshl edx, 4\n"
        "mov dword ptr [_mlf_ring + edx + 4], ebp\nmov dword ptr [_mlf_ring + edx + 8], eax\n"
        "mov dword ptr [_mlf_ring + edx], -1\n"
        "mov ecx, dword ptr [_mlf_world]\nmov ecx, dword ptr [ecx]\ntest ecx, ecx\njz 3f\n"
        "mov ecx, dword ptr [ecx + 0x178]\nmov dword ptr [_mlf_ring + edx], ecx\njmp 3f\n"
        "2:\nmov dword ptr [edx], ebp\nmov dword ptr [edx + 4], eax\nmov dword ptr [edx + 8], 0\n"
        "3:\npop edx\npop ecx\npop eax\n"
        "xorps xmm0, xmm0\ncomiss xmm0, dword ptr [esp + 0x54]\n"   // the replaced instructions
        "jae 4f\njmp dword ptr [_mlf_cont]\n4:\njmp dword ptr [_mlf_exit]\n"
        ".att_syntax prefix\n");
void mlf_cave_entry(void);
void mlf_cave_exit(void);

static wchar_t g_log[MAX_PATH];
static int g_lines;
static void logmsg(const char *fmt, ...) {
    if (g_lines > 2000) return; g_lines++;
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static void hex(char *out, const unsigned char *p, int n) { for (int i = 0; i < n; i++) sprintf(out + 3 * i, "%02X ", p[i]); }
static int same(const void *at, const unsigned char *want, int n, const char *what) {
    if (IsBadReadPtr(at, n)) { logmsg("NOT installed: no code at %08lX (%s) - not DOW2.exe 3.19?", (DWORD)at, what); return 0; }
    if (memcmp(at, want, n)) {
        char h[3 * 64 + 1]; hex(h, at, n);
        logmsg("NOT installed: DOW2.exe is not 3.19.10320 or this code is already changed (%s at %08lX: %s)", what, (DWORD)at, h);
        return 0;
    }
    return 1;
}
// 8 bytes at once: a jmp rel32 to our code + nops, the rest of the 8 kept (one locked 8-byte write)
static int put_jmp(DWORD at, void *to, int nops) {
    unsigned char *t = (unsigned char *)at; DWORD old;
    if (!VirtualProtect(t, 8, PAGE_EXECUTE_READWRITE, &old)) { logmsg("NOT installed: VirtualProtect %08lX failed (%lu)", at, GetLastError()); return 0; }
    LONGLONG oldv = *(volatile LONGLONG *)t, newv = oldv; unsigned char *nb = (unsigned char *)&newv;
    nb[0] = 0xE9; *(LONG *)(nb + 1) = (LONG)((DWORD)to - (at + 5)); for (int i = 0; i < nops; i++) nb[5 + i] = 0x90;
    InterlockedCompareExchange64((LONGLONG *)t, newv, oldv);
    VirtualProtect(t, 8, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 8);
    return 1;
}
static int install(void) {
    if (!same((void *)FN_STEP, ORIG_STEP, sizeof ORIG_STEP, "movement step") ||
        !same((void *)(AT_EXIT - 14), ORIG_EXIT, sizeof ORIG_EXIT, "movement loop exit test")) return 0;
    // the entry first: once the exit test counts, every call already starts from 0
    if (!put_jmp(FN_STEP, (void *)mlf_cave_entry, 1)) return 0;                 // 6 bytes replaced, 2 kept (push ebx ; push ebp)
    if (!put_jmp(AT_EXIT, (void *)mlf_cave_exit, 3)) return 0;                  // 8 of the 10 bytes; jae (2 bytes) after it is unreachable
    return 1;
}
// the fires are logged from here, not from the game's thread (the exit test only counts and notes the tick)
static DWORD WINAPI watch(LPVOID _) {
    (void)_; LONG done = 0;
    for (;;) {
        Sleep(1000);
        LONG n = mlf_fired;
        if (n - done > 16) done = n - 16;                                  // more than the ring holds since the last look
        for (; done < n; done++) {
            DWORD *r = &mlf_ring[(done & 15) * 4]; float left; memcpy(&left, &r[2], 4);
            int tick = (int)r[0];
            if (tick >= 0) logmsg("fired (%ld in this game process): game %d:%02d (tick %d) - a unit stood exactly in its target's centre, "
                                  "the movement loop was cut (mover %08lX, %.6f s of the tick left)", done + 1, tick / 600, (tick / 10) % 60, tick, r[1], left);
            else logmsg("fired (%ld in this game process): game time unknown - the movement loop was cut (mover %08lX)", done + 1, r[1]);
        }
    }
    return 0;
}
// one copy per game process: a patch can come both from Elite_DLLs (launcher) and from dow2-patches\ (winmm loader)
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"MoveLoopFix.ini"); wcscat(g_log, L"MoveLoopFix.log");
    if (already_loaded("MoveLoopFix")) { logmsg("another MoveLoopFix copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"MoveLoopFix", L"Enabled", 1, ini)) { logmsg("NOT installed: disabled in MoveLoopFix.ini"); return TRUE; }
    if (!install()) return TRUE;
    logmsg("MoveLoopFix " VERSION ": installed - a melee unit standing exactly in its target's centre no longer freezes the game "
           "(movement step %08X, loop exit %08X; cut after %d passes with no time spent)", FN_STEP, AT_EXIT, LIMIT);
    HANDLE w = CreateThread(NULL, 0, watch, NULL, 0, NULL); if (w) CloseHandle(w);
    return TRUE;
}
