// CtrlAttack.dylib - Ctrl attack ground for the native Mac game (Feral 1.0.3): hold the key and LEFT-click the ground, the selected
// squads that can attack the ground fire at that point - the game's own Attack ground (G), without pressing G. The same as the
// Windows CtrlAttack.dll 2 (ctrlattack/ctrlattack.c), with one difference: the key is Option (Alt) by default, not Ctrl.
//
// Why not Ctrl on the Mac: Feral's input layer turns Control + left click into a RIGHT click before the game sees it
// (CHWindowEventHandler::PostMouseModifierMessage; the game's "momr" property = 0x9000, controlKey, written to MacDoze's
// MouseModifierRight by CMacGame::InitialiseInput at every start; Command + click is the middle button, "momm" = 0x100). The game
// gets a plain right click with Ctrl taken off - a move order - so Ctrl + left click can't be seen. Option is not taken by Feral
// and reaches the game as Alt (Plat::Input key 0x86). CtrlAttack.ini Key=Alt (default) | Ctrl | Shift-free Plat key number.
//
// How G works in the Mac game (1.0.3 symbol map; the "binding" UI of DoW2): BindingAttackGround (vtable 0x102df9530) is validated
// every UI update by Binding::Update -> its OnValidate (vtable +0x20, 0x1020819f0: selected squads that can attack the ground ->
// true, the binding's "active" byte +0x10). G (or the button) runs its OnExecute (0x102081d80) = BindingManager::SetModalBinding:
// WorldViewSM::NewStartModal puts the world view into modal mode (WorldViewSM +0x10 = 3). The next left button down (input action
// 0x13 = MouseLeft 0xAE, event type 3) goes to NewModalSM -> NewModalCL::ModalClick -> BindingManager::ModalCBClick (position,
// entity under the cursor, Shift = queued); BindingManager::ExecuteSimCommandQueue then calls the binding's OnExecuteModal, which
// sends the ordinary attack-ground order of the selected squads (CommandProxy, the same order as on Windows) and leaves the mode.
// CtrlAttack: SimVisPanel::OnInputEvent (vtable slot 0x102dfd480 -> WorldViewSM::HandleEvent) is wrapped. A left button down with
// the key held, while Attack ground is valid for the selection (OnValidate hooked in its vtable slot: binding, BindingContext and
// its answer, at most 1 s old), no modal binding active, the world view idle (state 0), the cursor inside the world and NOT over
// an entity (BindingContext +0x178, the game's own mouse-over: a click on a unit stays the game's own click) -> the binding's own
// OnExecute, i.e. exactly G, before the world view sees the click; the click itself then goes to Attack ground as after G. If the
// mode is still on 0.4 s after the button is up (no order: e.g. inside the minimum range), it is left the way the game leaves it
// on Esc / right click (BindingManager::ModalCBAbort). Everything else: the game's own. Replays / observers: OnValidate says no.
// Nothing in the simulation: the order is the game's own player order, as with G.
//
// CtrlAttack.ini [CtrlAttack]: Enabled=1 (0 at the start: nothing hooked; re-read every 2 s), Key=Alt, Verbose=0.
// CtrlAttack.log next to the dylib: start, every attack-ground click.
#include "../common/machook.h"
#include <mach/mach_time.h>

#define CA_VERSION 2
#define VT_AG_VALIDATE 0x102df9550UL     // BindingAttackGround vtable +0x20: OnValidate(this, ctx) -> bool
#define FN_AG_VALIDATE 0x1020819f0UL
#define VT_AG_EXECUTE  0x102df9578UL     // vtable +0x48: OnExecute (G)
#define FN_AG_EXECUTE  0x102081d80UL     // BindingAttackGround::OnExecute = BindingManager::SetModalBinding(mgr, this)
#define VT_SVP_INPUT   0x102dfd480UL     // SimVisPanel vtable: OnInputEvent(this, const InputEvent&) -> bool
#define FN_SVP_INPUT   0x1020e7c50UL     // = WorldViewSM::HandleEvent(this+0x58, ev)
#define FN_WVSM_EVENT  0x101fcfa70UL     // WorldViewSM::HandleEvent (bytes checked only)
#define AT_WVSM_STATE  0x101fcfb07UL     // mov eax,[r12+0x10]; cmp rax,6 - its state
#define AT_WVSM_MODAL  0x101fd030dUL     // NewStartModal: mov dword [rbx+0x10],3
#define FN_MODALSM     0x1020168a0UL     // NewModalSM::HandleEvent: event type 6 or 3, then action 0x13 -> ModalClick
#define AT_BIND_LMB    0x101a9b9f0UL     // InputBind::SetupDefaults: action 0x13 = key 0xAE (MouseLeft)
#define FN_SETMODAL    0x1020e39b0UL     // BindingManager::SetModalBinding (bytes checked only)
#define FN_ISMODAL     0x1020e3f40UL     // BindingManager::IsModalBinding: cmp [rdi+0x20],rsi
#define FN_ABORT       0x1020e3f60UL     // BindingManager::ModalCBAbort(mgr): leave modal mode (Esc / right click)
#define FN_GETMGR      0x1020df470UL     // Binding::GetBindingManager: mov rax,[rdi+8]
#define FN_GETACTIVE   0x1020df3b0UL     // Binding::GetActive: mov al,[rdi+0x10]
#define FN_MOUSEENT    0x1020e1ab0UL     // BindingContext::GetMouseOverEntity: [rdi+0x178]
#define FN_INWORLD     0x1020e1ac0UL     // BindingContext::IsMouseInsideWorld: [rdi+0x180]
#define FN_MOUSEPOS    0x1020e1aa0UL     // BindingContext::GetMouseOverPosition: rdi+0x16c
#define FN_KEYDOWN     0x10183fc80UL     // Plat::Input::IsKeyPressed(key) -> bool
#define EV_DOWN 3
#define EV_UP   4
#define KEY_MOUSELEFT 0xAE
#define KEY_CTRL 0x84
#define KEY_ALT  0x86

typedef char (*Validate_t)(void *self, void *ctx);
typedef char (*Input_t)(void *panel, const int32_t *ev);
typedef void (*Exec_t)(void *self);
typedef void (*Abort_t)(void *mgr);
typedef char (*KeyDown_t)(uint32_t key);
static Validate_t o_validate; static Input_t o_input; static Exec_t p_exec; static Abort_t p_abort; static KeyDown_t p_keydown;
static int g_enabled = 1, g_key = KEY_ALT, g_verbose;
static uint64_t g_iniAt, g_canAt, g_armed, g_upAt, g_orders;
static void *g_ag, *g_ctx; static int g_can;                   // from Attack ground's OnValidate (UI thread)
static float g_at[3]; static int g_queued;

static uint64_t now_ms(void) {                       // mach_absolute_time: there on every macOS (clock_gettime only from 10.12)
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    return mach_absolute_time() / 1000000 * tb.numer / tb.denom;
}
static void load_ini(void) {
    char k[32] = "Alt";
    g_enabled = mh_ini_int("CtrlAttack", "Enabled", 1);
    g_verbose = mh_ini_int("CtrlAttack", "Verbose", 0);
    mh_ini_str("CtrlAttack", "Key", k, sizeof k);
    int v = !strcasecmp(k, "Alt") || !strcasecmp(k, "Option") ? KEY_ALT : !strcasecmp(k, "Ctrl") || !strcasecmp(k, "Control") ? KEY_CTRL : (int)strtol(k, 0, 0);
    g_key = (v <= 0 || v >= 0xB8 || v == KEY_MOUSELEFT) ? KEY_ALT : v;
}
static const char *keyname(void) { return g_key == KEY_ALT ? "Option (Alt)" : g_key == KEY_CTRL ? "Ctrl" : "the key"; }
static void *modal_of(void *mgr) { return mgr ? *(void **)((char *)mgr + 0x20) : 0; }

// Attack ground's per-update validation: remember the binding, its context and the answer; leave a mode we started that gave no order
static char hk_validate(void *self, void *ctx) {
    char r = o_validate(self, ctx);
    uint64_t now = now_ms();
    g_ag = self; g_ctx = ctx; g_can = r; g_canAt = now;
    if (g_armed) {
        void *mgr = *(void **)((char *)self + 8);
        if (modal_of(mgr) != self) {                  // the click went through: the game left the mode after the order (or Esc)
            g_orders++;
            mh_log("%s + left click at (%.1f, %.1f)%s: attack ground (the game's own Attack ground) - %llu so far", keyname(), g_at[0], g_at[2],
                   g_queued ? " queued" : "", (unsigned long long)g_orders);
            g_armed = 0;
        } else if ((g_upAt && now - g_upAt > 400) || now - g_armed > 5000) {
            p_abort(mgr); g_armed = 0;                // no order (e.g. inside the minimum range): leave the mode as Esc does
            mh_log("%s + left click at (%.1f, %.1f): no attack-ground order (too close?) - mode left", keyname(), g_at[0], g_at[2]);
        }
    }
    return r;
}

static char hk_input(void *panel, const int32_t *ev) {
    if (ev && ev[2] == KEY_MOUSELEFT && ev[0] == EV_DOWN && !g_armed) {
        uint64_t now = now_ms();
        if (now - g_iniAt > 2000) { load_ini(); g_iniAt = now; }
        void *mgr = g_ag ? *(void **)((char *)g_ag + 8) : 0;
        char *wvsm = panel ? *(char **)((char *)panel + 0x58) : 0;
        if (g_enabled && mgr && g_ctx && wvsm && p_keydown(g_key) && !modal_of(mgr) && *(int32_t *)(wvsm + 0x10) == 0) {
            char *c = g_ctx;
            if (!g_can || now - g_canAt > 1000) { if (g_verbose) mh_log("%s + left click: no selected squad can attack the ground - the game's own click", keyname()); }
            else if (!c[0x180]) { if (g_verbose) mh_log("%s + left click outside the world - the game's own click", keyname()); }
            else if (*(void **)(c + 0x178)) { if (g_verbose) mh_log("%s + left click on a unit or building - the game's own click", keyname()); }
            else {
                memcpy(g_at, c + 0x16c, sizeof g_at); g_queued = p_keydown(0x85) != 0;
                p_exec(g_ag);                         // = G
                if (modal_of(mgr) == g_ag) { g_armed = now; g_upAt = 0; if (g_verbose) mh_log("%s + left click on the ground: Attack ground mode for this click", keyname()); }
                else mh_log("%s + left click: Attack ground did not take the mode - the game's own click", keyname());
            }
        }
    } else if (ev && ev[2] == KEY_MOUSELEFT && ev[0] == EV_UP && g_armed) g_upAt = now_ms();
    return o_input(panel, ev);
}

static int slot_is(uintptr_t slot, uintptr_t fn) { return *(uint64_t *)mh_at(slot) == fn + mh_slide; }
static int put_slot(uintptr_t slot, void *hk) { uint64_t v = (uint64_t)hk; return mh_write(mh_at(slot), &v, 8, MH_DATA); }

__attribute__((constructor)) static void ctrlattack_init(void) {
    if (!mh_start("CtrlAttack", CA_VERSION)) return;
    load_ini(); g_iniAt = now_ms();
    if (*(uint64_t *)mh_at(VT_SVP_INPUT) == (uint64_t)hk_input) { mh_log("already hooked"); return; }
    static const uint8_t VALIDATE[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50, 0x48, 0x89 };
    static const uint8_t EXECUTE[32] = { 0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x48, 0x89, 0xFB, 0xE8, 0xE2, 0xD6, 0x05, 0x00, 0x48, 0x89, 0xC7, 0x48, 0x89, 0xDE,
                                         0x48, 0x83, 0xC4, 0x08, 0x5B, 0x5D, 0xE9, 0x11, 0x1C, 0x06, 0x00, 0x90 };   // GetBindingManager; jmp SetModalBinding
    static const uint8_t SVPINPUT[14] = { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x7F, 0x58, 0x5D, 0xE9, 0x12, 0x7E, 0xEE, 0xFF };  // jmp WorldViewSM::HandleEvent
    static const uint8_t WVSM[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC };
    static const uint8_t WVSMSTATE[9] = { 0x41, 0x8B, 0x44, 0x24, 0x10, 0x48, 0x83, 0xF8, 0x06 };
    static const uint8_t WVSMMODAL[7] = { 0xC7, 0x43, 0x10, 0x03, 0x00, 0x00, 0x00 };
    static const uint8_t MODALSM[32] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x89, 0xF3, 0x49, 0x89, 0xFE, 0x8B, 0x03, 0x83, 0xF8, 0x06, 0x74, 0x05,
                                         0x83, 0xF8, 0x03, 0x75, 0x2A, 0x41, 0x83, 0x7E, 0x10, 0x01, 0x75, 0x27 };
    static const uint8_t BINDLMB[56] = { 0xC7, 0x45, 0xDC, 0x13, 0x00, 0x00, 0x00, 0xBF, 0x18, 0x00, 0x00, 0x00, 0xE8, 0xFB, 0xCC, 0x74, 0x00, 0x49, 0x89, 0xC7,
                                         0x4C, 0x89, 0xFF, 0xE8, 0xF4, 0x0B, 0x00, 0x00, 0xBF, 0x18, 0x00, 0x00, 0x00, 0xE8, 0xE6, 0xCC, 0x74, 0x00, 0x48, 0x89,
                                         0xC3, 0x48, 0x89, 0xDF, 0xE8, 0x7F, 0x0B, 0x00, 0x00, 0xBE, 0xAE, 0x00, 0x00, 0x00, 0x48, 0x89 };
    static const uint8_t SETMODAL[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0xF3, 0x49, 0x89 };
    static const uint8_t ISMODAL[12] = { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x39, 0x77, 0x20, 0x0F, 0x94, 0xC0, 0x5D };
    static const uint8_t ABORT[16] = { 0x55, 0x48, 0x89, 0xE5, 0x53, 0x50, 0x48, 0x89, 0xFB, 0x48, 0x83, 0x7B, 0x20, 0x00, 0x74, 0x28 };
    static const uint8_t GETMGR[10] = { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x47, 0x08, 0x5D, 0xC3 };
    static const uint8_t GETACTIVE[9] = { 0x55, 0x48, 0x89, 0xE5, 0x8A, 0x47, 0x10, 0x5D, 0xC3 };
    static const uint8_t MOUSEENT[12] = { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x8B, 0x87, 0x78, 0x01, 0x00, 0x00, 0x5D };
    static const uint8_t INWORLD[12] = { 0x55, 0x48, 0x89, 0xE5, 0x8A, 0x87, 0x80, 0x01, 0x00, 0x00, 0x5D, 0xC3 };
    static const uint8_t MOUSEPOS[12] = { 0x55, 0x48, 0x89, 0xE5, 0x48, 0x8D, 0x87, 0x6C, 0x01, 0x00, 0x00, 0x5D };
    static const uint8_t KEYDOWN[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x56, 0x53, 0x89, 0xF8, 0x80, 0x3D, 0x39, 0x09, 0xD3, 0x01, 0x00 };
    if (!slot_is(VT_AG_VALIDATE, FN_AG_VALIDATE) || !slot_is(VT_AG_EXECUTE, FN_AG_EXECUTE) || !slot_is(VT_SVP_INPUT, FN_SVP_INPUT)
        || !mh_same(FN_AG_VALIDATE, VALIDATE, 16) || !mh_same(FN_AG_EXECUTE, EXECUTE, 32) || !mh_same(FN_SVP_INPUT, SVPINPUT, 14)
        || !mh_same(FN_WVSM_EVENT, WVSM, 16) || !mh_same(AT_WVSM_STATE, WVSMSTATE, 9) || !mh_same(AT_WVSM_MODAL, WVSMMODAL, 7)
        || !mh_same(FN_MODALSM, MODALSM, 32) || !mh_same(AT_BIND_LMB, BINDLMB, 56) || !mh_same(FN_SETMODAL, SETMODAL, 16)
        || !mh_same(FN_ISMODAL, ISMODAL, 12) || !mh_same(FN_ABORT, ABORT, 16) || !mh_same(FN_GETMGR, GETMGR, 10)
        || !mh_same(FN_GETACTIVE, GETACTIVE, 9) || !mh_same(FN_MOUSEENT, MOUSEENT, 12) || !mh_same(FN_INWORLD, INWORLD, 12)
        || !mh_same(FN_MOUSEPOS, MOUSEPOS, 12) || !mh_same(FN_KEYDOWN, KEYDOWN, 16)) {
        mh_log("CtrlAttack %d: Attack ground / world view input code is not as in 1.0.3 (or hooked by another add-on) - NOT installed", CA_VERSION); return;
    }
    o_validate = (Validate_t)mh_at(FN_AG_VALIDATE); o_input = (Input_t)mh_at(FN_SVP_INPUT); p_exec = (Exec_t)mh_at(FN_AG_EXECUTE);
    p_abort = (Abort_t)mh_at(FN_ABORT); p_keydown = (KeyDown_t)mh_at(FN_KEYDOWN);
    if (!put_slot(VT_AG_VALIDATE, hk_validate) || !put_slot(VT_SVP_INPUT, hk_input)) { mh_log("vm_protect failed - hooks may be partial"); return; }
    mh_log("CtrlAttack %d: hold %s and LEFT-click the ground - the selected squads that can attack the ground fire there - installed "
           "(BindingAttackGround::OnValidate %#lx, SimVisPanel::OnInputEvent %#lx vtable slots)%s%s", CA_VERSION, keyname(), VT_AG_VALIDATE, VT_SVP_INPUT,
           g_key == KEY_CTRL ? " - NOTE: Feral turns Ctrl + left click into a right click, Key=Alt works" : "", g_enabled ? "" : " - Enabled=0 for now");
}
