// win24.h - Windows DoW2's simulation arithmetic for doubles: the game sets the x87 precision control to 24 bits
// (_controlfp_s(_PC_24, _MCW_PC), DOW2.exe 0x4319da, and "GAME -- Resetting fp control word." before every
// GameObj::Simulate), so every x87 add/sub/mul of the sim rounds its exact result once to a 24-bit mantissa (round to
// nearest even), whatever the C++ type. The Mac port does these in SSE2 doubles. Here: the exact result (TwoSum /
// Dekker TwoProduct = an unevaluated sum s + e) correctly rounded to 24 bits - no x87 needed (Rosetta's x87 is not
// relied on). Normal ranges only (no float overflow/underflow in these uses).
#include <math.h>
#include <stdint.h>

static inline float w24_round(double s, double e) {          // s + e exactly (|e| <= ulp(s)/2) -> 24-bit mantissa
    float f = (float)s;                                        // nearest even of s
    double fd = (double)f;
    if (fd == s || e == 0) return f;
    float g = nextafterf(f, s > fd ? INFINITY : -INFINITY);   // the other float around s
    if ((double)f + (double)g != 2.0 * s) return f;            // s is not the midpoint: f is right for s + e too
    return (e > 0) == (g > f) ? g : f;                         // the exact value is past the midpoint, toward e
}
static inline float w24_add(double a, double b) {               // x87 fadd at PC24 of two doubles (exact a + b)
    double s = a + b, bb = s - a, e = (a - (s - bb)) + (b - bb);
    return w24_round(s, e);
}
static inline void w24_split(double a, double *hi, double *lo) {
    double c = 134217729.0 * a;                                 // 2^27 + 1
    *hi = c - (c - a); *lo = a - *hi;
}
static inline float w24_mul(double a, double b) {               // x87 fmul at PC24 of two doubles (exact a * b)
    double p = a * b, ah, al, bh, bl;
    w24_split(a, &ah, &al); w24_split(b, &bh, &bl);
    double e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
    return w24_round(p, e);
}
// Util.dll Random::GetUnitInclusive (0x10015a40): fild (int)h; if < 0: fadd 2^32; fmul 1/(2^32-1) [double 0x100344f0]
static inline float w24_unit_inclusive(uint32_t h) {
    const double C = 2.3283064370807974e-10;
    double t = (int32_t)h >= 0 ? (double)h : (double)(float)(double)h;   // fadd 2^32 rounds h (exact) to 24 bits
    return w24_mul(t, C);
}
// SimEngine.dll SimManager::GetRandRange (0x100233d0): u = GetUnitInclusive; lo + (hi - lo) * u, x87 at PC24
static inline float w24_rand_range(uint32_t h, double lo, double hi) {
    float u = w24_unit_inclusive(h);
    float d = w24_add(hi, -lo);
    float p = (float)((double)u * (double)d);                   // 24 x 24 bits: exact in a double, one rounding
    return w24_add((double)p, lo);
}
