// ReinforceFix.dylib - Reinforce fix for the native Mac game (Feral 1.0.3): a squad that goes into an Eldar webway gate (or
// anything else that takes it off the map for a while) no longer comes out with auto-reinforce off. The same as the Windows
// ReinforceFix.dll (reinforcefix/reinforcefix.c).
//
// Auto-reinforce is client-side: OverwatchManager::UpdateReinforceOverwatch (Mac 0x101fcdf20, Windows FUN_00B26570) walks the
// HUD's list of squad ids every tick and sends a normal reinforce command for each. The walk drops a squad when it no longer
// exists (UIGroupUtil::ConvertIDToSquad gives 0) OR when its entity container is empty. Windows calls
// ObservableContainer<Entity>::empty there; the Mac port inlines it:
//     0x101fcdfa8  mov rax,[r13+0x88]; mov rax,[rax-0x18]; cmp qword [r13+rax+0xe8],0; je 0x101fce2d0 (drop it)
// A squad in a webway gate keeps its id but has no entities while inside, so it is dropped and comes out without auto-reinforce.
// Fix: the "empty" case goes to the loop step (0x101fce340: add r15,4) instead of the drop: the 4-byte target of that je
// (0x101fcdfbe) becomes 0x37e. Squads that are really gone are still dropped by the first check. The reinforce commands are
// the game's own player orders - no sync impact. ReinforceFix.ini [ReinforceFix]: Enabled=1 (as on Windows).
#include "../common/machook.h"

#define RF_VERSION 1
#define AT_CHECK  0x101fcdfa8UL
#define AT_REL    0x101fcdfbeUL          // rel32 of the je
#define LOOP_STEP 0x101fce340UL
static const uint8_t ORIG[26] = { 0x49, 0x8B, 0x85, 0x88, 0x00, 0x00, 0x00,          // mov rax,[r13+0x88]
                                  0x48, 0x8B, 0x40, 0xE8,                            // mov rax,[rax-0x18]
                                  0x49, 0x83, 0xBC, 0x05, 0xE8, 0x00, 0x00, 0x00, 0x00, // cmp qword [r13+rax+0xe8],0
                                  0x0F, 0x84, 0x0E, 0x03, 0x00, 0x00 };              // je 0x101fce2d0
static const uint8_t STEP[11] = { 0x49, 0x83, 0xC7, 0x04, 0x4C, 0x3B, 0xBD, 0x20, 0xFF, 0xFF, 0xFF };   // add r15,4; cmp r15,[rbp-0xe0]

__attribute__((constructor)) static void reinforcefix_init(void) {
    if (!mh_start("ReinforceFix", RF_VERSION)) return;
    int32_t rel = (int32_t)(LOOP_STEP - (AT_REL + 4));
    char h[100];
    if (mh_same(AT_CHECK, ORIG, 22) && mh_same(AT_REL, &rel, 4)) { mh_log("already patched (another ReinforceFix copy in this game)"); return; }
    if (!mh_same(AT_CHECK, ORIG, sizeof ORIG) || !mh_same(LOOP_STEP, STEP, sizeof STEP)) {
        mh_hex(h, mh_at(AT_CHECK), 26); mh_log("the auto-reinforce walk is not where 1.0.3 has it (bytes at %#lx: %s) - not patching", AT_CHECK, h); return; }
    if (!mh_write(mh_at(AT_REL), &rel, 4, MH_CODE)) { mh_log("vm_protect failed - not patched"); return; }
    mh_log("ReinforceFix %d: squads off the map (webway gate) keep auto-reinforce (OverwatchManager::UpdateReinforceOverwatch, je at %#lx -> loop step %#lx)",
           RF_VERSION, AT_REL - 2, LOOP_STEP);
}
