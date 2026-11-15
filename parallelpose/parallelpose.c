// ParallelPose.dll - DoW2 Retribution 3.19 (DOW2.exe 3.19.10320): the skeleton poses of all animated models updated in a frame
// are built on several CPU cores instead of one. Add-on parallelpose-3 (experiment, off by default, Elite and vanilla).
//
// Where the time goes: the Viz manager's update queue 0x0077BB60 runs, for every visual queued this frame (all on-screen and
// shadow-view models), its update (vtable slot 2) and right after it its "build pose" (slot 3). For AnimatorVisual (vtable
// 0x010C5D94) slot 3 is 0x00751B60 -> 0x00C76250: Havok animation sampling (spline-compressed track decode 0xD21BF0/0xD21DB0,
// blend 0xD12DD0) and the bone matrices copied into the model's shader-constant buffer (0xC76040). In late 3v3 fights that is
// ~13 ms of a ~50 ms frame and it grows with the number of models in view. One model's pose reads only its own animation state
// and the (read-only) animation data, and writes only its own buffers (see REPORT_RU.md, "thread safety").
//
// What this DLL does:
//  1. AnimatorVisual vtable slot 3 -> hk_pose. A call coming from the queue's first loop (return address 0x0077BC26) with the
//     model's dirty bit set is not run but remembered (its dirty bit is taken over; we run the original with force=1 later,
//     which sets the bit again, builds and clears it - the same as the original call).
//  2. Right after that loop (0x0077BC50, before the queue's later loops that use the poses) a jmp to our flush: the remembered
//     models are grouped by their render model (ra::RenderModelInstance[0] - every model sharing it goes to the same thread, so a
//     shared object is never touched by two threads at once) and the groups are built by the main thread plus N worker threads.
//     The main thread waits until all are done, then the game continues exactly as before.
//  3. Workers are our own threads (not XThread jobs: ScheduleJob costs ~240 us of the main thread under Wine). Each worker
//     initialises Havok for itself exactly as the game does for its XThread workers (0x006DABC0: hkThreadMemory + 512 KB stack
//     area + hkBaseSystem::initThread incl. the monitor stream), and takes the main thread's x87 control word and MXCSR for
//     every batch, so the float math is the same as on the main thread.
// Visual only: the visual update is not part of the simulation (the headless harness cuts the whole visual update 0x40E58D with
// an identical per-tick CRC). Nothing is done when any checked byte differs from 3.19.
//
// ParallelPose.ini [ParallelPose]: Enabled=1 (0 = do nothing), Threads=0 (workers; 0 = cores-1, at most 7), Mode=1
// (1 = parallel, 0 = off, 2 = stand comparison: PoseSkip - every other frame), Poll=0 (stand: re-read Mode every 0.5 s),
// Stats=0 (1 = a line every 10 s in ParallelPose.log), Watchdog=100 (ms the main thread waits for the workers before it writes a
// "STALL" line and yields; a wait of StallOff=2000 ms or more switches the workers off for the rest of the game session),
// RaceTest=0 (stand only: a worker sleeps that many ms between taking a group number and checking it).
//
// Version 2: (a) a group number and the batch's group count are one atomic word (g_claim). Version 1 took the number from g_next
// and compared it with g_ngroups afterwards; a worker preempted between the two compared a number left over from the flush before
// with the NEXT flush's count -> one group posed by two threads at once and g_done counted twice, so the main thread went on while
// a worker still wrote a pose. (b) A visual seen for the first time (new squad, new model) gets its first pose on the main thread,
// as ParallelDraw does with cold models: the first pose may fill lazily built data. (c) Watchdog and a crash note (see veh).
// Version 3 (only the version in the log line changed): the add-on is offered for vanilla Retribution too (launcher "Vanilla Retribution": the launcher
// copies it into dow2-patches\, the launcher's winmm.dll loads it ~0.1 s after the start). Vanilla runs the same DOW2.exe
// 3.19.10320 (Elite is DOW2.exe -modname Elite) and nothing here depends on the mod: no mod name, ini/log next to the DLL.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define VT_AV        0x010C5D94u
#define FN_POSE      0x00751B60u
#define RET_LOOP1    0x0077BC26u   // return address of "call edx" (slot 3) in the queue's first loop
#define AT_LOOP1_END 0x0077BC50u   // mov dword ptr [esp+0x20], -1 (8 bytes) - the first instruction after the loop
#define BACK_LOOP1   0x0077BC58u
// Havok (3.19): thread init as the game's own XThread workers do it (0x006DABC0, worker branch)
#define P_ALIGNED_MALLOC 0x0124F7E8u   // pointer to the game's aligned malloc (size, align) cdecl
#define G_HK_MEMMGR      0x01302B50u   // hkMemory manager
#define G_HK_TLS_MEM     0x01302D28u   // TLS index of hkThreadMemory
#define G_HK_TLS_MON     0x01331400u   // TLS index of hkMonitorStream
#define G_HK_INITED      0x01303B64u   // hkBaseSystem initialised (byte)
#define G_SPLINE_TABLE   0x01331590u   // lazily built table of the spline decoder 0xD21DB0 (byte flag) - built before going wide
#define FN_TM_CTOR       0x00CD8F60u   // hkThreadMemory::hkThreadMemory(mgr, 0x10) thiscall
#define FN_INIT_THREAD   0x00CF10B0u   // hkBaseSystem::initThread(tm) cdecl
#define FN_TM_ALLOC      0x00CD9580u   // hkThreadMemory::allocateChunk(size, cls) thiscall

typedef void (__attribute__((thiscall)) *PoseFn)(void *, char);
typedef void *(__cdecl *AMallocFn)(size_t, size_t);
typedef void *(__attribute__((thiscall)) *TmCtorFn)(void *, void *, int);
typedef int (__cdecl *InitThreadFn)(void *);
typedef void *(__attribute__((thiscall)) *TmAllocFn)(void *, int, int);
typedef void (__attribute__((thiscall)) *SetStackFn)(void *, void *, int);

static PoseFn o_pose;
static wchar_t g_dir[MAX_PATH], g_log[MAX_PATH], g_ini[MAX_PATH];
static volatile LONG g_mode = 1;
static int g_threads, g_stats;
static DWORD g_main_tid;

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---------------------------------------------------------------- the frame's deferred poses
#define MAXP 8192
static void *g_pend[MAXP];             // visuals whose pose is deferred this frame (main thread only until the flush)
static int g_npend;
static void *g_item[MAXP];             // the same, ordered by group
static int g_gstart[MAXP + 1], g_gorder[MAXP];   // group i = g_item[g_gstart[i] .. g_gstart[i+1]), run in g_gorder order
static int g_ngroups;
// g_claim = group count << 16 | next group number (MAXP < 65536); 0 between flushes (every late claim fails)
static volatile LONG g_claim, g_done, g_flushno;
static int g_wd_ms = 100, g_off_ms = 2000, g_racetest; static volatile LONG g_par_off;
static DWORD g_wtid[16]; static void *volatile g_wcur[16]; static volatile LONGLONG g_wt0[16];
static volatile LONG g_gen;            // batch number, workers read the FPU state of the batch
static unsigned short g_fcw; static unsigned int g_mxcsr;
static HANDLE g_sem;
static volatile LONG g_workers_ready, g_workers_failed;

// stats (main thread writes, logger reads - approximate is fine)
static volatile LONG s_flush, s_items, s_groups, s_maxgroup, s_serial, s_byworker, s_skips, s_calls, s_cold, s_stalls, s_late;
static volatile LONGLONG s_qpc_flush; static LARGE_INTEGER g_qf;
static HANDLE g_worker_h[16]; static int g_nworkers;

static inline void *rmi_key(void *v) {             // ra::RenderModelInstance[0] of AnimatorVisual (+0x9C): the shared part
    void *rmi = *(void **)((char *)v + 0x9C);
    return rmi ? *(void **)rmi : NULL;
}

static void run_groups(int wid) {                  // wid: worker index + 1, 0 = main thread
    for (;;) {
        LONG b = g_flushno;
        unsigned v = (unsigned)InterlockedIncrement(&g_claim) - 1, i = v & 0xffff, n = v >> 16;
        if (g_racetest && wid && i >= n) Sleep(g_racetest);
        if (i >= n) { if (wid && g_flushno != b) InterlockedIncrement(&s_late); return; }
        // a valid number: the flush stays alive (g_item, g_gorder, g_fcw unchanged) until this group is counted in g_done
        if (wid) { unsigned short fcw = g_fcw; unsigned int mx = g_mxcsr; __asm__ volatile("fldcw %0" :: "m"(fcw)); __asm__ volatile("ldmxcsr %0" :: "m"(mx)); }
        int g = g_gorder[i];
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        for (int k = g_gstart[g]; k < g_gstart[g + 1]; k++) {
            if (wid) { g_wt0[wid - 1] = t.QuadPart; g_wcur[wid - 1] = g_item[k]; }
            o_pose(g_item[k], 1);
        }
        if (wid) { g_wcur[wid - 1] = NULL; InterlockedExchangeAdd(&s_byworker, g_gstart[g + 1] - g_gstart[g]); }
        InterlockedIncrement(&g_done);
    }
}

// --------------------------------------------------------------- workers
static int hk_thread_init(void) {
    // the same steps as 0x006DABC0 (game's worker branch); our own thread gets its own Havok thread memory and monitor stream
    void *mgr = *(void **)G_HK_MEMMGR;
    if (!mgr || !*(volatile BYTE *)G_HK_INITED) return 0;
    void *tm = (*(AMallocFn *)P_ALIGNED_MALLOC)(0x330, 0x10);
    if (!tm) return 0;
    tm = ((TmCtorFn)FN_TM_CTOR)(tm, mgr, 0x10);
    ((InitThreadFn)FN_INIT_THREAD)(tm);
    void *cur = TlsGetValue(*(DWORD *)G_HK_TLS_MEM);
    if (cur != tm || !TlsGetValue(*(DWORD *)G_HK_TLS_MON)) return 0;
    void *stack = ((TmAllocFn)FN_TM_ALLOC)(cur, 0x80000, 1);
    if (!stack) return 0;
    SetStackFn setstack = (SetStackFn)(*(void ***)tm)[2];
    setstack(tm, stack, 0x80000);
    return 1;
}

static DWORD WINAPI worker(LPVOID arg) {
    int id = (int)(INT_PTR)arg, ok = 0;
    g_wtid[id] = GetCurrentThreadId();
    for (;;) {
        WaitForSingleObject(g_sem, INFINITE);
        if (!ok) {
            ok = hk_thread_init();
            logmsg("worker %d (tid %lu): Havok thread init %s", id, GetCurrentThreadId(), ok ? "ok" : "FAILED - this worker stays idle");
            if (!ok) { InterlockedIncrement(&g_workers_failed); for (;;) Sleep(INFINITE); }
            InterlockedIncrement(&g_workers_ready);
        }
        run_groups(id + 1);
    }
    return 0;
}

static int cmp_key(const void *a, const void *b) {
    ULONG_PTR x = (ULONG_PTR)rmi_key(*(void *const *)a), y = (ULONG_PTR)rmi_key(*(void *const *)b);
    return x < y ? -1 : x > y;
}
static int cmp_gsize(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (g_gstart[y + 1] - g_gstart[y]) - (g_gstart[x + 1] - g_gstart[x]);
}

// visuals seen before (with the same render model): their first pose, which may fill lazily built data, was on the main thread
#define HS 65536
static void *s_key_v[HS], *s_key_rm[HS];
static int seen_before(void *v) {
    void *rm = rmi_key(v);
    unsigned h = ((unsigned)(ULONG_PTR)v >> 4) * 2654435761u >> 16;
    for (int i = 0; i < 8; i++) {
        unsigned j = (h + i) & (HS - 1);
        if (s_key_v[j] == v) { int ok = s_key_rm[j] == rm; s_key_rm[j] = rm; return ok; }
        if (!s_key_v[j]) { s_key_v[j] = v; s_key_rm[j] = rm; return 0; }
    }
    unsigned j = h & (HS - 1); s_key_v[j] = v; s_key_rm[j] = rm; return 0;
}

static void stall_report(LONGLONG ms, int ng) {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    char b[1200]; int o = snprintf(b, sizeof b, "STALL: main thread (tid %lu) waits %lld ms for the workers: flush %ld, %ld of %d groups done",
                                   g_main_tid, ms, (long)g_flushno, (long)g_done, ng);
    for (int t = 0; t < g_nworkers && o < (int)sizeof b - 120; t++) {
        void *v = g_wcur[t]; if (!v) continue;
        o += snprintf(b + o, sizeof b - o, " | worker %d (tid %lu): visual %p vtable %08lx for %lld ms", t, g_wtid[t], v,
                      IsBadReadPtr(v, 4) ? 0ul : *(DWORD *)v, (now.QuadPart - g_wt0[t]) * 1000 / g_qf.QuadPart);
    }
    logmsg("%s", b);
}

// called from the trampoline after the queue's first loop (main thread, all registers saved)
void __cdecl pp_flush(void) {
    int n = g_npend; if (!n) return;
    g_npend = 0;
    LARGE_INTEGER q0, q1; QueryPerformanceCounter(&q0);
    InterlockedIncrement(&s_flush); InterlockedExchangeAdd(&s_items, n);
    // serial when the workers are not there or the spline table is not built yet (it is built lazily by the first decode)
    if (!g_nworkers || !*(volatile BYTE *)G_SPLINE_TABLE || g_workers_failed || g_par_off) {
        for (int i = 0; i < n; i++) o_pose(g_pend[i], 1);
        InterlockedIncrement(&s_serial); goto out;
    }
    // a render model whose sorted map is still marked dirty (+0xD) would be sorted by the first pose, and a visual's first pose may
    // fill lazily built data: do those on this thread first
    int m = 0;
    for (int i = 0; i < n; i++) {
        BYTE *rm = (BYTE *)rmi_key(g_pend[i]);
        int cold = !seen_before(g_pend[i]);
        if (!rm || rm[0xD] || cold) { if (cold) s_cold++; o_pose(g_pend[i], 1); } else g_item[m++] = g_pend[i];
    }
    qsort(g_item, m, sizeof(void *), cmp_key);
    int ng = 0, mx = 0;
    for (int i = 0; i < m; i++)
        if (i == 0 || rmi_key(g_item[i]) != rmi_key(g_item[i - 1])) g_gstart[ng++] = i;
    g_gstart[ng] = m;
    for (int i = 0; i < ng; i++) { g_gorder[i] = i; int s = g_gstart[i + 1] - g_gstart[i]; if (s > mx) mx = s; }
    qsort(g_gorder, ng, sizeof(int), cmp_gsize);   // biggest groups first
    InterlockedExchangeAdd(&s_groups, ng); if (mx > s_maxgroup) s_maxgroup = mx;
    if (ng) {
        unsigned short fcw; unsigned int mxc;
        __asm__ volatile("fnstcw %0" : "=m"(fcw)); __asm__ volatile("stmxcsr %0" : "=m"(mxc));
        g_fcw = fcw; g_mxcsr = mxc; g_ngroups = ng; g_done = 0; g_flushno++;
        InterlockedIncrement(&g_gen);
        InterlockedExchange(&g_claim, ng << 16);         // publish (full barrier)
        int wake = g_nworkers < ng - 1 ? g_nworkers : ng - 1;
        if (wake > 0) ReleaseSemaphore(g_sem, wake, NULL);
        run_groups(0);
        // every group is taken now; only groups being posed by workers remain (tens of us). Yield after 2 ms, STALL line after g_wd_ms
        LARGE_INTEGER w0, w1; QueryPerformanceCounter(&w0);
        unsigned sp = 0; LONGLONG next_ms = g_wd_ms, ms = 0;
        while (g_done < ng) {
            __builtin_ia32_pause();
            if (++sp & 1023) continue;
            QueryPerformanceCounter(&w1); ms = (w1.QuadPart - w0.QuadPart) * 1000 / g_qf.QuadPart;
            if (ms >= 2) SwitchToThread();
            if (g_wd_ms > 0 && ms >= next_ms) {
                if (next_ms == g_wd_ms) InterlockedIncrement(&s_stalls);
                stall_report(ms, ng);
                next_ms = next_ms < 1000 ? 1000 : next_ms + 5000;
            }
        }
        InterlockedExchange(&g_claim, 0);                // late-waking workers find nothing
        if (g_wd_ms > 0 && ms >= g_wd_ms) {
            QueryPerformanceCounter(&w1); ms = (w1.QuadPart - w0.QuadPart) * 1000 / g_qf.QuadPart;
            logmsg("STALL over: the workers finished after %lld ms (flush %ld)", ms, (long)g_flushno);
            if (g_off_ms > 0 && ms >= g_off_ms) { g_par_off = 1; logmsg("STALL: workers switched off for the rest of this game session"); }
        }
    }
out:
    QueryPerformanceCounter(&q1); s_qpc_flush += q1.QuadPart - q0.QuadPart;
}

// --------------------------------------------------------------- PoseSkip (stand comparison only, Mode=2)
#define HN 32768
static void *hk[HN]; static unsigned char hv[HN];
static unsigned char *skip_slot(void *p) {
    unsigned h = ((unsigned)(ULONG_PTR)p >> 4) * 2654435761u >> 17;
    for (int i = 0; i < 64; i++) { unsigned j = (h + i) & (HN - 1); if (hk[j] == p) return &hv[j]; if (!hk[j]) { hk[j] = p; hv[j] = 0; return &hv[j]; } }
    return NULL;
}

static void __attribute__((fastcall)) hk_pose(void *self, void *edx, char force) {
    (void)edx;
    LONG mode = g_mode;
    if (mode && !force && __builtin_return_address(0) == (void *)RET_LOOP1 && (*(DWORD *)((char *)self + 0x214) & 1)) {
        DWORD tid = GetCurrentThreadId();
        if (!g_main_tid) g_main_tid = tid;               // the Viz queue runs on the game's main thread (XThread 0) only
        if (tid != g_main_tid) { o_pose(self, force); return; }
        s_calls++;
        if (mode == 1 && g_npend < MAXP) {
            *(DWORD *)((char *)self + 0x214) &= ~1u;     // taken over: built later with force=1 (sets, builds, clears)
            g_pend[g_npend++] = self; return;
        }
        if (mode == 2) { unsigned char *t = skip_slot(self); if (t) { *t ^= 1; if (*t == 0) { s_skips++; return; } } }
    }
    o_pose(self, force);
}

// --------------------------------------------------------------- crash note
// A crash inside the game ends in the game's BugSplat handler (TerminateProcess, nothing in the Windows log). A fatal exception on
// one of our workers writes one line first, without the C runtime.
static volatile LONG s_exc;
static LONG CALLBACK veh(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION
        && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW && code != 0xC0000409u) return EXCEPTION_CONTINUE_SEARCH;
    DWORD tid = GetCurrentThreadId(); int w = -1;
    for (int t = 0; t < g_nworkers; t++) if (g_wtid[t] == tid) w = t;
    if (w < 0 || InterlockedIncrement(&s_exc) > 3) return EXCEPTION_CONTINUE_SEARCH;
    void *at = ep->ExceptionRecord->ExceptionAddress; HMODULE m = NULL; char mod[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &m) && m) {
        GetModuleFileNameA(m, mod, sizeof mod); char *q = strrchr(mod, '\\'); if (q) memmove(mod, q + 1, strlen(q));
    }
    SYSTEMTIME t; GetLocalTime(&t); char b[512];
    int n = wsprintfA(b, "%02d:%02d:%02d EXCEPTION %08lX at %s+%lX (%p), data %p, on worker %d (tid %lu) posing visual %p - the game's crash "
                      "handler takes over\r\n", t.wHour, t.wMinute, t.wSecond, code, mod, (DWORD)((char *)at - (char *)m), at,
                      ep->ExceptionRecord->NumberParameters > 1 ? (void *)ep->ExceptionRecord->ExceptionInformation[1] : NULL, w, tid, g_wcur[w]);
    HANDLE f = CreateFileW(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(f, b, n, &wr, NULL); CloseHandle(f); }
    return EXCEPTION_CONTINUE_SEARCH;
}

// --------------------------------------------------------------- install
static int bytes_are(DWORD at, const BYTE *b, int n) { return !IsBadReadPtr((void *)at, n) && !memcmp((void *)at, b, n); }

static DWORD WINAPI logger(LPVOID _) {
    (void)_; LONG pf = 0, pi = 0, pg = 0, ps = 0, pw = 0, pk = 0, pc = 0; LONGLONG pq = 0; int k = 0;
    ULONGLONG wprev[16] = {0}; int poll = GetPrivateProfileIntW(L"ParallelPose", L"Poll", 0, g_ini);
    for (;;) {
        Sleep(500);
        if (poll) {
            int m = GetPrivateProfileIntW(L"ParallelPose", L"Mode", g_mode, g_ini);
            if (m != g_mode) { InterlockedExchange(&g_mode, m); logmsg("mode %d", m); }
        }
        if (!g_stats || ++k % 20) continue;
        LONG f = s_flush, it = s_items, g = s_groups, s = s_serial, w = s_byworker, sk = s_skips, c = s_calls; LONGLONG q = s_qpc_flush;
        char cpu[200]; int o = 0; cpu[0] = 0;
        for (int i = 0; i < g_nworkers && o < 180; i++) {
            FILETIME a, b, kt, ut; ULONGLONG t = 0;
            if (GetThreadTimes(g_worker_h[i], &a, &b, &kt, &ut)) t = ((ULONGLONG)kt.dwHighDateTime << 32 | kt.dwLowDateTime) + ((ULONGLONG)ut.dwHighDateTime << 32 | ut.dwLowDateTime);
            o += snprintf(cpu + o, sizeof cpu - o, " %.0f", (t - wprev[i]) / 1e4 / 10.0); wprev[i] = t;   // % of a core over 10 s
        }
        LONG df = f - pf;
        logmsg("10 s: mode %ld, flushes %ld, poses %ld (%.0f/flush), groups/flush %.1f, max group %ld, by workers %.0f %%, serial %ld, "
               "flush %.2f ms avg | skip-mode calls %ld skipped %ld | first poses on the main thread %ld, stalls %ld, late claims %ld%s | worker CPU %%:%s",
               (long)g_mode, df, it - pi, df ? (double)(it - pi) / df : 0, df ? (double)(g - pg) / df : 0, (long)s_maxgroup,
               it - pi ? 100.0 * (w - pw) / (it - pi) : 0, s - ps, df ? (q - pq) * 1000.0 / g_qf.QuadPart / df : 0, c - pc, sk - pk,
               (long)s_cold, (long)s_stalls, (long)s_late, g_par_off ? ", WORKERS OFF" : "", cpu);
        s_maxgroup = 0; pf = f; pi = it; pg = g; ps = s; pw = w; pk = sk; pc = c; pq = q;
    }
    return 0;
}

static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}

static void install(void) {
    static const BYTE CALL1[] = {0x8B, 0x36, 0x8B, 0x06, 0x8B, 0x50, 0x0C, 0x6A, 0x00, 0x8B, 0xCE, 0xFF, 0xD2};   // 0x77BC19..0x77BC25
    static const BYTE END1[] = {0xC7, 0x44, 0x24, 0x20, 0xFF, 0xFF, 0xFF, 0xFF};
    static const BYTE TMCTOR[] = {0x53, 0x8B, 0xD9, 0xC7, 0x03, 0x60, 0x4A, 0x02, 0x01};
    static const BYTE INITT[] = {0xE8, 0x4B, 0x84, 0xFE, 0xFF, 0x8B, 0x44, 0x24, 0x04};
    static const BYTE POSE[] = {0x80, 0x7C, 0x24, 0x04, 0x00, 0x56, 0x8B, 0xF1};
    void **vt = (void **)VT_AV;
    if (IsBadReadPtr(vt, 16) || vt[3] != (void *)FN_POSE || !bytes_are(FN_POSE, POSE, sizeof POSE) || !bytes_are(0x0077BC19, CALL1, sizeof CALL1)
        || !bytes_are(AT_LOOP1_END, END1, sizeof END1) || !bytes_are(FN_TM_CTOR, TMCTOR, sizeof TMCTOR) || !bytes_are(FN_INIT_THREAD, INITT, sizeof INITT)) {
        logmsg("not DoW2 3.19.10320 (a checked byte differs) - nothing done"); return;
    }
    // trampoline: pushad; pushfd; call pp_flush; popfd; popad; mov dword ptr [esp+0x20],-1; jmp 0x77BC58
    BYTE *t = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) { logmsg("VirtualAlloc failed - nothing done"); return; }
    int i = 0;
    t[i++] = 0x60; t[i++] = 0x9C; t[i++] = 0xE8; *(DWORD *)(t + i) = (DWORD)pp_flush - (DWORD)(t + i + 4); i += 4;
    t[i++] = 0x9D; t[i++] = 0x61; memcpy(t + i, END1, 8); i += 8;
    t[i++] = 0xE9; *(DWORD *)(t + i) = BACK_LOOP1 - (DWORD)(t + i + 4); i += 4;
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    // workers
    SYSTEM_INFO si; GetSystemInfo(&si); int ncpu = si.dwNumberOfProcessors;
    g_threads = GetPrivateProfileIntW(L"ParallelPose", L"Threads", 0, g_ini);
    if (g_threads <= 0) g_threads = ncpu - 1 < 7 ? ncpu - 1 : 7;
    if (g_threads > 15) g_threads = 15;
    g_sem = CreateSemaphoreW(NULL, 0, 0x7fffffff, NULL);
    for (int w = 0; w < g_threads && g_sem; w++) {
        HANDLE h = CreateThread(NULL, 1 << 20, worker, (LPVOID)(INT_PTR)w, 0, NULL);
        if (!h) break;
        g_worker_h[g_nworkers++] = h;
    }
    DWORD old;
    o_pose = (PoseFn)vt[3];
    if (!VirtualProtect(&vt[3], 4, PAGE_EXECUTE_READWRITE, &old)) { logmsg("VirtualProtect vtable failed - nothing done"); return; }
    // the jmp at 0x77BC50 first (harmless with nothing deferred), then the slot
    DWORD o2;
    if (!VirtualProtect((void *)AT_LOOP1_END, 8, PAGE_EXECUTE_READWRITE, &o2)) { logmsg("VirtualProtect code failed - nothing done"); return; }
    BYTE j[8]; j[0] = 0xE9; *(DWORD *)(j + 1) = (DWORD)t - (AT_LOOP1_END + 5); j[5] = 0x90; j[6] = 0x90; j[7] = 0x90;
    // 8-byte atomic write (the main thread may be in this code only after the game started; we run in DllMain at process start)
    __atomic_store_n((unsigned long long *)AT_LOOP1_END, *(unsigned long long *)j, __ATOMIC_SEQ_CST);
    VirtualProtect((void *)AT_LOOP1_END, 8, o2, &o2);
    FlushInstructionCache(GetCurrentProcess(), (void *)AT_LOOP1_END, 8);
    AddVectoredExceptionHandler(1, veh);
    InterlockedExchange((LONG *)&vt[3], (LONG)hk_pose);
    VirtualProtect(&vt[3], 4, old, &old);
    logmsg("ParallelPose 3: AnimatorVisual slot 3 (0x751B60) deferred from the Viz queue loop, built after it (0x77BC50) by the main "
           "thread + %d workers (%d CPUs); mode %ld, watchdog %d ms", g_nworkers, ncpu, (long)g_mode, g_wd_ms);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
    wcscpy(g_ini, g_dir); wcscat(g_ini, L"ParallelPose.ini"); wcscpy(g_log, g_dir); wcscat(g_log, L"ParallelPose.log");
    QueryPerformanceFrequency(&g_qf);
    if (already_loaded("ParallelPose")) { logmsg("another ParallelPose copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"ParallelPose", L"Enabled", 1, g_ini)) { logmsg("disabled in ParallelPose.ini"); return TRUE; }
    g_mode = GetPrivateProfileIntW(L"ParallelPose", L"Mode", 1, g_ini);
    g_stats = GetPrivateProfileIntW(L"ParallelPose", L"Stats", 0, g_ini);
    g_wd_ms = GetPrivateProfileIntW(L"ParallelPose", L"Watchdog", 100, g_ini);
    g_off_ms = GetPrivateProfileIntW(L"ParallelPose", L"StallOff", 2000, g_ini);
    g_racetest = GetPrivateProfileIntW(L"ParallelPose", L"RaceTest", 0, g_ini);
    install();
    CloseHandle(CreateThread(NULL, 0, logger, NULL, 0, NULL));
    return TRUE;
}
