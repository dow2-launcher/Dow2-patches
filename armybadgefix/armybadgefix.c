// ArmyBadgeFix.dll - a computer player with no army (pink "missing texture" squares where the army badge goes), DoW2:
// Retribution 3.19, Elite and vanilla.
//
// How it happens (stand 1, 14.11.2026, vanilla and Elite alike): in a custom game a human leaves his team slot (moves to an
// observer slot or to another team slot) and a computer is added into that slot. The slot keeps the human's race and hero
// (the computer shows "Orks / Warboss" at once) but its army (chapter name, colours, badge) is empty; a computer gets an army
// only when its race is Random (picked at the start) or when its hero screen is confirmed in the lobby. At the match start
// TeamColourManager (FUN_00806F80) copies each slot's army into its list {player id -> chapter}:
//     entry = list[playerId];                 // 0x0080716E call 0x00809760 (find or insert)
//     copy(entry, &slotChapter);              // 0x00807177 call 0x004893FB (chapter assignment)
// and warnings.txt shows it: "PlayerID : 1000, chapterName : , paintIndex : (0, 0, 0, 0), badge : , patternIndex : 0".
// Every unit and building of that player is then painted with colour 0 and an empty badge texture.
//
// Fix: the call at 0x0080716E goes through a stub. When the slot's chapter has no badge and no name, it asks the game's own
// GetPlayerChapter (0x00805F80) for this player before he is in the list - the game then falls back to the first default
// army of the player's race (its own code path, "will use the first default chapter" in warnings.txt) - and copies that into
// the slot's chapter with the game's own assignment (0x004893FB); then the original call goes on. A chapter that has a badge
// or a name (every human's army, every normal computer) is left alone.
// Team colours and badges are drawing only: nothing of the simulation, the lobby data or the replay is changed (a replay of
// such a match shows the default army again only with this DLL). All 33 bytes around the call are checked first.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define SITE      0x0080715B   // mov edx,[esp+1Ch] ... call 0x00809760 ... call 0x004893FB
#define CALL_AT   0x0080716E
#define FIND_FN   0x00809760
#define GETCHAP   0x00805F80
#define COPYCHAP  0x004893FB
#define TCM_PTR   0x01335690
static const unsigned char ORIG[33] = {
    0x8B, 0x54, 0x24, 0x1C,                 // mov edx,[esp+1Ch]   (the list)
    0x8D, 0x4C, 0x24, 0x20,                 // lea ecx,[esp+20h]   (the slot's chapter, a local copy)
    0x51, 0x51, 0x52,                       // push ecx ; push ecx ; push edx
    0x8D, 0x4C, 0x24, 0x1C,                 // lea ecx,[esp+1Ch]   (the key)
    0x89, 0x44, 0x24, 0x1C,                 // mov [esp+1Ch],eax   (player id)
    0xE8, 0xED, 0x25, 0x00, 0x00,           // call 0x00809760     <- goes through the stub
    0x83, 0xC4, 0x04,                       // add esp,4
    0x50,                                   // push eax
    0xE8, 0x7F, 0x22, 0xC8, 0xFF};          // call 0x004893FB
static wchar_t g_log[MAX_PATH];

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// chapter: +0x00 paint indices (4 x int), +0x10 pattern, +0x14 badge path (char[256]), +0x114 LocString name, ...
typedef const unsigned char *(__stdcall *GetChapFn)(void *tcm, int playerId);
typedef void (__stdcall *CopyFn)(void *dst, const void *src);   // ret 8
typedef const wchar_t *(__thiscall *CstrFn)(const void *locstr);
static CstrFn loc_cstr;
static volatile LONG g_fixed;

static const wchar_t *chap_name(const unsigned char *c) { const wchar_t *n = loc_cstr ? loc_cstr(c + 0x114) : NULL; return n ? n : L""; }
static int chap_empty(const unsigned char *c) { return c[0x14] == 0 && chap_name(c)[0] == 0; }

static void __cdecl on_slot_chapter(const int *key, unsigned char *chap) {   // game thread, match load, once per player
    int pid = *key;
    if (!chap_empty(chap)) return;
    void *tcm = *(void **)TCM_PTR;
    if (!tcm) { logmsg("player %d has no army, but the team colour manager is not there - left as it is", pid); return; }
    const unsigned char *def = ((GetChapFn)GETCHAP)(tcm, pid);
    if (!def || chap_empty(def)) { logmsg("player %d has no army and the game has no default army for his race - left as it is", pid); return; }
    ((CopyFn)COPYCHAP)(chap, def);
    InterlockedIncrement(&g_fixed);
    logmsg("player %d had no army (a computer in a slot a human left): given the default army of his race, %ls (badge %s, "
           "paint %d %d %d %d)", pid, chap_name(chap), (const char *)chap + 0x14,
           ((int *)chap)[0], ((int *)chap)[1], ((int *)chap)[2], ((int *)chap)[3]);
}

static void install(void) {
    const unsigned char *t = (const unsigned char *)SITE;
    if (IsBadReadPtr(t, sizeof ORIG)) { logmsg("no code at %08X (not DOW2.exe 3.19?) - not patching", SITE); return; }
    if (memcmp(t, ORIG, sizeof ORIG)) {
        char hex[3 * sizeof ORIG + 1]; for (unsigned i = 0; i < sizeof ORIG; i++) sprintf(hex + 3 * i, "%02X ", t[i]);
        logmsg("DOW2.exe is not 3.19 or already changed (bytes at %08X: %s) - not patching", SITE, hex); return;
    }
    HMODULE loc = GetModuleHandleA("Localizer.dll");
    loc_cstr = loc ? (CstrFn)GetProcAddress(loc, "?c_str@LocString@@QBEPB_WXZ") : NULL;
    if (!loc_cstr) { logmsg("Localizer.dll LocString::c_str not found - not patching"); return; }
    unsigned char *s = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE), *p = s;
    if (!s) { logmsg("VirtualAlloc failed (%lu)", GetLastError()); return; }
    #define B(x) (*p++ = (unsigned char)(x))
    #define D(x) (*(DWORD *)p = (DWORD)(x), p += 4)
    B(0x60);                                             // pushad
    B(0xFF); B(0x74); B(0x24); B(0x28);                  // push [esp+28h]   (the chapter: [esp+8] at the stub's entry)
    B(0x51);                                             // push ecx         (the key: player id)
    B(0xE8); D((DWORD)on_slot_chapter - (DWORD)(p + 4)); // call on_slot_chapter
    B(0x83); B(0xC4); B(0x08);                           // add esp,8
    B(0x61);                                             // popad
    B(0xE9); D(FIND_FN - (DWORD)(p + 4));                // jmp 0x00809760 (returns to 0x00807173)
    unsigned char *c = (unsigned char *)CALL_AT;
    LONGLONG oldv = *(volatile LONGLONG *)c, newv = oldv; unsigned char *nb = (unsigned char *)&newv;
    *(LONG *)(nb + 1) = (LONG)(s - (c + 5));
    DWORD old; if (!VirtualProtect(c, 8, PAGE_EXECUTE_READWRITE, &old)) { logmsg("VirtualProtect failed (%lu)", GetLastError()); return; }
    InterlockedCompareExchange64((LONGLONG *)c, newv, oldv);
    VirtualProtect(c, 8, old, &old); FlushInstructionCache(GetCurrentProcess(), c, 8);
    logmsg("ArmyBadgeFix 1: a player whose slot has no army gets the default army of his race (call %08X -> stub %p)", CALL_AT, s);
}

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
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"ArmyBadgeFix.ini"); wcscat(g_log, L"ArmyBadgeFix.log");
    if (already_loaded("ArmyBadgeFix")) { logmsg("another ArmyBadgeFix copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"ArmyBadgeFix", L"Enabled", 1, ini)) { logmsg("disabled in ArmyBadgeFix.ini"); return TRUE; }
    install();
    return TRUE;
}
