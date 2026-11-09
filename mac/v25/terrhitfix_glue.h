// terrhitfix_glue.h - the port's per-cell terrain hit (Mac 0x101997050 ABI) running DOW2.exe's 0xcb3ae0 (lift/lift_th.c);
// shared by terrhitfix.c and the Linux test t15/m15.c (mode lift)
#include <stdint.h>
#include <string.h>
#include "lift/lift_rt13.h"
static double th_sample(struct lift_cpu *c, uint32_t i, uint32_t j);
#include "lift/lift_th.c"

struct th_smp { uint32_t vt, pad; void *mac; };          // region 2: [0] -> itself as the "vtable", the port's sampler
static double th_sample(struct lift_cpu *c, uint32_t i, uint32_t j) {
    void *s = ((struct th_smp *)c->base[2])->mac;
    float (*get)(void *, long, long) = *(float (**)(void *, long, long))(*(char **)s + 0x10);
    return (double)get(s, (long)(int32_t)i, (long)(int32_t)j);
}
static uint32_t th_fb(float f) { uint32_t b; memcpy(&b, &f, 4); return b; }
// the port's functor: +8 sampler, +0x10 / +0x14 x / z offset of the field, +0x18 hit point, +0x24 hit normal
__attribute__((used)) char terrhitfix_cell(char *self, uint32_t nx, uint32_t nz, const float *start, const float *dir,
                                           int32_t i, int32_t j, float cell, float ylo, float yhi) {
    struct lift_cpu cpu, *c = &cpu; memset(c, 0, offsetof(struct lift_cpu, base)); lift_init(c);
    struct th_smp smp = { 2u << 24, 0, *(void **)(self + 8) };
    float offx, offz; memcpy(&offx, self + 0x10, 4); memcpy(&offz, self + 0x14, 4);
    uint32_t a[12] = { lift_region(c, 2, &smp), th_fb(cell), th_fb(offx), th_fb(offz), lift_region(c, 3, (void *)start),
                       lift_region(c, 4, (void *)dir), (uint32_t)i, (uint32_t)j, th_fb(ylo), th_fb(yhi),
                       lift_region(c, 5, self + 0x18), lift_region(c, 6, self + 0x24) };
    for (int k = 11; k >= 0; k--) { c->esp -= 4; S32(c->esp, a[k]); }
    c->esp -= 4; S32(c->esp, 0xdeadbeefu);
    c->eax = nx; c->ecx = nz;
    dw_cb3ae0(c);
    return (char)(c->eax & 0xff);
}

