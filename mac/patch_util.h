// patch_util.h - shared by the v9 parts: log line to /tmp/crossplay_fix.log and "jmp qword [rip+0]; .quad to" over a function entry
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static void pu_log(const char *s, unsigned long a, unsigned long b) {
    FILE *f = fopen("/tmp/crossplay_fix.log", "a"); if (f) { fprintf(f, s, a, b); fputc('\n', f); fclose(f); }
}
static int pu_jump(unsigned char *p, void *to) {
    unsigned char code[14] = { 0xFF, 0x25, 0, 0, 0, 0 };
    uint64_t t = (uint64_t)to; memcpy(code + 6, &t, 8);
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, code, sizeof code);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    return 1;
}
