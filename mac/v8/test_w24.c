#include <stdio.h>
#include <stdlib.h>
#include "win24.h"
int main(int c, char **v) {      // reads lines "h lo hi" (lo, hi as %la), prints the float bits of rand_range and unit
    unsigned h; double lo, hi;
    while (scanf("%u %la %la", &h, &lo, &hi) == 3) {
        float r = w24_rand_range(h, lo, hi), u = w24_unit_inclusive(h);
        unsigned rb, ub; __builtin_memcpy(&rb, &r, 4); __builtin_memcpy(&ub, &u, 4); printf("%08x %08x\n", rb, ub);
    }
}
