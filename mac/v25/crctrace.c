// crctrace.c - crossplay_fix v12, sync-error diagnostic (built with -DRNGTRACE; pair of CrcTrace.dll on the Windows host).
// Why: in match_1102_v11 the network checksum failed at frame 573 while both sides' dumped CRC blocks of frame 573 were
// identical: the failing block was an earlier one, and the game keeps only the newest block (ReleaseCRCBlock(frame - 1),
// plus releases of frames the relay has confirmed).
// What, in the game (CRCBlockManager, Mac 0x1021553c0 / 0x102155500 / 0x102155600):
//  1. every CRC block the game creates is logged to /tmp/dow2_crc.log, one line per frame: "key crc c@256 c@512 ... n<entries>
//     c@last" - the total and the running CRC after every 256th entry (the same format as CrcTrace.dll's C:\crctrace.log), so
//     the two logs show the first frame and the first 256-entry chunk that differ;
//  2. the newest KEEP blocks are kept (ReleaseCRCBlock ignores newer ones; older ones are released here);
//  3. DumpCRCBlock (called by the game on a sync error) also writes the kept older blocks as SyncError_Prev_<key>.
// DOW2_NOCRCTRACE=1: off.
#include "patch_util.h"
#include <sys/mman.h>
#include <limits.h>

#define KEEP 24
static const uint64_t CREATE_TAIL = 0x1021554d4ULL, RELEASE = 0x102155600ULL, DUMP = 0x102155500ULL, PRINT = 0x1016bb0c0ULL;
static const unsigned char WANT_CREATE[17] = { 0x41,0x8b,0x46,0x10, 0x48,0x83,0xc4,0x10, 0x5b, 0x41,0x5c, 0x41,0x5e, 0x41,0x5f, 0x5d, 0xc3 };
static const unsigned char WANT_RELEASE[17] = { 0x55, 0x48,0x89,0xe5, 0x41,0x57, 0x41,0x56, 0x53, 0x50, 0x49,0x89,0xfe, 0x49,0x8b,0x56,0x08 };
static const unsigned char WANT_DUMP[14] = { 0x55, 0x48,0x89,0xe5, 0x41,0x57, 0x41,0x56, 0x41,0x54, 0x53, 0x49,0x89,0xd6 };

static FILE *cf;
static unsigned long nframes;
static uintptr_t g_slide;
__attribute__((used)) int32_t crctrace_keep_min = INT_MAX;     // keys above this are kept (the game's releases are ignored)
__attribute__((used)) uint64_t crctrace_tramp;                  // the original first 17 bytes of ReleaseCRCBlock + jmp back

__attribute__((used)) void crctrace_frame(void *mgr, int32_t key, uint64_t *block) {
    if (cf) {
        fprintf(cf, "%d %08x", key, *(uint32_t *)((char *)block + 0x10));
        unsigned long n = 0; uint32_t last = 0;
        for (char *e = (char *)block[0]; e; e = *(char **)(e + 0x10)) {
            n++; last = *(uint32_t *)(e + 0x20);
            if ((n & 255) == 0) fprintf(cf, " %08x", last);
        }
        fprintf(cf, " n%lu %08x\n", n, last);
        if (++nframes % 16 == 0) fflush(cf);
    }
    crctrace_keep_min = key - KEEP;
    ((void (*)(void *, int32_t))crctrace_tramp)(mgr, key - KEEP - 1);
}

static uint64_t *find_block(void *mgr, int32_t key) {
    char *header = (char *)mgr + 8, *node = *(char **)header, *cand = header;
    while (node) { if (*(int32_t *)(node + 0x20) >= key) { cand = node; node = *(char **)node; } else node = *(char **)(node + 8); }
    if (cand == header || *(int32_t *)(cand + 0x20) != key) return 0;
    return *(uint64_t **)(cand + 0x28);
}
static int dumped_prev;
static void crctrace_dump(void *mgr, int32_t key, const char *name) {
    void (*print)(void *, const char *) = (void (*)(void *, const char *))(PRINT + g_slide);
    uint64_t *b = find_block(mgr, key);
    pu_log(b ? "crctrace: dump %lu found%.0lu" : "crctrace: dump %lu NOT found%.0lu", (unsigned long)key, 0);
    if (b) print(b, name);
    if (!dumped_prev) {
        dumped_prev = 1;
        for (int32_t k = key - 1; k >= key - KEEP - 8; k--) {
            uint64_t *p = find_block(mgr, k); if (!p) continue;
            char nm[64]; snprintf(nm, sizeof nm, "SyncError_Prev_%d", k); print(p, nm);
        }
    }
    if (cf) fflush(cf);
}

// create: jumped to from 0x1021554d4 (rsp 16-aligned; r15 = manager, ebx = key, r14 = block); does the original epilogue
// release: in place of ReleaseCRCBlock's entry - a kept key is ignored, else the original code runs
#ifdef __APPLE__
#define CT_SYM(x) "_" #x
#else
#define CT_SYM(x) #x
#endif
void crctrace_create_stub(void), crctrace_release_stub(void);
__asm__(".text\n"
    ".globl " CT_SYM(crctrace_create_stub) "\n" CT_SYM(crctrace_create_stub) ":\n"
    "  mov %r15, %rdi\n mov %ebx, %esi\n mov %r14, %rdx\n call " CT_SYM(crctrace_frame) "\n"
    "  mov 0x10(%r14), %eax\n add $0x10, %rsp\n pop %rbx\n pop %r12\n pop %r14\n pop %r15\n pop %rbp\n ret\n"
    ".globl " CT_SYM(crctrace_release_stub) "\n" CT_SYM(crctrace_release_stub) ":\n"
    "  cmp " CT_SYM(crctrace_keep_min) "(%rip), %esi\n jg 1f\n jmpq *" CT_SYM(crctrace_tramp) "(%rip)\n"
    "1: ret\n");

static int write_code(unsigned char *p, const void *b, int len) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, len);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
static void jmp14(unsigned char *at, void *to, int len) {
    unsigned char j[32]; memset(j, 0x90, sizeof j); j[0] = 0xFF; j[1] = 0x25; memset(j + 2, 0, 4); memcpy(j + 6, &to, 8);
    write_code(at, j, len);
}

void crctrace_init(void) {
    if (getenv("DOW2_NOCRCTRACE")) { pu_log("crctrace: DOW2_NOCRCTRACE set - off%.0lu%.0lu", 0, 0); return; }
    g_slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *c = (unsigned char *)(CREATE_TAIL + g_slide), *r = (unsigned char *)(RELEASE + g_slide), *d = (unsigned char *)(DUMP + g_slide);
    if (memcmp(c, WANT_CREATE, 17) || memcmp(r, WANT_RELEASE, 17) || memcmp(d, WANT_DUMP, 14)) {
        pu_log("crctrace: unexpected bytes (another game build?) - off%.0lu%.0lu", 0, 0); return; }
    unsigned char *t = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (t == MAP_FAILED) { pu_log("crctrace: no memory%.0lu%.0lu", 0, 0); return; }
    memcpy(t, r, 17); t[17] = 0xFF; t[18] = 0x25; memset(t + 19, 0, 4); uint64_t back = (uint64_t)(r + 17); memcpy(t + 23, &back, 8);
    mprotect(t, 4096, PROT_READ | PROT_EXEC);
    crctrace_tramp = (uint64_t)t;
    cf = fopen("/tmp/dow2_crc.log", "w");
    jmp14(c, (void *)crctrace_create_stub, 17);
    jmp14(r, (void *)crctrace_release_stub, 14);
    jmp14(d, (void *)crctrace_dump, 14);
    pu_log("crctrace: on - keep %lu CRC blocks, log /tmp/dow2_crc.log%.0lu", (unsigned long)KEEP, 0);
}
