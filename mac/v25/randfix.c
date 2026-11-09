// randfix.c - crossplay_fix v8, part 6: the simulation's random floats exactly as Windows computes them.
// Why: Windows DoW2 runs the simulation with the x87 precision control at 24 bits (DOW2.exe sets _PC_24 and resets it
// before every GameObj::Simulate), and its random floats are x87 code: Util Random::GetUnitInclusive = fild h (+2^32) *
// 1/(2^32-1), SimEngine SimManager::GetRandRange = lo + (hi - lo) * u - every step rounded to 24 bits. The Mac port does
// h / 4294967295.0 and the range in SSE2 doubles. The float results differ in 24% of the formation-drift angles
// (GetRandRange(0, 2pi), ElementExt::CalculateFormationDrift) -> squads drift apart -> the element split and the
// random-call count differ -> sync error in the first seconds (seen 02.11.2026: Mac Rosetta vs Windows, frames 48 / 81).
// What: the float tails after Random::hashRandom in SimManager::GetRand and SimManager::GetRandRange jump to stubs that
// compute the Windows value (win24.h, checked bit-exact against real x87 at PC24 on 30k inputs). DOW2_NORANDFIX=1: off.
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "win24.h"

__attribute__((used)) float randfix_getrand(uint32_t h) { return w24_unit_inclusive(h); }
__attribute__((used)) float randfix_range(uint32_t h, double lo, double hi) { return w24_rand_range(h, lo, hi); }
__attribute__((used)) uint64_t randfix_back_getrand, randfix_back_range;

// entered by jmp from inside the game's function: eax = hashRandom(), rbp = that function's frame
__asm__(
    ".text\n"
    ".globl _randfix_stub_getrand\n"
    "_randfix_stub_getrand:\n"
    "  movl %eax, %edi\n"
    "  pushq %rbp\n  movq %rsp, %rbp\n  andq $-16, %rsp\n"
    "  call _randfix_getrand\n"                       // -> xmm0 (float)
    "  movq %rbp, %rsp\n  popq %rbp\n"
    "  jmpq *_randfix_back_getrand(%rip)\n"
    ".globl _randfix_stub_range\n"
    "_randfix_stub_range:\n"
    "  movl %eax, %edi\n"
    "  movsd -0x30(%rbp), %xmm0\n"                     // lo (the 1st double argument, saved by GetRandRange)
    "  movsd -0x28(%rbp), %xmm1\n"                     // hi
    "  pushq %rbp\n  movq %rsp, %rbp\n  andq $-16, %rsp\n"
    "  call _randfix_range\n"
    "  movq %rbp, %rsp\n  popq %rbp\n"
    "  jmpq *_randfix_back_range(%rip)\n");
void randfix_stub_getrand(void), randfix_stub_range(void);

static void rlog(const char *s, unsigned long a) {
    FILE *f = fopen("/tmp/elitefix.log", "a"); if (f) { fprintf(f, s, a); fputc('\n', f); fclose(f); }
}
static const struct site { const char *name; uint64_t addr, back; unsigned char want[44]; int len; void (*stub)(void); uint64_t *backvar; } SITES[] = {
    {"SimManager::GetRand", 0x10216d89fULL, 0x10216d8b2ULL,
     {0x89,0xc0,0xf2,0x48,0x0f,0x2a,0xc0,0xf2,0x0f,0x5e,0x05,0xca,0x68,0x2f,0x00,0xf2,0x0f,0x5a,0xc0}, 19,
     randfix_stub_getrand, &randfix_back_getrand},
    {"SimManager::GetRandRange", 0x10216db6cULL, 0x10216db98ULL,
     {0x89,0xc0,0xf2,0x48,0x0f,0x2a,0xc0,0xf2,0x0f,0x5e,0x05,0xfd,0x65,0x2f,0x00,0xf2,0x0f,0x10,0x4d,0xd8,0xf2,0x0f,0x10,0x55,
      0xd0,0xf2,0x0f,0x5c,0xca,0xf2,0x0f,0x59,0xc8,0xf2,0x0f,0x58,0xca,0x0f,0x57,0xc0,0xf2,0x0f,0x5a,0xc1}, 44,
     randfix_stub_range, &randfix_back_range},
};

__attribute__((constructor)) static void randfix_init(void) {
    if (getenv("DOW2_NORANDFIX")) { rlog("randfix: DOW2_NORANDFIX set - the port's own random floats%.0lu", 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    size_t n = sizeof SITES / sizeof SITES[0];
    for (size_t i = 0; i < n; i++)   // all or nothing: another game build is left alone
        if (memcmp((void *)(SITES[i].addr + slide), SITES[i].want, SITES[i].len)) {
            rlog("randfix: unexpected bytes at site %lu (another game build?) - NOT patched", (unsigned long)i);
            return;
        }
    int done = 0;
    for (size_t i = 0; i < n; i++) {
        const struct site *S = &SITES[i];
        unsigned char *p = (unsigned char *)(S->addr + slide);
        *S->backvar = S->back + slide;
        unsigned char code[44]; memset(code, 0x90, sizeof code);                // jmp qword [rip+0]; .quad stub; nops
        code[0] = 0xFF; code[1] = 0x25; memset(code + 2, 0, 4);
        uint64_t to = (uint64_t)S->stub; memcpy(code + 6, &to, 8);
        vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
        if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
            rlog("randfix: vm_protect failed%.0lu", 0); continue; }
        memcpy(p, code, S->len);
        vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
        done++;
    }
    // self-test against values from real x87 at PC24 (Intel, Wine): h, lo, hi -> GetRandRange, GetRand bits
    static const struct { uint32_t h; double lo, hi; uint32_t r, u; } T[] = { {3355305989u, 0x1.999999999999ap-4, 0x1.999999999999ap-3, 0x3e366590u, 0x3f47fde8u},
        {815623033u, 0x1.999999999999ap-4, 0x1.999999999999ap-3, 0x3df3b123u, 0x3e4275aeu},
        {2636933101u, -0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x3e696340u, 0x3f1d2c68u},
        {2978295591u, -0x1.3a8ed7c3a416ep+4, -0x1.4776ac8621724p+5, 0xc209a551u, 0x3f31852fu},
        {1945598122u, 0x0.0p+0, 0x1.921fb60000000p+2, 0x403628f9u, 0x3ee7eef1u},
        {271291275u, -0x1.0000000000000p+0, 0x1.0000000000000p+0, 0xbf5fa8d9u, 0x3d815c9cu},
        {1401419283u, 0x1.2fa6e80831bdap+5, 0x1.3ec7229c08750p+3, 0x41e6937eu, 0x3ea70fecu},
        {1003500358u, 0x1.999999999999ap-4, 0x1.999999999999ap-3, 0x3dfca690u, 0x3e6f40cdu},
        {1964216887u, -0x1.6666666666666p-1, 0x1.2666666666666p+1, 0x3f2c0783u, 0x3eea2724u},
        {4001557567u, -0x1.6666666666666p-1, 0x1.2666666666666p+1, 0x40061564u, 0x3f6e82ecu},
        {986407526u, 0x0.0p+0, 0x1.0000000000000p+0, 0x3e6b2d8au, 0x3e6b2d8au},
        {1249418419u, 0x1.941c47a507c20p+2, -0x1.39a89153f5c3ep+5, 0xc0ddb328u, 0x3e94f139u},
        {3981615681u, -0x1.827cd4cb44debp+5, 0x1.27d601781be68p+4, 0x4159db92u, 0x3f6d52a2u},
        {2147483648u, 0x0.0p+0, 0x1.921fb60000000p+2, 0x40490fdbu, 0x3f000000u},
        {374281491u, 0x1.9e4176cf65638p+3, 0x1.bd9e0c36fb594p+4, 0x4163e92du, 0x3db278a9u},
        {0u, -0x1.6666666666666p-1, 0x1.2666666666666p+1, 0xbf333333u, 0x00000000u},
        {763121736u, -0x1.3f29b746e78d4p+5, -0x1.6015d4c1818a5p+5, 0xc222819bu, 0x3e35f141u},
        {3143461704u, 0x0.0p+0, 0x1.3333340000000p-2, 0x3e60d681u, 0x3f3b5d6bu},
        {1695882304u, -0x1.0000000000000p+0, 0x1.0000000000000p+0, 0xbe57571cu, 0x3eca2a39u},
        {3712286006u, -0x1.921f9f01b866ep+1, 0x1.921f9f01b866ep+1, 0x401281dau, 0x3f5d44fdu},
        {804478983u, -0x1.6666666666666p-1, 0x1.2666666666666p+1, 0xbe0d644du, 0x3e3fcd80u},
        {2478023641u, -0x1.921f9f01b866ep+1, 0x1.921f9f01b866ep+1, 0x3ef7943cu, 0x3f13b3a4u},
        {2621360980u, -0x1.1ae9c20eacf84p+5, -0x1.76221c1160c20p+2, 0xc18ac8fcu, 0x3f1c3ecbu},
        {319034450u, 0x1.6000000000000p+2, 0x1.1400000000000p+4, 0x40cbedfeu, 0x3d9820a3u},
        {2672110599u, 0x1.b514671134860p+3, -0x1.8397bde3df224p+5, 0xc1c7da52u, 0x3f1f452cu},
        {1658274117u, 0x1.17449fb17d640p+3, 0x1.09e32b8975060p+5, 0x419184ebu, 0x3ec5ae83u},
        {980899077u, -0x1.0000000000000p+0, 0x1.0000000000000p+0, 0xbf0b1156u, 0x3e69dd54u},
        {2701436821u, 0x0.0p+0, 0x1.3333340000000p-2, 0x3e4138cau, 0x3f2104a8u},
        {2072391340u, 0x0.0p+0, 0x1.3333340000000p-2, 0x3e143a9fu, 0x3ef70c5du},
        {166200089u, 0x0.0p+0, 0x1.0000000000000p+0, 0x3d1e8032u, 0x3d1e8032u},
        {2561803281u, 0x1.4753bf3e527b4p+4, -0x1.84d46b53d641ep+4, 0xc0c7ac36u, 0x3f18b204u},
        {980954811u, -0x1.03a73d87138ddp+5, 0x1.6090b9136f864p+4, 0xc1a0164du, 0x3e69e0bbu},
        {2638411376u, 0x1.999999999999ap-4, 0x1.999999999999ap-3, 0x3e254dfcu, 0x3f1d42f6u},
        {1156179109u, 0x1.4de35d537eaa8p+5, -0x1.d9a2404233f66p+4, 0x41b44200u, 0x3e89d3c9u},
        {757004223u, -0x1.921f9f01b866ep+1, 0x1.921f9f01b866ep+1, 0xc0022f9cu, 0x3e347bdfu},
        {565125450u, -0x1.6666666666666p-1, 0x1.2666666666666p+1, 0xbe9c4b9eu, 0x3e06bc85u},
        {4211187188u, 0x0.0p+0, 0x1.0000000000000p+0, 0x3f7b019eu, 0x3f7b019eu},
        {2510151829u, 0x1.6000000000000p+2, 0x1.1400000000000p+4, 0x4145dff1u, 0x3f159de1u},
        {2039219966u, -0x1.0000000000000p+0, 0x1.0000000000000p+0, 0xbd4e7f20u, 0x3ef3180eu},
        {0u, 0x1.6000000000000p+2, 0x1.1400000000000p+4, 0x40b00000u, 0x00000000u},
        {1369684368u, 0x0.0p+0, 0x1.0000000000000p+0, 0x3ea34773u, 0x3ea34773u},
        {2345461709u, -0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x3dbcce80u, 0x3f0bcce8u},
        {3165769412u, 0x1.6000000000000p+2, 0x1.1400000000000p+4, 0x41629294u, 0x3f3cb1cfu},
        {1511115941u, -0x1.0000000000000p+0, 0x1.0000000000000p+0, 0xbe97b8ceu, 0x3eb42399u},
        {3741351418u, 0x0.0p+0, 0x1.0000000000000p+0, 0x3f5f007eu, 0x3f5f007eu},
        {2147164390u, -0x1.921f9f01b866ep+1, 0x1.921f9f01b866ep+1, 0xb9f4d01cu, 0x3efff642u},
        {2502586802u, 0x0.0p+0, 0x1.921fb60000000p+2, 0x406a4f21u, 0x3f152a72u},
        {2746220611u, 0x0.0p+0, 0x1.921fb60000000p+2, 0x40808f53u, 0x3f23b000u},
        {3127625596u, 0x0.0p+0, 0x1.0000000000000p+0, 0x3f3a6bc7u, 0x3f3a6bc7u},
        {2495173613u, 0x1.6000000000000p+2, 0x1.1400000000000p+4, 0x4145381au, 0x3f14b954u},
        {359841907u, 0x1.999999999999ap-4, 0x1.999999999999ap-3, 0x3dddf567u, 0x3dab9604u},
        {95737686u, 0x0.0p+0, 0x1.921fb60000000p+2, 0x3e0f6af2u, 0x3cb69aebu},
        {3349843187u, 0x0.0p+0, 0x1.3333340000000p-2, 0x3e6f9977u, 0x3f47aa8du},
        {870050475u, -0x1.86f448cf34721p+5, -0x1.3e5907bb5b1e0p+4, 0xc22c0030u, 0x3e4f6fabu},
        {3995614238u, -0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x3f5c5078u, 0x3f6e283cu},
        {1738418785u, 0x1.999999999999ap-4, 0x1.999999999999ap-3, 0x3e0fd8deu, 0x3ecf3c55u},
        {3780726212u, -0x1.ba452b5d076e0p+2, 0x1.e994d92eed0a4p+4, 0x41d0dccbu, 0x3f61594eu},
        {1359300665u, 0x0.0p+0, 0x1.3333340000000p-2, 0x3dc27314u, 0x3ea20a90u},
        {3054657036u, -0x1.69c6627676600p-2, 0x1.0dad2eb590b6ep+5, 0x41befb86u, 0x3f36125eu},
        {726403161u, 0x0.0p+0, 0x1.0000000000000p+0, 0x3e2d3021u, 0x3e2d3021u},
        {919468193u, 0x0.0p+0, 0x1.0000000000000p+0, 0x3e5b37e3u, 0x3e5b37e3u},
        {2886850673u, -0x1.921f9f01b866ep+1, 0x1.921f9f01b866ep+1, 0x3f8a72edu, 0x3f2c11d8u},
        {2683393977u, 0x1.6000000000000p+2, 0x1.1400000000000p+4, 0x414d753cu, 0x3f1ff158u},
        {1194532247u, 0x1.999999999999ap-4, 0x1.999999999999ap-3, 0x3e02e13fu, 0x3e8e663bu} };
    int bad = 0;
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        float r = w24_rand_range(T[i].h, T[i].lo, T[i].hi), u = w24_unit_inclusive(T[i].h); uint32_t rb, ub;
        memcpy(&rb, &r, 4); memcpy(&ub, &u, 4); if (rb != T[i].r || ub != T[i].u) bad++;
    }
    rlog(done == (int)n ? "randfix: OK - GetRand and GetRandRange compute Windows' x87 (24-bit) values%.0lu" : "randfix: only partly patched%.0lu", 0);
    rlog(bad ? "randfix self-test: %lu values differ from Windows x87 - report this" : "randfix self-test: all values bit-exact with Windows x87%.0lu", (unsigned long)bad);
}
