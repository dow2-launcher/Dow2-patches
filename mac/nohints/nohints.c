// NoHints.dylib - NoHints for the native Mac game (Feral 1.0.3): no beginner hints in a match ("select your squad",
// "build a generator", "capture the victory points"...). The same as the Windows NoHints.dll (nohints/nohints.c).
//
// The hints are started by simulation/scar/skirmish.scar only "if Misc_QuickTipsEnabled()"; that Lua function asks the
// player's own setting "ShowQuicktips" (not synchronised - players with it on and off already play the same matches).
// Mac 1.0.3: the function is 0x101ff6400 (31 bytes: SystemConfig::Instance, SystemConfig::GetSetting(Key), setne al; it is the
// one registered under "Misc_QuickTipsEnabled": lea rax,[0x101ff6400] at 0x101fedd58, the name at 0x1026764cf loaded at
// 0x101fedd70). Its first 3 bytes become "xor eax,eax; ret": it answers false and the hints never start. The setting itself is
// not changed. All 31 bytes and the registration are checked first; another build is left alone.
// NoHints.ini [NoHints]: Enabled=1 (as on Windows).
#include "../common/machook.h"

#define NH_VERSION 1
#define FN_QUICKTIPS 0x101ff6400UL
#define AT_REGISTER  0x101fedd58UL      // lea rax, [rip+0x86a1] -> FN_QUICKTIPS
#define AT_NAME      0x101fedd70UL      // lea rdx, [rip+0x688758] -> "Misc_QuickTipsEnabled"
#define STR_NAME     0x1026764cfUL

static const uint8_t ORIG[31] = {
    0x55, 0x48, 0x89, 0xE5,                         // push rbp; mov rbp,rsp
    0xE8, 0x57, 0xB1, 0x6D, 0xFF,                   // call SystemConfig::Instance (0x1016d1560)
    0x48, 0x8D, 0x35, 0x88, 0x9D, 0x59, 0x01,       // lea rsi, [Key "ShowQuicktips" 0x103590198]
    0x48, 0x89, 0xC7,                               // mov rdi, rax
    0xE8, 0x58, 0xB3, 0x6D, 0xFF,                   // call SystemConfig::GetSetting (0x1016d1770)
    0x85, 0xC0, 0x0F, 0x95, 0xC0,                   // test eax,eax; setne al
    0x5D, 0xC3 };                                   // pop rbp; ret
static const uint8_t REG[7]  = { 0x48, 0x8D, 0x05, 0xA1, 0x86, 0x00, 0x00 };
static const uint8_t NAME[7] = { 0x48, 0x8D, 0x15, 0x58, 0x87, 0x68, 0x00 };
static const uint8_t PATCH[3] = { 0x31, 0xC0, 0xC3 };   // xor eax,eax; ret

__attribute__((constructor)) static void nohints_init(void) {
    if (!mh_start("NoHints", NH_VERSION)) return;
    char h[100];
    if (mh_same(FN_QUICKTIPS, PATCH, 3) && !memcmp((uint8_t *)mh_at(FN_QUICKTIPS) + 3, ORIG + 3, sizeof ORIG - 3)) { mh_log("already patched (another NoHints copy in this game)"); return; }
    if (!mh_same(FN_QUICKTIPS, ORIG, sizeof ORIG) || !mh_same(AT_REGISTER, REG, 7) || !mh_same(AT_NAME, NAME, 7) || !mh_same(STR_NAME, "Misc_QuickTipsEnabled", 22)) {
        mh_hex(h, mh_at(FN_QUICKTIPS), 16); mh_log("Misc_QuickTipsEnabled is not where 1.0.3 has it (bytes at %#lx: %s) - not changing anything", FN_QUICKTIPS, h); return; }
    if (!mh_write(mh_at(FN_QUICKTIPS), PATCH, 3, MH_CODE)) { mh_log("vm_protect failed - not changed"); return; }
    mh_hex(h, mh_at(FN_QUICKTIPS), 8);
    mh_log("Misc_QuickTipsEnabled (%#lx) answers false: no beginner hints in matches (bytes now %s)", FN_QUICKTIPS, h);
}
