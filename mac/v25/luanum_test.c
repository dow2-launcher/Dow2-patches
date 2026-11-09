// luanum_test.c - feeds strings to luanum_str2number and prints the bits in luaref.exe's format (compare the two outputs)
#include <stdio.h>
#include "luanum_impl.h"
#include "dylib_map.h"
typedef double (*s2n_fn)(const char *, char **);
int main(int argc, char **argv) {   // argv: [dylib luanum_str2number-off ldexp-stub-off] = check the shipped code
    s2n_fn fn = luanum_str2number; char s[256];
    if (argc > 3) { char *d = dylib_map(argv[1]); dylib_stub(d, strtoul(argv[3], 0, 16), (void *)ldexp); fn = (s2n_fn)(d + strtoul(argv[2], 0, 16)); }
    while (scanf("%255s", s) == 1) {
        char *end = 0; double v = fn(s, &end); unsigned long long b; memcpy(&b, &v, 8);
        printf("%s %016llx %d\n", s, b, (int)(end - s));
    }
    return 0;
}
