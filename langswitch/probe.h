// STAND ONLY (-DSTAND_PROBE, never shipped): checks of LangSwitch on the Wine stand.
//  a) a hidden window of the game's thread (as the text services' windows are) gets a message posted from another thread every
//     200 ms, and the game's thread gets a thread message: counted every 5 s - without the pump (Pump=0) nothing arrives,
//     with it everything does;
//  b) the Russian layout is loaded and WM_INPUTLANGCHANGEREQUEST is posted to the game window from another thread (as
//     Windows' hotkey handling posts it): the game thread's layout before / after - the request went through the game's
//     window procedures to DefWindowProc.
static HWND p_wnd; static volatile LONG p_posted, p_got, p_tposted; static DWORD p_t0;
static HKL p_ru, p_orig;
static LRESULT CALLBACK probe_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_APP + 7) { InterlockedIncrement(&p_got); return 0; }
    return DefWindowProcW(h, m, w, l);
}
static DWORD WINAPI probe_thread(LPVOID _) {
    for (int i = 0; i < 600; i++) {                          // 120 s
        Sleep(200);
        if (PostMessageW(p_wnd, WM_APP + 7, i, 0)) InterlockedIncrement(&p_posted);
        if (PostThreadMessageW(g_thread, WM_APP + 8, i, 0)) InterlockedIncrement(&p_tposted);
        int s = (i + 1) * 200;
        if (s % 5000 == 0) logmsg("PROBE %2ds: posted to a hidden window of the game thread %ld, dispatched %ld; posted to the thread %ld, taken out %lu; thread layout %08lX",
                                  s / 1000, p_posted, p_got, p_tposted, g_threadmsgs, (unsigned long)(ULONG_PTR)GetKeyboardLayout(g_thread));
        if (s == 20000) {
            p_orig = GetKeyboardLayout(g_thread);
            p_ru = LoadKeyboardLayoutW(L"00000419", 0);
            logmsg("PROBE LoadKeyboardLayout(00000419) -> %08lX", (unsigned long)(ULONG_PTR)p_ru);
            log_layouts("PROBE after load");
        }
        if (s == 25000 && p_ru) { logmsg("PROBE post WM_INPUTLANGCHANGEREQUEST(%08lX) to the game window (thread layout %08lX)",
                                         (unsigned long)(ULONG_PTR)p_ru, (unsigned long)(ULONG_PTR)GetKeyboardLayout(g_thread));
                                  PostMessageW(g_game, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)p_ru); }
        if (s == 27000) logmsg("PROBE 2 s later: thread layout %08lX", (unsigned long)(ULONG_PTR)GetKeyboardLayout(g_thread));
        if (s == 30000 && p_orig) { logmsg("PROBE post WM_INPUTLANGCHANGEREQUEST(%08lX) back", (unsigned long)(ULONG_PTR)p_orig);
                                    PostMessageW(g_game, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)p_orig); }
        if (s == 32000) logmsg("PROBE 2 s later: thread layout %08lX", (unsigned long)(ULONG_PTR)GetKeyboardLayout(g_thread));
    }
    return 0;
}
static void probe_tick(HWND game) {
    if (p_wnd) return;
    WNDCLASSW wc; memset(&wc, 0, sizeof wc); wc.lpfnWndProc = probe_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"LangSwitchProbe";
    RegisterClassW(&wc);
    p_wnd = CreateWindowExW(0, L"LangSwitchProbe", L"", WS_POPUP, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    p_t0 = GetTickCount();
    logmsg("PROBE hidden window %p in the game thread %lu", p_wnd, GetCurrentThreadId());
    CloseHandle(CreateThread(NULL, 0, probe_thread, NULL, 0, NULL));
}
