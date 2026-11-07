// shotprobe.c - ShotProbe.dll, STAND ONLY (never for players): measures how weapons really fire, to check UnitStats 0.4's
// DPS of the firing cycle. Hooks (vtable slots, DOW2.exe 3.19), all on the simulation thread:
//   RangedWeapon::fire one shot   vtable 0x010FC38C + 0x74 (0x9AC0A0)   - one event per shot
//   ProjectileWeapon::fire        vtable 0x010FC434 + 0x74 (0x9AD170)   - one event per projectile
//   Ranged/ProjectileWeapon update vtable +0x5C (0xA4AA10)               - state changes: 6 windup (a cycle starts), 9 reload
//   MeleeWeapon update            vtable 0x010FC2EC + 0x5C (0x9AAC70)   - 6 windup, 7 -> 8 = a strike
// Each event: the sim tick (world +0x178), the weapon, the kind. Written to ShotProbe.log (next to the DLL) once a second by
// its own thread; the first time a weapon is seen: its weapon file, damage, the owner's squad blueprint.
// Reads only; the hooks call the game's own function unchanged. Build: i686-w64-mingw32-gcc -O2 -s -shared -o build/ShotProbe.dll
// unitstats/stand/shotprobe.c -static-libgcc -Wl,--exclude-all-symbols (not in the Makefile: never shipped).
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define G_WORLD 0x013356A4u
static wchar_t g_log[MAX_PATH];
typedef struct { int tick; void *w; char kind; } Ev;       // kind: 'S' shot, 'W' windup start, 'R' reload start, 'M' melee strike
#define NEV 65536
static Ev g_ev[NEV]; static volatile LONG g_head, g_tail;
typedef struct { void *w; char line[400]; } Known;
#define NKNOWN 512
static Known g_known[NKNOWN]; static volatile LONG g_nKnown, g_nKnownOut;

typedef const char *(__thiscall *S0_t)(const void *obj);
static S0_t p_pbgFile, p_sqName;
static int sim_tick(void) { char *w = *(char **)G_WORLD; return w ? *(int *)(w + 0x178) : -1; }
static void know(void *w) {
    for (int i = 0; i < g_nKnown; i++) if (g_known[i].w == w) return;
    if (g_nKnown >= NKNOWN) return;
    char *bag = *(char **)((char *)w + 0x54), *pbg = *(char **)((char *)w + 0x4C), *hp = *(char **)((char *)w + 0xAC);
    char *ent = hp ? *(char **)(hp + 0x14) : NULL, *sq = ent ? *(char **)(ent + 0x38) : NULL;
    Known *k = &g_known[g_nKnown];
    k->w = w;
    snprintf(k->line, sizeof k->line, "WEAPON %p vt %08lX %s dmg %.3f cd %d wu %d wd %d reload %d-%d x%d-%d burst %d %d-%d rof %.3f pps %d entity %p squad %p %s",
             w, *(DWORD *)w, pbg && p_pbgFile ? p_pbgFile(pbg) : "?", *(float *)(bag + 0x64), *(int *)(bag + 0xF0), *(int *)(bag + 0x10C),
             *(int *)(bag + 0x110), *(int *)(bag + 0x11C), *(int *)(bag + 0x120), *(int *)(bag + 0x124), *(int *)(bag + 0x128),
             *(bag + 0x170), *(int *)(bag + 0x174), *(int *)(bag + 0x178), *(float *)(bag + 0x17C), *(int *)(bag + 0x114),
             ent, sq, sq && p_sqName ? p_sqName(sq) : "?");
    InterlockedIncrement(&g_nKnown);
}
static void ev(void *w, char kind) {
    LONG h = g_head; if (h - g_tail >= NEV) return;
    Ev *e = &g_ev[h % NEV]; e->tick = sim_tick(); e->w = w; e->kind = kind;
    g_head = h + 1;
}
typedef char (__thiscall *Fire_t)(void *w, unsigned i, unsigned n);
typedef void (__thiscall *Upd_t)(void *w);
static Fire_t o_fireR, o_fireP; static Upd_t o_updR, o_updP, o_updM;
static char __thiscall hk_fireR(void *w, unsigned i, unsigned n) { know(w); ev(w, 'S'); return o_fireR(w, i, n); }
static char __thiscall hk_fireP(void *w, unsigned i, unsigned n) { know(w); ev(w, 'S'); return o_fireP(w, i, n); }
static void upd_common(void *w, int before) {
    int after = *(int *)((char *)w + 0x58);
    if (after != before) { if (after == 6) { know(w); ev(w, 'W'); } else if (after == 9) ev(w, 'R'); }
}
static void __thiscall hk_updR(void *w) { int b = *(int *)((char *)w + 0x58); o_updR(w); upd_common(w, b); }
static void __thiscall hk_updP(void *w) { int b = *(int *)((char *)w + 0x58); o_updP(w); upd_common(w, b); }
static void __thiscall hk_updM(void *w) {
    int b = *(int *)((char *)w + 0x58); o_updM(w); int a = *(int *)((char *)w + 0x58);
    if (b == 7 && a != 7) { know(w); ev(w, 'M'); }
    if (a == 6 && b != 6) { know(w); ev(w, 'W'); }
}
static int hook(DWORD slot, DWORD expect, void *fn, void **orig) {
    DWORD *s = (DWORD *)slot, old;
    if (*s != expect) return 0;
    if (!VirtualProtect(s, 4, PAGE_READWRITE, &old)) return 0;
    *orig = (void *)*s; *s = (DWORD)fn; VirtualProtect(s, 4, old, &old);
    return 1;
}
static DWORD WINAPI writer(LPVOID _) {
    for (int i = 0; i < 120 && !GetModuleHandleA("SimEngine.dll"); i++) Sleep(500);
    HMODULE se = GetModuleHandleA("SimEngine.dll");
    p_pbgFile = (S0_t)GetProcAddress(se, "?GetFilename@PropertyBagGroup@@QBEPBDXZ");
    p_sqName = (S0_t)GetProcAddress(se, "?GetBlueprintName@Squad@@QBEPBDXZ");
    int ok = hook(0x010FC400, 0x009AC0A0, (void *)hk_fireR, (void **)&o_fireR) + hook(0x010FC4A8, 0x009AD170, (void *)hk_fireP, (void **)&o_fireP)
           + hook(0x010FC3E8, 0x00A4AA10, (void *)hk_updR, (void **)&o_updR) + hook(0x010FC490, 0x00A4AA10, (void *)hk_updP, (void **)&o_updP)
           + hook(0x010FC348, 0x009AAC70, (void *)hk_updM, (void **)&o_updM);
    FILE *f = _wfopen(g_log, L"a"); if (f) { fprintf(f, "ShotProbe: %d of 5 hooks\n", ok); fclose(f); }
    for (;;) {
        Sleep(1000);
        f = _wfopen(g_log, L"a"); if (!f) continue;
        while (g_nKnownOut < g_nKnown) { fprintf(f, "%s\n", g_known[g_nKnownOut].line); g_nKnownOut++; }
        while (g_tail < g_head) { Ev *e = &g_ev[g_tail % NEV]; fprintf(f, "%d %c %p\n", e->tick, e->kind, e->w); g_tail++; }
        fclose(f);
    }
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    static wchar_t dir[MAX_PATH];
    GetModuleFileNameW(h, dir, MAX_PATH); wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) sl[1] = 0; else dir[0] = 0;
    _snwprintf(g_log, MAX_PATH, L"%lsShotProbe.log", dir);
    CloseHandle(CreateThread(NULL, 0, writer, NULL, 0, NULL));
    return TRUE;
}
