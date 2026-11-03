// Continue.dll - the game's own Continue after a match from a Custom Games lobby (DoW2: Retribution 3.19, Elite).
//
// Owner 03.11.2026: "the host presses Continue, the others have it greyed out, and everyone is taken straight back into the
// room - the game has this logic". It is all still in DOW2.exe (PostGameStatsForm::OnClickNext 0x480A71): the host broadcasts
// PPMT_TransitionToGameSetup over the match session (0x456976), every game goes to game_setup_movie of the SAME session
// (0x481880; the others get "You have been pulled back to game setup by the host"), and the host puts the room back into the
// lobby: SetMatchState 0, SetVisible 1 (advertisement/update state=0 visible=1).
// Why it did nothing: canContinue (0x47F7EF) returns false at once when the match type of the game (settings+0xEAC, copied
// from GameInfo+0xC0 at the start, 0x447538) is 1 = custom game, so the button stays grey for everyone. Relic's lobby server
// keeps a started custom room started and answers [4] to the host's update; our lobby server (own_lobby.py, "reopen") then
// sets the room back to the lobby on Relic (updateState 0) and repeats the update - without it the room is back for the
// players in it, it is only not in the Custom Games list.
// What this add-on changes, and only while the match session is still connected (a match with other people in it):
//   1. canContinue: for a custom game it answers as for the other match types (host + everybody on the post-game screen);
//   2. SetVisible on Continue (call at 0x480C79): a private room stays private (the game would list it - visible 1).
// Nothing in the simulation or the lobby protocol is touched. Log: Continue.log. Test only: Continue.ini [Continue]
// KeepSoloSession=1 - a match with one human (against computers) keeps its session (0x46FE8E jnz -> jmp), so the path can be
// run on one machine.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define VERSION "1"
#define FN_CANCONT     0x0047F7EF                   // canContinue (no args, result in AL); starts with mov eax, 0F081CDh
#define FN_ONCLICKNEXT 0x00480A71                   // PostGameStatsForm::OnClickNext (__fastcall, ecx = form)
#define FN_TO_SETUP    0x00481880                   // post-game screen: PartyProgresser callback, 0 = to game_setup, 1 = to matchmaking
#define AT_SETVISIBLE  0x00480C79                   // OnClickNext: call [eax+88h] = match->SetVisible(1)
#define G_MGR          0x01335014                   // network manager: vtbl+0x24 = match (RLink::MatchInternal)
#define G_SETTINGS     0x01335A78                   // +0xEAC match type (1 = custom game), +0xEB0 party
#define G_RLINK        0x01335030                   // +0x1AF8 = WorldwidePartyService
#define FN_FIND_SESS   0x00588C50                   // __thiscall(svc, id lo, id hi, &session)
#define SOLO_AT        0x0046FE8E                   // test: jnz over "Disconnect(0, OnStartMessage)" when the match has one human
#define MT_CUSTOM      1

static wchar_t g_log[MAX_PATH]; static CRITICAL_SECTION g_cs;
static void logmsg(const char *fmt, ...) {
    EnterCriticalSection(&g_cs);
    FILE *f = _wfopen(g_log, L"a");
    if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_cs);
}
static int rd_ok(const void *p, int n) { return p && !IsBadReadPtr(p, n); }
typedef char (__stdcall *MatchBool)(void *match);
typedef void (__stdcall *MatchSet)(void *match, int a);
typedef char (__fastcall *FindSess)(void *svc, void *edx, DWORD lo, DWORD hi, void **out);

static unsigned char *match_obj(void) {
    void **mgr = rd_ok((void *)G_MGR, 4) ? *(void ***)G_MGR : NULL; if (!rd_ok(mgr, 4) || !rd_ok(*mgr, 0x28)) return NULL;
    void *(__fastcall *get)(void *, void *) = ((void *(__fastcall **)(void *, void *))*mgr)[0x24 / 4];   // __thiscall, no args
    unsigned char *m = get(mgr, 0);
    return rd_ok(m, 0x10) && rd_ok(*(void **)m, 0x98) ? m : NULL;
}
static int match_valid(unsigned char *m) { return m && (*(DWORD *)(m + 8) & *(DWORD *)(m + 0xC)) != 0xFFFFFFFF; }
static unsigned char *match_session(unsigned char *m) {      // RLink's session of the match (the room), or NULL
    unsigned char *svc = rd_ok((void *)G_RLINK, 4) ? *(unsigned char **)G_RLINK : NULL; void *s = NULL;
    if (!match_valid(m) || !rd_ok(svc, 0x1B00)) return NULL;
    return ((FindSess)FN_FIND_SESS)(svc + 0x1AF8, 0, *(DWORD *)(m + 8), *(DWORD *)(m + 0xC), &s) && rd_ok(s, 0x350) ? s : NULL;
}
static int match_connected(unsigned char *m) { return match_valid(m) && ((MatchBool)(*(void ***)m)[0x70 / 4])(m); }

// ---- 1. canContinue ----
void *g_ccTramp; static int g_lastCc = -1;
char __stdcall cc_orig(void);
__asm__(".globl _cc_orig@0\n_cc_orig@0:\n  jmp *_g_ccTramp\n");
int __stdcall on_cc(int r) {
    unsigned char *st = rd_ok((void *)G_SETTINGS, 4) ? *(unsigned char **)G_SETTINGS : NULL;
    int custom = rd_ok(st, 0xEB4) && *(int *)(st + 0xEAC) == MT_CUSTOM, ours = 0;
    if (!r && custom) {
        unsigned char *m = match_obj();
        if (match_connected(m)) {                    // the same check with the custom type out of the way (main thread only)
            *(int *)(st + 0xEAC) = 0; r = (unsigned char)cc_orig(); *(int *)(st + 0xEAC) = MT_CUSTOM; ours = 1;
        }
    }
    if (r != g_lastCc) { g_lastCc = r; logmsg("Continue %s%s", r ? "on" : "off", ours ? " (custom game, session connected)" : custom ? " (custom game)" : ""); }
    return r;
}
__asm__(".globl _cc_stub\n_cc_stub:\n"
        "  call *_g_ccTramp\n  pushal\n  movzbl %al, %eax\n  pushl %eax\n  call _on_cc@4\n  movl %eax, 28(%esp)\n  popal\n  ret\n");
void cc_stub(void);

// ---- 2. SetVisible on Continue: keep a private room private ----
void __stdcall my_setvisible(unsigned char *m, int a) {
    unsigned char *s = match_session(m);
    int listed = s ? s[0x344] : 1;                   // session+0x344: the room has been listed ("Publishing Visible match")
    if (a && !listed) { a = 0; logmsg("Continue: a private room - stays out of the list"); }
    ((MatchSet)(*(void ***)m)[0x88 / 4])(m, a);
}

// ---- logging of the click and of the move to game_setup ----
void *g_nextTramp, *g_setupTramp;
void __stdcall on_click_next(unsigned char *form) {
    unsigned char *st = rd_ok((void *)G_SETTINGS, 4) ? *(unsigned char **)G_SETTINGS : NULL, *m = match_obj(), *s = match_session(m);
    logmsg("Continue pressed: enabled %d, match type %d, party %d, session %s%s", rd_ok(form, 0x68) ? form[0x64] : -1,
           rd_ok(st, 0xEB4) ? *(int *)(st + 0xEAC) : -1, rd_ok(st, 0xEB4) ? st[0xEB0] : -1, s ? "found" : "none",
           s ? (match_connected(m) ? ", connected" : ", NOT connected") : "");
}
void __stdcall on_to_setup(int arg) { logmsg("back to %s (the host's Continue)", arg == 0 ? "game setup" : arg == 1 ? "matchmaking" : "?"); }
__asm__(".globl _next_stub\n_next_stub:\n"
        "  pushal\n  pushl %ecx\n  call _on_click_next@4\n  popal\n  jmp *_g_nextTramp\n"
        ".globl _setup_stub\n_setup_stub:\n"
        "  pushal\n  pushl 36(%esp)\n  call _on_to_setup@4\n  popal\n  jmp *_g_setupTramp\n");
void next_stub(void); void setup_stub(void);

static int patch_jmp(DWORD at, const unsigned char *pro, int n, void *stub, void **tramp) {   // n bytes moved to a trampoline
    unsigned char *t = (unsigned char *)at;
    if (IsBadReadPtr(t, n) || memcmp(t, pro, n)) { logmsg("%08lX: bytes differ (not DOW2 3.19?) - not hooked", at); return 0; }
    unsigned char *tr = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); if (!tr) return 0;
    memcpy(tr, pro, n); tr[n] = 0xE9; *(DWORD *)(tr + n + 1) = (at + n) - (DWORD)(tr + n + 5); *tramp = tr;
    DWORD old; VirtualProtect(t, n, PAGE_EXECUTE_READWRITE, &old);
    t[0] = 0xE9; *(DWORD *)(t + 1) = (DWORD)stub - (DWORD)(t + 5); for (int i = 5; i < n; i++) t[i] = 0x90;
    VirtualProtect(t, n, old, &old); FlushInstructionCache(GetCurrentProcess(), t, n);
    return 1;
}
static int patch_call(DWORD at, const unsigned char *orig, int n, void *fn) {   // an n-byte call -> call fn (+ nops)
    unsigned char *t = (unsigned char *)at;
    if (IsBadReadPtr(t, n) || memcmp(t, orig, n)) { logmsg("%08lX: bytes differ (not DOW2 3.19?) - not patched", at); return 0; }
    DWORD old; VirtualProtect(t, n, PAGE_EXECUTE_READWRITE, &old);
    t[0] = 0xE8; *(DWORD *)(t + 1) = (DWORD)fn - (DWORD)(t + 5); for (int i = 5; i < n; i++) t[i] = 0x90;
    VirtualProtect(t, n, old, &old); FlushInstructionCache(GetCurrentProcess(), t, n);
    return 1;
}
static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_cs);
    GetModuleFileNameW(h, g_log, MAX_PATH); wchar_t *sl = wcsrchr(g_log, L'\\'); if (sl) sl[1] = 0; else g_log[0] = 0;
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"Continue.ini"); wcscat(g_log, L"Continue.log");
    if (already_loaded("Continue")) { logmsg("another Continue copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"Continue", L"Enabled", 1, ini)) { logmsg("Continue " VERSION ": disabled in Continue.ini"); return TRUE; }
    static const unsigned char CC[5] = {0xB8, 0xCD, 0x81, 0xF0, 0x00}, PRO6[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8},
                               SV[6] = {0xFF, 0x90, 0x88, 0x00, 0x00, 0x00};
    // all or nothing: the visibility fix without the button would never run, the button without it would list private rooms
    unsigned char *a = (unsigned char *)FN_CANCONT, *b = (unsigned char *)AT_SETVISIBLE;
    if (IsBadReadPtr(a, 5) || memcmp(a, CC, 5) || IsBadReadPtr(b, 6) || memcmp(b, SV, 6)) { logmsg("Continue " VERSION ": not DOW2.exe 3.19 - nothing changed"); return TRUE; }
    patch_jmp(FN_CANCONT, CC, 5, cc_stub, &g_ccTramp);
    patch_call(AT_SETVISIBLE, SV, 6, my_setvisible);
    int l1 = patch_jmp(FN_ONCLICKNEXT, PRO6, 6, next_stub, &g_nextTramp), l2 = patch_jmp(FN_TO_SETUP, PRO6, 6, setup_stub, &g_setupTramp);
    logmsg("Continue " VERSION ": the game's own Continue after a custom game (host: back to the room with everyone; private rooms stay private)%s",
           l1 && l2 ? "" : " - logging hooks partly off");
    if (GetPrivateProfileIntW(L"Continue", L"KeepSoloSession", 0, ini)) {
        unsigned char *t = (unsigned char *)SOLO_AT;
        if (!IsBadReadPtr(t, 2) && t[0] == 0x75 && t[1] == 0x0C) { DWORD old; VirtualProtect(t, 1, PAGE_EXECUTE_READWRITE, &old); t[0] = 0xEB;
            VirtualProtect(t, 1, old, &old); FlushInstructionCache(GetCurrentProcess(), t, 2); logmsg("TEST KeepSoloSession: a match with one human keeps its session"); }
    }
    return TRUE;
}
