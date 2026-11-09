// x87ref.exe: the exact instruction sequences of Windows DoW2 (Util GetUnitInclusive + SimEngine GetRandRange) at PC24
#include <stdio.h>
#include <float.h>
static const double C1 = 2.3283064370807974e-10, TWO32 = 4294967296.0;
int main(void) {
    unsigned cw; _controlfp_s(&cw, _PC_24, _MCW_PC);
    unsigned h; double lo, hi;
    while (scanf("%u %la %la", &h, &lo, &hi) == 3) {
        float r, u; int xi = (int)h;
        float *pu = &u, *pr = &r; const double *pc = &C1, *pt = &TWO32, *phi = &hi, *plo = &lo; int *pxi = &xi;
        __asm__ volatile(
            ".intel_syntax noprefix\n\t"
            "fild dword ptr [%[pxi]]\n\t"
            "cmp dword ptr [%[pxi]], 0\n\t"
            "jge 1f\n\t"
            "fadd qword ptr [%[pt]]\n"
            "1:\n\t"
            "fmul qword ptr [%[pc]]\n\t"
            "fst dword ptr [%[pu]]\n\t"
            "fld qword ptr [%[phi]]\n\t"
            "fld qword ptr [%[plo]]\n\t"
            "fsub st(1), st\n\t"
            "fxch st(2)\n\t"
            "fmulp st(1), st\n\t"
            "faddp st(1), st\n\t"
            "fstp dword ptr [%[pr]]\n\t"
            ".att_syntax prefix\n\t"
            : : [pxi] "r"(pxi), [pt] "r"(pt), [pc] "r"(pc), [pu] "r"(pu), [phi] "r"(phi), [plo] "r"(plo), [pr] "r"(pr) : "memory", "cc");
        unsigned rb, ub; __builtin_memcpy(&rb, &r, 4); __builtin_memcpy(&ub, &u, 4); printf("%08x %08x\n", rb, ub);
    }
    return 0;
}
