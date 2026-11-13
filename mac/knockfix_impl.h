// knockfix_impl.h - the knockback fly target as DOW2.exe computes it (shared by knockfix.c and t17/m17.c).
// StateKnockback::DoKnockback, Windows 0x976a10 -> 0x976ca0 (the port inlines it at 0x101bc36b2..0x101bc3835):
//   d = target - pos (x, z); L = |d|; if 0.01 > L: no throw (0 ticks, no ClampToWorld)
//   dir = d * (1 / L); v = n * 0.1 (n = (int)(nice duration * 10)); speed = v > 0.001 (as double) ? L / v : 0
//   t = min(speed * 0.0666667f, 1.5)        (the port: speed / 15 - MSVC /fp:fast multiplies by the float reciprocal)
//   f = min(((t * t) * speed) * 0.25, 6)    (the port: ((speed * t) * t) * 0.25)
//   out = target + dir * f; ticks = (int)(t * 10)
#include <stdint.h>
#include <math.h>
#include <xmmintrin.h>
static int32_t kb_cvtt(float x) { return _mm_cvttss_si32(_mm_set_ss(x)); }   // cvttss2si (INT_MIN when out of range)
// returns the ticks, or -1 for "too close, nothing" (the port then sets 0 ticks and skips ClampToWorld, as Windows)
static int32_t kb_fly(const float *tgt, const float *pos, float nice, float *out) {
    int32_t n = kb_cvtt(nice);
    float dx = tgt[0] - pos[0], dz = tgt[2] - pos[2];
    float L = sqrtf(dx * dx + dz * dz);                    // 976cd0: x*x + z*z, sqrtss
    if (0.01f > L) return -1;                              // comiss 0.01, L; ja (a NaN goes on)
    float inv = 1.0f / L;
    float dirx = inv * dx, dirz = inv * dz, diry = inv * 0.0f;
    float v = (float)n * 0.1f, speed = 0.0f;
    if ((double)v > 0.001) speed = L / v;
    float t = speed * 0.0666666701f;                       // [0x113cd24] = 0x3d888889
    if (t > 1.5f) t = 1.5f;
    float f = ((t * t) * speed) * 0.25f;
    if (f > 6.0f) f = 6.0f;
    float ticks = t * 10.0f;
    out[0] = out[0] + dirx * f;
    out[1] = out[1] + f * diry;
    out[2] = out[2] + dirz * f;
    return kb_cvtt(ticks);
}
