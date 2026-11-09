// rngtrace - sync-error diagnostic for Mac <-> Windows games (added to crossplay_fix.dylib when built with -DRNGTRACE).
// Every call of the sim random generator (Random::hashRandom, 0x1016c84d0 in the Feral 1.0.2 binary; the same integer
// hash as Util.dll on Windows) is logged with the generator's seed and index and the chain of return addresses, so the
// Mac log can be lined up with the Windows one by (seed, index) and the first call only one side makes points at the
// game code that diverges. Log: /tmp/dow2_rng.log, one line per call: seed index ret caller1 caller2 caller3 caller4
// (addresses are static, slide removed).
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define HASHRANDOM 0x1016c84d0UL
static const unsigned char PROLOGUE[8] = {0x55, 0x48, 0x89, 0xE5, 0x53, 0x44, 0x8B, 0x1F};   // push rbp; mov rbp,rsp; push rbx; mov r11d,[rdi]

static FILE *rf;
static uintptr_t g_slide;
static unsigned long g_n;
static pthread_mutex_t rm = PTHREAD_MUTEX_INITIALIZER;

// saved: the registers the stub pushed, saved[9] = hashRandom's return address; rbp = the caller's frame
__attribute__((used)) static void rng_log(const uint32_t *obj, const uint64_t *saved, const uint64_t *rbp) {
    uint64_t c[4] = {0, 0, 0, 0};
    char *lo = (char *)pthread_get_stackaddr_np(pthread_self()) - pthread_get_stacksize_np(pthread_self());
    char *hi = (char *)pthread_get_stackaddr_np(pthread_self());
    const uint64_t *fp = rbp;
    for (int i = 0; i < 4; i++) {
        if ((char *)fp < lo || (char *)fp + 16 > hi || ((uintptr_t)fp & 7)) break;
        c[i] = fp[1] ? fp[1] - g_slide : 0;
        const uint64_t *n = (const uint64_t *)fp[0];
        if (n <= fp) break;
        fp = n;
    }
    pthread_mutex_lock(&rm);
    if (rf) {
        fprintf(rf, "%08x %u %lx %lx %lx %lx %lx\n", obj[0], obj[1], (unsigned long)(saved[9] - g_slide),
                (unsigned long)c[0], (unsigned long)c[1], (unsigned long)c[2], (unsigned long)c[3]);
        if (++g_n % 256 == 0) fflush(rf);
    }
    pthread_mutex_unlock(&rm);
}

// jumped to from hashRandom's first 5 bytes; logs, replays the 5 overwritten bytes and jumps back to hashRandom+5
extern char rng_stub[], rng_stub_log[], rng_stub_back[], rng_stub_end[];
__asm__(
    ".globl _rng_stub\n_rng_stub:\n"
    "  push %rdi\n push %rsi\n push %rdx\n push %rcx\n push %r8\n push %r9\n push %r10\n push %r11\n push %rax\n"
    "  mov %rsp, %rsi\n mov %rbp, %rdx\n"           // rdi = Random*, rsi = saved regs, rdx = caller frame
    "  call *_rng_stub_log(%rip)\n"
    "  pop %rax\n pop %r11\n pop %r10\n pop %r9\n pop %r8\n pop %rcx\n pop %rdx\n pop %rsi\n pop %rdi\n"
    "  push %rbp\n mov %rsp, %rbp\n push %rbx\n"
    "  jmp *0(%rip)\n"
    ".globl _rng_stub_back\n_rng_stub_back:\n .quad 0\n"
    ".globl _rng_stub_log\n_rng_stub_log:\n .quad 0\n"
    ".globl _rng_stub_end\n_rng_stub_end:\n");

void rngtrace_init(void (*logf)(const char *, unsigned long, unsigned long)) {
    g_slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(HASHRANDOM + g_slide);
    if (memcmp(p, PROLOGUE, sizeof PROLOGUE)) { logf("rngtrace: unexpected bytes at %#lx (slide %#lx), off", (unsigned long)p, g_slide); return; }
    // the stub must sit within +-2 GB of the game code for a 5-byte jmp: take a page right after the game image
    size_t sz = rng_stub_end - rng_stub;
    vm_address_t at = 0;
    for (vm_address_t want = 0x104000000UL + g_slide; want < 0x140000000UL + g_slide; want += 0x100000) {
        at = want;
        if (vm_allocate(mach_task_self(), &at, vm_page_size, 0) == KERN_SUCCESS) break;
        at = 0;
    }
    long long d = at ? (long long)at - (long long)(p + 5) : 0;
    if (!at || d > 0x7fffffffLL || d < -0x80000000LL) { logf("rngtrace: no page near the game code (%#lx), off%lu", at, 0); return; }
    memcpy((void *)at, rng_stub, sz);
    uint64_t back = (uint64_t)(p + 5);
    memcpy((char *)at + (rng_stub_back - rng_stub), &back, 8);
    uint64_t lf = (uint64_t)rng_log;
    memcpy((char *)at + (rng_stub_log - rng_stub), &lf, 8);
    vm_protect(mach_task_self(), at, vm_page_size, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    rf = fopen("/tmp/dow2_rng.log", "w");
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
        logf("rngtrace: vm_protect failed at %#lx%lu", (unsigned long)p, 0); return; }
    unsigned char j[5] = {0xE9}; int32_t rel = (int32_t)d; memcpy(j + 1, &rel, 4);
    memcpy(p, j, 5);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    logf("rngtrace: on, hashRandom %#lx -> stub %#lx, log /tmp/dow2_rng.log", (unsigned long)p, (unsigned long)at);
}
