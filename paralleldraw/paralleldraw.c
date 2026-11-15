// ParallelDraw.dll - DoW2 Retribution 3.19 (DOW2.exe 3.19.10320): the draw packets of the animated models in a render list are
// built on several CPU cores; the packets are still handed to the renderer (Spooge) by the main thread, in the original order.
// Add-on paralleldraw-1 (experiment, off by default, Elite only); stand prototype latelag_1114 stage 2 (v7).
//
// How the game draws a scene: the render-list walk 0x0078B730 (stack args: lists, pass, deferred list; eax = view buffer) calls
// vtable slot 7 (+0x1C) of every render object of the list, in list order: list 0 (with the deferred list), then the deferred
// list is sorted and submitted (0x006C5610), then 5 + 2 buckets (deferred list = 0). For AnimatorVisual (vtable 0x010C5D94) slot
// 7 is 0x007525D0 -> 0x007685A0 -> 0x00768720 -> 0x00769110 -> 0x006CEA90: for every sub-mesh it fills the sub-mesh's draw
// packet (world matrix, bound material variables copied/multiplied from the view buffer 0x006C2880/0x00675BD0, lights
// 0x006B12E0/0x006B1440, shader 0x006C5E20/GetShader) and then either submits it at once (sp::Device vtable +0xEC = Spooge
// 0x10017970 -> infernal::BatchModel -> d3dx9 Effect -> D3D9) or pushes it into the deferred list (0x006CEE36).
// The building is the expensive part (~4-5 ms of a 50 ms late-fight frame on the stand) and works on the model's own data.
//
// What this DLL does:
//  1. Entry of 0x0078B730 -> pre_batch (main thread only): every AnimatorVisual of the lists whose slot 7 would do something is
//     built NOW, in "record" mode: the sp::Device+0xEC call (Dx9Device vtable) and the deferred-list push (0x006CEE36) only
//     append (packet, list) to the building thread's arena instead of submitting. Models seen for the first time (or whose render
//     model changed) are built on the main thread first (their first build fills lazily built caches); the rest by the main
//     thread plus N workers.
//  2. Then the original walk runs. When it calls slot 7 of a model built in step 1 (main thread), the recorded packets are
//     submitted / pushed in the recorded order instead of building again - so Spooge gets the same packets in the same order.
//  3. A packet recorded twice in one batch (two builds into one packet object) -> those models are built again at their own
//     place in the walk (as the original game does). Records that the walk never asked for are counted (should be 0).
//  4. The shared caches touched while building are guarded while workers run, by big-reader spin locks (a reader writes only its
//     own flag; a writer = a cache miss, rare after the first frames, waits for the readers):
//     - shaders: sp::Device::GetShader and 0x006C5E20 (shader-variable layout) look up their sorted (key, value) vectors
//       ourselves under the reader lock; a miss runs the original under the writer lock, as does the per-model packet-buffer
//       creation (0x006CEB94..0x006CED72);
//     - material bindings 0x006C2880 up to its copy loop: reader lock, upgraded to the writer lock on a miss (or when dirty);
//     - attachments 0x00C771D0 (only when the model has any) and 0x00772FC0: exclusive.
//     Lights (0x006B12E0 -> 0x006B13B0/0x006B1440) only read the light manager, under the game's own light-manager lock: for
//     the parallel phase the main thread holds that lock for all building threads, whose own Enter/Leave on it are skipped.
// Visual only: nothing here runs in the simulation. Nothing is done when any checked byte differs from 3.19.
//
// ParallelDraw.ini [ParallelDraw]: Enabled=1, Threads=0 (workers; 0 = cores-1, at most 7), Mode=1 (1 = parallel, 0 = off,
// 2 = stand check: record + replay on the main thread only), Poll=0 (stand: re-read Mode every 0.5 s), Stats=0 (1 = a line every 10 s in ParallelDraw.log),
// Watchdog=100 (ms the main thread waits for the workers before it writes a "STALL" line, takes the light lock back and yields;
// a wait of StallOff=2000 ms or more switches the workers off for the rest of the game session), RaceTest=0 (stand only: a worker
// sleeps that many ms between taking a model number and checking it - the window of the version 1 race, see run_todo).
//
// Version 2 (watchdog): a worker's model number and the batch size are one atomic word (g_claim). Version 1 took the number from
// g_next and compared it with g_ntodo afterwards: a worker preempted between the two compared a number left over from the batch
// before with the NEXT batch's size -> one model built by two threads at once, g_done counted twice, the main thread went on while
// that worker still built (no locks: g_phase 0), and its Leave of the light lock (Enter skipped in the phase) became a real
// LeaveCriticalSection by a thread that did not own it (Windows: "another thread using EnterCriticalSection may wait indefinitely").
// The light-lock skip is now remembered per thread at Enter (stack), not looked up from g_phase at Leave.
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define VT_AV        0x010C5D94u
#define FN_S7        0x007525D0u
#define FN_RL        0x0078B730u   // render-list walk
#define RL_CONT      0x0078B735u
#define SITE_PUSH    0x006CEE36u   // mov esi,[ebp+0x18]; test esi,esi; je 0x6cee64 (7 bytes)
#define PUSH_CONT    0x006CEE3Du
#define PUSH_DEV     0x006CEE64u
#define PUSH_DONE    0x006CEE7Au
#define SITE_MISS    0x006CEB94u   // push 0x1c; call 0xdaa9b6 (7 bytes)
#define MISS_CONT    0x006CEB9Bu
#define FN_NEW       0x00DAA9B6u
#define SITE_3A0     0x006CED6Du   // call 0x6ce3a0 (end of the packet-buffer creation)
#define FN_3A0       0x006CE3A0u
#define RET_3A0      0x006CED72u
#define SITE_6C2     0x006CEAFDu   // call 0x6c2880
#define FN_6C2       0x006C2880u
#define SITE_6C2LOOP 0x006C2B10u   // mov esi,[ebx+8]; cmp esi,[ebx+0xc] (6 bytes) - before the copy loop
#define C6C2LOOP     0x006C2B16u
#define SITE_5E20    0x006CEDDDu   // call 0x6c5e20
#define FN_5E20      0x006C5E20u
#define SITE_GS      0x006CEDFAu   // call [0xf88f44] (GetShader, 6 bytes)
#define IAT_GS       0x00F88F44u
#define SITE_12E0    0x007685D5u   // call 0x6b12e0
#define FN_12E0      0x006B12E0u
#define SITE_C771    0x0076872Du   // call 0xc771d0
#define FN_C771      0x00C771D0u
#define SITE_2FC0    0x0076866Fu   // call 0x772fc0
#define FN_2FC0      0x00772FC0u
#define FN_VGROW     0x00AE5CA0u   // vector<ptr> grow-insert: esi = vector, eax = position, push &value; ret 4
#define SP_BASE      0x10000000u
#define SP_DEV_EC    0x10017970u   // Dx9Device vtable +0xEC
#define SP_VT_DX9    0x10081794u

typedef void (__attribute__((thiscall)) *S7Fn)(void *, void *, void *, void *);
typedef void (__attribute__((thiscall)) *DevFn)(void *, void *);

static wchar_t g_dir[MAX_PATH], g_log[MAX_PATH], g_ini[MAX_PATH];
static volatile LONG g_mode = 1;
static int g_threads, g_stats, g_wd_ms = 100, g_off_ms = 2000, g_racetest;
static volatile LONG g_par_off;        // a stall of g_off_ms or more: no workers for the rest of the game session
static DWORD g_main_tid, g_tls = TLS_OUT_OF_INDEXES;
static S7Fn o_s7 = (S7Fn)FN_S7;
static DevFn o_dev;
static void *g_dev_obj;
static LARGE_INTEGER g_qf;

static void logmsg(const char *fmt, ...) {
    FILE *f = _wfopen(g_log, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}

// ---------------------------------------------------------------- per-thread context, arenas, batches
#define MAXD 4                 // nested render-list walks handled
#define MAXT 16                // threads (main = 0)
#define ARENA 32768
#define MAXI 8192
typedef struct { void *pkt, *list; } Emit;
typedef struct {
    int id, rec_on;            // rec_on: emits are recorded into arena[depth]
    int depth, n[MAXD];        // arena fill per depth
    Emit *arena[MAXD];
    int lstk[64], lsp;         // lock stack: lock id + 1 if this level took it, 0 if not
    int x[4];                  // exclusive depth per lock
    int matmode;               // material-binding lock held: 1 shared, 2 exclusive
    LONG overflow;
    volatile LONG rd[4];       // reading under lock k (big-reader locks: readers touch only their own cache line)
    LONG fast[2];
    // watchdog view of this thread (written by the thread itself, read by the main thread when a wait is long)
    void *volatile cur;        // model being built
    volatile LONGLONG t_cur;   // QPC at its start
    void *volatile mx_wait;    // Plat::RecursiveMutex (CRITICAL_SECTION) this thread is blocked on
    volatile LONG spin_k;      // our spin lock k + 1 this thread spins on
    DWORD tid;
    // light-lock Enter calls of this thread: skipped (handed over to the main thread) or real; Leave pops the same decision
    void *lcs_stk[16]; char lskip[16]; int llsp;
} __attribute__((aligned(64))) Ctx;
static Ctx g_ctx[MAXT];
typedef struct { void *obj, *a3; int thr, start, n, d; char rebuild, done, cold; } Item;
typedef struct {
    void *a2, *e; int ni; Item *it;
    int *hkey;                 // open-addressing table obj -> item index + 1
} Batch;
#define HB 16384
static Item g_items[MAXD][MAXI];
static int g_hash[MAXD][HB];
static int g_hused[MAXD][MAXI], g_nhused[MAXD];
static void *g_hobj[MAXD][HB];
static Batch g_b[MAXD];
static int g_depth;            // number of active batches (main thread only)

// locks (taken only while workers run): 0 shaders (GetShader, 0x6C5E20, packet-buffer creation), 1 unused (lights: the game's
// own mutex), 2 attachments (0x00C771D0, 0x00772FC0), 3 material bindings (0x6C2880: reader, writer on a miss)
#define LK_SH 0
#define LK_LI 1
#define LK_AT 2
#define LK_MAT 3
// big-reader spin locks (the guarded pieces take microseconds): a reader sets its own flag and checks that no writer is in; a
// writer (rare: a cache miss) takes the writer word and waits until no reader flag is set
static volatile LONG g_wr[4][16];      // one cache line each
static volatile LONG s_wait[4];
static int g_nworkers;
// a long spin (~1 ms) yields the CPU: a preempted lock holder on the same core gets it back
static inline void spin_pause(unsigned *n) { __builtin_ia32_pause(); if (!(++*n & 4095)) SwitchToThread(); }
static void acq_x(Ctx *c, int k) {
    unsigned n = 0;
    if (InterlockedCompareExchange(&g_wr[k][0], c->id + 1, 0) != 0) {
        InterlockedIncrement(&s_wait[k]); c->spin_k = k + 1;
        do { while (g_wr[k][0]) spin_pause(&n); } while (InterlockedCompareExchange(&g_wr[k][0], c->id + 1, 0) != 0);
    }
    for (int t = 0; t <= g_nworkers; t++) if (g_ctx[t].rd[k]) { c->spin_k = k + 1; while (g_ctx[t].rd[k]) spin_pause(&n); }
    c->spin_k = 0;
}
static void rel_x(int k) { __atomic_store_n(&g_wr[k][0], 0, __ATOMIC_RELEASE); }
static void acq_s(Ctx *c, int k) {
    unsigned n = 0;
    for (;;) {
        InterlockedExchange(&c->rd[k], 1);                 // full barrier: the flag is visible before the writer word is read
        if (!g_wr[k][0]) { c->spin_k = 0; return; }
        __atomic_store_n(&c->rd[k], 0, __ATOMIC_RELEASE);
        InterlockedIncrement(&s_wait[k]); c->spin_k = k + 1;
        while (g_wr[k][0]) spin_pause(&n);
    }
}
static void rel_s(Ctx *c, int k) { __atomic_store_n(&c->rd[k], 0, __ATOMIC_RELEASE); }
static volatile LONG g_phase;  // workers running: the shared caches are locked
static volatile LONG g_handover;       // the main thread holds the light lock for the building threads (their Enter/Leave skipped)
static HANDLE g_sem; static HANDLE g_worker_h[MAXT];
// g_claim = batch size << 16 | next model number (MAXI < 65536): taking a number and checking it against the size of the SAME
// batch is one atomic step. 0 between batches (size 0: every late claim fails).
static volatile LONG g_claim, g_done, g_ntodo, g_batchno;
static Item *volatile g_todo[MAXI];
static void *volatile g_cur_a2, *volatile g_cur_e;
static volatile int g_cur_d;
static unsigned short g_fcw; static unsigned int g_mxcsr;

// stats
static volatile LONG s_walks, s_batches, s_items, s_cold, s_emits, s_pushes, s_rebuild, s_unconsumed, s_replayed, s_nest_rec,
    s_overflow, s_byworker, s_par_batches, s_dev_calls_worker_norec, s_skipdepth, s_stalls, s_late, s_mx_other, s_mx_other_busy,
    s_mx_foreign, s_mx_foreign_phase;
static volatile LONGLONG s_wait_max_qpc;
static volatile LONGLONG s_qpc_pre, s_qpc_wait, s_qpc_walk;   // walk: main-thread time in slot 7 calls inside a walk (build or replay)

static inline Ctx *ctx_get(void) { return g_tls == TLS_OUT_OF_INDEXES ? NULL : (Ctx *)TlsGetValue(g_tls); }

// ---------------------------------------------------------------- lock levels (only while workers run)
static void lk_x(Ctx *c, int k) { if (c->x[k]++ == 0) acq_x(c, k); }
static void lk_unx(Ctx *c, int k) { if (--c->x[k] == 0) rel_x(k); }
static void big_enter_k(int k) {
    Ctx *c = ctx_get(); if (!c) return;
    int l = g_phase ? k + 1 : 0;
    if (l) lk_x(c, k);
    if (c->lsp < 64) c->lstk[c->lsp] = l;
    c->lsp++;
}
void __cdecl big_enter_sh(void) { big_enter_k(LK_SH); }
void __cdecl big_enter_li(void) { big_enter_k(LK_LI); }
void __cdecl big_enter_at(void) { big_enter_k(LK_AT); }
void __cdecl big_enter_att(BYTE *edi) {     // 0xC771D0: nothing to guard when the model has no attachments
    Ctx *c = ctx_get(); if (!c) return;
    int l = g_phase && *(DWORD *)(edi + 0x240) != *(DWORD *)(edi + 0x244) ? LK_AT + 1 : 0;
    if (l) lk_x(c, LK_AT);
    if (c->lsp < 64) c->lstk[c->lsp] = l;
    c->lsp++;
}
void __cdecl big_leave(void) {
    Ctx *c = ctx_get(); if (!c || c->lsp <= 0) return;
    c->lsp--;
    if (c->lsp < 64 && c->lstk[c->lsp]) lk_unx(c, c->lstk[c->lsp] - 1);
}
// material bindings 0x6C2880(p1 = binding table): sorted vector with a dirty flag (+0x19, sorted at entry) -> exclusive when dirty,
// shared otherwise; a lookup miss upgrades to exclusive before the new entry is built (a second thread missing the same key at
// the same time adds a duplicate entry, which the lookup tolerates)
void __cdecl enter_mat(BYTE *p1) {
    Ctx *c = ctx_get(); if (!c || c->matmode || !g_phase) return;
    if (p1[0x19]) { lk_x(c, LK_MAT); c->matmode = 2; return; }
    acq_s(c, LK_MAT);
    c->matmode = 1;
}
void __cdecl upgrade_mat(void) {
    Ctx *c = ctx_get(); if (!c || c->matmode != 1) return;
    rel_s(c, LK_MAT); lk_x(c, LK_MAT); c->matmode = 2;
}
void __cdecl leave_mat(void) {
    Ctx *c = ctx_get(); if (!c || !c->matmode) return;
    if (c->matmode == 1) rel_s(c, LK_MAT); else lk_unx(c, LK_MAT);
    c->matmode = 0;
}
// sp::Device::GetShader hit path without the exclusive lock: Spooge keeps (key, shader) sorted by key at [dev+8, dev+0xC), resorted
// when [dev+0x15] is set; misses create the shader and insert -> the original under the exclusive lock
typedef void *(__attribute__((thiscall)) *KeyFn)(void *);
void *__cdecl fast_gs(BYTE *dev, unsigned *key) {
    Ctx *c = ctx_get(); if (!c || !g_phase) return NULL;
    int sh = c->x[LK_SH] == 0;
    if (sh) acq_s(c, LK_SH);
    void *r = NULL;
    if (!dev[0x15]) {
        unsigned *b = *(unsigned **)(dev + 8), *e = *(unsigned **)(dev + 0xC), k = *key;
        int n = (int)(e - b) / 2;
        while (n > 0) { int h = n >> 1; if (b[2 * h] < k) { b += 2 * h + 2; n -= h + 1; } else n = h; }
        if (b != e && b[0] == k) r = (void *)b[1];
    }
    if (sh) rel_s(c, LK_SH);
    if (r) c->fast[0]++;
    return r;
}
// 0x6C5E20(p1, key) fast paths: the key equals the binding's own current key -> [p1+0x1C] (read-only, no lock); else the
// (key, layout) vector [p1+0x20, p1+0x24) sorted by key (resorted when [p1+0x2D] is set) under the shared lock; a miss creates
// the layout -> the original under the exclusive lock
void *__cdecl fast_5e20(BYTE *p1, unsigned *key) {
    Ctx *c = ctx_get(); if (!g_phase || !c) return NULL;
    void *o = *(void **)(p1 + 8);
    unsigned cur = 0;
    if (o) cur = *(unsigned *)((KeyFn)(*(void ***)o)[2])(o);
    void *r = NULL;
    if (*key == cur) r = *(void **)(p1 + 0x1C);
    else {
        int sh = c->x[LK_SH] == 0;
        if (sh) acq_s(c, LK_SH);
        if (!p1[0x2D]) {
            unsigned *b = *(unsigned **)(p1 + 0x20), *e = *(unsigned **)(p1 + 0x24), k = *key;
            int n = (int)(e - b) / 2;
            while (n > 0) { int h = n >> 1; if (b[2 * h] < k) { b += 2 * h + 2; n -= h + 1; } else n = h; }
            if (b != e && b[0] == k) r = (void *)b[1];
        }
        if (sh) rel_s(c, LK_SH);
    }
    if (r) c->fast[1]++;
    return r;
}

// ---------------------------------------------------------------- recording
static inline int rec_emit(Ctx *c, void *pkt, void *list) {
    int d = c->depth, k = c->n[d];
    if (k >= ARENA) { c->overflow++; return 0; }
    c->arena[d][k].pkt = pkt; c->arena[d][k].list = list; c->n[d] = k + 1;
    return 1;
}
// Dx9Device vtable +0xEC
static void __attribute__((fastcall)) hk_dev(void *self, void *edx, void *pkt) {
    (void)edx;
    Ctx *c = ctx_get();
    if (c && c->rec_on) { g_dev_obj = self; rec_emit(c, pkt, NULL); return; }   // on overflow the whole batch is rebuilt
    o_dev(self, pkt);
}
// deferred-list push in 0x6CEA90 (from the asm thunk): 1 = recorded
int __cdecl rec_push(void *list, void *pkt) {
    Ctx *c = ctx_get();
    if (c && c->rec_on) { if (rec_emit(c, pkt, list)) InterlockedIncrement(&s_pushes); return 1; }
    return 0;
}

// vector push_back the game's way (inline fast path; slow path = the game's own grow-insert 0xAE5CA0 via th_grow)
extern void __cdecl th_grow(void *list, void **pval);
__asm__(".text\n_th_grow:\n\t"
        "pushl %esi\n\t" "movl 8(%esp), %esi\n\t" "movl 4(%esi), %eax\n\t" "pushl 0xc(%esp)\n\t"
        "movl $0x00AE5CA0, %ecx\n\t" "call *%ecx\n\t" "popl %esi\n\t" "ret\n");
static void vec_push(void *list, void *val) {
    void **v = (void **)list;                      // begin, end, capacity
    if (v[1] != v[2]) { if (v[1]) *(void **)v[1] = val; v[1] = (char *)v[1] + 4; return; }
    void *tmp = val; th_grow(list, &tmp);
}

// ---------------------------------------------------------------- building
static void build_item(Ctx *c, Item *it, void *a2, void *e) {
    int d = it->d; c->depth = d;
    int s = c->n[d];
    LARGE_INTEGER t; QueryPerformanceCounter(&t); c->t_cur = t.QuadPart; c->cur = it->obj;
    c->rec_on = 1;
    o_s7(it->obj, a2, it->a3, e);
    c->rec_on = 0;
    c->cur = NULL;
    it->thr = c->id; it->start = s; it->n = c->n[d] - s;
}
static void run_todo(Ctx *c) {
    for (;;) {
        LONG b = g_batchno;
        unsigned v = (unsigned)InterlockedIncrement(&g_claim) - 1, i = v & 0xffff, n = v >> 16;
        if (g_racetest && c->id && i >= n) Sleep(g_racetest);              // stand: the window version 1 lost a race in
        if (i >= n) { if (c->id && g_batchno != b) InterlockedIncrement(&s_late); return; }   // s_late: version 1 could have raced
        // a valid number: the batch stays alive (and g_todo, g_cur_*, g_fcw unchanged) until this model is counted in g_done
        if (c->id) { unsigned short fcw = g_fcw; unsigned int mx = g_mxcsr; __asm__ volatile("fldcw %0" :: "m"(fcw)); __asm__ volatile("ldmxcsr %0" :: "m"(mx)); }
        build_item(c, g_todo[i], g_cur_a2, g_cur_e);
        if (c->id) InterlockedIncrement(&s_byworker);
        InterlockedIncrement(&g_done);
    }
}
static DWORD WINAPI worker(LPVOID arg) {
    Ctx *c = (Ctx *)arg;
    TlsSetValue(g_tls, c);
    c->tid = GetCurrentThreadId();
    for (;;) {
        WaitForSingleObject(g_sem, INFINITE);
        run_todo(c);
    }
    return 0;
}

// The light manager's own lock (Plat::RecursiveMutex = CRITICAL_SECTION at [[0x13351B4]]) is taken around every light query and
// light-constant fill (0x006B13B0, 0x006B1440) - it would serialise the workers. The light manager is only read while drawing, so
// for the parallel phase the main thread takes that lock once on behalf of all building threads (foreign threads still wait for
// it as before) and the building threads' own Enter/Leave on it are no-ops (DOW2.exe's imports of RecursiveMutex::Enter/Leave).
typedef void (__attribute__((thiscall)) *MxFn)(void *);
static MxFn o_mx_enter, o_mx_leave;
static void *volatile g_light_cs;
static volatile LONG s_mx_skipped;
// Plat::RecursiveMutex is a CRITICAL_SECTION (Platform.dll: Enter = EnterCriticalSection(this)). Whether a light-lock Enter was
// skipped is pushed per thread and popped by its Leave, so a Leave never releases a lock its thread did not take.
static void __attribute__((fastcall)) hk_mx_enter(void *cs) {
    Ctx *c = ctx_get();
    if (!c) {                                                       // the game's other threads: as before (counted)
        if (cs && cs == g_light_cs) { s_mx_foreign++; if (g_handover) s_mx_foreign_phase++; }
        o_mx_enter(cs); return;
    }
    if (cs && cs == g_light_cs) {
        int skip = g_handover && c->llsp < 16;
        if (c->llsp < 16) { c->lcs_stk[c->llsp] = cs; c->lskip[c->llsp] = (char)skip; }
        c->llsp++;
        if (skip) { InterlockedIncrement(&s_mx_skipped); return; }
    } else if (g_phase) {                                           // another game lock taken while building in the phase
        InterlockedIncrement(&s_mx_other);
        if (TryEnterCriticalSection((CRITICAL_SECTION *)cs)) return;
        InterlockedIncrement(&s_mx_other_busy);
    }
    c->mx_wait = cs; o_mx_enter(cs); c->mx_wait = NULL;
}
static void __attribute__((fastcall)) hk_mx_leave(void *cs) {
    Ctx *c = ctx_get();
    if (c && cs && c->llsp > 0 && (c->llsp > 16 || c->lcs_stk[c->llsp - 1] == cs)) {
        c->llsp--;
        if (c->llsp < 16 && c->lskip[c->llsp]) return;
    }
    o_mx_leave(cs);
}
static void *light_cs(void) { void *m = *(void **)0x013351B4u; return m ? *(void **)m : NULL; }

// ---------------------------------------------------------------- watchdog
static DWORD cs_owner(void *cs) { return cs ? (DWORD)(ULONG_PTR)((CRITICAL_SECTION *)cs)->OwningThread : 0; }
static const char *lk_name(int k) { return k == LK_SH ? "shaders" : k == LK_AT ? "attachments" : k == LK_MAT ? "materials" : "?"; }
static void stall_report(LONGLONG ms, int nt, int d, void *lcs) {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    char b[1400]; int o = snprintf(b, sizeof b, "STALL: main thread (tid %lu) waits %lld ms for the workers: batch %ld (depth %d), %ld of %d models "
        "done; light lock %p owner tid %lu%s", g_main_tid, ms, (long)g_batchno, d, (long)g_done, nt, lcs, cs_owner(lcs),
        g_handover ? " (handed over)" : "");
    for (int t = 1; t <= g_nworkers && o < (int)sizeof b - 200; t++) {
        Ctx *c = &g_ctx[t]; void *obj = c->cur;
        if (!obj) continue;
        o += snprintf(b + o, sizeof b - o, " | worker %d (tid %lu): model %p vtable %08lx for %lld ms", t, c->tid, obj,
                      IsBadReadPtr(obj, 4) ? 0ul : *(DWORD *)obj, (now.QuadPart - c->t_cur) * 1000 / g_qf.QuadPart);
        void *mw = c->mx_wait; LONG sk = c->spin_k;
        if (mw) o += snprintf(b + o, sizeof b - o, ", blocked on game lock %p%s owned by tid %lu%s", mw, mw == lcs ? " (light)" : "",
                              cs_owner(mw), cs_owner(mw) == g_main_tid ? " = MAIN" : "");
        if (sk) o += snprintf(b + o, sizeof b - o, ", spins on our %s lock (writer: thread %ld)", lk_name(sk - 1), (long)g_wr[sk - 1][0] - 1);
    }
    logmsg("%s", b);
}

// models seen before (with the same render model, in the same pass): their lazily built caches exist
#define HS 65536
static void *s_key_obj[HS], *s_key_a2[HS], *s_key_rm[HS];
static int seen_before(void *obj, void *a2) {
    void *rm = *(void **)((char *)obj + 0x9C); rm = rm ? *(void **)rm : NULL;
    unsigned h = (((unsigned)(ULONG_PTR)obj >> 4) * 2654435761u ^ ((unsigned)(ULONG_PTR)a2 >> 3) * 40503u) >> 16;
    for (int i = 0; i < 8; i++) {
        unsigned j = (h + i) & (HS - 1);
        if (s_key_obj[j] == obj && s_key_a2[j] == a2) { int ok = s_key_rm[j] == rm; s_key_rm[j] = rm; return ok; }
        if (!s_key_obj[j]) { s_key_obj[j] = obj; s_key_a2[j] = a2; s_key_rm[j] = rm; return 0; }
    }
    unsigned j = h & (HS - 1); s_key_obj[j] = obj; s_key_a2[j] = a2; s_key_rm[j] = rm; return 0;
}

static inline unsigned hobj(void *p) { return ((unsigned)(ULONG_PTR)p >> 3) * 2654435761u >> 18; }
static Item *find_item(int d, void *obj) {
    unsigned h = hobj(obj);
    for (int i = 0; i < 32; i++) { unsigned j = (h + i) & (HB - 1); if (!g_hash[d][j]) return NULL; if (g_hobj[d][j] == obj) return &g_items[d][g_hash[d][j] - 1]; }
    return NULL;
}

// duplicated packets in a batch -> rebuild those models at their own place in the walk
#define HP 32768
static void *s_pk[HP]; static int s_pki[HP]; static int s_pkgen[HP]; static int s_gen;
static void mark_dups(int d, int ni) {
    s_gen++;
    for (int i = 0; i < ni; i++) {
        Item *it = &g_items[d][i]; Emit *a = g_ctx[it->thr].arena[d] + it->start;
        for (int k = 0; k < it->n; k++) {
            unsigned h = ((unsigned)(ULONG_PTR)a[k].pkt >> 2) * 2654435761u >> 17;
            for (int p = 0; p < 64; p++) {
                unsigned j = (h + p) & (HP - 1);
                if (s_pkgen[j] != s_gen) { s_pkgen[j] = s_gen; s_pk[j] = a[k].pkt; s_pki[j] = i; break; }
                if (s_pk[j] == a[k].pkt) {
                    if (!it->rebuild) { it->rebuild = 1; InterlockedIncrement(&s_rebuild); }
                    Item *o = &g_items[d][s_pki[j]]; if (!o->rebuild) { o->rebuild = 1; InterlockedIncrement(&s_rebuild); }
                    break;
                }
            }
        }
    }
}

// called from the 0x78B730 entry thunk (all registers saved)
void __cdecl pre_batch(void *e, void **lists, void *a2, void *a3) {
    DWORD tid = GetCurrentThreadId();
    if (!g_main_tid) { g_main_tid = tid; g_ctx[0].tid = tid; TlsSetValue(g_tls, &g_ctx[0]); }
    Ctx *c = ctx_get();
    if (c && c->rec_on) { InterlockedIncrement(&s_nest_rec); return; }   // a walk inside a model being built: left as it is
    if (tid != g_main_tid) return;
    int d = g_depth;
    g_depth++;
    if (d >= MAXD) { s_skipdepth++; return; }
    Batch *b = &g_b[d]; b->ni = 0; b->a2 = a2; b->e = e; s_walks++;
    for (int i = 0; i < g_nhused[d]; i++) g_hash[d][g_hused[d][i]] = 0;
    g_nhused[d] = 0;
    LONG mode = g_mode;
    if (mode != 1 && mode != 2) return;
    if (mode == 1 && g_par_off) return;                              // switched off after a stall: the game's own walk
    LARGE_INTEGER q0, q1; QueryPerformanceCounter(&q0);
    // the walk's lists: list 0 (deferred list a3), 5 buckets at +0x30, 2 at +0x6C (deferred list 0)
    int ni = 0;
    for (int l = 0; l < 8; l++) {
        void ***v = l == 0 ? (void ***)lists : l < 6 ? (void ***)((char *)lists + 0x30 + 12 * (l - 1)) : (void ***)((char *)lists + 0x6C + 12 * (l - 6));
        void **p = v[0], **q = v[1];
        for (; p != q && ni < MAXI; p++) {
            void *obj = *p;
            if (!obj || *(DWORD *)obj != VT_AV) continue;
            if (!(*(DWORD *)((char *)obj + 0x214) & 2)) continue;          // slot 7 returns at once
            if (find_item(d, obj)) continue;                                // twice in the lists: only the first is prebuilt
            Item *it = &g_items[d][ni]; it->obj = obj; it->a3 = l == 0 ? a3 : NULL; it->rebuild = it->done = 0; it->n = 0; it->d = d; it->thr = 0; it->start = 0;
            unsigned h = hobj(obj);
            int ok = 0;
            for (int i = 0; i < 32; i++) { unsigned j = (h + i) & (HB - 1); if (!g_hash[d][j]) { g_hash[d][j] = ni + 1; g_hobj[d][j] = obj; g_hused[d][g_nhused[d]++] = j; ok = 1; break; } }
            if (!ok) continue;                                              // table crowded: this one is built by the walk
            it->cold = !seen_before(obj, a2);
            ni++;
        }
    }
    b->ni = ni;
    if (!ni) return;
    InterlockedIncrement(&s_batches); InterlockedExchangeAdd(&s_items, ni);
    for (int t = 0; t <= g_nworkers; t++) { g_ctx[t].n[d] = 0; g_ctx[t].depth = d; }
    Ctx *m = &g_ctx[0];
    // cold models (and everything in mode 2) on this thread first
    int nt = 0;
    for (int i = 0; i < ni; i++) {
        Item *it = &g_items[d][i];
        if (mode == 2 || it->cold || !g_nworkers) { if (it->cold) s_cold++; build_item(m, it, a2, e); }
        else g_todo[nt++] = it;
    }
    if (nt) {
        unsigned short fcw; unsigned int mxc;
        __asm__ volatile("fnstcw %0" : "=m"(fcw)); __asm__ volatile("stmxcsr %0" : "=m"(mxc));
        g_fcw = fcw; g_mxcsr = mxc; g_cur_a2 = a2; g_cur_e = e; g_cur_d = d; g_ntodo = nt; g_done = 0; g_batchno++;
        void *lcs = o_mx_enter ? light_cs() : NULL;
        g_light_cs = lcs;
        if (lcs) { o_mx_enter(lcs); InterlockedExchange(&g_handover, 1); }   // before the phase: our threads skip it only then
        InterlockedExchange(&g_phase, 1);
        InterlockedExchange(&g_claim, nt << 16);                             // publish (full barrier)
        int wake = g_nworkers < nt - 1 ? g_nworkers : nt - 1;
        if (wake > 0) ReleaseSemaphore(g_sem, wake, NULL);
        run_todo(m);
        // every model is taken now; only models being built by workers remain. Spin (they take tens of us), yield after 2 ms, and
        // after g_wd_ms write a STALL line and take the light lock back (a foreign thread waiting for it may hold what a worker needs)
        LARGE_INTEGER w0, w1; QueryPerformanceCounter(&w0);
        unsigned sp = 0; LONGLONG next_ms = g_wd_ms, ms = 0;
        while (g_done < nt) {
            __builtin_ia32_pause();
            if (++sp & 1023) continue;
            QueryPerformanceCounter(&w1); ms = (w1.QuadPart - w0.QuadPart) * 1000 / g_qf.QuadPart;
            if (ms >= 2) SwitchToThread();
            if (g_wd_ms > 0 && ms >= next_ms) {
                if (next_ms == g_wd_ms) InterlockedIncrement(&s_stalls);
                stall_report(ms, nt, d, lcs);
                if (g_handover) { InterlockedExchange(&g_handover, 0); o_mx_leave(lcs); logmsg("STALL: light lock taken back from the workers"); }
                next_ms = next_ms < 1000 ? 1000 : next_ms + 5000;
            }
        }
        QueryPerformanceCounter(&w1); s_qpc_wait += w1.QuadPart - w0.QuadPart;
        if (w1.QuadPart - w0.QuadPart > s_wait_max_qpc) s_wait_max_qpc = w1.QuadPart - w0.QuadPart;
        InterlockedExchange(&g_claim, 0);
        InterlockedExchange(&g_phase, 0);
        if (g_handover) { InterlockedExchange(&g_handover, 0); o_mx_leave(lcs); }
        if (g_wd_ms > 0 && ms >= g_wd_ms) {
            ms = (w1.QuadPart - w0.QuadPart) * 1000 / g_qf.QuadPart;
            logmsg("STALL over: the workers finished after %lld ms (batch %ld)", ms, (long)g_batchno);
            if (g_off_ms > 0 && ms >= g_off_ms) { g_par_off = 1; logmsg("STALL: workers switched off for the rest of this game session"); }
        }
        s_par_batches++;
    }
    LONG ov = 0;
    for (int t = 0; t <= g_nworkers; t++) { ov += g_ctx[t].overflow; g_ctx[t].overflow = 0; }
    if (ov) {                                                               // an arena overflowed: rebuild everything in the walk
        s_overflow += ov;
        for (int i = 0; i < ni; i++) g_items[d][i].rebuild = 1;
    } else mark_dups(d, ni);
    int em = 0; for (int i = 0; i < ni; i++) em += g_items[d][i].n;
    s_emits += em;
    QueryPerformanceCounter(&q1); s_qpc_pre += q1.QuadPart - q0.QuadPart;
}
void __cdecl post_batch(void) {
    if (GetCurrentThreadId() != g_main_tid) return;
    Ctx *c = ctx_get();
    if (c && c->rec_on) return;
    if (g_depth <= 0) return;
    g_depth--;
    int d = g_depth;
    if (d >= MAXD) return;
    Batch *b = &g_b[d];
    for (int i = 0; i < b->ni; i++) if (!g_items[d][i].done) s_unconsumed++;
    b->ni = 0;
}

// AnimatorVisual slot 7
static void __attribute__((fastcall)) hk_s7(void *self, void *edx, void *a2, void *a3, void *e) {
    (void)edx;
    Ctx *c = ctx_get();
    if (g_depth > 0 && g_depth <= MAXD && GetCurrentThreadId() == g_main_tid && !(c && c->rec_on)) {
        LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
        int d = g_depth - 1;
        Item *it = g_b[d].ni ? find_item(d, self) : NULL;
        if (it && !it->done) {
            it->done = 1;
            if (!it->rebuild && a2 == g_b[d].a2 && a3 == it->a3 && e == g_b[d].e) {
                Emit *a = g_ctx[it->thr].arena[d] + it->start;
                for (int k = 0; k < it->n; k++) {
                    if (a[k].list) vec_push(a[k].list, a[k].pkt);
                    else o_dev(g_dev_obj, a[k].pkt);
                }
                s_replayed++;
                QueryPerformanceCounter(&t1); s_qpc_walk += t1.QuadPart - t0.QuadPart;
                return;
            }
        }
        o_s7(self, a2, a3, e);
        QueryPerformanceCounter(&t1); s_qpc_walk += t1.QuadPart - t0.QuadPart;
        return;
    }
    o_s7(self, a2, a3, e);
}

// ---------------------------------------------------------------- asm thunks
// 0x78B730 entry: pre_batch(eax, a1, a2, a3), original walk, post_batch
extern char th_rl[], th_rl_orig[], th_push[], th_miss[], th_3a0[], th_6c2[], th_6c2loop[], th_6c2miss[], th_gs_slow[], th_5e20_slow[], th_5e20[], th_gs[], th_c771[], th_2fc0[];
__asm__(
    ".text\n"
    "_th_rl:\n\t"
    "pushal\n\t"
    "pushl 0x2c(%esp)\n\t" "pushl 0x2c(%esp)\n\t" "pushl 0x2c(%esp)\n\t" "pushl %eax\n\t"
    "call _pre_batch\n\t" "addl $16, %esp\n\t"
    "popal\n\t"
    "pushl 0xc(%esp)\n\t" "pushl 0xc(%esp)\n\t" "pushl 0xc(%esp)\n\t"
    "call _th_rl_orig\n\t"
    "pushal\n\t" "call _post_batch\n\t" "popal\n\t"
    "ret $0xc\n"
    "_th_rl_orig:\n\t"
    "pushl %ebx\n\t" "movl 8(%esp), %ebx\n\t"
    "pushl $0x0078B735\n\t" "ret\n"
    // deferred-list push in 0x6CEA90
    "_th_push:\n\t"
    "movl 0x18(%ebp), %esi\n\t"
    "testl %esi, %esi\n\t"
    "jne 1f\n\t"
    "pushl $0x006CEE64\n\t" "ret\n"
    "1:\n\t"
    "pushal\n\t" "pushl 0xc(%ebx)\n\t" "pushl %esi\n\t" "call _rec_push\n\t" "addl $8, %esp\n\t" "testl %eax, %eax\n\t" "popal\n\t"
    "jne 2f\n\t"
    "pushl $0x006CEE3D\n\t" "ret\n"
    "2:\n\t"
    "movl 0xc(%ebx), %ebx\n\t" "movl %ebx, 0x1c(%esp)\n\t"
    "pushl $0x006CEE7A\n\t" "ret\n"
    // packet-buffer creation start (lock) ... end (unlock after 0x6CE3A0)
    "_th_miss:\n\t"
    "pushal\n\t" "pushfl\n\t" "call _big_enter_sh\n\t" "popfl\n\t" "popal\n\t"
    "pushl $0x1c\n\t" "movl $0x00DAA9B6, %eax\n\t" "call *%eax\n\t"
    "pushl $0x006CEB9B\n\t" "ret\n"
    "_th_3a0:\n\t"
    "movl $3f, (%esp)\n\t" "pushl $0x006CE3A0\n\t" "ret\n"
    "3:\n\t"
    "pushal\n\t" "pushfl\n\t" "call _big_leave\n\t" "popfl\n\t" "popal\n\t"
    "pushl $0x006CED72\n\t" "ret\n"
    // material binding lookup: lock at the call, unlock before the copy loop
    "_th_6c2:\n\t"
    "pushal\n\t" "pushfl\n\t" "pushl 0x28(%esp)\n\t" "call _enter_mat\n\t" "addl $4, %esp\n\t" "popfl\n\t" "popal\n\t"
    "pushl $0x006C2880\n\t" "ret\n"
    "_th_6c2miss:\n\t"
    "pushal\n\t" "pushfl\n\t" "call _upgrade_mat\n\t" "popfl\n\t" "popal\n\t"
    "xorl %ebx, %ebx\n\t" "movl %ebx, 0x24(%esp)\n\t"
    "pushl $0x006C290C\n\t" "ret\n"
    "_th_6c2loop:\n\t"
    "pushal\n\t" "pushfl\n\t" "call _leave_mat\n\t" "popfl\n\t" "popal\n\t"
    "movl 8(%ebx), %esi\n\t" "cmpl 0xc(%ebx), %esi\n\t"
    "pushl $0x006C2B16\n\t" "ret\n"      /* push/ret keep the flags of the cmp */
);
// call-site wrappers: lock, call the target with the caller's own stack frame (return address swapped), unlock, continue
#define WRAP(name, lk, target_insn, cont)                                                       \
    __asm__(".text\n_" #name ":\n\t"                                                            \
            "pushal\n\t" "pushfl\n\t" "call _big_enter_" #lk "\n\t" "popfl\n\t" "popal\n\t"     \
            "movl $9f, (%esp)\n\t" target_insn "\n"                                             \
            "9:\n\t"                                                                             \
            "pushal\n\t" "pushfl\n\t" "call _big_leave\n\t" "popfl\n\t" "popal\n\t"             \
            "pushl $" #cont "\n\t" "ret\n");
WRAP(th_5e20_slow, sh, "pushl $0x006C5E20\n\tret", 0x006CEDE2)
WRAP(th_gs_slow, sh, "jmp *0x00F88F44", 0x006CEE00)
__asm__(".text\n_th_c771:\n\t"
        "pushal\n\t" "pushfl\n\t" "pushl 4(%esp)\n\t" "call _big_enter_att\n\t" "addl $4, %esp\n\t" "popfl\n\t" "popal\n\t"
        "movl $9f, (%esp)\n\t" "pushl $0x00C771D0\n\t" "ret\n"
        "9:\n\t"
        "pushal\n\t" "pushfl\n\t" "call _big_leave\n\t" "popfl\n\t" "popal\n\t"
        "pushl $0x00768732\n\t" "ret\n");
WRAP(th_2fc0, at, "pushl $0x00772FC0\n\tret", 0x00768674)
// fast paths (hit without the exclusive lock), else the locked original
__asm__(".text\n_th_gs:\n\t"
        "pushal\n\t" "pushfl\n\t" "pushl 0x28(%esp)\n\t" "pushl %ecx\n\t" "call _fast_gs\n\t" "addl $8, %esp\n\t"
        "testl %eax, %eax\n\t" "jz 1f\n\t"
        "movl %eax, 0x20(%esp)\n\t" "popfl\n\t" "popal\n\t" "ret $4\n"
        "1:\n\t" "popfl\n\t" "popal\n\t" "jmp _th_gs_slow\n"
        "_th_5e20:\n\t"
        "pushal\n\t" "pushfl\n\t" "pushl 0x2c(%esp)\n\t" "pushl 0x2c(%esp)\n\t" "call _fast_5e20\n\t" "addl $8, %esp\n\t"
        "testl %eax, %eax\n\t" "jz 1f\n\t"
        "movl %eax, 0x20(%esp)\n\t" "popfl\n\t" "popal\n\t" "ret $8\n"
        "1:\n\t" "popfl\n\t" "popal\n\t" "jmp _th_5e20_slow\n");

// ---------------------------------------------------------------- crash note
// A crash inside the game ends in the game's BugSplat handler (TerminateProcess, nothing in the Windows log). A fatal exception on
// one of our workers - or on the main thread while it builds in the parallel phase - writes one line first, without the C runtime.
static volatile LONG s_exc;
static LONG CALLBACK veh(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION
        && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW && code != 0xC0000409u) return EXCEPTION_CONTINUE_SEARCH;
    Ctx *c = ctx_get();
    if (!c || (c->id == 0 && !g_phase)) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&s_exc) > 3) return EXCEPTION_CONTINUE_SEARCH;
    void *at = ep->ExceptionRecord->ExceptionAddress; HMODULE m = NULL; char mod[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)at, &m) && m) {
        GetModuleFileNameA(m, mod, sizeof mod); char *q = strrchr(mod, '\\'); if (q) memmove(mod, q + 1, strlen(q));
    }
    void *obj = c->cur; SYSTEMTIME t; GetLocalTime(&t); char b[512];
    int n = wsprintfA(b, "%02d:%02d:%02d EXCEPTION %08lX at %s+%lX (%p), data %p, on %s %d (tid %lu) building model %p - the game's crash "
                      "handler takes over\r\n", t.wHour, t.wMinute, t.wSecond, code, mod, (DWORD)((char *)at - (char *)m), at,
                      ep->ExceptionRecord->NumberParameters > 1 ? (void *)ep->ExceptionRecord->ExceptionInformation[1] : NULL,
                      c->id ? "worker" : "main thread", c->id, GetCurrentThreadId(), obj);
    HANDLE f = CreateFileW(g_log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, b, n, &w, NULL); CloseHandle(f); }
    return EXCEPTION_CONTINUE_SEARCH;
}

// ---------------------------------------------------------------- install
static int bytes_are(DWORD at, const BYTE *b, int n) { return !IsBadReadPtr((void *)at, n) && !memcmp((void *)at, b, n); }
static int call_to(DWORD at, DWORD target) { return *(BYTE *)at == 0xE8 && at + 5 + *(int *)(at + 1) == target; }
static int write_code(DWORD at, const BYTE *b, int n) {
    DWORD o; if (!VirtualProtect((void *)at, n, PAGE_EXECUTE_READWRITE, &o)) return 0;
    memcpy((void *)at, b, n); VirtualProtect((void *)at, n, o, &o); FlushInstructionCache(GetCurrentProcess(), (void *)at, n); return 1;
}
static int jmp_at(DWORD at, void *to, int len) {          // jmp/call rel32 + nops
    BYTE b[16]; b[0] = 0xE9; *(DWORD *)(b + 1) = (DWORD)to - (at + 5); for (int i = 5; i < len; i++) b[i] = 0x90; return write_code(at, b, len);
}
static int call_at(DWORD at, void *to, int len) {
    BYTE b[16]; b[0] = 0xE8; *(DWORD *)(b + 1) = (DWORD)to - (at + 5); for (int i = 5; i < len; i++) b[i] = 0x90; return write_code(at, b, len);
}

static DWORD WINAPI logger(LPVOID _) {
    (void)_; int poll = GetPrivateProfileIntW(L"ParallelDraw", L"Poll", 0, g_ini), k = 0;
    LONG p[20] = {0}; LONGLONG pq = 0, pw = 0, pwk = 0; LONG pnw = 0; ULONGLONG wprev[MAXT] = {0};
    for (;;) {
        Sleep(500);
        if (poll) {
            int m = GetPrivateProfileIntW(L"ParallelDraw", L"Mode", g_mode, g_ini);
            if (m != g_mode) { InterlockedExchange(&g_mode, m); logmsg("mode %d", m); }
        }
        if (!g_stats || ++k % 20) continue;
        LONG v[20] = {s_batches, s_items, s_cold, s_emits, s_pushes, s_rebuild, s_unconsumed, s_replayed, s_nest_rec, s_overflow,
                      s_wait[0], s_byworker, s_par_batches, s_skipdepth, s_wait[1], s_wait[2], s_wait[3], 0, 0, 0};
        LONG x[7] = {s_stalls, s_late, s_mx_other, s_mx_other_busy, s_mx_foreign, s_mx_foreign_phase, 0}; static LONG px[7];
        double wmax = s_wait_max_qpc * 1000.0 / g_qf.QuadPart; s_wait_max_qpc = 0;
        for (int t = 0; t < MAXT; t++) { v[17] += g_ctx[t].fast[0]; v[18] += g_ctx[t].fast[1]; }
        v[19] = s_mx_skipped;
        LONGLONG q = s_qpc_pre, w = s_qpc_wait, wk = s_qpc_walk; LONG nw = s_walks;
        char cpu[200]; int o = 0; cpu[0] = 0;
        for (int i = 0; i < g_nworkers && o < 180; i++) {
            FILETIME a, b, kt, ut; ULONGLONG t = 0;
            if (GetThreadTimes(g_worker_h[i], &a, &b, &kt, &ut)) t = ((ULONGLONG)kt.dwHighDateTime << 32 | kt.dwLowDateTime) + ((ULONGLONG)ut.dwHighDateTime << 32 | ut.dwLowDateTime);
            o += snprintf(cpu + o, sizeof cpu - o, " %.0f", (t - wprev[i]) / 1e4 / 10.0); wprev[i] = t;
        }
        LONG db = v[0] - p[0];
        logmsg("10 s: mode %ld, batches %ld (parallel %ld), models %ld (%.0f/batch), cold %ld, packets %ld, pushes %ld, rebuilt %ld, "
               "unconsumed %ld, replayed %ld, walks inside a build %ld, overflow %ld, lock waits sh/li/at/mat %ld/%ld/%ld/%ld, fast gs/5e20 %ld/%ld, "
               "built by workers %.0f %%, too deep %ld, light-lock enters skipped %ld | "
               "prebuild %.2f ms/batch (wait %.2f, longest %.2f) | main thread per 10 s: prebuild %.0f ms + slot 7 in the walk %.0f ms (%ld walks) | "
               "stalls %ld, late claims %ld, other game locks in the phase %ld (busy %ld), light lock by other threads %ld (in the phase %ld)%s | worker CPU %%:%s",
               (long)g_mode, db, v[12] - p[12], v[1] - p[1], db ? (double)(v[1] - p[1]) / db : 0, v[2] - p[2], v[3] - p[3], v[4] - p[4],
               v[5] - p[5], v[6] - p[6], v[7] - p[7], v[8] - p[8], v[9] - p[9], v[10] - p[10], v[14] - p[14], v[15] - p[15], v[16] - p[16], v[17] - p[17], v[18] - p[18],
               v[1] - p[1] ? 100.0 * (v[11] - p[11]) / (v[1] - p[1]) : 0, v[13] - p[13], v[19] - p[19],
               db ? (q - pq) * 1000.0 / g_qf.QuadPart / db : 0, db ? (w - pw) * 1000.0 / g_qf.QuadPart / db : 0, wmax,
               (q - pq) * 1000.0 / g_qf.QuadPart, (wk - pwk) * 1000.0 / g_qf.QuadPart, nw - pnw,
               x[0] - px[0], x[1] - px[1], x[2] - px[2], x[3] - px[3], x[4] - px[4], x[5] - px[5], g_par_off ? ", WORKERS OFF" : "", cpu);
        memcpy(px, x, sizeof px);
        memcpy(p, v, sizeof p); pq = q; pw = w; pwk = wk; pnw = nw;
    }
    return 0;
}

static void **find_iat(HMODULE m, const char *dll, const char *fn) {
    BYTE *b = (BYTE *)m; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(b + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; d->Name; d++) {
        if (lstrcmpiA((char *)(b + d->Name), dll)) continue;
        IMAGE_THUNK_DATA *nm = (IMAGE_THUNK_DATA *)(b + d->OriginalFirstThunk), *at = (IMAGE_THUNK_DATA *)(b + d->FirstThunk);
        for (; nm->u1.AddressOfData; nm++, at++)
            if (!(nm->u1.Ordinal & IMAGE_ORDINAL_FLAG) && !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(b + nm->u1.AddressOfData))->Name, fn)) return (void **)&at->u1.Function;
    }
    return NULL;
}

static int already_loaded(const char *name) {
    char n[96]; wsprintfA(n, "Local\\dow2patch_%s_%lu", name, GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, n);
    return m && GetLastError() == ERROR_ALREADY_EXISTS;
}

static void install(void) {
    static const BYTE RL[] = {0x53, 0x8B, 0x5C, 0x24, 0x08, 0x55, 0x8B, 0x6C, 0x24, 0x10};
    static const BYTE RLCALL[] = {0x8B, 0x0E, 0x8B, 0x54, 0x24, 0x1C, 0x8B, 0x01, 0x8B, 0x40, 0x1C, 0x57, 0x52, 0x55, 0xFF, 0xD0};
    static const BYTE PUSH[] = {0x8B, 0x75, 0x18, 0x85, 0xF6, 0x74, 0x27, 0x8B, 0x46, 0x04, 0x3B, 0x46, 0x08};
    static const BYTE DEVC[] = {0xFF, 0x15, 0x88, 0x8E, 0xF8, 0x00, 0x8B, 0x4B, 0x0C, 0x8B, 0x10, 0x8B, 0x92, 0xEC, 0x00, 0x00, 0x00};
    static const BYTE MISS[] = {0x6A, 0x1C, 0xE8, 0x1B, 0xBE, 0x6D, 0x00, 0x8B, 0xF8};
    static const BYTE LOOP[] = {0x8B, 0x73, 0x08, 0x3B, 0x73, 0x0C, 0x74, 0x28};
    static const BYTE MAT[] = {0x80, 0x7F, 0x0D, 0x00};                    // 0x6C28BB cmp byte [edi+0xd],0 (edi = p1+0xC)
    static const BYTE MATMISS[] = {0x0F, 0x85, 0x0A, 0x02, 0x00, 0x00, 0x33, 0xDB, 0x89, 0x5C, 0x24, 0x24, 0x89, 0x5C, 0x24, 0x28};
    static const BYTE GSFN[] = {0x8D, 0x59, 0x08, 0x80, 0x7B, 0x0D, 0x00, 0x74, 0x06};   // Spooge GetShader +0x13: dirty flag [dev+0x15]
    static const BYTE GS[] = {0xFF, 0x15, 0x44, 0x8F, 0xF8, 0x00};
    static const BYTE S7[] = {0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8B, 0x86, 0x14, 0x02, 0x00, 0x00};
    static const BYTE GROW[] = {0x83, 0xEC, 0x08, 0x53, 0x55, 0x57, 0x8B, 0xF8, 0x8B, 0x46, 0x04, 0x2B, 0x06};
    void **vt = (void **)VT_AV;
    BYTE *sp = (BYTE *)GetModuleHandleA("Spooge.dll");
    DWORD dvt = sp ? (DWORD)sp - SP_BASE + SP_VT_DX9 : 0, dfn = sp ? (DWORD)sp - SP_BASE + SP_DEV_EC : 0;
    static const BYTE DEVFN[] = {0x8B, 0x44, 0x24, 0x04, 0x56, 0x50, 0x8B, 0xF1, 0xE8};
    if (IsBadReadPtr(vt, 32) || vt[7] != (void *)FN_S7 || !bytes_are(FN_S7, S7, sizeof S7) || !bytes_are(FN_RL, RL, sizeof RL)
        || !bytes_are(0x0078B745, RLCALL, sizeof RLCALL) || !bytes_are(SITE_PUSH, PUSH, sizeof PUSH) || !bytes_are(0x006CEE64, DEVC, sizeof DEVC)
        || !bytes_are(SITE_MISS, MISS, sizeof MISS) || !call_to(SITE_3A0, FN_3A0) || !call_to(SITE_6C2, FN_6C2)
        || !bytes_are(SITE_6C2LOOP, LOOP, sizeof LOOP) || !bytes_are(0x006C28BB, MAT, sizeof MAT) || !bytes_are(0x006C2900, MATMISS, sizeof MATMISS) || !call_to(SITE_5E20, FN_5E20) || !bytes_are(SITE_GS, GS, sizeof GS)
        || !call_to(SITE_12E0, FN_12E0) || !call_to(SITE_C771, FN_C771) || !call_to(SITE_2FC0, FN_2FC0) || !bytes_are(FN_VGROW, GROW, sizeof GROW)
        || !sp || !bytes_are((DWORD)sp - SP_BASE + 0x10001C93u, GSFN, sizeof GSFN) || *(DWORD *)IAT_GS != (DWORD)sp - SP_BASE + 0x10001C80u
        || IsBadReadPtr((void *)(dvt + 0xEC), 4) || *(DWORD *)(dvt + 0xEC) != dfn || !bytes_are(dfn, DEVFN, sizeof DEVFN)) {
        logmsg("not DoW2 3.19.10320 (a checked byte differs) - nothing done"); return;
    }
    g_tls = TlsAlloc(); if (g_tls == TLS_OUT_OF_INDEXES) { logmsg("TlsAlloc failed - nothing done"); return; }
    for (int t = 0; t < MAXT; t++) g_ctx[t].id = t;
    SYSTEM_INFO si; GetSystemInfo(&si); int ncpu = si.dwNumberOfProcessors;
    g_threads = GetPrivateProfileIntW(L"ParallelDraw", L"Threads", 0, g_ini);
    if (g_threads <= 0) g_threads = ncpu - 1 < 7 ? ncpu - 1 : 7;
    if (g_threads > MAXT - 1) g_threads = MAXT - 1;
    for (int t = 0; t <= g_threads; t++)
        for (int d = 0; d < MAXD; d++) {
            g_ctx[t].arena[d] = VirtualAlloc(NULL, ARENA * sizeof(Emit), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!g_ctx[t].arena[d]) { logmsg("arena alloc failed - nothing done"); return; }
        }
    g_sem = CreateSemaphoreW(NULL, 0, 0x7fffffff, NULL);
    for (int w = 0; w < g_threads && g_sem; w++) {
        HANDLE h = CreateThread(NULL, 1 << 20, worker, &g_ctx[w + 1], 0, NULL);
        if (!h) break;
        g_worker_h[g_nworkers++] = h;
    }
    // shared-cache locks first (inactive until workers run), then recording, then the walk entry, then the slot
    int ok = call_at(SITE_5E20, th_5e20, 5) && call_at(SITE_GS, th_gs, 6) && call_at(SITE_C771, th_c771, 5)
          && call_at(SITE_2FC0, th_2fc0, 5) && call_at(SITE_6C2, th_6c2, 5) && jmp_at(SITE_6C2LOOP, th_6c2loop, 6) && jmp_at(0x006C2906, th_6c2miss, 6)
          && call_at(SITE_3A0, th_3a0, 5) && jmp_at(SITE_MISS, th_miss, 7) && jmp_at(SITE_PUSH, th_push, 7);
    if (!ok) { logmsg("code patch failed (partly installed patches are inactive without the walk hook)"); return; }
    DWORD old;
    o_dev = (DevFn)*(DWORD *)(dvt + 0xEC);
    if (!VirtualProtect((void *)(dvt + 0xEC), 4, PAGE_READWRITE, &old)) { logmsg("VirtualProtect Dx9Device vtable failed"); return; }
    InterlockedExchange((LONG *)(dvt + 0xEC), (LONG)hk_dev); VirtualProtect((void *)(dvt + 0xEC), 4, old, &old);
    {   // light-manager lock (see hk_mx_enter); without it the workers just wait on the game's lock
        HMODULE ex = GetModuleHandleA(NULL);
        void **ie = find_iat(ex, "Platform.dll", "?Enter@RecursiveMutex@Plat@@QAEXXZ"), **il = find_iat(ex, "Platform.dll", "?Leave@RecursiveMutex@Plat@@QAEXXZ");
        DWORD o1, o2;
        if (ie && il && VirtualProtect(ie, 4, PAGE_READWRITE, &o1) && VirtualProtect(il, 4, PAGE_READWRITE, &o2)) {
            o_mx_enter = (MxFn)*ie; o_mx_leave = (MxFn)*il;
            InterlockedExchange((LONG *)ie, (LONG)hk_mx_enter); InterlockedExchange((LONG *)il, (LONG)hk_mx_leave);
            VirtualProtect(ie, 4, o1, &o1); VirtualProtect(il, 4, o2, &o2);
        } else logmsg("RecursiveMutex imports not found - the light lock stays as it is");
    }
    if (!jmp_at(FN_RL, th_rl, 5)) { logmsg("walk hook failed"); return; }
    if (!VirtualProtect(&vt[7], 4, PAGE_EXECUTE_READWRITE, &old)) { logmsg("VirtualProtect AV vtable failed"); return; }
    InterlockedExchange((LONG *)&vt[7], (LONG)hk_s7); VirtualProtect(&vt[7], 4, old, &old);
    AddVectoredExceptionHandler(1, veh);
    logmsg("ParallelDraw 2: AnimatorVisual slot 7 (0x7525D0) prebuilt at the render-list walk (0x78B730) by the main "
           "thread + %d workers (%d CPUs), packets replayed in order; mode %ld, watchdog %d ms", g_nworkers, ncpu, (long)g_mode, g_wd_ms);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_; if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *sl = wcsrchr(g_dir, L'\\'); if (sl) sl[1] = 0; else g_dir[0] = 0;
    wcscpy(g_ini, g_dir); wcscat(g_ini, L"ParallelDraw.ini"); wcscpy(g_log, g_dir); wcscat(g_log, L"ParallelDraw.log");
    QueryPerformanceFrequency(&g_qf);
    if (already_loaded("ParallelDraw")) { logmsg("another ParallelDraw copy is already active in this game, this one stays idle"); return TRUE; }
    if (!GetPrivateProfileIntW(L"ParallelDraw", L"Enabled", 1, g_ini)) { logmsg("disabled in ParallelDraw.ini"); return TRUE; }
    g_mode = GetPrivateProfileIntW(L"ParallelDraw", L"Mode", 1, g_ini);
    g_stats = GetPrivateProfileIntW(L"ParallelDraw", L"Stats", 0, g_ini);
    g_wd_ms = GetPrivateProfileIntW(L"ParallelDraw", L"Watchdog", 100, g_ini);
    g_off_ms = GetPrivateProfileIntW(L"ParallelDraw", L"StallOff", 2000, g_ini);
    g_racetest = GetPrivateProfileIntW(L"ParallelDraw", L"RaceTest", 0, g_ini);
    install();
    TlsSetValue(g_tls, NULL);
    CloseHandle(CreateThread(NULL, 0, logger, NULL, 0, NULL));
    return TRUE;
}
