// NoHints.dll - no beginner hints in a match: "select your squad", "build a generator", "capture the victory points", "you
// have a lot of resources", "population cap reached", the WAAAGH/faith bar hint... (Dawn of War II: Retribution 3.19, Elite
// and vanilla). Orca 12.11.2026: experienced players do not need them.
//
// Where they come from: simulation/scar/skirmishquicktips.scar (vanilla 000_data.sga; Elite has no own copy), started by
// Skirmish_Start in simulation/scar/skirmish.scar (vanilla and Elite's Elite_Data_18_Misc.sga, same lines):
//     if Misc_QuickTipsEnabled() then Rule_AddInterval( Skirmish_UpdateQuickTips, 1 ) end
// The tips are HintPoint_Add markers + UI_CreateEventCueTutorialWithImages cues (with speech) for the LOCAL player only;
// Relic's own comment there: "Non-deterministic EVIL! Everything after this point shouldn't touch the simulation".
// Misc_QuickTipsEnabled is DOW2.exe 0x00AFE940 (registered at 0x00B023F9 with the name at 0x0110F864):
//     return SystemConfig::Instance()->GetSetting(Key "ShowQuicktips") != 0
// i.e. the player's own setting "showquicktips" (Settings\configuration.lua; the in-game Options -> Game "quick tips" radio of
// ui/movies/hud/ingame_game_options.gfx). Players with it on and off already play the same matches - it is not synchronised.
//
// NoHints: the first 3 bytes of 0x00AFE940 become "xor eax,eax; ret" (the 26 bytes of the function are checked first, another
// build is left alone and logged). Misc_QuickTipsEnabled() then answers false: Skirmish_UpdateQuickTips is never started. The
// player's setting is not changed (nothing is written to configuration.lua; without the add-on the tips are as before).
// No archive is touched, the data checksum stays. NoHints.ini [NoHints]: Enabled=1.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define NH_VERSION 1
#define FN_QUICKTIPS 0x00AFE940u                       // Misc_QuickTipsEnabled (Lua binding, no arguments, returns bool in AL/EAX)
#define AT_REGISTER  0x00B023F9u                       // push 0x00AFE940; push "Misc_QuickTipsEnabled"
#define STR_NAME     0x0110F864u

static wchar_t g_dir[MAX_PATH];
static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsNoHints.log", g_dir);
    FILE *f = _wfopen(p, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static void hex(char *o, const unsigned char *b, int n) { for (int i = 0; i < n; i++) sprintf(o + i * 3, "%02X ", b[i]); }

static const unsigned char ORIG[26] = {
    0x68, 0xE0, 0xA2, 0x3A, 0x01,           // push 0x013AA2E0 (Key "ShowQuicktips")
    0xFF, 0x15, 0x44, 0x90, 0xF8, 0x00,     // call [SystemConfig::Instance]
    0x8B, 0xC8,                             // mov ecx,eax
    0xFF, 0x15, 0xBC, 0x8F, 0xF8, 0x00,     // call [SystemConfig::GetSetting]
    0xF7, 0xD8, 0x1B, 0xC0, 0xF7, 0xD8,     // neg; sbb; neg  (!= 0)
    0xC3 };                                 // ret
static const unsigned char REG[10] = { 0x68, 0x40, 0xE9, 0xAF, 0x00, 0x68, 0x64, 0xF8, 0x10, 0x01 };
static const unsigned char PATCH[3] = { 0x33, 0xC0, 0xC3 };      // xor eax,eax; ret

static void apply(void) {
    unsigned char *f = (unsigned char *)FN_QUICKTIPS, *r = (unsigned char *)AT_REGISTER;
    char h[100];
    if (IsBadReadPtr(f, sizeof ORIG) || IsBadReadPtr(r, sizeof REG) || IsBadReadPtr((void *)STR_NAME, 22)) {
        logmsg("no DOW2.exe 3.19 code at %08X - not changing anything", FN_QUICKTIPS); return; }
    if (!memcmp(f, PATCH, sizeof PATCH) && !memcmp(f + 3, ORIG + 3, sizeof ORIG - 3)) { logmsg("already patched"); return; }
    if (memcmp(f, ORIG, sizeof ORIG) || memcmp(r, REG, sizeof REG) || memcmp((void *)STR_NAME, "Misc_QuickTipsEnabled", 22)) {
        hex(h, f, 26); logmsg("not the DOW2.exe 3.19 this was made for (bytes at %08X: %s) - not changing anything", FN_QUICKTIPS, h); return; }
    DWORD old;
    if (!VirtualProtect(f, sizeof PATCH, PAGE_EXECUTE_READWRITE, &old)) { logmsg("VirtualProtect failed, error %lu", GetLastError()); return; }
    memcpy(f, PATCH, sizeof PATCH);
    VirtualProtect(f, sizeof PATCH, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f, sizeof PATCH);
    hex(h, f, 8);
    logmsg("Misc_QuickTipsEnabled (%08X) answers false: no beginner hints in matches (bytes now %s)", FN_QUICKTIPS, h);
}

static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
    wchar_t ini[MAX_PATH]; _snwprintf(ini, MAX_PATH, L"%lsNoHints.ini", g_dir);
    if (already_loaded("NoHints")) { logmsg("another NoHints copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"NoHints", L"Enabled", 1, ini)) { logmsg("NoHints %d: disabled in NoHints.ini", NH_VERSION); return TRUE; }
    logmsg("NoHints %d", NH_VERSION);
    apply();
    return TRUE;
}
