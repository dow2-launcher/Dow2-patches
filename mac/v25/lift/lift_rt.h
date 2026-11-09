// lift_rt.h - runtime of the C code lift.py makes from 32-bit Windows functions: the emulated CPU and the 32-bit handle memory
#ifndef LIFT_RT_H
#define LIFT_RT_H
#include <stdint.h>
#include <string.h>
#include <stddef.h>
typedef union { float f[4]; uint32_t u[4]; uint64_t q[2]; } lift_xmm;
struct lift_cpu {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp;
    int zf, cf, pf, sf, of;
    lift_xmm x[8];
    char *base[16];                 // region r covers handles r << 24 .. (r << 24) + 0xffffff; region 1 = the stack
    char stack[0x1000];
};
#define hp(a) (c->base[(uint32_t)(a) >> 24] + ((uint32_t)(a) & 0xffffffu))
static inline float U2F(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
#define L32(a) (*(uint32_t *)hp(a))
#define L8(a) (*(uint8_t *)hp(a))
#define LF(a) (*(float *)hp(a))
#define S32(a, v) (*(uint32_t *)hp(a) = (uint32_t)(v))
#define S8(a, v) (*(uint8_t *)hp(a) = (uint8_t)(v))
#define SF(a, v) (*(float *)hp(a) = (v))
#define LIFT_STACK 1u
static inline void lift_init(struct lift_cpu *c) {
    memset(c->base, 0, sizeof c->base);
    c->base[LIFT_STACK] = c->stack; c->esp = (LIFT_STACK << 24) + sizeof c->stack - 64;
}
static inline uint32_t lift_region(struct lift_cpu *c, int r, void *p) { c->base[r] = (char *)p; return (uint32_t)r << 24; }
#endif
