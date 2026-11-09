// v24 adds protfix.c (part 28: Feral's anti-modification timers - exit(1) / 0xcafebabe 15-23 min after start - never armed), artfix.c (part 29:
//   OffMapArtilleryActionState scatter points with DOW2.exe's x87 cos/sin, one rounding) and recipfix site 3 (DamageAction::PostLoadValidate 1/x).
// v23 adds recipfix.c (part 27: WeaponBag::PostLoadValidate scales by 1/x as DOW2.exe) and the 2nd holdfix copy (OnModEvent).
// v22 adds homingfix.c (part 25: ModProjectileDynamics::initHoming draws its random numbers in DOW2.exe order) and holdfix.c (part 26: combat-hold slot priority (scale * t) * 10 as DOW2.exe).
// v21 adds costfix.c (part 23: high-level pathfinder step cost PathHighEnvironment::ActualCost in DOW2.exe order) and fowcellfix.c (part 24: fog-of-war cell rounding as DOW2.exe 0x868330).
// v19 adds sortfix.c (part 21: WeaponUtil::FindWeaponTarget sorts its candidates with STLport std::sort as DOW2.exe - equal priorities in the Windows order).
// v18 adds walkfix.c (part 20: walkable-surface height / normal - CollisionModel::GetTriangleHeightNormal - as SimEngine.dll).
// v17 adds knockfix.c (part 19: knockback fly target and durations - PlotThrow, DoKnockback, flight duration - as DOW2.exe).
// v16 adds meleefix.c (part 18: the melee special attack chance, the accuracy power, the sync-kill transform as DOW2.exe).
// crossplay_fix.dylib v7 - the native Mac (Feral) Dawn of War II: Retribution plays with Windows players (vanilla and Elite).
// v12 adds crctrace.c (diagnostic, with RNGTRACE: per-frame CRC log /tmp/dow2_crc.log, the last 24 CRC blocks kept and
//    dumped on a sync error - pair of CrcTrace.dll on the Windows host) and combatfix.c (see there).
// v11 adds projfix.c (part 11: projectiles - the transform, missile flight, homing - and entity facings: 3-term sums of
//    squares added in Windows' MSVC /fp:fast order; the desync at frame 186 of match_1102_v10, an Ork stun grenade).
// v10 adds pathfix.c (part 10: ground units' turns - the S-turn's cosine, the turn costs - and the line walk through the cells
//    rounded as Windows' MSVC /fp:fast build rounds them: x*(1/L) for x/L, (angle/speed)*r; the desync at frame 200 of match_1102_v9).
// v9 adds heapfix.c (part 7: the pathfinder's open list breaks ties as Windows' STLport heap - the starting squad spawned on other
//    cells and turned around), terrainfix.c (part 8: terrain height/normal summed in Windows' order) and luanumfix.c (part 9:
//    numbers in Lua text - simbox scales, scripts - parsed as Windows' x87 at 24 bits).
// v8 adds randfix.c (part 6: the simulation's random floats as Windows' x87 at 24-bit precision computes them).
// v7 adds mathfix.c (part 5: Windows' msvcr80 math instead of the port's own libm - the desync at the first move order).
// Build: build.sh (osxcross on rts)
// Load: DYLD_INSERT_LIBRARIES=elitefix.dylib (see run_elite.command). Log: /tmp/elitefix.log
//  1. modDllCRC: the native port advertises 0 (no SimEngine.dll), Windows the CRC of its SimEngine.dll (0xBB60C7FC), so the
//     join check never passes. GameApp::UpdateVersionInfo: "mov dword [rbp-0x38], 0" at 0x10001a327 -> the Windows value.
//  2. An Elite model overruns a fixed buffer of Feral's material loader (__memcpy_chk aborts the game): clamp the copy.
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
static void logf_(const char *s, unsigned long a, unsigned long b) {
    FILE *f = fopen("/tmp/elitefix.log", "a"); if (f) { fprintf(f, s, a, b); fputc('\n', f); fclose(f); }
}
void *__memcpy_chk(void *dst, const void *src, size_t len, size_t dstlen);
static void *my_memcpy_chk(void *dst, const void *src, size_t len, size_t dstlen) {
    if (len > dstlen) { static int n; if (n++ < 20) logf_("memcpy_chk clamp %lu -> %lu", len, dstlen); len = dstlen; }
    return memmove(dst, src, len);
}
__attribute__((used, section("__DATA,__interpose"))) static struct { const void *repl, *orig; } interpose_memcpy_chk =
    { (const void *)my_memcpy_chk, (const void *)__memcpy_chk };

__attribute__((constructor)) static void elitefix_init(void) {
    uintptr_t slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    unsigned char *p = (unsigned char *)(0x10001a327UL + slide);
    static const unsigned char want[7] = {0xC7, 0x45, 0xC8, 0, 0, 0, 0};
    if (memcmp(p, want, 7)) { logf_("modDllCRC: unexpected bytes at %#lx (slide %#lx), not patched", (unsigned long)p, slide); return; }
    vm_address_t page = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
        logf_("modDllCRC: vm_protect failed at %#lx%lu", (unsigned long)p, 0); return; }
    uint32_t v = 0xBB60C7FCu; memcpy(p + 3, &v, 4);
    vm_protect(mach_task_self(), page, vm_page_size * 2, 0, VM_PROT_READ | VM_PROT_EXECUTE);
    logf_("modDllCRC patched at %#lx -> %#lx", (unsigned long)p, v);
#ifdef RNGTRACE
    void rngtrace_init(void (*)(const char *, unsigned long, unsigned long));
    rngtrace_init(logf_);
    void crctrace_init(void);
    crctrace_init();
#endif
}

// 3. log what the game sends to Relic's servers (to find why Mac players land in a separate chat / game list):
//    every URL and POST body passed to libcurl, auth and session values masked. /tmp/dow2_net.log
typedef void CURL;
extern int curl_easy_setopt(CURL *, int, ...);
static void masked(FILE *f, const char *s) {
    for (const char *p = s; *p; ) {
        const char *k = 0; static const char *keys[] = {"auth=", "sessionID=", "ticket=", "session_id=", 0};
        for (int i = 0; keys[i]; i++) if (!strncmp(p, keys[i], strlen(keys[i]))) { k = keys[i]; break; }
        if (k) { fputs(k, f); fputs("***", f); p += strlen(k); while (*p && *p != '&' && *p != '"') p++; continue; }
        fputc(*p++, f);
    }
}
static int my_curl_easy_setopt(CURL *c, int opt, ...) {
    va_list ap; va_start(ap, opt); int r;
    if (opt >= 30000) { long long v = va_arg(ap, long long); r = curl_easy_setopt(c, opt, v); }
    else if (opt >= 10000) {
        void *v = va_arg(ap, void *);
        // 4. Relic's servers put clients into chat channels and game lists by majorVersion: Windows sends "0.0.0", the Mac port
        //    "5.0.0". Only in the URLs going to Relic (the literal itself is used elsewhere too - patching it broke Steam online)
        //    Login also says installationType "mac" where Windows says "client" (with 0.0.0 + mac the login kept failing).
        if (opt == 10002 && v && strstr((const char *)v, "reliclink.com")) {
            const char *in = (const char *)v; size_t L = strlen(in);
            char *u = malloc(L + 16); char *o = u; int changed = 0;
            for (const char *p = in; *p; ) {
                if (!strncmp(p, "majorVersion=5.0.0", 18)) { memcpy(o, "majorVersion=0.0.0", 18); o += 18; p += 18; changed = 1; }
                else if (!strncmp(p, "installationType=mac", 20) && (p[20] == '&' || !p[20])) { memcpy(o, "installationType=client", 23); o += 23; p += 20; changed = 1; }
                else *o++ = *p++;
            }
            *o = 0;
            if (changed) { static int n; if (n++ < 3) logf_("Relic request: majorVersion 0.0.0, installationType client%lu%lu", 0, 0); v = u; } else free(u);
        }
        r = curl_easy_setopt(c, opt, v);
        if ((opt == 10002 || opt == 10015 || opt == 10165) && v) {          // URL, POSTFIELDS, COPYPOSTFIELDS
            FILE *f = fopen("/tmp/dow2_net.log", "a");
            if (f) { fputs(opt == 10002 ? "URL  " : "POST ", f); masked(f, (const char *)v); fputc('\n', f); fclose(f); }
        }
    } else { long v = va_arg(ap, long); r = curl_easy_setopt(c, opt, v); }
    va_end(ap); return r;
}
__attribute__((used, section("__DATA,__interpose"))) static struct { const void *repl, *orig; } interpose_curl_setopt =
    { (const void *)my_curl_easy_setopt, (const void *)curl_easy_setopt };
