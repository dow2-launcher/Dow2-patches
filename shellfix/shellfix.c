// ShellFix.dll v1 - the game no longer closes at the start of a match when Elite's ShellPatch.dll could not hook in
// (Dawn of War II: Retribution 3.19, Elite 3.0-3.2; ShellPatch.dll of 17.07.2024, TimeDateStamp 669C7309, md5 7332d98b...).
//
// What ShellPatch.dll does (the meter above the command panel: Waaagh for orks, the race meters of the new races):
//  - DOW2.exe 0x00B6CF40 is the "activate" of the meter shell of the HUD (vtable 0x0111953C slot +0x10): it finds the five
//    race meters of the movie (/waaagh_meter_shell/meter_mc/sm, ig, eld, tyr, csm -> this+0x40..0x50), hides them all and
//    shows the one of the local player's race. It runs once per match, on the first frame of the match HUD.
//  - ShellPatch puts four jumps into the game's code when it is loaded (0x00B3C18A widgets of Elite.shells, 0x00B6CF6F,
//    0x00B6D023, 0x00B6D0A7) and wraps 0x00B6CF40 itself with Detours (ShellPatch+0x3A10). The wrapper is what reads the
//    player's race into ShellPatch (std::string at ShellPatch+0xF2B8) before the game's function runs; the jump at
//    0x00B6D0A7 (ShellPatch+0x3E40) then shows the meter of that race: SetVisible(widget, 1) = DOW2.exe 0x00685450,
//    which writes widget+0x209 (the visible flag) - with NO check of the widget.
//  - ShellPatch's Detours (version 3) puts its trampoline only into target +-2 GB without 0x70000000-0x80000000, i.e. for
//    0x00B6CF40 only into the lower 1.75 GB. When there is no free 64 KB block there at the moment ShellPatch starts (a lot
//    of video driver code - an Intel and an NVIDIA driver both in the game -, the game's own reservations, and every add-on
//    DLL before it: their threads' stacks, their small VirtualAllocs of 64 KB each), DetourAttach fails and ShellPatch does
//    not check it: the four jumps are in, the wrapper is not. The first match: the race string is empty, no meter matches,
//    ShellPatch takes the widget of its first entry (none = NULL) and SetVisible(NULL) reads 0x00000209 - access violation,
//    BugSplat closes the game (TerminateProcess -3, no Windows crash record). Seen at BeArD 04.11.2026 (CrashLog dump:
//    0xC0000005 at DOW2.exe+0x285450 reading 0x209, ecx=0, return to ShellPatch.dll+0x3E65, the game's frame returns straight
//    to 0x00AD5A92 - no ShellPatch wrapper in between); repeated on the stand by filling the lower address space before
//    ShellPatch starts: the same crash 1 s into the match, ShellPatch's hook target pointer still 0x00B6CF40.
//
// ShellFix waits until ShellPatch.dll is loaded and has started (its DllMain is over), then looks: ShellPatch's jumps in,
// 0x00B6CF40 untouched and ShellPatch's Detours pointer still 0x00B6CF40 = the wrapper is missing. Then it puts the wrapper
// in itself, exactly as Detours would have: a trampoline (the five bytes push ecx/ebx/ebp/esi/edi and a jump back to
// 0x00B6CF45) anywhere in memory - in 32-bit code a jump reaches every address -, ShellPatch's pointer set to it, and a
// jump to ShellPatch+0x3A10 at 0x00B6CF40. ShellPatch then works as it should (all race meters). In every other state
// (ShellPatch hooked fine, another ShellPatch build, no ShellPatch, the game's bytes not 3.19) ShellFix changes nothing.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define HUD_FN      0x00B6CF40                   // the meter shell's activate (DOW2.exe 3.19)
#define HUD_SHOW    0x00B6D0A7                   // ShellPatch's jump to its "show the race meter" (ShellPatch+0x3E40)
#define SP_STAMP    0x669C7309                   // ShellPatch.dll TimeDateStamp (Elite 3.0, 3.0.1, 3.1, 3.2 - the same file)
#define SP_IMAGE    0x15000                      // its SizeOfImage
#define SP_WRAPPER  0x3A10                       // its Detours detour of HUD_FN
#define SP_SHOW     0x3E40                       // its code at HUD_SHOW
#define SP_TARGET   0xF2A0                       // its Detours pointer: HUD_FN before the attach, the trampoline after
static const unsigned char HUD_PROLOGUE[11] = {0x51, 0x53, 0x55, 0x56, 0x57, 0x8B, 0x3D, 0x00, 0x91, 0xF8, 0x00};
static const unsigned char SP_WRAPPER_START[5] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF};    // push ebp; mov ebp,esp; push -1

static wchar_t g_log[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
// the mutex all our DLLs share for patching the game's code (one patch at a time in the game process)
static HANDLE patch_lock(void) {
    char n[64]; wsprintfA(n, "Local\\dow2patch_iat_%lu", GetCurrentProcessId());
    HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx) WaitForSingleObject(mx, 5000); return mx;
}
static void patch_unlock(HANDLE mx) { if (mx) { ReleaseMutex(mx); CloseHandle(mx); } }
static int code_write(void *at, const void *b, int len) {
    HANDLE mx = patch_lock(); DWORD o; SIZE_T w = 0; int ok = 0;
    if (VirtualProtect(at, len, PAGE_EXECUTE_READWRITE, &o)) {
        ok = WriteProcessMemory(GetCurrentProcess(), at, b, len, &w) && w == (SIZE_T)len;
        VirtualProtect(at, len, o, &o);
    }
    FlushInstructionCache(GetCurrentProcess(), at, len); patch_unlock(mx); return ok;
}
static DWORD jmp_dest(const unsigned char *at) { return (DWORD)at + 5 + *(const DWORD *)(at + 1); }
// free address space in [lo, hi) in blocks of at least 64 KB (what Detours could have used), in MB
static unsigned free_mb(DWORD lo, DWORD hi) {
    unsigned long long sum = 0; MEMORY_BASIC_INFORMATION mi;
    for (DWORD p = lo; p < hi && VirtualQuery((void *)p, &mi, sizeof mi); ) {
        DWORD b = (DWORD)mi.BaseAddress, e = b + mi.RegionSize; if (e <= b || e > hi) e = hi;
        if (mi.State == MEM_FREE) { DWORD a = (b + 0xFFFF) & ~0xFFFF; if (e > a && e - a >= 0x10000) sum += (e - a) & ~0xFFFF; }
        if (e <= p) break;
        p = e;
    }
    return (unsigned)(sum >> 20);
}
static void check(unsigned char *sp) {
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(sp + ((IMAGE_DOS_HEADER *)sp)->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != SP_STAMP || nt->OptionalHeader.SizeOfImage != SP_IMAGE) {
        logmsg("ShellPatch.dll is another build (stamp %08lX, size %lX) - nothing changed", nt->FileHeader.TimeDateStamp,
               nt->OptionalHeader.SizeOfImage); return; }
    unsigned char *hud = (unsigned char *)HUD_FN, *show = (unsigned char *)HUD_SHOW;
    DWORD *target = (DWORD *)(sp + SP_TARGET);
    if (hud[0] == 0xE9) {
        logmsg("ShellPatch hooked in as it should (the meter shell -> %08lX) - nothing to do", jmp_dest(hud)); return; }
    if (show[0] != 0xE9 || jmp_dest(show) != (DWORD)(sp + SP_SHOW)) {
        logmsg("ShellPatch's jumps are not in the game's code (%02X at %08X) - nothing to do", show[0], HUD_SHOW); return; }
    if (memcmp(hud, HUD_PROLOGUE, sizeof HUD_PROLOGUE) || *target != HUD_FN || memcmp(sp + SP_WRAPPER, SP_WRAPPER_START, 5)) {
        logmsg("unexpected state (meter shell %02X %02X %02X %02X %02X, ShellPatch pointer %08lX) - nothing changed",
               hud[0], hud[1], hud[2], hud[3], hud[4], *target); return; }
    // ShellPatch's jumps are in, its wrapper is not: the first match would crash (SetVisible(NULL) at 0x00685450)
    unsigned char *tr = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN, PAGE_EXECUTE_READWRITE);
    if (!tr) { logmsg("ShellPatch could not hook in and there is no memory for its trampoline either - nothing changed"); return; }
    memcpy(tr, hud, 5); tr[5] = 0xE9; *(DWORD *)(tr + 6) = (HUD_FN + 5) - (DWORD)(tr + 10);
    FlushInstructionCache(GetCurrentProcess(), tr, 16);
    DWORD o; if (!VirtualProtect(target, 4, PAGE_READWRITE, &o)) { logmsg("ShellPatch's pointer not writable - nothing changed"); return; }
    *target = (DWORD)tr; VirtualProtect(target, 4, o, &o);
    unsigned char j[5]; j[0] = 0xE9; *(DWORD *)(j + 1) = (DWORD)(sp + SP_WRAPPER) - (HUD_FN + 5);
    if (!code_write(hud, j, 5)) { *target = HUD_FN; logmsg("could not write the jump at %08X - nothing changed", HUD_FN); return; }
    logmsg("REPAIRED: ShellPatch.dll had not hooked the meter shell %08X (its Detours found no room for a trampoline: free "
           "address space now below 0x70000000 %u MB, 0x80000000-0xFFFE0000 %u MB) - its wrapper put in (trampoline %p): "
           "no crash at the start of a match, the race meters work", HUD_FN, free_mb(0x10000, 0x70000000),
           free_mb(0x80000000, 0xFFFE0000), tr);
}
static DWORD WINAPI worker(LPVOID _) {
    HMODULE m = NULL;
    for (int i = 0; i < 6000 && !(m = GetModuleHandleA("ShellPatch.dll")); i++) Sleep(100);      // up to 10 minutes
    if (!m) { logmsg("ShellPatch.dll not loaded in 10 minutes - nothing to do"); return 0; }
    // LoadLibrary of a module that is loading waits for the loader: ShellPatch's DllMain (its patches, its Detours) is over
    wchar_t p[MAX_PATH]; GetModuleFileNameW(m, p, MAX_PATH);
    HMODULE m2 = LoadLibraryW(p);
    if (m2 != m) { logmsg("ShellPatch.dll changed while loading - nothing to do"); if (m2) FreeLibrary(m2); return 0; }
    check((unsigned char *)m);
    FreeLibrary(m2);
    return 0;
}
// one copy per game process: a patch can come both from Elite_DLLs (launcher) and from dow2-patches\ (winmm loader)
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE mx = CreateMutexA(NULL, FALSE, n);
    return mx && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"ShellFix.ini"); wcscat(g_log, L"ShellFix.log");
    if (already_loaded("ShellFix")) { logmsg("another ShellFix copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"ShellFix", L"Enabled", 1, ini)) { logmsg("disabled in ShellFix.ini"); return TRUE; }
    unsigned char *hud = (unsigned char *)HUD_FN;
    if (IsBadReadPtr(hud, sizeof HUD_PROLOGUE) || (hud[0] != 0xE9 && memcmp(hud, HUD_PROLOGUE, sizeof HUD_PROLOGUE))) {
        logmsg("DOW2.exe is not 3.19 (no meter shell at %08X) - nothing to do", HUD_FN); return TRUE; }
    logmsg("ShellFix 1: watching ShellPatch.dll's hook of the HUD meter (free address space below 0x70000000: %u MB)",
           free_mb(0x10000, 0x70000000));
    CloseHandle(CreateThread(NULL, 0x10000, worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL));   // 64 KB, not 1 MB of low space
    return TRUE;
}
