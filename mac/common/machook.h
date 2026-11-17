// machook.h - the shared base of the Mac add-ons (native Dawn of War II: Retribution, Feral 1.0.3, x86_64, macOS).
//
// One add-on = one dylib = one tick (docs/ADDON_RULES.md), loaded by the launcher with DYLD_INSERT_LIBRARIES after
// crossplay_fix.dylib (dow2-launcher/Dow2-mac-crossplay), from ~/Library/Application Support/EliteLauncher/mac_addons/<id>/.
// What this header gives each add-on:
//   - mh_start(name): its folder (dladdr of the dylib itself): <Name>.log and <Name>.ini live next to the dylib, not in /tmp;
//     [<Name>] Enabled=0 in the ini turns it off as on Windows.
//   - the game check: only the binary this was made for - Mac 1.0.3, LC_UUID AE6A4E52-9F4B-339E-A87A-EBD97FD77848. The game
//     code is addressed by the names of the 1.0.3 symbol map (macsyms; the binary exports only ~11k C/ObjC names, none of the
//     C++ game classes, so dlsym cannot find them) and every site is also checked byte by byte before it is touched.
//   - protfix: Feral's anti-modification check (CFeralGame::DoAMTask, every 120 s) sees ANY changed code and then ends the
//     game 15-18 min later. crossplay_fix (protfix, v24+) stops that by putting a jump over DoAMTask's entry. A Mac add-on
//     therefore changes nothing unless that jump is already there (crossplay_fix loaded before it and its protfix on):
//     DYLD_INSERT_LIBRARIES runs the constructors in list order and the launcher puts crossplay_fix first.
//   - mh_write: bytes into the game (code or vtables): vm_protect with VM_PROT_COPY, the old protection put back.
// Nothing here touches the simulation; add-ons must not either (crossplay sync) unless through normal player orders.
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define MH_IMAGE_LO 0x100000000UL           // the game's __TEXT .. end of __LINKEDIT in the 1.0.3 binary (unslid)
#define MH_IMAGE_HI 0x103ffac68UL
static const uint8_t MH_UUID[16] = { 0xAE, 0x6A, 0x4E, 0x52, 0x9F, 0x4B, 0x33, 0x9E, 0xA8, 0x7A, 0xEB, 0xD9, 0x7F, 0xD7, 0x78, 0x48 };
#define MH_DOAMTASK 0x100898450UL           // CFeralGame::DoAMTask (crossplay_fix protfix jumps over it)

static char mh_name[64], mh_logp[1024], mh_inip[1024];
static uintptr_t mh_slide;

static void mh_log(const char *fmt, ...) {
    FILE *f = fopen(mh_logp[0] ? mh_logp : "/dev/null", "a"); if (!f) return;
    time_t t = time(0); struct tm tm; localtime_r(&t, &tm);
    fprintf(f, "%02d:%02d:%02d ", tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static void mh_hex(char *o, const void *p, int n) { const uint8_t *b = p; for (int i = 0; i < n; i++) sprintf(o + i * 3, "%02X ", b[i]); if (n) o[n * 3 - 1] = 0; }

static int mh_sp(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
// <Name>.ini next to the dylib, the Windows format: [Section] key=value, ';' comments, names without case
static int mh_ini_str(const char *sec, const char *key, char *out, int n) {
    FILE *f = fopen(mh_inip, "r"); if (!f) return 0;
    char ln[512]; int in = 0, found = 0;
    while (fgets(ln, sizeof ln, f)) {
        char *s = ln; while (mh_sp(*s)) s++;
        char *e = s + strlen(s); while (e > s && mh_sp(e[-1])) *--e = 0;
        if (*s == ';' || *s == '#' || !*s) continue;
        if (*s == '[') { char *r = strchr(s, ']'); if (r) *r = 0; in = !strcasecmp(s + 1, sec); continue; }
        char *eq = strchr(s, '='); if (!in || !eq) continue;
        char *k = s, *ke = eq; while (ke > k && mh_sp(ke[-1])) ke--; *ke = 0;
        if (strcasecmp(k, key)) continue;
        char *v = eq + 1; while (mh_sp(*v)) v++;
        snprintf(out, n, "%s", v); found = 1;
    }
    fclose(f); return found;
}
static int mh_ini_int(const char *sec, const char *key, int def) {
    char v[64]; return mh_ini_str(sec, key, v, sizeof v) ? (int)strtol(v, 0, 0) : def;
}

static void *mh_at(uintptr_t unslid) { return (void *)(unslid + mh_slide); }
static int mh_same(uintptr_t unslid, const void *b, int n) { return !memcmp(mh_at(unslid), b, n); }

// bytes into the game image; prot = what the page gets back (code: READ|EXECUTE, __DATA: READ|WRITE)
static int mh_write(void *p, const void *b, int n, int prot) {
    vm_address_t lo = (vm_address_t)p & ~(vm_address_t)(vm_page_size - 1), hi = ((vm_address_t)p + n + vm_page_size - 1) & ~(vm_address_t)(vm_page_size - 1);
    if (vm_protect(mach_task_self(), lo, hi - lo, 0, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) return 0;
    memcpy(p, b, n);
    vm_protect(mach_task_self(), lo, hi - lo, 0, prot);
    return 1;
}
#define MH_CODE (VM_PROT_READ | VM_PROT_EXECUTE)
#define MH_DATA (VM_PROT_READ | VM_PROT_WRITE)

// the 1.0.3 binary? (its LC_UUID)
static int mh_game_103(void) {
    const struct mach_header_64 *h = (const struct mach_header_64 *)_dyld_get_image_header(0);
    if (!h || h->magic != MH_MAGIC_64) return 0;
    const struct load_command *lc = (const struct load_command *)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; i++, lc = (const struct load_command *)((const char *)lc + lc->cmdsize))
        if (lc->cmd == LC_UUID) return !memcmp(((const struct uuid_command *)lc)->uuid, MH_UUID, 16);
    return 0;
}
// crossplay_fix's protfix in place: DoAMTask starts with "jmp qword [rip+0]" to code outside the game image
static int mh_protfix_on(void) {
    const uint8_t *p = mh_at(MH_DOAMTASK);
    if (p[0] != 0xFF || p[1] != 0x25 || p[2] || p[3] || p[4] || p[5]) return 0;
    uint64_t to; memcpy(&to, p + 6, 8);
    return to < MH_IMAGE_LO + mh_slide || to >= MH_IMAGE_HI + mh_slide;
}

// constructor start of every add-on: folder, log, ini, Enabled, game build, protfix. 1 = go on and patch.
static int mh_start(const char *name, int version) {
    snprintf(mh_name, sizeof mh_name, "%s", name);
    Dl_info di; char dir[900] = "";
    if (dladdr((const void *)&mh_start, &di) && di.dli_fname) {
        snprintf(dir, sizeof dir, "%s", di.dli_fname); char *s = strrchr(dir, '/'); if (s) s[1] = 0; else dir[0] = 0;
    }
    snprintf(mh_logp, sizeof mh_logp, "%s%s.log", dir, name);
    snprintf(mh_inip, sizeof mh_inip, "%s%s.ini", dir, name);
    mh_slide = (uintptr_t)_dyld_get_image_vmaddr_slide(0);
    if (!mh_ini_int(name, "Enabled", 1)) { mh_log("%s %d (Mac): disabled in %s.ini", name, version, name); return 0; }
    if (!mh_game_103()) { mh_log("%s %d (Mac): not the Mac game 1.0.3 this was made for (LC_UUID differs) - not changing anything", name, version); return 0; }
    if (!mh_protfix_on()) {
        mh_log("%s %d (Mac): crossplay_fix (its protfix) is not active in this game - not changing anything (Feral's integrity check "
               "would end the game 15-18 min after start). crossplay_fix.dylib must come first in DYLD_INSERT_LIBRARIES.", name, version);
        return 0;
    }
    mh_log("%s %d (Mac): game 1.0.3, crossplay_fix protfix active, slide %#lx, folder %s", name, version, (unsigned long)mh_slide, dir[0] ? dir : "?");
    return 1;
}
