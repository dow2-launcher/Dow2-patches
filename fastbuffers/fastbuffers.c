// FastBuffers.dll - the renderer probes every page of each dynamic vertex / index buffer it locks (DoW2: Retribution 3.19).
//
// Spooge.dll (the game's Direct3D 9 renderer) streams dynamic geometry (FX, particles, decals, UI quads...) through two ring
// buffers: Lock(offset, size, NOOVERWRITE/DISCARD) and then IsBadWritePtr(ptr, size) on the returned pointer
// (Spooge 0x1004D414 vertex buffer, 0x1004D4FA index buffer). IsBadWritePtr touches every 4 KB page of the range under an
// exception handler; on Windows the locked buffer is usually write-combined driver memory, where that read-back is slow,
// under Wine/DXVK it is a page walk with SEH. Measured on the stand (3v3 replay, 60 fps): 7% of the game thread's time.
// A Lock that returned success always gives a writable pointer, so the probe never changes the outcome.
//
// Fix: Spooge's import slot of IsBadWritePtr (used only at these two places) points to a stub that only rejects NULL.
// One atomic pointer write, no game code is modified. Rendering only - nothing of the simulation is touched, no sync impact.
#include <windows.h>
#include <stdio.h>
#include <string.h>

static wchar_t g_log[MAX_PATH];
static volatile LONG g_calls;
static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static BOOL WINAPI fast_IsBadWritePtr(LPVOID p, UINT_PTR n) { InterlockedIncrement(&g_calls); return p == NULL && n != 0; }

// address of the import slot <dll>!<fn> in module m (by name, so a rebased Spooge.dll works too)
static void **find_slot(HMODULE m, const char *dll, const char *fn) {
    BYTE *b = (BYTE *)m; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dd->VirtualAddress) return NULL;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(b + dd->VirtualAddress); d->Name; d++) {
        if (_stricmp((char *)(b + d->Name), dll) || !d->OriginalFirstThunk) continue;
        IMAGE_THUNK_DATA *o = (IMAGE_THUNK_DATA *)(b + d->OriginalFirstThunk), *f = (IMAGE_THUNK_DATA *)(b + d->FirstThunk);
        for (; o->u1.AddressOfData; o++, f++)
            if (!(o->u1.Ordinal & IMAGE_ORDINAL_FLAG) && !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(b + o->u1.AddressOfData))->Name, fn))
                return (void **)&f->u1.Function;
    }
    return NULL;
}
static void install(void) {
    HMODULE sp = GetModuleHandleA("Spooge.dll");
    if (!sp) { logmsg("no Spooge.dll in this process - not patching"); return; }
    void **slot = find_slot(sp, "KERNEL32.dll", "IsBadWritePtr");
    BYTE *b = (BYTE *)sp;
    // the two callers of 3.19: mov edx,[esp+N]; push edi; push edx; call [slot]
    static const DWORD SITES[2] = {0x4D414, 0x4D4FA};
    if (!slot) { logmsg("Spooge.dll has no IsBadWritePtr import (another build?) - not patching"); return; }
    for (int i = 0; i < 2; i++) {
        BYTE *c = b + SITES[i];
        if (IsBadReadPtr(c - 2, 8) || c[-2] != 0x57 || c[-1] != 0x52 || c[0] != 0xFF || c[1] != 0x15 || *(void ***)(c + 2) != slot) {
            logmsg("Spooge.dll is not the 3.19 one (call site %d differs) - not patching", i); return; }
    }
    DWORD old;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) { logmsg("VirtualProtect failed (%lu)", GetLastError()); return; }
    void *prev = (void *)InterlockedExchange((LONG *)slot, (LONG)fast_IsBadWritePtr);
    VirtualProtect(slot, 4, old, &old);
    logmsg("FastBuffers 1: dynamic vertex/index buffer locks without the page probe (Spooge %p, import slot %p, was %p)", sp, slot, prev);
}
static DWORD WINAPI stats(LPVOID _) {
    for (int n = 0;; n++) { Sleep(n ? 600000 : 120000); logmsg("probes skipped: %ld", (long)InterlockedExchange(&g_calls, 0)); }
    return 0;
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
    wchar_t ini[MAX_PATH]; wcscpy(ini, g_log); wcscat(ini, L"FastBuffers.ini"); wcscat(g_log, L"FastBuffers.log");
    if (already_loaded("FastBuffers")) { logmsg("another FastBuffers copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"FastBuffers", L"Enabled", 1, ini)) { logmsg("disabled in FastBuffers.ini"); return TRUE; }
    install();
    if (GetPrivateProfileIntW(L"FastBuffers", L"Stats", 0, ini)) CloseHandle(CreateThread(NULL, 0, stats, NULL, 0, NULL));
    return TRUE;
}
