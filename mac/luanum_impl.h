// luanum_impl.h - Relic's Lua number parser (lua_str2number) as Windows DoW2 computes it: LuaConfig.dll + 0xe931 runs it on
// the x87 with the precision control at 24 bits (DOW2.exe sets _PC_24), so every step is rounded to a 24-bit mantissa:
//   int part  b = b*10 + d          (left to right)
//   fraction  a = a*0.1 + d/10      (right to left; 0.1 is the double 0x3fb999999999999a)
//   result    (b + a) * sign
// The Mac port (_luaRelic_str2number 0x101419950) is the same code in SSE2 doubles (53 bits) -> data read from Lua text
// differs by an ulp: simbox scale 1.236 -> Windows 0x3f9e3540, Mac 0x3f9e353f (11 of 125 scale values on 1v1typhonlowlands).
// Emulation: + - and / of 24-bit values are rounded to double and then to 24 bits (53 >= 2*24+2, so this is exact);
// a*0.1 has a 53-bit factor, so its 77-bit product is rounded from integers.
#include <stdint.h>
#include <string.h>

static inline double ln_r24(double x) {              // round a double to 24 significant bits, ties to even
    uint64_t u; memcpy(&u, &x, 8);
    if ((u & 0x7ff0000000000000ull) == 0 || (u & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) return x;
    uint64_t rem = u & 0x1fffffffull; u &= ~0x1fffffffull;
    if (rem > 0x10000000ull || (rem == 0x10000000ull && (u & 0x20000000ull))) u += 0x20000000ull;
    memcpy(&x, &u, 8); return x;
}

static inline double ln_mul_tenth_r24(double a) {    // a (>= 0, 24 bits) * 0.1, rounded once to 24 bits
    if (a == 0) return 0;
    uint64_t ua; memcpy(&ua, &a, 8);
    int ea = (int)((ua >> 52) & 0x7ff); uint64_t ma = (ua & 0xfffffffffffffull) | 0x10000000000000ull;   // a = ma * 2^(ea-1075)
    const uint64_t mt = 0x1999999999999aull; const int et = 0x3fb;                                        // 0.1 = mt * 2^(et-1075)
    unsigned __int128 p = (unsigned __int128)ma * mt;                                                     // a*0.1 = p * 2^(ea+et-2150)
    int top = 127; while (!((p >> top) & 1)) top--;
    int drop = top - 23;                                                                                  // keep 24 bits
    unsigned __int128 keep = p >> drop, rem = p & (((unsigned __int128)1 << drop) - 1), half = (unsigned __int128)1 << (drop - 1);
    if (rem > half || (rem == half && (keep & 1))) keep++;
    double r = (double)(uint64_t)keep;                                                                    // exact (<= 2^24)
    int e2 = ea + et - 2150 + drop;
    return __builtin_ldexp(r, e2);                                                                        // exact
}

// the Windows value of Relic's lua_str2number; same scanning as the port (sign, digits, '.', digits; *end = after them)
__attribute__((used, visibility("default"))) double luanum_str2number(const char *s, char **end) {
    double sign = 1.0;
    if (*s == '-') { sign = -1.0; s++; } else if (*s == '+') s++;
    const char *ip = s; while (*s >= '0' && *s <= '9') s++;
    const char *ie = s, *fp = s, *fe = s;
    if (*s == '.') { fp = ++s; while (*s >= '0' && *s <= '9') s++; fe = s; }
    if (end) *end = (char *)fe;
    double b = 0, a = 0;
    for (const char *p = ip; p < ie; p++) b = ln_r24(ln_r24(b * 10.0) + (double)(*p - '0'));
    for (const char *p = fe; p > fp; ) { p--; a = ln_r24(ln_mul_tenth_r24(a) + ln_r24((double)(*p - '0') / 10.0)); }
    return ln_r24(ln_r24(b + a) * sign);
}
