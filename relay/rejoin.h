// rejoin.h - Relay 24 (06.11.2026): back into a running match of OUR server after a network drop (included once by relay.c).
//
// 1. The game's own relay client, BattleServerRelay::Process (DOW2.exe 3.19, 0x63B910, ebx = the relay object, eax = ms since
//    the relay last sent anything): silence > [obj+0x20] (RL_ClientReconnectMS, 15000) and reconnects on (byte [obj+0x1C]) ->
//    it connects again with "15 sid 0 id 02 <ticks got>" and the server sends what it missed (dow2-ladder battle_relay, 06.11);
//    silence > 40000 (cmp eax, 9C40h at 0x63B979) -> the match is given up ("Lost connection to Relay ... forcing Disconnect").
//    For a game whose relay is our node (g_native): reconnect after ReconnectSec (5), give up after RejoinSec (180) - the cmp
//    calls rejoin_stub, which sets the flags as the cmp did against our number. Relic's relays keep 15 s / 40 s.
// 2. The server's notices (0x7e, "DWR5" streams only): number, seconds left, seconds away, name -> g_wait, shown by the
//    minimap panel's per-frame update (vtable 0x011160D8 slot 4, chained) in a text field of its own movie, above the
//    minimap: "Ждём <name>… 0:23 (ещё 2:37)" (short: the minimap movie is ~200 px wide). A notice older than 3 s = not shown.
#include "../common/gfxui.h"

#define RJ_CMP_AT   0x0063B979      // 3D 40 9C 00 00   cmp eax, 40000
#define RJ_MM_SLOT  0x011160E8      // minimap panel form vtable 0x011160D8, slot 4: per-frame update (0xB3F6E0)
#define RJ_MM_FN    0x00B3F6E0
static DWORD g_giveupMs = 180000, g_reconnMs = 5000;
static int g_ru;

DWORD __stdcall rejoin_policy(unsigned char *obj) {
    if (g_native && g_rejoin) {
        if (!IsBadWritePtr(obj + 0x20, 4) && *(DWORD *)(obj + 0x20) != g_reconnMs) *(DWORD *)(obj + 0x20) = g_reconnMs;
        return g_giveupMs;
    }
    if (!IsBadWritePtr(obj + 0x20, 4) && *(DWORD *)(obj + 0x20) == g_reconnMs) *(DWORD *)(obj + 0x20) = 15000;   // a Relic relay again
    return 40000;
}
// called in place of "cmp eax, 40000": the flags as that cmp would leave them, against the policy's number; all registers kept
__asm__(".globl _rejoin_stub\n_rejoin_stub:\n"
        "  pushl %ecx\n  pushl %edx\n  pushl %eax\n"
        "  pushl %ebx\n  call _rejoin_policy@4\n"
        "  movl %eax, %ecx\n  popl %eax\n  popl %edx\n"
        "  cmpl %ecx, %eax\n  popl %ecx\n  ret\n");
void rejoin_stub(void);

// ---- who is away (the server's notices) ----
typedef struct { DWORD at; int left, away; wchar_t name[64]; } RjWait;
static RjWait g_wait[32]; static CRITICAL_SECTION g_wk; static int g_wkOk;
static void du_notice(const unsigned char *m, int n) {          // 7e <idx> <left u16 BE> <away u16 BE> <nlen> <name utf-8>
    if (n < 7 || !g_wkOk) return;
    int idx = m[1] & 31, left = m[2] << 8 | m[3], away = m[4] << 8 | m[5], nl = m[6];
    if (7 + nl > n) nl = n - 7;
    EnterCriticalSection(&g_wk);
    RjWait *w = &g_wait[idx];
    if (left == 0xFFFF) {
        if (w->at) logmsg("rejoin: player %d (%ls) is no longer waited for", idx, w->name);
        w->at = 0;
    } else {
        if (!w->at || (w->left == 0xFFFE) != (left == 0xFFFE)) {
            char nm[64] = {0}; memcpy(nm, m + 7, nl < 63 ? nl : 63);
            if (!MultiByteToWideChar(CP_UTF8, 0, nm, -1, w->name, 64)) w->name[0] = 0;
            if (left == 0xFFFE) logmsg("rejoin: player %d (%s) sends nothing - the others wait", idx, nm);
            else logmsg("rejoin: player %d (%s) is away - the server waits %d s more", idx, nm, left);
        }
        w->at = GetTickCount(); w->left = left; w->away = away;
    }
    LeaveCriticalSection(&g_wk);
}

// ---- the text in the minimap panel's movie ----
typedef void (__thiscall *MmUpd_t)(char *form, float dt);
static MmUpd_t o_mmUpd; static void *g_rjMovie; static int g_rjShown;
static int g_rjSaid;
static void rj_text(void *mv, const wchar_t *t) {
    char u[400], h[700];                                         // UTF-8 (GFx takes a char string as UTF-8)
    if (!WideCharToMultiByte(CP_UTF8, 0, t, -1, u, sizeof u, NULL, NULL)) u[0] = 0;
    for (int k = 0; k < 2; k++) {
        if (u[0]) snprintf(h, sizeof h, "<p align=\"left\"><font face=\"$TitleFont\" size=\"22\" color=\"#%s\">%s</font></p>", k ? "F7A020" : "000000", u);
        else h[0] = 0;
        int r = gfx_setvar(mv, k ? "_root.rj_wait.htmlText" : "_root.rj_wait_sh.htmlText", gv_str(h));
        if (k && u[0] && g_rjSaid < 3) { g_rjSaid++; logmsg("rejoin: waiting line set (%d): %s", r, u); }
    }
}
static void rj_make(void *mv) {
    double W = 600, fh = 32, px = 40, fy = 5;                     // inside the movie's stage, above the frame's top bar (y 40); stand 06.11: a
                                                                  // field above the stage (y < 0) is not shown
    for (int k = 0; k < 2; k++) {
        const char *nm = k ? "rj_wait" : "rj_wait_sh"; char p[64];
        GfxVal a[6] = { gv_str(nm), gv_num(16100 + k), gv_num(px + (k ? 0 : 1.5)), gv_num(fy + (k ? 0 : 2)), gv_num(W), gv_num(fh) };
        gfx_invoke(mv, "_root.createTextField", NULL, a, 6);
        snprintf(p, sizeof p, "_root.%s.html", nm); gfx_setvar(mv, p, gv_bool(1));
        snprintf(p, sizeof p, "_root.%s.selectable", nm); gfx_setvar(mv, p, gv_bool(0));
        if (!k) { snprintf(p, sizeof p, "_root.%s._alpha", nm); gfx_setvar(mv, p, gv_num(70)); }
    }
    logmsg("rejoin: the waiting line made in the minimap movie %p: %s", mv, gfx_exists(mv, "rj_wait") ? "ok" : "NOT THERE");
}
static void __thiscall rj_mm_upd(char *form, float dt) {
    o_mmUpd(form, dt);
    if (!g_wkOk) return;
    wchar_t t[256] = {0}; DWORD now = GetTickCount(); int any = 0;
    EnterCriticalSection(&g_wk);
    for (int i = 0; i < 32; i++) {
        RjWait *w = &g_wait[i];
        if (!w->at || now - w->at > 3000) continue;
        int el = (int)((now - w->at) / 1000), away = w->away + el, left = w->left == 0xFFFE ? 0 : w->left - el; if (left < 0) left = 0;
        wchar_t one[128];
        const wchar_t *nm = w->name[0] ? w->name : (g_ru ? L"?" : L"a player");
        if (w->left == 0xFFFE)                                   // connected, but nothing from him: not dropped yet
            _snwprintf(one, 127, g_ru ? L"Ждём %ls… %d:%02d" : L"Waiting: %ls… %d:%02d", nm, away / 60, away % 60);
        else
            _snwprintf(one, 127, g_ru ? L"Ждём %ls… %d:%02d (ещё %d:%02d)" : L"Waiting: %ls… %d:%02d (%d:%02d left)",
                       nm, away / 60, away % 60, left / 60, left % 60);
        one[127] = 0;
        if (any) wcsncat(t, L"   ", 255 - wcslen(t));
        wcsncat(t, one, 255 - wcslen(t)); any = 1;
    }
    LeaveCriticalSection(&g_wk);
    if (!any && !g_rjShown) return;
    void *mv = gfx_movie_of_form(form); if (!mv) return;
    if (mv != g_rjMovie) { g_rjMovie = mv; rj_make(mv); }
    rj_text(mv, t); g_rjShown = any;
}

static void rejoin_install(void) {
    g_rejoin = GetPrivateProfileIntW(L"Relay", L"Rejoin", 1, g_ini) != 0;
    int rs = GetPrivateProfileIntW(L"Relay", L"RejoinSec", 180, g_ini), cs = GetPrivateProfileIntW(L"Relay", L"ReconnectSec", 5, g_ini);
    if (rs < 40) rs = 40; if (rs > 600) rs = 600; if (cs < 3) cs = 3; if (cs > 15) cs = 15;
    g_giveupMs = rs * 1000u; g_reconnMs = cs * 1000u;
    LANGID lg = GetUserDefaultUILanguage(); g_ru = PRIMARYLANGID(lg) == LANG_RUSSIAN || PRIMARYLANGID(lg) == LANG_UKRAINIAN || PRIMARYLANGID(lg) == LANG_BELARUSIAN;
    if (!g_rejoin) { logmsg("rejoin: off (Relay.ini Rejoin=0)"); return; }
    InitializeCriticalSection(&g_wk); g_wkOk = 1;
    unsigned char *c = (unsigned char *)RJ_CMP_AT; static const unsigned char want[5] = {0x3D, 0x40, 0x9C, 0x00, 0x00};
    if (IsBadReadPtr(c, 5) || memcmp(c, want, 5)) { logmsg("rejoin: not DOW2.exe 3.19 at %08X - the game keeps its 40 s", RJ_CMP_AT); }
    else {
        DWORD old; VirtualProtect(c, 5, PAGE_EXECUTE_READWRITE, &old);
        c[0] = 0xE8; *(DWORD *)(c + 1) = (DWORD)rejoin_stub - (DWORD)(c + 5);
        VirtualProtect(c, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), c, 5);
    }
    int ui = gfx_hook_slot(RJ_MM_SLOT, RJ_MM_FN, (void *)rj_mm_upd, (void **)&o_mmUpd);
    logmsg("rejoin: on - a match on our server: reconnect after %lu s of silence, given up after %lu s (Relic's relays: 15 / 40 s); "
           "the waiting line %s", g_reconnMs / 1000, g_giveupMs / 1000, ui ? "above the minimap" : "NOT hooked (the minimap panel slot is someone else's)");
}
