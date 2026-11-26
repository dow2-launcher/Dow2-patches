// TexturePackFix.dll - the FX texture packer of DOW2: Retribution 3.19 no longer crashes on a texture without resource data.
//
// Crash (5 reports, 2 players: likebeat 17-18.11 x3, noofancy 22.11 + 26.11 at the start of a match on Hades Wastes):
//   C0000005 reading 0x00000008 at DOW2.exe+0x2B9B35, eax=0, esi=3 (mipDrop), on a worker thread of the platform pool.
// warnings.txt just before it: "Texture Pack size [4096] was overflowed with mipDrop [0..3]" - the engine packs the FX
// textures into a 4096 atlas again with fewer mips and on mipDrop 3 meets a texture whose resource data is absent.
//
// FUN_006B99F0 packs one texture and returns an Inv::RefBase BY VALUE (hidden pointer = its 2nd argument, [esp+0x164]
// in the body): its caller FUN_006B9910 calls RefBase::IsValid on it right away and destroys it at the end. In it:
//   006B9B2F  FF 15 90 93 F8 00   CALL [Inv::RefBase::GetResourceData]   ; may return NULL
//   006B9B35  8B 48 08            MOV ECX,[EAX+8]                        ; <-- the crash
//   006B9B38  8B 11               MOV EDX,[ECX]
//   006B9B3A  8B 42 18            MOV EAX,[EDX+0x18]
// The function already has a "nothing to pack" exit at 006B9A52: it default-constructs the returned RefBase
// (RefBase::RefBase() into the 2nd argument) and leaves through the common epilogue 006B9F39. Its stack is the same as at
// 006B9B35 (every push before is balanced, GetResourceData takes no arguments), so the fix sends the NULL case there:
// the caller gets an invalid RefBase, IsValid() is false and it takes its own "no texture" path (the same as for a
// texture the engine refuses to pack). Jumping straight to the epilogue instead would leave the returned RefBase
// unconstructed - the caller would then use and destroy stack garbage.
// Client-side render path only (the FX atlas); no simulation code -> no sync impact (same class as ReinforceFix).
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define PATCH_AT   0x006B9B35u   /* MOV ECX,[EAX+8]; MOV EDX,[ECX] (5 bytes) */
#define CONT_AT    0x006B9B3Au   /* the instruction after those two */
#define EMPTY_RET  0x006B9A52u   /* the function's own "return an empty RefBase" exit */
static const unsigned char ORIG[5] = {0x8B, 0x48, 0x08, 0x8B, 0x11};
static const unsigned char EMPTY_RET_BYTES[13] = {0x8B, 0xB4, 0x24, 0x64, 0x01, 0x00, 0x00,   /* mov esi,[esp+0x164] */
                                                  0x8B, 0xCE,                                 /* mov ecx,esi */
                                                  0xFF, 0x15, 0x8C, 0x93};                    /* call [RefBase::RefBase] */
static wchar_t g_log[MAX_PATH];
static volatile LONG g_hits;

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
// called from the code cave (all registers and flags saved around it)
static void __cdecl on_null(void) {
    LONG n = InterlockedIncrement(&g_hits);
    if (n <= 10 || (n & (n - 1)) == 0)
        logmsg("FX texture without resource data skipped (%ld so far) - the game would have closed here", n);
}
static int code_ok(const void *p, int len) {
    MEMORY_BASIC_INFORMATION mi;
    return VirtualQuery(p, &mi, sizeof mi) && mi.State == MEM_COMMIT &&
           (const char *)p + len <= (const char *)mi.BaseAddress + mi.RegionSize;
}
// the mutex all our DLLs share for patching the game's code (one patch at a time in the game process)
static HANDLE patch_lock(void) {
    char n[64]; wsprintfA(n, "Local\\dow2patch_iat_%lu", GetCurrentProcessId());
    HANDLE mx = CreateMutexA(NULL, FALSE, n); if (mx) WaitForSingleObject(mx, 5000); return mx;
}
static void install(void) {
    unsigned char *t = (unsigned char *)PATCH_AT;
    if (!code_ok(t, 5) || memcmp(t, ORIG, 5) || !code_ok((void *)EMPTY_RET, 13) ||
        memcmp((void *)EMPTY_RET, EMPTY_RET_BYTES, sizeof EMPTY_RET_BYTES)) {
        logmsg("DOW2.exe is not 3.19 (no match at %08X / %08X) - not patching", PATCH_AT, EMPTY_RET); return; }
    /* code cave:
       00 85 C0          test eax,eax
       02 74 0A          jz   0E
       04 8B 48 08       mov  ecx,[eax+8]        (original)
       07 8B 11          mov  edx,[ecx]          (original)
       09 E9 rel32       jmp  CONT_AT
       0E 60             pushad
       0F 9C             pushfd
       10 E8 rel32       call on_null
       15 9D             popfd
       16 61             popad
       17 E9 rel32       jmp  EMPTY_RET          (empty RefBase returned, the caller skips the texture) */
    unsigned char *c = (unsigned char *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!c) { logmsg("VirtualAlloc failed - not patching"); return; }
    c[0] = 0x85; c[1] = 0xC0; c[2] = 0x74; c[3] = 0x0A;
    c[4] = 0x8B; c[5] = 0x48; c[6] = 0x08; c[7] = 0x8B; c[8] = 0x11;
    c[9] = 0xE9; *(DWORD *)(c + 10) = CONT_AT - (DWORD)(c + 14);
    c[14] = 0x60; c[15] = 0x9C;
    c[16] = 0xE8; *(DWORD *)(c + 17) = (DWORD)on_null - (DWORD)(c + 21);
    c[21] = 0x9D; c[22] = 0x61;
    c[23] = 0xE9; *(DWORD *)(c + 24) = EMPTY_RET - (DWORD)(c + 28);
    FlushInstructionCache(GetCurrentProcess(), c, 28);
    unsigned char j[5] = {0xE9}; *(DWORD *)(j + 1) = (DWORD)c - (DWORD)(t + 5);
    HANDLE mx = patch_lock(); DWORD old; SIZE_T w = 0; int ok = 0;
    if (VirtualProtect(t, 5, PAGE_EXECUTE_READWRITE, &old)) {
        ok = WriteProcessMemory(GetCurrentProcess(), t, j, 5, &w) && w == 5;
        VirtualProtect(t, 5, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), t, 5);
    if (mx) { ReleaseMutex(mx); CloseHandle(mx); }
    logmsg(ok ? "TexturePackFix v1: FX textures without resource data are skipped instead of crashing (patched %08X)"
              : "TexturePackFix v1: could not write the patch at %08X", PATCH_AT);
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
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"TexturePackFix.ini"); wcscat(g_log, L"TexturePackFix.log");
    if (already_loaded("TexturePackFix")) { logmsg("another TexturePackFix copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"TexturePackFix", L"Enabled", 1, ini)) { logmsg("disabled in TexturePackFix.ini"); return TRUE; }
    install();
    return TRUE;
}
