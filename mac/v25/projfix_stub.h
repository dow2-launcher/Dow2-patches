// projfix_stub.h - code of the sites that do not fit in place.
// Site 12's code: entered by jmp from 0x102157dfa in Entity::SetPositionAndHeading with xmm3/xmm4/xmm2 = the
// heading x/y/z (also saved at [rbp-0x1c]/[rbp-0x20]/[rbp-0x24]) and xmm0 = the float argument. Does what the replaced
// instructions did (save xmm0, keep the squared length at [rbp-0x2c] for the port's NaN retry, sqrtf = sqrtss) with
// Windows' sum (z*z + y*y) + x*x, and jumps back to 0x102157e0c with xmm0 = the length.
#ifdef __APPLE__
#define PJ_SYM(x) "_" #x
#else
#define PJ_SYM(x) #x
#endif
__attribute__((used)) uint64_t projfix_back_heading = 0;
__asm__(".text\n"
    ".globl " PJ_SYM(projfix_stub_heading) "\n" PJ_SYM(projfix_stub_heading) ":\n"
    "  movss  %xmm0, -0x28(%rbp)\n"
    "  mulss  %xmm2, %xmm2\n"                       // z*z
    "  mulss  %xmm4, %xmm4\n"                       // y*y
    "  mulss  %xmm3, %xmm3\n"                       // x*x
    "  addss  %xmm4, %xmm2\n"
    "  addss  %xmm3, %xmm2\n"                       // (z*z + y*y) + x*x
    "  movss  %xmm2, -0x2c(%rbp)\n"
    "  sqrtss %xmm2, %xmm0\n"
    "  jmpq   *" PJ_SYM(projfix_back_heading) "(%rip)\n");
void projfix_stub_heading(void);
// Site 13's code: entered by jmp from 0x102168366 in EntityUtil::MoveEntityCloserToPoint with xmm4 = (dx, dy), xmm3 = dy,
// xmm2 = dz, xmm0 = the distance argument. s1 = (x*x + y*y) + z*z as the port (kept at [rbp-0x84] for "close enough"),
// s2 = (z*z + y*y) + x*x as Windows for the 0.01 test and the length. Then exactly what the port did next.
__attribute__((used)) uint64_t projfix_back_closer = 0, projfix_back_closer_setpos = 0;
__attribute__((used)) const float projfix_eps_closer = 0.01f;
__asm__(".text\n"
    ".globl " PJ_SYM(projfix_stub_closer) "\n" PJ_SYM(projfix_stub_closer) ":\n"
    "  movaps %xmm4, %xmm5\n  mulss %xmm5, %xmm5\n"            // x*x
    "  mulss  %xmm3, %xmm3\n"                                   // y*y
    "  movaps %xmm2, %xmm6\n  mulss %xmm6, %xmm6\n"            // z*z
    "  movaps %xmm3, %xmm1\n  addss %xmm5, %xmm1\n  addss %xmm6, %xmm1\n"   // s1 = (y*y + x*x) + z*z
    "  movaps %xmm6, %xmm7\n  addss %xmm3, %xmm7\n  addss %xmm5, %xmm7\n"   // s2 = (z*z + y*y) + x*x
    "  movss  " PJ_SYM(projfix_eps_closer) "(%rip), %xmm3\n"
    "  ucomiss %xmm7, %xmm3\n"
    "  jae    1f\n"
    "  movaps %xmm4, -0x70(%rbp)\n  movaps %xmm0, -0x80(%rbp)\n  movss %xmm2, -0x54(%rbp)\n  movss %xmm1, -0x84(%rbp)\n"
    "  sqrtss %xmm7, %xmm0\n"
    "  jmpq   *" PJ_SYM(projfix_back_closer) "(%rip)\n"
    "1:\n"
    "  jmpq   *" PJ_SYM(projfix_back_closer_setpos) "(%rip)\n");
void projfix_stub_closer(void);
