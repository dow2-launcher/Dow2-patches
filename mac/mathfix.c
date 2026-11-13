// mathfix.c - crossplay_fix v7, part 2: Windows math for the Mac port (Feral 1.0.3, x86_64).
// Why: the Mac binary carries its own libm (FreeBSD msun, linked in), Windows DoW2 uses msvcr80's SSE2 routines. They differ
// in the last bits for 0.03-16% of inputs (acosf/atan2f/tanf/expf/powf...), the formation geometry of a moving squad drifts,
// and the game desyncs at the first move order (seen in the random-generator traces as extra "SMCC" calls on the Mac).
// What: every trig/exp/log/pow entry of the game jumps to msmath.c - msvcr80's own code translated instruction by instruction
// (mathcmp/lift/lift.py), bit-exact with the real DLL over 5M inputs per function. Float versions = (float)f((double)x),
// which is what MSVC's <cmath> float overloads compile to. Lua's math.asin/atan/atan2 get the _CI* flavour Windows Lua uses.
// A self-test with msvcr80's own outputs runs at load; the result goes to /tmp/crossplay_fix.log. DOW2_NOMATHFIX=1 turns it off.
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>

double ms_acos(double), ms_asin(double), ms_atan(double), ms_atan2(double, double), ms_sin(double), ms_cos(double),
       ms_tan(double), ms_exp(double), ms_log(double), ms_pow(double, double), ms_log10(double);
double msci_asin(double), msci_atan(double), msci_atan2(double, double), msci_log10(double);

static uintptr_t g_slide;
static int from_lua(const uint64_t *rets, uintptr_t ret) {
    ret -= g_slide; return ret == rets[0] || ret == rets[1];
}

static float f_acosf(float x) { return (float)ms_acos(x); }
static float f_asinf(float x) { return (float)ms_asin(x); }
static float f_atanf(float x) { return (float)ms_atan(x); }
static float f_atan2f(float y, float x) { return (float)ms_atan2(y, x); }
static float f_sinf(float x) { return (float)ms_sin(x); }
static float f_cosf(float x) { return (float)ms_cos(x); }
static float f_tanf(float x) { return (float)ms_tan(x); }
static float f_expf(float x) { return (float)ms_exp(x); }
static float f_logf(float x) { return (float)ms_log(x); }
static float f_powf(float x, float y) { return (float)ms_pow(x, y); }
static double h_acos(double x) { return ms_acos(x); }
static double h_asin(double), h_atan(double), h_atan2(double, double);

#include "mathfix_tables.h"

// entered by a jmp from the game's own function, so the return address is the game's call site
__attribute__((noinline)) static double h_asin(double x) {
    return from_lua(LUA_ASIN, (uintptr_t)__builtin_return_address(0)) ? msci_asin(x) : ms_asin(x); }
__attribute__((noinline)) static double h_atan(double x) {
    return from_lua(LUA_ATAN, (uintptr_t)__builtin_return_address(0)) ? msci_atan(x) : ms_atan(x); }
__attribute__((noinline)) static double h_atan2(double y, double x) {
    return from_lua(LUA_ATAN2, (uintptr_t)__builtin_return_address(0)) ? msci_atan2(y, x) : ms_atan2(y, x); }

static void mlog(const char *fmt, ...) {
    FILE *f = fopen("/tmp/crossplay_fix.log", "a"); if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fclose(f);
}

// msmath against msvcr80's outputs on this machine (Intel Mac or Rosetta): x87 paths and rcpss are the CPU's/Rosetta's own
static void selftest(void) {
    int bad_total = 0;
    for (size_t t = 0; t < sizeof TESTS / sizeof TESTS[0]; t++) {
        const struct selftest *T = &TESTS[t]; int bad = 0, first = -1, bad87 = 0;
        int trig = !T->ci && (!strcmp(T->name, "sin") || !strcmp(T->name, "cos") || !strcmp(T->name, "tan"));
        for (int i = 0; i < T->n; i++) {
            double a, b = 0, r; memcpy(&a, &T->in[i * T->nargs], 8); if (T->nargs > 1) memcpy(&b, &T->in[i * T->nargs + 1], 8);
            const char *n = T->name;
            if (T->ci) r = !strcmp(n, "asin") ? msci_asin(a) : !strcmp(n, "atan") ? msci_atan(a) : !strcmp(n, "atan2") ? msci_atan2(a, b) : msci_log10(a);
            else r = !strcmp(n, "acos") ? ms_acos(a) : !strcmp(n, "asin") ? ms_asin(a) : !strcmp(n, "atan") ? ms_atan(a) :
                     !strcmp(n, "atan2") ? ms_atan2(a, b) : !strcmp(n, "sin") ? ms_sin(a) : !strcmp(n, "cos") ? ms_cos(a) :
                     !strcmp(n, "tan") ? ms_tan(a) : !strcmp(n, "exp") ? ms_exp(a) : !strcmp(n, "log") ? ms_log(a) : ms_pow(a, b);
            if (memcmp(&r, &T->want[i], 8)) { if (first < 0) first = i; bad++; if (trig && !(fabs(a) < 86016.0)) bad87++; }
        }
        if (bad) {
            char nm[32]; snprintf(nm, sizeof nm, "%s%s", T->ci ? "lua " : "", T->name);
            uint64_t q; memcpy(&q, &T->in[first * T->nargs], 8);
            if (trig && bad87 == bad)        // v15: say what these are (seen on Rosetta: 2-3 of 312, inputs like 1e299, DBL_MAX)
                mlog("math self-test: %s differs from Windows in %d of %d inputs, all |x| >= 86016 (first: input %#llx) - msvcr80's "
                     "x87 fsin/fcos/fptan + fprem1 path run by this CPU (Rosetta emulates x87); game angles never get there, harmless",
                     nm, bad, T->n, (unsigned long long)q);
            else
                mlog("math self-test: %s differs from Windows in %d of %d inputs (first: input %#llx)", nm, bad, T->n, (unsigned long long)q);
        }
        bad_total += bad - bad87;
    }
    if (!bad_total) mlog("math self-test: all %d functions bit-exact with Windows msvcr80 (on every input below 86016 rad for sin/cos/tan)", (int)(sizeof TESTS / sizeof TESTS[0]));
}

__attribute__((constructor)) static void mathfix_init(void) {
    char cpu[128] = "?"; size_t len = sizeof cpu; sysctlbyname("machdep.cpu.brand_string", cpu, &len, NULL, 0);
    int tr = 0; len = sizeof tr; if (sysctlbyname("sysctl.proc_translated", &tr, &len, NULL, 0)) tr = 0;
    mlog("math: cpu %s%s", cpu, tr ? " (Rosetta)" : "");
    if (getenv("DOW2_NOMATHFIX")) { mlog("math: DOW2_NOMATHFIX set - the game's own math is left in place"); return; }
    g_slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    size_t nh = sizeof HOOKS / sizeof HOOKS[0];
    for (size_t i = 0; i < nh; i++)   // all or nothing: a different build of the game is left alone
        if (memcmp((void *)(HOOKS[i].addr + g_slide), HOOKS[i].want, 14)) {
            mlog("math: %s has unexpected bytes (another game build?) - math NOT replaced", HOOKS[i].name);
            return;
        }
    int done = 0;
    for (size_t i = 0; i < nh; i++) {
        unsigned char *p = (unsigned char *)(HOOKS[i].addr + g_slide);
        unsigned char jmp[14] = {0xFF, 0x25, 0, 0, 0, 0};          // jmp qword [rip+0]; .quad target
        uint64_t to = (uint64_t)HOOKS[i].to; memcpy(jmp + 6, &to, 8);
        vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
        if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
            mlog("math: vm_protect failed at %s", HOOKS[i].name); continue; }
        memcpy(p, jmp, sizeof jmp);
        vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
        done++;
    }
    // through the game's own entry points now, inputs where the Mac's msun and Windows disagree in the last bit
    float (*g_acosf)(float) = (float (*)(float))(0x100931890ULL + g_slide);
    float (*g_atan2f)(float, float) = (float (*)(float, float))(0x1009314b0ULL + g_slide);
    union { uint32_t u; float f; } in1 = {0x3e9a8e91}, y = {0x438d6f79}, x = {0xc3103d8e}, r1, r2;   // 0.30186895; 282.87088, -144.24045
    r1.f = g_acosf(in1.f); r2.f = g_atan2f(y.f, x.f);
    int via = r1.u == 0x3fa1cf77 && r2.u == 0x4002b5c9;     // Windows: 0x3fa1cf77, 0x4002b5c9 (the Mac's own: ...78, ...ca)
    mlog("math: %s - %d of %d functions now run msvcr80's code", via ? "OK" : "HOOK CHECK FAILED", done, (int)nh);
    selftest();
}
