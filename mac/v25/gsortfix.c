// gsortfix.c - crossplay_fix v20, part 22: more simulation sorts ordered like DOW2.exe (STLport std::sort), generic.
// Why (difftest step 5, 08.11.2026): the sort audit (macrun MR_SORTAUDIT: every sort call of the Mac simulation through a stub that
// re-sorts the same input with STLport's algorithm, v20/sortaudit) on the 8 replays found Mac call sites whose result differs
// from STLport std::sort's when keys tie, and the Windows pairing run (DiffTrace.dll DT_SORTS: every call site of a DOW2.exe
// STLport std::sort entry, matched to the Mac sites by per-frame call counts) showed that DOW2.exe sorts there with std::sort.
// The port calls libc++ std::stable_sort (or libc++ std::sort) there -> another order of equal elements -> another slot / cover
// spot / target. Same class as sortfix.c (FindWeaponTarget), see GS_SITES below for the list.
// What: each listed call (e8 rel32 to the port's sort instantiation) goes to a stub on a page near the game code ("mov r11, i;
// jmp [rip]") -> gsf_entry -> gsf_c: STLport std::sort over the elements (gsort_impl.h, element size from the site table or from
// stable_sort's len argument). The comparator is the site's own: the port's instantiation called on a 2-element scratch array
// [b, a] swaps iff comp(a, b) (libc++ __sort / __stable_sort, len 2: "if (comp(*--last, *first)) swap"), so the comparison
// code is exactly the port's. Bytes checked first; self-test at load: the generic algorithm on sortfix's 48 DOW2.exe lists (a
// C comparator) and, per site, the oracle on a synthetic pair is not possible (game comparators read the entities), so only the
// call bytes and target are checked. DOW2_NOGSORTFIX=1: off.
#include "patch_util.h"
#include "gsort_impl.h"
#include "sortfix_st.inc"
typedef struct { uint64_t site, target; char kind; int size; const char *what; } gs_site;
#include "gsortfix_sites.h"          // static const gs_site GS_SITES[] = { ... }; (generated from the audit + pairing)
#define GS_N (sizeof GS_SITES / sizeof *GS_SITES)
_Static_assert(GS_N <= 64, "gsortfix: at most 64 sites");
static uintptr_t gs_slide;
static unsigned long gs_calls[64];
typedef struct { const gs_site *s; uint64_t fn; long comp; char *x; size_t sz; } gs_ctx;
static int gsf_less(const void *a, const void *b, void *c_) {
    gs_ctx *c = c_; char *x = c->x; size_t S = c->sz;
    memcpy(x, b, S); memcpy(x + S, a, S);
    if (c->s->kind == 't') ((void (*)(void *, void *, long, long, void *, long))c->fn)(x, x + 2 * S, c->comp, 2, 0, 0);
    else ((void (*)(void *, void *, long))c->fn)(x, x + 2 * S, c->comp);
    return memcmp(x, b, S) != 0;                                   // moved = swapped = comp(a, b)
}
static unsigned char gs_off[64]; static unsigned short gs_checked[64];
static void gsf_orig(const gs_site *s, long *a) { ((void (*)(long, long, long, long, long, long))(s->target + gs_slide))(a[0], a[1], a[2], a[3], a[4], a[5]); }
void gsf_c(long i, long *args) {                                  // args: rdi rsi rdx rcx r8 r9 of the original call
    const gs_site *s = &GS_SITES[i]; char *first = (char *)args[0], *last = (char *)args[1];
    long S = s->size;
    if (gs_off[i]) { gsf_orig(s, args); return; }
    if (s->kind == 't') { long len = args[3]; if (len < 2) return; S = (last - first) / len; }
    if (S <= 0 || S > GS_MAXEL || last - first < 2 * S) { gsf_orig(s, args); return; }
    gs_calls[i]++;
    char x[2 * GS_MAXEL];
    gs_ctx c = { s, s->target + gs_slide, args[2], x, (size_t)S }; gs_t g = { (size_t)S, gsf_less, &c };
    if (gs_checked[i] < 64) {                                     // first calls: the port's own sort too; same tie classes or the site is switched off
        long n = (last - first) / S; char *w = malloc((size_t)(last - first)); if (!w) { gsf_orig(s, args); return; }
        memcpy(w, first, (size_t)(last - first)); gs_sort(&g, w, w + n * S);
        gsf_orig(s, args);
        for (long j = 0; j < n; j++) {
            char *p = first + j * S, *q = w + j * S;
            if (gsf_less(p, q, &c) || gsf_less(q, p, &c) || (j + 1 < n && gsf_less(q + S, q, &c))) {
                gs_off[i] = 1; free(w);
                pu_log("gsortfix: site %#lx check failed - this site keeps the port's sort (%lu)", (unsigned long)s->site, (unsigned long)n); return;
            }
        }
        memcpy(first, w, (size_t)(n * S)); free(w); gs_checked[i]++; return;
    }
    gs_sort(&g, first, last);
}
__asm__(".text\n.globl _gsf_entry\n_gsf_entry:\n push %rbp\n mov %rsp,%rbp\n push %r9\n push %r8\n push %rcx\n push %rdx\n push %rsi\n push %rdi\n"
        " mov %r11,%rdi\n mov %rsp,%rsi\n call _gsf_c\n add $48,%rsp\n pop %rbp\n ret\n");
void gsf_entry(void);
// generic algorithm vs DOW2.exe 0x98f0a0's orders (sortfix_st.inc, FindWeaponTarget lists: n, keys, ids in, ids out)
typedef struct { float k; uint32_t id; } gs_t8;
static int gs_t8_gt(const void *a, const void *b, void *c) { (void)c; return ((const gs_t8 *)a)->k > ((const gs_t8 *)b)->k; }
static unsigned long gs_selftest(void) {
    static gs_t8 a[64]; unsigned long bad = 0; const uint32_t *q = SO_ST; gs_t g = { sizeof(gs_t8), gs_t8_gt, 0 };
    for (unsigned long c = 0; c < SO_NST; c++) {
        long n = (long)*q++;
        for (long i = 0; i < n; i++) { memcpy(&a[i].k, q + i, 4); a[i].id = q[n + i]; }
        gs_sort(&g, a, a + n);
        for (long i = 0; i < n; i++) if (a[i].id != q[2 * n + i]) { bad++; break; }
        q += 3 * n;
    }
    return bad;
}
unsigned long gsortfix_calls(int i) { return i >= 0 && i < 64 ? gs_calls[i] | (unsigned long)gs_off[i] << 63 : 0; }
__attribute__((constructor)) static void gsortfix_init(void) {
    if (getenv("DOW2_NOGSORTFIX")) { pu_log("gsortfix: DOW2_NOGSORTFIX set - the port's own sorts%.0lu%.0lu", 0, 0); return; }
    gs_slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned long bad = gs_selftest();
    if (bad) { pu_log("gsortfix self-test: %lu of %lu lists differ from DOW2.exe - NOT patched", bad, SO_NST); return; }
    vm_address_t near = 0;
    for (vm_address_t w = 0x104300000UL + gs_slide; w < 0x140000000UL + gs_slide; w += 0x100000) {
        near = w;
        if (vm_allocate(mach_task_self(), &near, vm_page_size, 0) == KERN_SUCCESS) break;
        near = 0;
    }
    if (!near) { pu_log("gsortfix: no page near the game code - NOT patched%.0lu%.0lu", 0, 0); return; }
    unsigned char *pg = (unsigned char *)near; unsigned long ok = 0, rlo = 0, rhi = GS_N;
    if (getenv("DOW2_GSORTFIX_RANGE")) sscanf(getenv("DOW2_GSORTFIX_RANGE"), "%lu:%lu", &rlo, &rhi);   // bisecting aid
    for (unsigned long i = 0; i < GS_N && 32 * (i + 1) <= vm_page_size; i++) {
        if (i < rlo || i >= rhi) continue;
        unsigned char *c = (unsigned char *)(GS_SITES[i].site + gs_slide), want[5] = { 0xe8 };
        int32_t rel = (int32_t)(GS_SITES[i].target - (GS_SITES[i].site + 5)); memcpy(want + 1, &rel, 4);
        if (memcmp(c, want, 5)) { pu_log("gsortfix: unexpected bytes at %#lx - this site NOT patched%.0lu", (unsigned long)GS_SITES[i].site, 0); continue; }
        unsigned char *st = pg + 32 * i; int32_t ii = (int32_t)i; void *e = (void *)gsf_entry;
        st[0] = 0x49; st[1] = 0xc7; st[2] = 0xc3; memcpy(st + 3, &ii, 4); st[7] = 0xff; st[8] = 0x25; memset(st + 9, 0, 4); memcpy(st + 13, &e, 8);
        long long d = (long long)(uintptr_t)st - (long long)(uintptr_t)(c + 5);
        if (d > 0x7fffffffLL || d < -0x80000000LL) continue;
        unsigned char put[5] = { 0xe8 }; int32_t r2 = (int32_t)d; memcpy(put + 1, &r2, 4);
        vm_address_t page = (vm_address_t)c & ~(vm_address_t)(vm_page_size - 1);
        if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) continue;
        memcpy(c, put, 5);
        vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
        ok++;
    }
    vm_protect(mach_task_self(), near, vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    pu_log("gsortfix: OK - %lu of %lu simulation sort calls sorted like DOW2.exe (STLport std::sort, comparator = the port's own), self-test 48 DOW2.exe lists OK", ok, GS_N);
}
