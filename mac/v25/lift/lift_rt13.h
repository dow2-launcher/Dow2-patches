// lift_rt13.h (v13: + double lanes, msvcr80 math for external calls) - runtime of the C code lift.py makes from 32-bit Windows functions: the emulated CPU and the 32-bit handle memory
#ifndef LIFT_RT13_H
#define LIFT_RT13_H
#include <stdint.h>
#include <string.h>
#include <stddef.h>
typedef union { float f[4]; uint32_t u[4]; uint64_t q[2]; double d[2]; } lift_xmm;
struct lift_cpu {
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp, esp;
    int zf, cf, pf, sf, of;
    lift_xmm x[8];
    double st[8]; int top, fsw;                // x87 (values as the 24-bit-precision unit holds them)
    char *base[16];                 // region r covers handles r << 24 .. (r << 24) + 0xffffff; region 1 = the stack
    char stack[0x1000];
};
#define hp(a) (c->base[(uint32_t)(a) >> 24] + ((uint32_t)(a) & 0xffffffu))
static inline float U2F(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static inline double U2D(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static inline int32_t lift_cvtt(float f) { return (f != f || f >= 2147483648.0f || f < -2147483648.0f) ? (int32_t)0x80000000 : (int32_t)f; }
static inline int32_t lift_cvtn(float f) { return (f != f || f >= 2147483648.0f || f < -2147483648.0f) ? (int32_t)0x80000000 : (int32_t)__builtin_rintf(f); }
#define LD(a) (*(double *)hp(a))
#include "../win24.h"
static inline float w24_div(double a, double b) {             // x87 fdiv at PC24: a / b correctly rounded to 24 bits
    double q = a / b, p = q * b, qh, ql, bh, bl;
    w24_split(q, &qh, &ql); w24_split(b, &bh, &bl);
    double ep = ((qh * bh - p) + qh * bl + ql * bh) + ql * bl;   // q*b = p + ep exactly
    double r = (a - p) - ep;                                  // a - q*b (exact for q close to a/b)
    return w24_round(q, r / b);
}
static inline float w24_sqrt(double a) { return (float)__builtin_sqrt(a); }   // operands here are 24-bit: one rounding is exact
#ifndef LIFT_BAD_CALL
#define LIFT_BAD_CALL
static void lift_bad_call(struct lift_cpu *c, uint32_t at) { (void)c; (void)at; __builtin_trap(); }
#endif
#define L32(a) (*(uint32_t *)hp(a))
#define L8(a) (*(uint8_t *)hp(a))
#define LF(a) (*(float *)hp(a))
#define S32(a, v) (*(uint32_t *)hp(a) = (uint32_t)(v))
#define S8(a, v) (*(uint8_t *)hp(a) = (uint8_t)(v))
#define SF(a, v) (*(float *)hp(a) = (v))
#define LIFT_STACK 1u
double ms_acos(double), ms_asin(double), ms_atan(double), ms_atan2(double, double), ms_sin(double), ms_cos(double), ms_tan(double),
       ms_exp(double), ms_log(double), ms_pow(double, double), ms_log10(double);
static inline void lift_init(struct lift_cpu *c) {
    memset(c->base, 0, sizeof c->base);
    c->base[LIFT_STACK] = c->stack; c->esp = (LIFT_STACK << 24) + sizeof c->stack - 64;
}
static inline uint32_t lift_region(struct lift_cpu *c, int r, void *p) { c->base[r] = (char *)p; return (uint32_t)r << 24; }
#endif
