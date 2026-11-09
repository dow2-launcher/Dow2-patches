// cdownfix.c - crossplay_fix v24, part 30: a projectile stuck to an entity (StateProjectile::ProjectileCountdown) placed with
// DOW2.exe's matrix-product sum order.
// Why (difftest step 9, calderis_outskirts 2026-03-15 Low F18771 / High H15163: eld_haywire_grenade 32286 position x,z, y axis
// x,z, collidedpos; macrun hardware watch (s9_haywatch): the write is Entity::SetTransform <- StateProjectile::ProjectileCountdown
// +0x268): world = local (StateProjectile +0xa8, 12 floats) * the parent entity's transform (+0x9c). DOW2.exe 0xa5c687 inlines
// the product with per-element sum orders (e.g. x = (L2*Q6 + L0*Q0) + L1*Q3, pos.z = ((L10*Q5 + L11*Q8) + L9*Q2) + Q11); the port
// (0x101bd1bc3, vectorised) adds in other orders.
// What: the span 0x101bd1bc3..0x101bd1da2 (the product into [rbp-0x50], rdi = entity) jumps to a stub calling cd_mul (DOW2.exe's
// order, SSE scalar, no contraction), back to 0x101bd1da2 (SetTransform call). Self-test at load: CD_NST cases from DOW2.exe
// (t24/w24 cdownst: its own bytes run by a trampoline) through the PATCHED span in place (fake state in rbx, fake frame in rbp,
// temporary landing on 0x101bd1da2). DOW2_NOCDOWNFIX=1: off (both sites).
#include "patch_util.h"
#include "cdownfix_st.inc"
#include "cdownfix_at_st.inc"
// site 2 (the cause found next, s9_cdL step trace: the stored local matrix itself already differs, L9 bf679310 / bf679300):
// StateProjectile::ProjectileMoving when the projectile sticks: local = own transform W (entity +0x9c) * inverse(parent) I
// (InverseSelf already as Windows, rangefix); DOW2.exe 0xa5c26a inline per-element orders, Mac 0x101bd1861..0x101bd1a14
// vectorised. Same treatment: span -> stub -> at_mul, back to 0x101bd1a14; self-test AT_NST cases (t24/w24 attachst).
#define AT_AT 0x101bd1861UL
#define AT_BACK 0x101bd1a14UL
static const unsigned char AT_OLD[16] = { 0xf3,0x44,0x0f,0x10,0x00, 0xf3,0x44,0x0f,0x10,0x48,0x04, 0xf3,0x44,0x0f,0x10,0x50 };
static const unsigned char AT_LAND_OLD[14] = { 0x49,0x8b,0x7c,0x24,0x70, 0xe8,0x72,0x08,0x58,0x00, 0x48,0x89,0xc7, 0xe8 };
__attribute__((noinline)) void at_mul(const float *I, const float *W, float *o) {
    o[0] = (W[1] * I[3] + W[2] * I[6]) + W[0] * I[0];
    o[1] = (W[0] * I[1] + W[2] * I[7]) + W[1] * I[4];
    o[2] = (W[0] * I[2] + W[2] * I[8]) + W[1] * I[5];
    o[3] = (W[4] * I[3] + W[5] * I[6]) + W[3] * I[0];
    o[4] = (W[3] * I[1] + W[5] * I[7]) + W[4] * I[4];
    o[5] = (W[3] * I[2] + W[5] * I[8]) + W[4] * I[5];
    o[6] = (W[7] * I[3] + W[8] * I[6]) + W[6] * I[0];
    o[7] = (W[6] * I[1] + W[8] * I[7]) + W[7] * I[4];
    o[8] = (W[6] * I[2] + W[8] * I[8]) + W[7] * I[5];
    o[9] = ((W[10] * I[3] + W[11] * I[6]) + W[9] * I[0]) + I[9];
    o[10] = ((W[9] * I[1] + W[11] * I[7]) + W[10] * I[4]) + I[10];
    o[11] = ((W[9] * I[2] + W[11] * I[8]) + W[10] * I[5]) + I[11];
}
uint64_t at_back;
void at_stub(void);
// ProjectileMoving: push rbp, 4 pushes, sub 0xc0 -> rsp 16-aligned; live after the span: r12 (state), rbx, rbp; 0x101bd1a14 reloads rdi
__asm__(".text\n.globl _at_stub\n_at_stub:\n mov %rax, %rdi\n lea 0x9c(%rbx), %rsi\n lea 0xa8(%r12), %rdx\n call _at_mul\n jmp *_at_back(%rip)\n");
void at_run(float *inv, void *ent, void *st, uint64_t entry);
__asm__(".text\n.globl _at_run\n_at_run:\n push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " mov %rsp, _cd_saved_rsp(%rip)\n sub $0x108, %rsp\n lea 0xc0(%rsp), %rbp\n mov %rdi, %rax\n mov %rsi, %rbx\n mov %rdx, %r12\n jmp *%rcx\n");
#define CD_AT 0x101bd1bc3UL
#define CD_BACK 0x101bd1da2UL
static const unsigned char CD_OLD[16] = { 0x48,0x8b,0x7b,0x70, 0xf3,0x0f,0x10,0x88,0xa8,0x00,0x00,0x00, 0xf3,0x44,0x0f,0x10 };
static const unsigned char CD_LAND_OLD[14] = { 0x48,0x8d,0x75,0xb0, 0xf3,0x0f,0x10,0x05,0xca,0x0a,0x89,0x00, 0xba,0x01 };
__attribute__((noinline)) void cd_mul(const char *st, float *o) {
    const float *L = (const float *)(st + 0xa8), *Q = (const float *)(*(const char *const *)(st + 0x98) + 0x9c);
    o[0] = (L[2] * Q[6] + L[0] * Q[0]) + L[1] * Q[3];
    o[1] = (L[1] * Q[4] + L[2] * Q[7]) + L[0] * Q[1];
    o[2] = (L[1] * Q[5] + L[2] * Q[8]) + L[0] * Q[2];
    o[3] = (L[5] * Q[6] + L[3] * Q[0]) + L[4] * Q[3];
    o[4] = (L[4] * Q[4] + L[5] * Q[7]) + L[3] * Q[1];
    o[5] = (L[4] * Q[5] + L[5] * Q[8]) + L[3] * Q[2];
    o[6] = (L[6] * Q[0] + L[7] * Q[3]) + L[8] * Q[6];
    o[7] = (L[6] * Q[1] + L[7] * Q[4]) + L[8] * Q[7];
    o[8] = (L[7] * Q[5] + L[8] * Q[8]) + L[6] * Q[2];
    o[9] = ((L[9] * Q[0] + L[10] * Q[3]) + L[11] * Q[6]) + Q[9];
    o[10] = ((L[9] * Q[1] + L[10] * Q[4]) + L[11] * Q[7]) + Q[10];
    o[11] = ((L[10] * Q[5] + L[11] * Q[8]) + L[9] * Q[2]) + Q[11];
}
uint64_t cd_back;
void cd_stub(void);
// in the game frame rsp is 16-aligned (push rbp, push rbx, sub 0x48): call is ABI-correct; only rbx / rbp are live after the span
__asm__(".text\n.globl _cd_stub\n_cd_stub:\n mov %rbx, %rdi\n lea -0x50(%rbp), %rsi\n call _cd_mul\n mov 0x70(%rbx), %rdi\n jmp *_cd_back(%rip)\n");
uint64_t cd_saved_rsp;
void cd_run(void *st, char *rbpv, uint64_t entry);
void cd_land(void);
__asm__(".text\n.globl _cd_run\n_cd_run:\n push %rbp\n push %rbx\n push %r12\n push %r13\n push %r14\n push %r15\n"
        " mov %rsp, _cd_saved_rsp(%rip)\n sub $0x108, %rsp\n mov %rdi, %rbx\n mov %rsi, %rbp\n jmp *%rdx\n"
        ".globl _cd_land\n_cd_land:\n mov _cd_saved_rsp(%rip), %rsp\n pop %r15\n pop %r14\n pop %r13\n pop %r12\n pop %rbx\n pop %rbp\n ret\n");
static int cd_wr(unsigned char *p, const void *b, size_t n) {
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, n); vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE); return 1;
}
static char cd_st[0x200], cd_ent[0x200], cd_fr[0x400];
void cdownfix_test_case(const uint32_t *c, uint32_t *o, uintptr_t slide) {   // t24 / self-test: landing on CD_BACK put by the caller
    memset(cd_st, 0, sizeof cd_st); memcpy(cd_st + 0xa8, c, 48); *(char **)(cd_st + 0x98) = cd_ent; memcpy(cd_ent + 0x9c, c + 12, 48);
    char *rb = cd_fr + 0x200; cd_run(cd_st, rb, CD_AT + slide); memcpy(o, rb - 0x50, 48);
}
static float at_inv[12]; static char at_ent[0x200], at_st[0x200];
void cdownfix_at_case(const uint32_t *c, uint32_t *o, uintptr_t slide) {     // landing on AT_BACK put by the caller
    memcpy(at_inv, c, 48); memcpy(at_ent + 0x9c, c + 12, 48); memset(at_st, 0, sizeof at_st);
    at_run(at_inv, at_ent, at_st, AT_AT + slide); memcpy(o, at_st + 0xa8, 48);
}
static int at_patch(uintptr_t slide, unsigned long *badp) {      // 1 patched + self-test OK, 0 not patched
    unsigned char *p = (unsigned char *)(AT_AT + slide), *l = (unsigned char *)(AT_BACK + slide), old[14]; unsigned long bad = 0; *badp = 0;
    if (memcmp(p, AT_OLD, sizeof AT_OLD) || memcmp(l, AT_LAND_OLD, sizeof AT_LAND_OLD)) return 0;
    memcpy(old, p, 14); at_back = AT_BACK + slide;
    if (!pu_jump(p, (void *)at_stub)) return 0;
    int ok = pu_jump(l, (void *)cd_land);
    if (ok) for (unsigned long q = 0; q < AT_NST; q++) { uint32_t o[12]; cdownfix_at_case(AT_ST[q], o, slide); if (memcmp(o, AT_ST[q] + 24, 48)) bad++; }
    if (!cd_wr(l, AT_LAND_OLD, 14)) ok = 0;
    *badp = bad; if (!ok || bad) { cd_wr(p, old, 14); return 0; }
    return 1;
}
__attribute__((constructor)) static void cdownfix_init(void) {
    if (getenv("DOW2_NOCDOWNFIX")) { pu_log("cdownfix: DOW2_NOCDOWNFIX set - the port's own stuck-projectile transform order%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(CD_AT + slide), *l = (unsigned char *)(CD_BACK + slide), old[14];
    if (memcmp(p, CD_OLD, sizeof CD_OLD) || memcmp(l, CD_LAND_OLD, sizeof CD_LAND_OLD)) { pu_log("cdownfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", CD_AT, 0); return; }
    memcpy(old, p, 14); cd_back = CD_BACK + slide;
    if (!pu_jump(p, (void *)cd_stub)) { pu_log("cdownfix: vm_protect failed - NOT patched%.0lu%.0lu", 0, 0); return; }
    unsigned long bad = 0; int ok = pu_jump(l, (void *)cd_land);
    if (ok) for (unsigned long q = 0; q < CD_NST; q++) { uint32_t o[12]; cdownfix_test_case(CD_ST[q], o, slide); if (memcmp(o, CD_ST[q] + 24, 48)) bad++; }
    if (!cd_wr(l, CD_LAND_OLD, 14)) ok = 0;
    if (!ok || bad) { cd_wr(p, old, 14); pu_log(ok ? "cdownfix self-test: %lu of %lu differ from DOW2.exe - NOT patched" : "cdownfix self-test could not run (%lu/%lu) - NOT patched", bad, CD_NST); return; }
    unsigned long abad; int at = at_patch(slide, &abad);
    if (!at) { pu_log("cdownfix: OK - ProjectileCountdown transform (self-test %lu); attach (ProjectileMoving) NOT patched (bytes / self-test %lu differ)", CD_NST, abad); return; }
    pu_log("cdownfix: OK - stuck projectile in DOW2.exe's sum orders: attach (ProjectileMoving local = own * inverse(parent)) + follow (ProjectileCountdown), self-test %lu + %lu (patched code run)", AT_NST, CD_NST);
}
