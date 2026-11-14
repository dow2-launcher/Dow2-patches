// moveloop_guard.h - the engine's melee freeze, guarded from EliteAI.dll (a copy of MoveLoopFix 1, dow2-patches 0.2.72,
// moveloopfix/moveloopfix.c - same patch, same bytes, same one-per-game mutex, so the two never both install).
//
// The bug (engine, Relic; DOW2.exe 3.19.10320, the same exe for vanilla and Elite): the per-unit movement step 0x009141A0
// runs a state machine while the time left of the tick ([esp+0x54], float) is > 0. A melee unit whose last step lands
// bit-exactly on the centre of the enemy unit it walks to loops 2 (follow the path: at its end, no time spent) -> 4
// (EntityUtil::IsWithinDistShifted(self, target, 0) is false at equal centres) -> 0 (PathHigh::FindPath from a cell to the
// same cell "succeeds") -> 2 ... for ever: the game freezes, memory grows ~15 MB/s until it dies. Watchdog signature: main
// thread at DOW2+0x2befe6..ee (0x006befd0 = vector fill of the pathfinder's A* reset, via 0x00a23400 / 0x00a23000 /
// 0x0095fc40 / 0x009a0330 / 0x00a1b820 under 0x009141A0). Seen on the bench: hang_s5_12da6e (tick 3323) and the three
// step-7 hangs (s7_f_s0 tick 6795, s7_f_p0 tick 7033, s7_h tick 5148) - every one a brain MELEE charge (approach 0).
// Any melee unit can hit it (players, Elite's computer, newai-23/24/25); the brain cannot see or avoid it from Lua.
//
// The guard: at the loop's exit test (0x009144F3: xorps xmm0,xmm0 / comiss xmm0,[esp+0x54] / jae 0x0091451A) count the
// passes of one call whose time left is bit-equal to the previous pass; after 256 in a row the time left is set to 0 and
// the loop leaves by its own exit. The count is reset at every entry of 0x009141A0 (its first 6 bytes go through a stub).
// A normal step makes a handful of passes at most: in a game that does not hit the bug the sim does not change (whole
// human replays: own sync hashes on every tick, never fired - MoveLoopFix tests).
// Both places are checked byte for byte first; otherwise nothing is patched. The mutex Local\dow2patch_MoveLoopFix_<pid>
// is MoveLoopFix.dll's own: whichever of the two loads first installs, the other one stays idle.
#pragma once
#include <windows.h>
#include <string.h>

#define MLG_STEP  0x009141A0u   // per-unit movement step
#define MLG_EXIT  0x009144F3u   // the loop's exit test
#define MLG_CONT  0x009144FDu   // after the exit test: time left > 0
#define MLG_OUT   0x0091451Au   // the loop's own exit
#define MLG_WORLD 0x013356A4u   // +0x178 sim tick

static const unsigned char MLG_ORIG_STEP[24] = {
    0x0F, 0x57, 0xC9, 0x83, 0xEC, 0x50, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x5C,
    0x0F, 0x2F, 0x4D, 0x50, 0x56, 0x57, 0x0F, 0x87, 0x62, 0x03, 0x00, 0x00};
static const unsigned char MLG_ORIG_EXIT[56] = {
    0x3B, 0x85, 0xC4, 0x00, 0x00, 0x00, 0x74, 0x06, 0x89, 0x85, 0xC4, 0x00, 0x00, 0x00,
    0x0F, 0x57, 0xC0, 0x0F, 0x2F, 0x44, 0x24, 0x54, 0x73, 0x1D,
    0x38, 0x5C, 0x24, 0x43, 0x0F, 0x84, 0x59, 0xFD, 0xFF, 0xFF,
    0x8B, 0x85, 0xC4, 0x00, 0x00, 0x00, 0x3B, 0xC3, 0x74, 0x09,
    0x83, 0xF8, 0x05, 0x0F, 0x85, 0x46, 0xFD, 0xFF, 0xFF, 0x32, 0xC0, 0x5F};

DWORD mlg_tab[256 * 4];                          // per slot: mover, time-left bits of the last pass, count, -
volatile LONG mlg_fired;                         // times the guard cut the loop
DWORD mlg_ring[16 * 4];                          // per fire: sim tick, mover, time-left bits, -
DWORD mlg_world = MLG_WORLD, mlg_step_cont = MLG_STEP + 6, mlg_cont = MLG_CONT, mlg_out = MLG_OUT;

__asm__(".intel_syntax noprefix\n"
        ".globl _mlg_cave_entry\n_mlg_cave_entry:\n"
        "push eax\npush ecx\n"
        "mov eax, dword ptr [esp + 12]\n"
        "mov ecx, eax\nshr ecx, 4\nand ecx, 255\nshl ecx, 4\n"
        "cmp eax, dword ptr [_mlg_tab + ecx]\njne 1f\n"
        "mov dword ptr [_mlg_tab + ecx], 0\nmov dword ptr [_mlg_tab + ecx + 8], 0\n"
        "1:\npop ecx\npop eax\n"
        "xorps xmm1, xmm1\nsub esp, 0x50\n"
        "jmp dword ptr [_mlg_step_cont]\n"

        ".globl _mlg_cave_exit\n_mlg_cave_exit:\n"
        "push eax\npush ecx\npush edx\n"
        "mov eax, dword ptr [esp + 12 + 0x54]\n"
        "mov ecx, ebp\nshr ecx, 4\nand ecx, 255\nshl ecx, 4\n"
        "lea edx, [_mlg_tab + ecx]\n"
        "cmp ebp, dword ptr [edx]\njne 2f\n"
        "cmp eax, dword ptr [edx + 4]\njne 2f\n"
        "inc dword ptr [edx + 8]\n"
        "cmp dword ptr [edx + 8], 256\njb 3f\n"
        "mov dword ptr [esp + 12 + 0x54], 0\n"
        "mov dword ptr [edx + 8], 0\n"
        "mov edx, 1\nlock xadd dword ptr [_mlg_fired], edx\n"
        "and edx, 15\nshl edx, 4\n"
        "mov dword ptr [_mlg_ring + edx + 4], ebp\nmov dword ptr [_mlg_ring + edx + 8], eax\n"
        "mov dword ptr [_mlg_ring + edx], -1\n"
        "mov ecx, dword ptr [_mlg_world]\nmov ecx, dword ptr [ecx]\ntest ecx, ecx\njz 3f\n"
        "mov ecx, dword ptr [ecx + 0x178]\nmov dword ptr [_mlg_ring + edx], ecx\njmp 3f\n"
        "2:\nmov dword ptr [edx], ebp\nmov dword ptr [edx + 4], eax\nmov dword ptr [edx + 8], 0\n"
        "3:\npop edx\npop ecx\npop eax\n"
        "xorps xmm0, xmm0\ncomiss xmm0, dword ptr [esp + 0x54]\n"
        "jae 4f\njmp dword ptr [_mlg_cont]\n4:\njmp dword ptr [_mlg_out]\n"
        ".att_syntax prefix\n");
void mlg_cave_entry(void);
void mlg_cave_exit(void);

static int mlg_put_jmp(DWORD at, void *to, int nops) {
    unsigned char *t = (unsigned char *)at; DWORD old;
    if (!VirtualProtect(t, 8, PAGE_EXECUTE_READWRITE, &old)) return 0;
    LONGLONG oldv = *(volatile LONGLONG *)t, newv = oldv; unsigned char *nb = (unsigned char *)&newv;
    nb[0] = 0xE9; *(LONG *)(nb + 1) = (LONG)((DWORD)to - (at + 5)); for (int i = 0; i < nops; i++) nb[5 + i] = 0x90;
    InterlockedCompareExchange64((LONGLONG *)t, newv, oldv);
    VirtualProtect(t, 8, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 8);
    return 1;
}
// 1 installed, 0 MoveLoopFix.dll (or another copy) already active, -1 bytes differ (other exe / code changed), -2 failed
static int mlg_install(void) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_MoveLoopFix_%lu", GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    if (IsBadReadPtr((void *)MLG_STEP, sizeof MLG_ORIG_STEP) || IsBadReadPtr((void *)(MLG_EXIT - 14), sizeof MLG_ORIG_EXIT) ||
        memcmp((void *)MLG_STEP, MLG_ORIG_STEP, sizeof MLG_ORIG_STEP) ||
        memcmp((void *)(MLG_EXIT - 14), MLG_ORIG_EXIT, sizeof MLG_ORIG_EXIT)) return -1;
    if (!mlg_put_jmp(MLG_STEP, (void *)mlg_cave_entry, 1)) return -2;
    if (!mlg_put_jmp(MLG_EXIT, (void *)mlg_cave_exit, 3)) return -2;
    return 1;
}
// the fires, read off the game's thread: fills `out` with one line per new fire since *done (returns how many)
static int mlg_poll(LONG *done, void (*log)(const char *fmt, ...)) {
    LONG n = mlg_fired; int k = 0;
    if (n - *done > 16) *done = n - 16;
    for (; *done < n; (*done)++, k++) {
        DWORD *r = &mlg_ring[(*done & 15) * 4]; float left; memcpy(&left, &r[2], 4); int tick = (int)r[0];
        log("move loop guard fired (%ld in this game process): game %d:%02d (tick %d) - a melee unit stood exactly in its target's "
            "centre, the engine's movement loop was cut (mover %08lX, %.6f s of the tick left)", *done + 1,
            tick >= 0 ? tick / 600 : -1, tick >= 0 ? (tick / 10) % 60 : 0, tick, r[1], left);
    }
    return k;
}
