// pathfix_stub.h - site 4's code: entered by jmp from 0x101e8dd58 (xmm0 = T = sqrt(L*L - 4r*r), [rbp-0x50] = L,
// [rbp-0x70] = 2r); leaves xmm2 = (1/L)*2r and xmm0 = (1/L)*T, [rbp-0x60] = T, and jumps back to 0x101e8dd6e.
#ifdef __APPLE__
#define PF_SYM(x) "_" #x
#else
#define PF_SYM(x) #x
#endif
__attribute__((used)) uint64_t pathfix_back_xcost = 0;
__attribute__((used)) const float pathfix_one = 1.0f;
__asm__(".text\n"
    ".globl " PF_SYM(pathfix_stub_xcost) "\n" PF_SYM(pathfix_stub_xcost) ":\n"
    "  movss  " PF_SYM(pathfix_one) "(%rip), %xmm1\n"
    "  divss  -0x50(%rbp), %xmm1\n"                 // 1/L, as Windows
    "  movaps -0x70(%rbp), %xmm2\n"                 // 2r
    "  mulss  %xmm1, %xmm2\n"                       // sin = 2r * (1/L)
    "  movss  %xmm0, -0x60(%rbp)\n"                 // T, kept as the port does
    "  mulss  %xmm1, %xmm0\n"                       // cos = T * (1/L)
    "  jmpq   *" PF_SYM(pathfix_back_xcost) "(%rip)\n");
void pathfix_stub_xcost(void);
// site 8's code: entered by jmp from 0x101e8d8e5 with xmm0 = the angle (Math::Angle), r14 = SimPathManipulatorData
// (+0x30 speed, +0x38 turn rate), [rbp-0xb0] = r; leaves xmm1 = the arc's time as Windows computes it, back to 0x101e8d9aa.
__attribute__((used)) uint64_t pathfix_back_tpcost = 0;
__asm__(".text\n"
    ".globl " PF_SYM(pathfix_stub_tpcost) "\n" PF_SYM(pathfix_stub_tpcost) ":\n"
    "  movaps %xmm0, %xmm1\n"                       // angle
    "  movss  " PF_SYM(pathfix_one) "(%rip), %xmm2\n"
    "  divss  0x38(%r14), %xmm2\n"                  // 1/turnrate
    "  movss  0x30(%r14), %xmm3\n"
    "  mulss  %xmm2, %xmm3\n"                       // k = speed * (1/turnrate)
    "  movaps -0xb0(%rbp), %xmm0\n"                 // r
    "  ucomiss %xmm3, %xmm0\n"
    "  jae    1f\n"
    "  mulss  %xmm2, %xmm1\n"                       // r < k: angle * (1/turnrate)
    "  jmpq   *" PF_SYM(pathfix_back_tpcost) "(%rip)\n"
    "1:\n"
    "  divss  0x30(%r14), %xmm1\n"                  // r >= k: (angle / speed) * r
    "  mulss  %xmm0, %xmm1\n"
    "  jmpq   *" PF_SYM(pathfix_back_tpcost) "(%rip)\n");
void pathfix_stub_tpcost(void);
// site 9's code: entered by jmp from 0x101e82dfc ([rbp-0x50] = L, [rbp-0xd0] = dx, [rbp-0xa0] = dy); leaves xmm2/xmm1 =
// the direction as Windows computes it, back to 0x101e82e17 (which reloads xmm0).
__attribute__((used)) uint64_t pathfix_back_line = 0;
__asm__(".text\n"
    ".globl " PF_SYM(pathfix_stub_line) "\n" PF_SYM(pathfix_stub_line) ":\n"
    "  movss  " PF_SYM(pathfix_one) "(%rip), %xmm0\n"
    "  divss  -0x50(%rbp), %xmm0\n"                 // 1/L
    "  movaps -0xd0(%rbp), %xmm2\n"
    "  mulss  %xmm0, %xmm2\n"                       // dx * (1/L)
    "  movaps -0xa0(%rbp), %xmm1\n"
    "  mulss  %xmm0, %xmm1\n"                       // dy * (1/L)
    "  jmpq   *" PF_SYM(pathfix_back_line) "(%rip)\n");
void pathfix_stub_line(void);
