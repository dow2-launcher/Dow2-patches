// luanumfix.c - crossplay_fix v9, part 9: numbers in Lua text read as Windows reads them.
// Why: Relic's lua_str2number (Mac _luaRelic_str2number 0x101419950, Windows LuaConfig.dll + 0xe931) builds the number digit
// by digit; on Windows on the x87 at 24-bit precision, on the Mac in SSE2 doubles. Simbox files (art/*.simbox, Lua text) give
// every map object its scale: "y = 1.236" -> Windows 0x3f9e3540, Mac 0x3f9e353f. At frame 0 of match_1102_v8: 141 entities
// with a different scale (11 of 125 distinct values), all reproduced by the Windows algorithm. The same parser reads SCAR/AI
// script constants. What: the function jumps to luanum_str2number (luanum_impl.h), checked bit for bit against the real
// LuaConfig.dll code under Wine at PC24 on 51834 strings (all 12199 numerals of the game's Lua files + random ones),
// including the end pointer. DOW2_NOLUANUMFIX=1: off.
#include "patch_util.h"
#include "luanum_impl.h"

static const uint64_t SITE = 0x101419950;
static const unsigned char WANT[36] = { 0x55,0x48,0x89,0xe5,0x0f,0xb6,0x07,0xf2,0x0f,0x10,0x05,0x41,0xbf,0x04,0x01,0x83,0xf8,0x2b,
    0x74,0x15,0x0f,0xb6,0xc0,0x83,0xf8,0x2d,0x75,0x10,0x48,0xff,0xc7,0xf2,0x0f,0x10,0x05,0xd9 };
static const struct { const char *s; uint64_t v; int end; } T[] = {      // from LuaConfig.dll on a real x87 (Wine), PC24
    {"1.236", 0x3ff3c6a800000000ull, 5}, {"-0", 0x8000000000000000ull, 2}, {"-0.001", 0xbf50624dc0000000ull, 6},
    {"9036661", 0x41613c6ea0000000ull, 7}, {"628430332081.875187568068806", 0x42624a2bc0000000ull, 28},
    {"+7.9658", 0x401fdcfaa0000000ull, 7}, {"-558688932982.520147768", 0xc260428e80000000ull, 23},
    {"67330127.87", 0x41900d8140000000ull, 11}, {"-823.333", 0xc089baaa00000000ull, 8}, {"+191.4", 0x4067ecccc0000000ull, 6},
    {"43135044.846243164", 0x4184918220000000ull, 18}, {".5abc", 0x3fe0000000000000ull, 2},
    {"16078.844883367556087", 0x40cf676c20000000ull, 21}, {"71141255.316848", 0x4190f61e20000000ull, 15} };

__attribute__((constructor)) static void luanumfix_init(void) {
    if (getenv("DOW2_NOLUANUMFIX")) { pu_log("luanumfix: DOW2_NOLUANUMFIX set - the port's own Lua number parser%.0lu%.0lu", 0, 0); return; }
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(SITE + slide);
    if (memcmp(p, WANT, sizeof WANT)) { pu_log("luanumfix: unexpected bytes at %#lx (another game build?) - NOT patched%.0lu", (unsigned long)SITE, 0); return; }
    unsigned long bad = 0;
    for (size_t k = 0; k < sizeof T / sizeof T[0]; k++) {
        char *end; double v = luanum_str2number(T[k].s, &end); uint64_t b; memcpy(&b, &v, 8);
        if (b != T[k].v || end - T[k].s != T[k].end) bad++;
    }
    if (bad) { pu_log("luanumfix self-test: %lu values differ from Windows x87 - NOT patched, report this%.0lu", bad, 0); return; }
    pu_log(pu_jump(p, (void *)luanum_str2number) ? "luanumfix: OK - Lua numbers parsed as Windows (x87, 24-bit)%.0lu%.0lu"
                                                 : "luanumfix: vm_protect failed%.0lu%.0lu", 0, 0);
}
