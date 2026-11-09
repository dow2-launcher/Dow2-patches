// dylib_map.h - maps the __TEXT of the built crossplay_fix.dylib (file offset == vmaddr, 0x31000 bytes) into this Linux test
// so the shipped machine code itself is checked (offsets from nm). Calls to libSystem stubs are pointed at Linux libm.
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <math.h>
static char *dylib_map(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
    char *m = mmap(0, 0x31000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (fread(m, 1, 0x31000, f) != 0x31000) { perror("read dylib"); exit(2); }
    fclose(f); return m;
}
static void dylib_stub(char *m, unsigned long off, void *to) {    // overwrite a __stubs entry with jmp qword [rip+0]; .quad to
    unsigned char c[14] = { 0xFF, 0x25 }; memcpy(c + 6, &to, 8); memcpy(m + off, c, 14);
}
