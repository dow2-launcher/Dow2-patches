// meleefix_impl.h - the three melee/fire-assessment computations as DOW2.exe does them (shared by meleefix.c and t16/m16.c).
// Each one is a transcription of the Windows instructions (the order and the rounding of every operation), checked against
// DOW2.exe's own code run side by side (t16/w16.c, the real msvcr80 _CIpow included) on 1M cases each.
#include <stdint.h>
#include <string.h>
#include "win24.h"
double ms_pow(double, double);

// 1) special attack chance per attacker - DOW2.exe 0xa2815b (in 0xa27f80, x87 at 24-bit precision):
//      fld chance; fmul 0.01f; fld1; fsubrp; fstp base      (base = 1 - chance * 0.01, float)
//      1 / n (fild, fld1, fdivrp)                           (y, float)
//      call _CIpow                                         (msvcr80: SSE2 pow on doubles, the DOUBLE result left in st0)
//      fld1; fsubrp; fmul 100.0f; fstp                     (1 - p rounded once to 24 bits, then * 100)
//    The port (0x101c8f655) calls powf - the pow result rounded to a float first - then 1 - p and * 100 in float: up to 8 ulp
//    apart on the result (match 1107: Ork boyz, chance 25 over 6 attackers, 4.681569099 on the Mac, 4.681571007 on Windows).
//    base and y come in as the port computes them (same values: x * 0.01f since sumfix, 1.0f / (float)n).
static float mf_chance(float base, float y) {
    double p = ms_pow((double)base, (double)y);
    if (p != p) return (float)p;                          // NaN goes through 1 - p and * 100 unchanged (sign and payload)
    float r = w24_add(1.0, -p);                           // the exact 1 - p, one rounding to a 24-bit mantissa
    return (float)((double)r * 100.0);                    // 24 x 7 bits: exact in a double, one rounding
}

// 2) accuracy multiplier ^ (n - 1) - DOW2.exe 0xa298c0 (MSVC's std::pow(float, int) = _Pow_int, squaring in float):
//    the port (0x101c8efcf) does (float)pow((double)x, (double)(n - 1)) - one rounding of the exact power instead of one per
//    multiplication: differs from n - 1 = 3 on (x^3 22%, x^4.. about half).
static float mf_powi(float x, int32_t n) {
    uint32_t e = (uint32_t)n; if (n < 0) e = 0u - e;      // mov eax, ecx; jge; neg eax
    float r = 1.0f;
    for (;;) {
        if (e & 1) r = r * x;                             // test al, 1; mulss xmm0, xmm1
        e >>= 1; if (!e) break;                           // shr eax, 1; je
        x = x * x;                                        // mulss xmm3(=x), xmm1
    }
    if (n < 0) r = 1.0f / r;                              // divss xmm2(=1), xmm0
    return r;
}

// 3) sync kill: the attacker / victim transform = local (from the sync-kill quaternion + offset) * the entity's transform -
//    DOW2.exe 0x97bfa8 (in 0x97bf30); MSVC adds the first row's terms as (x + z) + y, the rest (x + y) + z, the port's
//    vectorized code (0x101bc9bdb) its own way. L, E, out: Matrix43f (3 rows, then the translation), out may be L.
static void mf_synckill(float *out, const float *Lin, const float *E) {
    float L[12]; memcpy(L, Lin, sizeof L); float o[12];
    for (int j = 0; j < 3; j++) {
        o[j]     = (E[j] * L[0] + E[6 + j] * L[2]) + E[3 + j] * L[1];
        o[3 + j] = (E[j] * L[3] + E[3 + j] * L[4]) + E[6 + j] * L[5];
        o[6 + j] = (E[j] * L[6] + E[3 + j] * L[7]) + E[6 + j] * L[8];
        o[9 + j] = ((E[j] * L[9] + E[3 + j] * L[10]) + E[6 + j] * L[11]) + E[9 + j];
    }
    memcpy(out, o, sizeof o);
}
