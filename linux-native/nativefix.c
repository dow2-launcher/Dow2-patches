// LD_PRELOAD for the native (Feral) DoW2 with the Elite mod: an Elite model overruns a fixed 64-byte material buffer in Feral's
// renderer (memcpy with _FORTIFY_SOURCE -> abort). Clamp such copies to the destination size instead of aborting, log each spot once.
#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdint.h>
void *__memcpy_chk(void *dst, const void *src, size_t len, size_t dstlen) {
    if (len > dstlen) {
        static void *seen[64]; static int n; void *ra = __builtin_return_address(0); int k;
        for (k = 0; k < n && seen[k] != ra; k++);
        if (k == n && n < 64) { seen[n++] = ra; FILE *f = fopen("/mnt/dow2bench/dev/native_run/nativefix.log", "a");
            if (f) { fprintf(f, "memcpy_chk clamp %zu -> %zu at %p\n", len, dstlen, ra); fclose(f); } }
        len = dstlen;
    }
    return memcpy(dst, src, len);
}

// Native ports always advertise modDllCRC = 0 (no SimEngine.dll); Windows advertises the CRC of its SimEngine.dll (0xBB60C7FC),
// so a native player and a Windows player never pass the join check (and Windows replays). GameApp::UpdateVersionInfo
// (Linux 1.0.2 binary): "mov dword [rsp+0x58], 0" at 0xdb2a4a and 0xdb2a8b -> the Windows value. Bytes are checked first.
#include <sys/mman.h>
#include <unistd.h>
static void patch_imm(unsigned long at, unsigned int v) {
    static const unsigned char want[8] = {0xC7, 0x44, 0x24, 0x58, 0, 0, 0, 0};
    unsigned char *p = (unsigned char *)at; FILE *f = fopen("/mnt/dow2bench/dev/native_run/nativefix.log", "a");
    if (memcmp(p, want, 8)) { if (f) { fprintf(f, "modDllCRC: unexpected bytes at %#lx, not patched\n", at); fclose(f); } return; }
    long pg = sysconf(_SC_PAGESIZE); void *base = (void *)(at & ~(pg - 1));
    if (mprotect(base, pg * 2, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) { memcpy(p + 4, &v, 4); mprotect(base, pg * 2, PROT_READ | PROT_EXEC);
        if (f) fprintf(f, "modDllCRC: %#lx -> %#x\n", at, v); }
    if (f) fclose(f);
}
__attribute__((constructor)) static void nativefix_init(void) {
    char exe[256] = ""; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1); if (n > 0) exe[n] = 0;
    if (!strstr(exe, "DawnOfWar2Retribution")) return;          // only the game, not its helper processes
    patch_imm(0xdb2a4a, 0xBB60C7FCu); patch_imm(0xdb2a8b, 0xBB60C7FCu);
}
