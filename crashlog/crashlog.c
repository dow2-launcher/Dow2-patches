// CrashLog.dll - what closed the game (owner 02.11.2026: "прислать длл для более детальных логов"; first case: BeArD,
// the game vanished 20-30 s into a match with no Windows crash record and nothing in our logs).
// Writes Elite_DLLs\CrashLog.log (and CrashLog_<time>.dmp on a crash): no hooks into the game itself, only
//  - serious exceptions (access violation, stack overflow, illegal instruction, divide by zero, the /GS and fast-fail
//    codes) with the module+offset of the faulting address, the registers and a stack walk - first chance, so a
//    crash the game handles itself is seen too (the first 30 per run);
//  - an unhandled exception: the same, plus a minidump (dbghelp, small: threads + the faulting stack);
//  - the game ending itself: ExitProcess / TerminateProcess (of itself) / exit via the C runtime - with who called it
//    (module+offset and the stack), so "the game just closed" names the code that closed it;
//  - the process going away (DLL detach at exit).
// Its own add-on and tick (docs/ADDON_RULES.md); nothing is sent anywhere - the launcher's "Send logs" carries the log.
#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdarg.h>

static wchar_t g_dir[MAX_PATH]; static CRITICAL_SECTION g_lk; static volatile LONG g_seen, g_dumped;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter;
static CONTEXT g_lastCtx; static EXCEPTION_RECORD g_lastRec; static volatile DWORD g_lastTid;   // the last serious exception, for the dump at the exit
static __thread int t_in;                                         // our own reporting never reports itself
typedef VOID (WINAPI *ExitProcessFn)(UINT); typedef BOOL (WINAPI *TerminateProcessFn)(HANDLE, UINT);
static ExitProcessFn g_exitOrig; static TerminateProcessFn g_termOrig;

static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsCrashLog.log", g_dir);
    EnterCriticalSection(&g_lk); FILE *f = _wfopen(p, L"a");
    if (f) { SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d [%lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId());
             va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f); }
    LeaveCriticalSection(&g_lk);
}
static void where(DWORD addr, char *out, int cap) {               // "Module.dll+0x1234" for an address
    HMODULE m = NULL; char path[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(UINT_PTR)addr, &m) && m) {
        GetModuleFileNameA(m, path, MAX_PATH); const char *b = strrchr(path, '\\');
        snprintf(out, cap, "%s+0x%lX", b ? b + 1 : path, addr - (DWORD)(UINT_PTR)m);
    } else snprintf(out, cap, "0x%08lX (no module)", addr);
}
static int code_addr(DWORD v) {                                  // an address inside an executable section of a loaded module
    HMODULE m; MEMORY_BASIC_INFORMATION mb;
    return v > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(UINT_PTR)v, &m)
        && VirtualQuery((LPCVOID)(UINT_PTR)v, &mb, sizeof mb) && (mb.Protect & 0xF0);
}
static void stack_from(DWORD ebp, DWORD esp, DWORD eip) {          // frame-pointer walk, then a scan of the stack for return addresses
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb(); DWORD lo = esp, hi = (DWORD)(UINT_PTR)tib->StackBase; char w[160];
    if (esp < (DWORD)(UINT_PTR)tib->StackLimit || esp >= hi) { logmsg("  (the faulting stack is not this thread's - no walk)"); return; }
    where(eip, w, sizeof w); logmsg("  at %s; call chain:", w);
    DWORD fp = ebp; int n = 0;
    for (; n < 24 && fp >= lo && fp + 8 <= hi; n++) {               // only inside [esp, stack base): never reads outside the stack
        DWORD ret = ((DWORD *)(UINT_PTR)fp)[1], nx = ((DWORD *)(UINT_PTR)fp)[0];
        if (!code_addr(ret)) break;
        where(ret, w, sizeof w); logmsg("    #%02d %s", n, w);
        if (nx <= fp) break; fp = nx;
    }
    logmsg("  return addresses on the stack:");
    for (DWORD a = lo, k = 0; a + 4 <= hi && a < lo + 4096 && k < 20; a += 4) {
        DWORD v = *(DWORD *)(UINT_PTR)a;
        if (code_addr(v)) { where(v, w, sizeof w); logmsg("    ?%02lu %s (esp+%lu)", k++, w, a - lo); }
    }
}
static void caller_stack(const char *what) {                     // who called ExitProcess / TerminateProcess
    void *bt[32]; USHORT n = CaptureStackBackTrace(1, 32, bt, NULL); char w[160];
    logmsg("%s - called from:", what);
    for (USHORT i = 0; i < n; i++) { if (!code_addr((DWORD)(UINT_PTR)bt[i])) break; where((DWORD)(UINT_PTR)bt[i], w, sizeof w); logmsg("    #%02u %s", i, w); }
    DWORD esp; __asm__ volatile ("movl %%esp, %0" : "=r"(esp));
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb(); DWORD hi = (DWORD)(UINT_PTR)tib->StackBase;
    logmsg("  return addresses on the stack:");
    for (DWORD p = esp, k = 0; p + 4 <= hi && p < esp + 8192 && k < 24; p += 4) {
        DWORD v = *(DWORD *)(UINT_PTR)p; if (code_addr(v)) { where(v, w, sizeof w); logmsg("    ?%02lu %s", k++, w); }
    }
}

static int serious(DWORD c) {
    switch (c) { case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_STACK_OVERFLOW: case EXCEPTION_ILLEGAL_INSTRUCTION:
                 case EXCEPTION_PRIV_INSTRUCTION: case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_IN_PAGE_ERROR:
                 case 0xC0000409: case 0xC0000374: case 0xC000041D: case EXCEPTION_NONCONTINUABLE_EXCEPTION: return 1; }
    return 0;
}
static void report(EXCEPTION_POINTERS *e, const char *kind) {
    EXCEPTION_RECORD *r = e->ExceptionRecord; CONTEXT *c = e->ContextRecord; char w[160];
    where((DWORD)(UINT_PTR)r->ExceptionAddress, w, sizeof w);
    logmsg("%s: code %08lX at %s%s", kind, r->ExceptionCode, w, r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2 ?
           (r->ExceptionInformation[0] ? " (writing)" : " (reading)") : "");
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) logmsg("  address 0x%08lX", (DWORD)r->ExceptionInformation[1]);
    logmsg("  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX", c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    stack_from(c->Ebp, c->Esp, c->Eip);
}
static void write_dump(EXCEPTION_POINTERS *e) {
    if (InterlockedExchange(&g_dumped, 1)) return;
    HMODULE dh = LoadLibraryA("dbghelp.dll"); if (!dh) return;
    typedef BOOL (WINAPI *MDW)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    MDW f = (MDW)GetProcAddress(dh, "MiniDumpWriteDump"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); wchar_t p[MAX_PATH];
    _snwprintf(p, MAX_PATH, L"%lsCrashLog_%04d%02d%02d_%02d%02d%02d.dmp", g_dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); if (h == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mi = { GetCurrentThreadId(), e, FALSE };
    BOOL ok = f(GetCurrentProcess(), GetCurrentProcessId(), h, (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory), e ? &mi : NULL, NULL, NULL);
    CloseHandle(h); logmsg("minidump %s", ok ? "written" : "failed");
}
static LONG WINAPI veh(EXCEPTION_POINTERS *e) {
    if (t_in || !serious(e->ExceptionRecord->ExceptionCode) || InterlockedIncrement(&g_seen) > 30) return EXCEPTION_CONTINUE_SEARCH;
    t_in = 1; report(e, "exception (first chance)");
    g_lastRec = *e->ExceptionRecord; g_lastCtx = *e->ContextRecord; g_lastTid = GetCurrentThreadId(); t_in = 0;
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI uef(EXCEPTION_POINTERS *e) {
    t_in = 1; report(e, "UNHANDLED exception - the game crashes"); write_dump(e); t_in = 0;
    return g_prevFilter ? g_prevFilter(e) : EXCEPTION_CONTINUE_SEARCH;
}
static VOID WINAPI exit_hook(UINT code) {
    logmsg("ExitProcess(%u)", code); caller_stack("the game exits itself"); g_exitOrig(code);
}
static BOOL WINAPI term_hook(HANDLE h, UINT code) {
    if (h == GetCurrentProcess() || GetProcessId(h) == GetCurrentProcessId()) {
        logmsg("TerminateProcess(self, 0x%X)%s", code, code == 0xFFFFFFFD ? " - the game's own crash reporter (BugSplat) closes it after a crash: no Windows crash record" : "");
        caller_stack("the game is terminated from inside");
        if (g_lastTid == GetCurrentThreadId()) { EXCEPTION_POINTERS ep = { &g_lastRec, &g_lastCtx }; write_dump(&ep); }   // the crash of this thread, its stack still here
        else write_dump(NULL);
    }
    return g_termOrig(h, code);
}
static int patch_imports(void *real, void *hook) {               // every loaded module's import of this function
    HMODULE mods[512]; DWORD need = 0, n = 0;
    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof mods, &need)) return 0;
    for (DWORD m = 0; m < need / sizeof(HMODULE) && m < 512; m++) {
        unsigned char *b = (unsigned char *)mods[m]; IMAGE_DOS_HEADER *d = (IMAGE_DOS_HEADER *)b;
        if (IsBadReadPtr(d, sizeof *d) || d->e_magic != IMAGE_DOS_SIGNATURE) continue;
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + d->e_lfanew);
        IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress) continue;
        for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(b + dir.VirtualAddress); imp->Name; imp++)
            for (IMAGE_THUNK_DATA *t = (IMAGE_THUNK_DATA *)(b + imp->FirstThunk); t->u1.Function; t++)
                if ((void *)t->u1.Function == real) {
                    DWORD o; VirtualProtect(&t->u1.Function, 4, PAGE_READWRITE, &o); t->u1.Function = (DWORD)hook; VirtualProtect(&t->u1.Function, 4, o, &o); n++;
                }
    }
    return (int)n;
}
static DWORD WINAPI keep_filter(LPVOID _) {                       // the game may set its own filter later: ours stays first, theirs runs after
    for (int i = 0;; i++) {
        Sleep(1000);
        LPTOP_LEVEL_EXCEPTION_FILTER cur = SetUnhandledExceptionFilter(uef);
        if (cur != uef && cur) { g_prevFilter = cur; char w[160]; where((DWORD)(UINT_PTR)cur, w, sizeof w); logmsg("the game set its own crash filter (%s) - it runs after ours", w); }
        if (i % 60 == 59) {                                         // a game that runs out of address space just vanishes: the trend shows it
            MEMORYSTATUSEX ms = { sizeof ms }; GlobalMemoryStatusEx(&ms);
            PROCESS_MEMORY_COUNTERS pc = { sizeof pc }; GetProcessMemoryInfo(GetCurrentProcess(), &pc, sizeof pc);
            logmsg("memory: address space free %llu MB, game uses %lu MB (peak %lu MB), RAM free %llu MB",
                   ms.ullAvailVirtual >> 20, (DWORD)(pc.WorkingSetSize >> 20), (DWORD)(pc.PeakWorkingSetSize >> 20), ms.ullAvailPhys >> 20);
        }
    }
    return 0;
}

static DWORD WINAPI crash_test(LPVOID _) {                        // test only (env CRASHLOG_TEST=1): a real crash 40 s after the start
    Sleep(40000); logmsg("CRASHLOG_TEST: crashing on purpose"); volatile int *p = (int *)8; *p = 1; return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_DETACH) { if (g_dir[0]) logmsg("process ends (%s)", reserved ? "the process is exiting" : "the DLL is unloaded"); return TRUE; }
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    char mx[64]; wsprintfA(mx, "Local\\dow2patch_CrashLog_%lu", GetCurrentProcessId());
    CreateMutexA(NULL, FALSE, mx); if (GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
    InitializeCriticalSection(&g_lk);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0;
    { wchar_t p[MAX_PATH], o[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsCrashLog.log", g_dir); _snwprintf(o, MAX_PATH, L"%lsCrashLog.log.old", g_dir);
      WIN32_FILE_ATTRIBUTE_DATA a; if (GetFileAttributesExW(p, GetFileExInfoStandard, &a) && (a.nFileSizeHigh || a.nFileSizeLow > (1u << 20))) MoveFileExW(p, o, MOVEFILE_REPLACE_EXISTING); }
    AddVectoredExceptionHandler(1, veh);
    g_prevFilter = SetUnhandledExceptionFilter(uef);
    HMODULE k = GetModuleHandleA("kernel32.dll");
    g_exitOrig = (ExitProcessFn)GetProcAddress(k, "ExitProcess"); g_termOrig = (TerminateProcessFn)GetProcAddress(k, "TerminateProcess");
    int ne = g_exitOrig ? patch_imports((void *)g_exitOrig, (void *)exit_hook) : 0;
    int nt = g_termOrig ? patch_imports((void *)g_termOrig, (void *)term_hook) : 0;
    MEMORYSTATUSEX ms = { sizeof ms }; GlobalMemoryStatusEx(&ms);
    logmsg("CrashLog 1 start: exception watch on, crash filter on, ExitProcess hooked in %d import(s), TerminateProcess in %d; "
           "memory: %llu MB free of %llu, address space free %llu MB", ne, nt, ms.ullAvailPhys >> 20, ms.ullTotalPhys >> 20, ms.ullAvailVirtual >> 20);
    CloseHandle(CreateThread(NULL, 0, keep_filter, NULL, 0, NULL));
    { char t[4] = ""; if (GetEnvironmentVariableA("CRASHLOG_TEST", t, sizeof t) && t[0] == '1') CloseHandle(CreateThread(NULL, 0, crash_test, NULL, 0, NULL)); }
    return TRUE;
}
