// terrainfix_impl.h - Dirt::HeightMapCollision::GetHeightNormal as DOW2.exe computes it (0xcb40c0), for the Mac port.
// Same formula on both sides, different order of the float additions: Windows ((t1 + t2) + hc), the port t1 + (hc + t2);
// the normal: Windows (c*c + a*a) + 1 and Newton steps (y*y)*half, the port c*c + (a*a + 1) and (half*y)*y.
// Mac call: float f(const HeightSampler &s, unsigned long nx, unsigned long nz, float cell, float x, float z, Vector3f *n),
// the sampler's virtual +0x10 returns the height at (i, j).
#include <stdint.h>
#include <string.h>

typedef float (*tf_sample_fn)(const void *, long, long);
static inline float tf_sample(const void *s, long i, long j) { return (*(tf_sample_fn *)(*(const char *const *)s + 0x10))(s, i, j); }

// DOW2.exe's floor: (v + copysign(2^23, v)) - copysign(2^23, v), minus 1 if that came out above v
static inline float tf_floor(float v) {
    uint32_t vb, cb; memcpy(&vb, &v, 4);
    uint32_t sb = vb & 0x80000000u; float sgn; memcpy(&sgn, &sb, 4);
    cb = sb | 0x4b000000u; float c; memcpy(&c, &cb, 4);
    volatile float t = v + c; float r = t - c;
    if (!(r - v <= sgn)) r -= 1.0f;               // cmpnless
    return r;
}

static inline float tf_rsqrt(float s) {
    float half = s * 0.5f; uint32_t b; memcpy(&b, &s, 4);
    b = 0x5f3759dfu - (uint32_t)((int32_t)b >> 1); float y; memcpy(&y, &b, 4);
    y = (1.5f - (y * y) * half) * y;
    y = (1.5f - (y * y) * half) * y;
    return y;
}

__attribute__((used, visibility("default")))
float terrainfix_height_normal(const void *smp, unsigned long nx, unsigned long nz, float cell, float x, float z, float *n) {
    float inv = 1.0f / cell;
    float sx = inv * x, fx0 = tf_floor(sx);
    float sz = inv * z, fz0 = tf_floor(sz);
    int32_t i = (int32_t)(uint32_t)(nx >> 1) + (int32_t)fx0;
    int32_t j = (int32_t)(uint32_t)(nz >> 1) + (int32_t)fz0;
    if ((i | j) < 0 || i >= (int32_t)nx - 1 || j >= (int32_t)nz - 1) {
        if (n) { n[0] = 0.0f; n[1] = 1.0f; n[2] = 0.0f; }
        return 0.0f;
    }
    float fx = sx - fx0, fz = sz - fz0;
    float h00 = tf_sample(smp, i, j), h11 = tf_sample(smp, i + 1, j + 1);
    if (fz >= fx) {
        float hc = tf_sample(smp, i, j + 1), dB = h11 - hc, dA = h00 - hc;
        if (n) {
            float na = -(dB * inv), c = dA * inv, y = tf_rsqrt((c * c + na * na) + 1.0f);
            n[0] = na * y; n[1] = y; n[2] = c * y;
        }
        return ((1.0f - fz) * dA + dB * fx) + hc;
    } else {
        float hc = tf_sample(smp, i + 1, j), dB = h11 - hc, dA = h00 - hc;
        if (n) {
            float c = dA * inv, na = -(dB * inv), y = tf_rsqrt((na * na + c * c) + 1.0f);
            n[0] = c * y; n[1] = y; n[2] = na * y;
        }
        return ((1.0f - fx) * dA + dB * fz) + hc;
    }
}
