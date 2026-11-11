// CtrlAttack.dll - hold Ctrl and right-click the ground: the selected squads that can attack the ground fire at that point
// (Dawn of War II: Retribution 3.19, Elite and vanilla). Only while the key is held - it is not a toggle.
//
// The game has it (DOW2.exe 3.19.10320, 11.11.2026): the core action "Attack ground" (BindingAttackGround, vftable
// 0x0111977C, icon UI/Command/attack_ground, hotkey "attackground" = G in taskbar/keydefaults.lua). Its click handler
// 0x00B70610 takes the selected squads that pass the game's own check 0x00B70240 (EAX = squad: alive and spawned, not
// garrisoned/retreating, and some weapon of its models has weapon_bag behaviour attack_ground = true - attribute read at
// 0x00820C77 into weapon+0xEC, tested by 0x008615D0) and whose weapon's minimum range doesn't cover the point (0x00B70290,
// stdcall(squad, Vector3)), and sends one ordinary order: WorldDoCommandSquads(SCMD_Attack = 48, queued, player, squads)
// with the point packed as its SingleTargetData (0x007ED520, ESI = the command), through CommandProxy +0x5C (0x00AF58F0).
// That is a normal player order: it goes to every machine of the match and into the replay, simulated the same everywhere.
//
// CtrlAttack makes Ctrl + right-click do the same without pressing G first: the right-click order is
// CommandProxy::DefaultAction (vftable slot +0x74 of CommandProxy 0x0110E514 and EventGeneratorCommandProxy 0x01117704,
// the HUD's: 0x00AF62E0, thiscall group, Target*, ?, Player*, queued). While the key is held and the target is a ground
// position, the squads of the group that pass the game's check get the game's attack-ground order exactly as the button
// sends it (Shift = queued, as for any order); all the others get the game's own right-click order for the rest of the
// group (0x00AF62E0 itself - so Reverse.dll, which patches that function's first bytes, still backs them off). A squad that
// can attack the ground but is too close to the point (inside its minimum range) gets no order, as with the button.
// Vehicles and Reverse: when Reverse.dll is loaded with the same key (Reverse.ini [Reverse] Key), vehicles
// (sbps ...\vehicles\...) keep Reverse's back-off even if they can attack the ground (VehiclesReverse=1); without Reverse a
// vehicle that can attack the ground does.
// Nothing changes when the key is not held, the target is a unit/building, in replays (game type 0x800) or for squads that
// are not the local player's. Nothing in the simulation is written: only orders the player could give with G + click.
//
// CtrlAttack.ini [CtrlAttack]: Enabled=1, Key=Ctrl (Ctrl|Alt|<virtual key code>), VehiclesReverse=1, Verbose=0;
// re-read on every right-click with the key held. CtrlAttack.log: version, every checked hook byte, every order given.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define CA_VERSION    1
#define VT_PROXY      0x0110E514u                    // CommandProxy vftable
#define VT_EVPROXY    0x01117704u                    // EventGeneratorCommandProxy (derived; the HUD's)
#define SLOT_DEFACT   0x74                           // DefaultAction (the right-click order)
#define ORIG_DEFACT   0x00AF62E0u
#define SLOT_SEND     0x5C                           // send a squad command (0x00AF58F0), as the attack-ground button does
#define FN_CANAG      0x00B70240u                    // EAX = squad -> AL: the game's "can attack the ground" check
#define FN_MINRANGE   0x00B70290u                    // stdcall(squad, Vector3) -> AL 1: the point is not inside the min range
#define FN_PACKTGT    0x007ED520u                    // ESI = WorldCommand, [esp+4] = SingleTargetData*, ret 4
#define AT_BTNSEND    0x00B7081Du                    // push 0x30; call [WorldDoCommandSquads] in the button's handler
#define IAT_SQUADS    0x00F88BECu                    // DOW2.exe import slot of WorldDoCommandSquads
#define SCMD_ATTACK   48
#define G_SETTINGS    0x01335A78u                    // +0x118 game type: 0x800 = playback
#define G_WORLD       0x013356A4u                    // +0x194 PlayerManager
#define G_LOCALID     0x013356A8u                    // **: object whose vftable +8 returns the local player id (0x00867550)
#define T_PLAYBACK    0x800
#define MAXSQ         64

typedef struct { float x, y, z; } Vec3;
typedef void  (__fastcall *DefAct_t)(void *self, void *edx, void *group, void *target, int p4, void *player, int queued);
typedef void  (__fastcall *Send_t)(void *self, void *edx, void *cmd);
typedef int   (__fastcall *I0_t)(void *obj, void *edx);
typedef void *(__fastcall *P0_t)(void *obj, void *edx);
typedef void  (__fastcall *V0_t)(void *obj, void *edx);
typedef void  (__fastcall *V1_t)(void *obj, void *edx, const void *a);
typedef void  (__fastcall *GrpCtor_t)(void *grp, void *edx, int mostDerived);
typedef Vec3 *(__fastcall *TargetPos_t)(void *target, void *edx, Vec3 *out);
typedef const char *(__fastcall *Name_t)(const void *pbg, void *edx);
typedef void *(__fastcall *PlayerById_t)(void *mgr, void *edx, DWORD id);
typedef void *(__stdcall *CmdGrp_t)(DWORD type, int queued, void *player, void *group);
typedef unsigned char (__stdcall *MinRange_t)(void *squad, Vec3 pos);

static I0_t p_ttype; static TargetPos_t p_tpos;
static P0_t p_ebegin, p_eend, p_esquad, p_sowner, p_sbp; static Name_t p_fname;
static GrpCtor_t p_egctor; static V0_t p_egdtor; static V1_t p_epush;
static V0_t p_sgctor, p_sgdtor; static V1_t p_spush;
static V0_t p_tdctor, p_tddtor; static V1_t p_treset; static PlayerById_t p_pbyid;
static DefAct_t o_defact;

static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH];
static int g_enabled = 1, g_key = VK_CONTROL, g_vehrev = 1, g_verbose = 0, g_installed;

static void logmsg(const char *fmt, ...) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%lsCtrlAttack.log", g_dir);
    FILE *f = _wfopen(p, L"a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc('\n', f); fclose(f);
}
static int parse_key(const wchar_t *k) {
    int v = !lstrcmpiW(k, L"Alt") ? VK_MENU : !lstrcmpiW(k, L"Ctrl") ? VK_CONTROL : (int)wcstol(k, NULL, 0);
    return (v <= 0 || v > 254) ? VK_CONTROL : v;
}
static void load_ini(void) {
    wchar_t k[16];
    g_enabled = GetPrivateProfileIntW(L"CtrlAttack", L"Enabled", 1, g_ini);
    GetPrivateProfileStringW(L"CtrlAttack", L"Key", L"Ctrl", k, 16, g_ini); g_key = parse_key(k);
    g_vehrev = GetPrivateProfileIntW(L"CtrlAttack", L"VehiclesReverse", 1, g_ini);
    g_verbose = GetPrivateProfileIntW(L"CtrlAttack", L"Verbose", 0, g_ini);
}
static int rd(const void *p, unsigned n) { return p && !IsBadReadPtr(p, n); }

// Reverse.dll in this game with the same key (its first bytes jump on 0x00AF62E0): vehicles are its business
static int reverse_same_key(void) {
    HMODULE m = GetModuleHandleW(L"Reverse.dll"); if (!m) return 0;
    if (*(unsigned char *)ORIG_DEFACT != 0xE9) return 0;
    wchar_t p[MAX_PATH], k[16]; GetModuleFileNameW(m, p, MAX_PATH); wchar_t *s = wcsrchr(p, L'\\'); if (s) s[1] = 0; else p[0] = 0;
    wcsncat(p, L"Reverse.ini", MAX_PATH - wcslen(p) - 1);
    GetPrivateProfileStringW(L"Reverse", L"Key", L"Ctrl", k, 16, p);
    return parse_key(k) == g_key;
}
static int is_replay(void) {
    char *st = rd((void *)G_SETTINGS, 4) ? *(char **)G_SETTINGS : NULL;
    return rd(st + 0x118, 4) && (*(DWORD *)(st + 0x118) & T_PLAYBACK);
}
static void *local_player(void) {                    // as the game's own 0x00867550; NULL when it can't tell
    char *w = rd((void *)G_WORLD, 4) ? *(char **)G_WORLD : NULL;
    void **pp = rd((void *)G_LOCALID, 4) ? *(void ***)G_LOCALID : NULL;
    void *o = rd(pp, 4) ? *pp : NULL;
    if (!w || !rd(o, 4) || !rd(*(void **)o, 12)) return NULL;
    DWORD id = (DWORD)((I0_t)(*(void ***)o)[2])(o, NULL);
    return p_pbyid((char *)w + 0x194, NULL, id);
}
static int can_attack_ground(void *sq) {
    unsigned r;
    __asm__ volatile ("call *%2" : "=a"(r) : "a"(sq), "r"((DWORD)FN_CANAG) : "ecx", "edx", "memory", "cc");
    return (r & 0xFF) != 0;
}
static const DWORD g_fnpack = FN_PACKTGT;
static void pack_target(void *cmd, void *td) {
    __asm__ volatile ("pushl %%esi\n\tmovl %0, %%esi\n\tpushl %1\n\tcall *%2\n\tpopl %%esi"
                      : : "b"(cmd), "D"(td), "m"(g_fnpack) : "eax", "ecx", "edx", "memory", "cc");
}
static const char *sq_name(void *sq) {
    const void *bp = p_sbp ? p_sbp(sq, NULL) : NULL; const char *s = (bp && p_fname) ? p_fname(bp, NULL) : NULL;
    return rd(s, 1) ? s : "?";
}
static int is_vehicle(void *sq) { return strstr(sq_name(sq), "\\vehicles\\") != NULL; }

// the entities of an EntityGroupTemp: its ObservableContainer<Entity const*> is the virtual base (offset in the vbtable)
static void *ent_cont(void *group) { int *vbt = *(int **)group; return rd(vbt, 8) ? (char *)group + vbt[1] : NULL; }

static void attack(void *self, void *player, void **sq, int n, const Vec3 *pos, int queued) {
    static unsigned char sg[0x200], td[0x100];
    memset(sg, 0, sizeof sg); memset(td, 0, sizeof td);
    p_sgctor(sg, NULL);
    for (int i = 0; i < n; i++) p_spush(sg, NULL, &sq[i]);
    p_tdctor(td, NULL); p_treset(td, NULL, pos);
    void *cmd = (*(CmdGrp_t *)IAT_SQUADS)(SCMD_ATTACK, queued ? 1 : 0, player, sg);
    if (cmd) { pack_target(cmd, td); ((Send_t)(*(void ***)self)[SLOT_SEND / 4])(self, NULL, cmd); }
    p_tddtor(td, NULL); p_sgdtor(sg, NULL);
    if (!cmd) logmsg("the game made no attack order (WorldDoCommandSquads = NULL)");
}

static void __fastcall hk_defact(void *self, void *edx, void *group, void *target, int p4, void *player, int queued) {
    if (!g_installed || !(GetAsyncKeyState(g_key) & 0x8000) || !group || !target || !player || p_ttype(target, NULL) != 2) {
        o_defact(self, edx, group, target, p4, player, queued); return;
    }
    load_ini();
    if (!g_enabled || is_replay()) {
        if (g_verbose) logmsg("%s - the game's order untouched", g_enabled ? "replay" : "Enabled=0");
        o_defact(self, edx, group, target, p4, player, queued); return;
    }
    void *lp = local_player();
    if (lp && lp != player) { if (g_verbose) logmsg("order of another player (%p, local %p) - untouched", player, lp); o_defact(self, edx, group, target, p4, player, queued); return; }
    Vec3 pos; p_tpos(target, NULL, &pos);
    void *cont = ent_cont(group); if (!cont) { o_defact(self, edx, group, target, p4, player, queued); return; }
    void **b = p_ebegin(cont, NULL), **e = p_eend(cont, NULL);
    void *sq[MAXSQ]; int route[MAXSQ], nsq = 0;  // route: 1 attack, 2 too close (no order), 0 the game's order, 3 the game's order (a vehicle: Reverse)
    int vrev = g_vehrev && reverse_same_key();
    for (void **it = b; it && it < e; it++) {
        if (!*it) continue;
        void *s = p_esquad(*it, NULL); if (!s) continue;
        int k = 0; while (k < nsq && sq[k] != s) k++;
        if (k < nsq || nsq >= MAXSQ) continue;
        sq[nsq] = s; route[nsq] = 0;
        if (p_sowner(s, NULL) == player && can_attack_ground(s))
            route[nsq] = (vrev && is_vehicle(s)) ? 3 : ((MinRange_t)FN_MINRANGE)(s, pos) ? 1 : 2;
        nsq++;
    }
    void *att[MAXSQ]; int na = 0, nclose = 0, nrest = 0;
    for (int i = 0; i < nsq; i++) { if (route[i] == 1) att[na++] = sq[i]; else if (route[i] == 2) nclose++; else nrest++; }
    if (!na && !nclose) {                            // nobody here attacks the ground: the plain right-click (and Reverse)
        if (g_verbose) logmsg("no squad here attacks the ground (%d squads%s) - the game's order", nsq, vrev ? ", vehicles: Reverse" : "");
        o_defact(self, edx, group, target, p4, player, queued); return;
    }
    if (na) attack(self, player, att, na, &pos, queued);
    if (nrest) {                                     // the others: the game's own right-click order (Reverse sees it too)
        static unsigned char grp[0x200];
        memset(grp, 0, sizeof grp);
        p_egctor(grp, NULL, 1);
        void *rc = ent_cont(grp);
        for (void **it = b; it && it < e; it++) {
            if (!*it) continue;
            void *s = p_esquad(*it, NULL); int k = 0; while (k < nsq && sq[k] != s) k++;
            if (!s || k >= nsq || route[k] == 0 || route[k] == 3) p_epush(rc, NULL, it);
        }
        o_defact(self, edx, grp, target, p4, player, queued);
        p_egdtor(grp, NULL);
    }
    char names[400]; names[0] = 0;
    for (int i = 0; i < nsq; i++) {
        const char *nm = sq_name(sq[i]), *sl = strrchr(nm, '\\'); char one[96];
        snprintf(one, sizeof one, "%s%s:%s", i ? ", " : "", sl ? sl + 1 : nm, route[i] == 1 ? "attack" : route[i] == 2 ? "too close" : route[i] == 3 ? "vehicle, Reverse" : "game order");
        strncat(names, one, sizeof names - strlen(names) - 1);
    }
    logmsg("attack ground at (%.1f, %.1f, %.1f)%s: %d squad(s) attack, %d too close, %d the game's order%s - %s", pos.x, pos.y, pos.z,
           queued ? " queued" : "", na, nclose, nrest, vrev ? " (vehicles: Reverse)" : "", names);
}

// ---- install (checked bytes / slots; nothing is hooked when one doesn't match)
static int bytes_ok(DWORD at, const char *hex, const char *what) {
    unsigned char want[32]; int n = 0;
    for (const char *s = hex; s[0] && s[1] && n < 32; s += 2) { unsigned v; sscanf(s, "%2x", &v); want[n++] = (unsigned char)v; }
    if (IsBadReadPtr((void *)at, n)) { logmsg("%s: %08lX unreadable - not hooking", what, (unsigned long)at); return 0; }
    if (memcmp((void *)at, want, n)) {
        char got[80]; for (int i = 0; i < n; i++) sprintf(got + 2 * i, "%02x", ((unsigned char *)at)[i]);
        logmsg("%s: %08lX holds %s, not %s (not 3.19?) - not hooking", what, (unsigned long)at, got, hex); return 0;
    }
    logmsg("%s: %08lX = %s ok", what, (unsigned long)at, hex);
    return 1;
}
static int slot_ok(DWORD at, DWORD expect, const char *what) {
    if (IsBadReadPtr((void *)at, 4)) { logmsg("%s: %08lX unreadable - not hooking", what, (unsigned long)at); return 0; }
    if (*(DWORD *)at != expect) { logmsg("%s: %08lX holds %08lX, not %08lX (hooked by another add-on or not 3.19) - not hooking", what, (unsigned long)at, (unsigned long)*(DWORD *)at, (unsigned long)expect); return 0; }
    logmsg("%s: %08lX -> %08lX ok", what, (unsigned long)at, (unsigned long)expect);
    return 1;
}
static void put32(DWORD at, DWORD v) { DWORD o; VirtualProtect((void *)at, 4, PAGE_EXECUTE_READWRITE, &o); *(DWORD *)at = v; VirtualProtect((void *)at, 4, o, &o); FlushInstructionCache(GetCurrentProcess(), (void *)at, 4); }
static void *sim(HMODULE m, const char *name) { void *p = (void *)GetProcAddress(m, name); if (!p) logmsg("SimEngine.dll: %s not found", name); return p; }
static int install(void) {
    HMODULE m = GetModuleHandleA("SimEngine.dll"); if (!m) { logmsg("SimEngine.dll not loaded - not hooking"); return 0; }
    p_ttype  = (I0_t)       sim(m, "?GetType@ConstTarget@@QBE?AW4TargetType@1@XZ");
    p_tpos   = (TargetPos_t)sim(m, "?GetPosition@ConstTarget@@QBE?BVVector3f@Math@@XZ");
    p_ebegin = (P0_t)       sim(m, "?begin@?$ObservableContainer@PBVEntity@@$00@ast@@QAEPAPBVEntity@@XZ");
    p_eend   = (P0_t)       sim(m, "?end@?$ObservableContainer@PBVEntity@@$00@ast@@QAEPAPBVEntity@@XZ");
    p_epush  = (V1_t)       sim(m, "?push_back@?$ObservableContainer@PBVEntity@@$00@ast@@QAEXABQBVEntity@@@Z");
    p_egctor = (GrpCtor_t)  sim(m, "??0EntityGroupTemp@@QAE@XZ");
    p_egdtor = (V0_t)       sim(m, "??_DEntityGroupTemp@@QAEXXZ");      // with its virtual base (the entity list)
    p_esquad = (P0_t)       sim(m, "?GetSquad@Entity@@QAEPAVSquad@@XZ");
    p_sowner = (P0_t)       sim(m, "?GetOwner@Squad@@QAEPAVPlayer@@XZ");
    p_sbp    = (P0_t)       sim(m, "?GetBlueprint@Squad@@QBEPBVPropertyBagGroup@@XZ");
    p_fname  = (Name_t)     sim(m, "?GetFilename@PropertyBagGroup@@QBEPBDXZ");
    p_sgctor = (V0_t)       sim(m, "??0ConstSquadGroupTemp@@QAE@XZ");
    p_sgdtor = (V0_t)       sim(m, "??1ConstSquadGroupTemp@@UAE@XZ");
    p_spush  = (V1_t)       sim(m, "?push_back@?$ObservableContainer@PBVSquad@@$00@ast@@QAEXABQBVSquad@@@Z");
    p_tdctor = (V0_t)       sim(m, "??0SingleTargetData@@QAE@XZ");
    p_tddtor = (V0_t)       sim(m, "??1SingleTargetData@@QAE@XZ");
    p_treset = (V1_t)       sim(m, "?Reset@ConstTarget@@QAEXABVVector3f@Math@@@Z");
    p_pbyid  = (PlayerById_t)sim(m, "?GetPlayerFromID@PlayerManager@@QAEPAVPlayer@@K@Z");
    void *wdcs = sim(m, "?WorldDoCommandSquads@@YGPAVWorldCommand@@K_NPBVPlayer@@ABVConstSquadGroupTemp@@@Z");
    if (!p_ttype || !p_tpos || !p_ebegin || !p_eend || !p_epush || !p_egctor || !p_egdtor || !p_esquad || !p_sowner || !p_sbp || !p_fname ||
        !p_sgctor || !p_sgdtor || !p_spush || !p_tdctor || !p_tddtor || !p_treset || !p_pbyid || !wdcs) return 0;
    if (IsBadReadPtr((void *)IAT_SQUADS, 4)) { logmsg("import slot %08X unreadable - not hooking", IAT_SQUADS); return 0; }
    // the slot holds SimEngine's function, or another add-on's hook of it (JumpRetry watches it): either is fine to call
    logmsg("WorldDoCommandSquads: import slot %08X -> %08lX (SimEngine %p)", IAT_SQUADS, (unsigned long)*(DWORD *)IAT_SQUADS, wdcs);
    if (!bytes_ok(FN_CANAG, "568bf08b464c85c0", "can attack ground check") ||
        !bytes_ok(FN_MINRANGE, "64a1000000006aff68d9a2f100", "attack ground min range check") ||
        !bytes_ok(FN_PACKTGT, "558bec83e4f86aff68cc2bec00", "command target pack") ||
        !bytes_ok(AT_BTNSEND, "6a30ff15ec8bf800", "attack ground button: SCMD_Attack order") ||
        !slot_ok(VT_PROXY + SLOT_SEND, 0x00AF58F0u, "CommandProxy send squad order") ||
        !slot_ok(VT_PROXY + SLOT_DEFACT, ORIG_DEFACT, "CommandProxy default action") ||
        !slot_ok(VT_EVPROXY + SLOT_DEFACT, ORIG_DEFACT, "EventGeneratorCommandProxy default action")) return 0;
    if (*(unsigned char *)ORIG_DEFACT == 0xE9) logmsg("default action 0x00AF62E0 starts with a jump (Reverse.dll?) - the rest of an order goes through it");
    o_defact = (DefAct_t)ORIG_DEFACT;
    put32(VT_PROXY + SLOT_DEFACT, (DWORD)hk_defact);
    put32(VT_EVPROXY + SLOT_DEFACT, (DWORD)hk_defact);
    return 1;
}

static DWORD WINAPI worker(LPVOID _) {
    (void)_;
    for (int i = 0; i < 120 && !GetModuleHandleA("SimEngine.dll"); i++) Sleep(500);
    g_installed = install();
    logmsg("CtrlAttack %d start: hold %s and right-click the ground - the squads that can attack the ground fire there - %s%s", CA_VERSION,
           g_key == VK_CONTROL ? "Ctrl" : g_key == VK_MENU ? "Alt" : "the key", g_installed ? "installed" : "NOT installed",
           g_enabled ? "" : " (Enabled=0 in the ini for now)");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID _) {
    (void)_;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    GetModuleFileNameW(h, g_dir, MAX_PATH); wchar_t *s = wcsrchr(g_dir, L'\\'); if (s) s[1] = 0; else g_dir[0] = 0;
    _snwprintf(g_ini, MAX_PATH, L"%lsCtrlAttack.ini", g_dir);
    // one copy per game process: it can come both from Elite_DLLs (launcher) and from dow2-patches\ (winmm loader)
    char mx[64]; wsprintfA(mx, "Local\\dow2patch_CtrlAttack_%lu", GetCurrentProcessId());
    HANDLE m = CreateMutexA(NULL, FALSE, mx);
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) { logmsg("another CtrlAttack copy is already active in this game, this one stays idle"); return TRUE; }
    load_ini();
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
    return TRUE;
}
