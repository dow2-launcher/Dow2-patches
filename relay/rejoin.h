// rejoin.h - Relay 24 (06.11.2026): back into a running match of OUR server after a network drop (included once by relay.c).
//
// 1. The game's own relay client, BattleServerRelay::Process (DOW2.exe 3.19, 0x63B910, ebx = the relay object, eax = ms since
//    the relay last sent anything): silence > [obj+0x20] (RL_ClientReconnectMS, 15000) and reconnects on (byte [obj+0x1C]) ->
//    it connects again with "15 sid 0 id 02 <ticks got>" and the server sends what it missed (dow2-ladder battle_relay, 06.11);
//    silence > 40000 (cmp eax, 9C40h at 0x63B979) -> the match is given up ("Lost connection to Relay ... forcing Disconnect").
//    For a game whose relay is our node (g_native): reconnect after ReconnectSec (5), give up after RejoinSec (180) - the cmp
//    calls rejoin_stub, which sets the flags as the cmp did against our number. Relic's relays keep 15 s / 40 s.
// 2. The server's notices (0x7e, "DWR5" streams only): number, seconds left, seconds away, name, Relay 25: "don't wait" votes /
//    players asked -> g_wait. Relay 25 (owner 06.11: "не видно совсем - больше и в центр"): shown in the middle of the screen,
//    big, in the game's own full-screen HUD movie ui\movies\hud\resource_notifications (stage 1600 x 1200; its form, vtable
//    0x01116478, has an empty per-frame update in slot 4 - ours goes in, chained): "Waiting for <name>" / "0:13 – 2:41 left",
//    in the game's language (en / ru / es / fr / de: the text of the game's own "Resume", ucs 9032352).
// 3. Relay 25, the "Don't wait" button under it (owner 06.11): a text field of the same movie drawn as a button; a click (the
//    player's own left button over it, read with GetAsyncKeyState and the movie's _xmouse/_ymouse - nothing is sent to the
//    game) queues our frame 7d <number> into the tunnel to the server at the next frame boundary of the game's stream. The
//    server counts the votes; when every other connected player has voted it drops that player out of the next tick for all
//    at once (as when the window runs out) - one decision on the server, so the games stay in step.
#include "../common/gfxui.h"

#define RJ_CMP_AT   0x0063B979      // 3D 40 9C 00 00   cmp eax, 40000
#define RJ_RN_SLOT  0x01116488      // resource notifications form vtable 0x01116478, slot 4: empty per-frame update (ret 4)
#define RJ_RN_FN    0x009AB7D0
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
typedef struct { DWORD at; int left, away, votes, need; wchar_t name[64]; } RjWait;
static RjWait g_wait[32]; static CRITICAL_SECTION g_wk; static int g_wkOk;
static volatile LONG g_voteFor = -1, g_myVote = -1;              // queued for the tunnel / the number I voted for
static void du_notice(const unsigned char *m, int n) {          // 7e <idx> <left> <away> <nlen> <name> [<votes> <need>]
    if (n < 7 || !g_wkOk) return;
    int idx = m[1] & 31, left = m[2] << 8 | m[3], away = m[4] << 8 | m[5], nl = m[6];
    if (7 + nl > n) nl = n - 7;
    EnterCriticalSection(&g_wk);
    RjWait *w = &g_wait[idx];
    if (left == 0xFFFF) {
        if (w->at) logmsg("rejoin: player %d (%ls) is no longer waited for", idx, w->name);
        w->at = 0; if (g_myVote == idx) g_myVote = -1;
    } else {
        if (!w->at || (w->left == 0xFFFE) != (left == 0xFFFE)) {
            char nm[64] = {0}; memcpy(nm, m + 7, nl < 63 ? nl : 63);
            if (!MultiByteToWideChar(CP_UTF8, 0, nm, -1, w->name, 64)) w->name[0] = 0;
            if (left == 0xFFFE) logmsg("rejoin: player %d (%s) sends nothing - the others wait", idx, nm);
            else logmsg("rejoin: player %d (%s) is away - the server waits %d s more", idx, nm, left);
        }
        w->at = GetTickCount(); w->left = left; w->away = away;
        w->votes = n >= 9 + nl ? m[7 + nl] : 0; w->need = n >= 9 + nl ? m[8 + nl] : 0;
    }
    LeaveCriticalSection(&g_wk);
}
static int du_vote_take(void) { return InterlockedExchange(&g_voteFor, -1); }   // the tunnel: a vote to send now, -1 = none

// ---- the game's language (the text of its own "Resume") ----
enum { L_EN, L_RU, L_ES, L_FR, L_DE };
static int g_lang = -1;
typedef void *(__stdcall *LocGet_t)(void *ret, int id);
typedef const wchar_t *(__fastcall *LocCstr_t)(void *ls, void *edx);
static int game_lang(void) {
    if (g_lang >= 0) return g_lang;
    HMODULE lz = GetModuleHandleA("Localizer.dll"); g_lang = g_ru ? L_RU : L_EN;
    wchar_t lv[8]; GetPrivateProfileStringW(L"Relay", L"Language", L"", lv, 8, g_ini);   // stands / a player's own choice
    const wchar_t *codes[] = {L"en", L"ru", L"es", L"fr", L"de"};
    for (int i = 0; i < 5; i++) if (!_wcsicmp(lv, codes[i])) { g_lang = i; logmsg("rejoin: language %ls (Relay.ini)", lv); return g_lang; }
    LocGet_t get = lz ? (LocGet_t)GetProcAddress(lz, "?GetString@Localizer@@SG?AVLocString@@H@Z") : NULL;
    LocCstr_t cs = lz ? (LocCstr_t)GetProcAddress(lz, "?c_str@LocString@@QBEPB_WXZ") : NULL;
    static unsigned char ls[256];
    if (get && cs) {
        get(ls, 9032352); const wchar_t *t = cs(ls, 0);
        if (t && !IsBadReadPtr(t, 2)) {
            for (const wchar_t *c = t; *c; c++) if (*c >= 0x400 && *c < 0x500) { g_lang = L_RU; goto done; }
            if (!wcsncmp(t, L"Rean", 4) || !wcsncmp(t, L"Contin", 6)) g_lang = L_ES;
            else if (!wcsncmp(t, L"Repr", 4)) g_lang = L_FR;
            else if (!wcsncmp(t, L"Fort", 4) || !wcsncmp(t, L"Weiter", 6)) g_lang = L_DE;
            else g_lang = L_EN;
          done: logmsg("rejoin: the game's language %s (\"%ls\")", (const char *[]){"en", "ru", "es", "fr", "de"}[g_lang], t);
        }
    }
    return g_lang;
}
static const wchar_t *T_WAIT[] = {L"Waiting for %ls", L"Ждём игрока %ls", L"Esperando a %ls", L"En attente de %ls", L"Warten auf %ls"};
static const wchar_t *T_LEFT[] = {L"%d:%02d – %d:%02d left", L"%d:%02d – осталось %d:%02d", L"%d:%02d – quedan %d:%02d", L"%d:%02d – encore %d:%02d", L"%d:%02d – noch %d:%02d"};
static const wchar_t *T_SILENT[] = {L"%d:%02d – no data from the player", L"%d:%02d – от игрока ничего нет", L"%d:%02d – sin datos del jugador", L"%d:%02d – aucune donnée du joueur", L"%d:%02d – keine Daten vom Spieler"};
static const wchar_t *T_BTN[] = {L"Don't wait", L"Не ждать", L"No esperar", L"Ne pas attendre", L"Nicht warten"};
static const wchar_t *T_VOTES[] = {L"Don't wait: %d of %d", L"Не ждать: %d из %d", L"No esperar: %d de %d", L"Ne pas attendre : %d sur %d", L"Nicht warten: %d von %d"};

// ---- in the middle of the screen: the resource notifications movie (stage 1600 x 1200) ----
typedef void (__thiscall *RnUpd_t)(char *form, float dt);
typedef char (__thiscall *GfxGetVar_t)(void *movie, GfxVal *v, const char *path);
static RnUpd_t o_rnUpd; static void *g_rjMovie; static int g_rjShown, g_rjSaid, g_mouseOk = -1, g_hover, g_lbWas;
#define BX 650.0
#define BY 470.0
#define BW 300.0
#define BH 48.0
static void html_esc(const wchar_t *in, wchar_t *out, int cap) {
    int o = 0;
    for (; *in && o < cap - 7; in++) {
        if (*in == L'<') { wcscpy(out + o, L"&lt;"); o += 4; } else if (*in == L'>') { wcscpy(out + o, L"&gt;"); o += 4; }
        else if (*in == L'&') { wcscpy(out + o, L"&amp;"); o += 5; } else out[o++] = *in;
    }
    out[o] = 0;
}
static void set_html(void *mv, const char *field, const wchar_t *t, int size, const char *color) {
    char u[400], h[800], p[64];
    if (!WideCharToMultiByte(CP_UTF8, 0, t, -1, u, sizeof u, NULL, NULL)) u[0] = 0;
    if (u[0]) snprintf(h, sizeof h, "<p align=\"center\"><font face=\"$TitleFont\" size=\"%d\" color=\"#%s\">%s</font></p>", size, color, u);
    else h[0] = 0;
    snprintf(p, sizeof p, "_root.%s.htmlText", field); gfx_setvar(mv, p, gv_str(h));
}
static void mk_field(void *mv, const char *nm, int depth, double x, double y, double w, double h) {
    char p[64]; GfxVal a[6] = { gv_str(nm), gv_num(depth), gv_num(x), gv_num(y), gv_num(w), gv_num(h) };
    gfx_invoke(mv, "_root.createTextField", NULL, a, 6);
    snprintf(p, sizeof p, "_root.%s.html", nm); gfx_setvar(mv, p, gv_bool(1));
    snprintf(p, sizeof p, "_root.%s.selectable", nm); gfx_setvar(mv, p, gv_bool(0));
}
static void rj_make(void *mv) {
    // two lines with a black copy 3 px down-right under each (readable on any ground), the button: a field with a dark
    // background and an orange border, the votes under it. Stage 1600 x 1200, its middle x 800.
    mk_field(mv, "rj_l1s", 16200, 203, 323, 1200, 70); mk_field(mv, "rj_l1", 16201, 200, 320, 1200, 70);
    mk_field(mv, "rj_l2s", 16202, 203, 393, 1200, 52); mk_field(mv, "rj_l2", 16203, 200, 390, 1200, 52);
    mk_field(mv, "rj_btn", 16204, BX, BY, BW, BH);
    mk_field(mv, "rj_v", 16205, 200, BY + BH + 8, 1200, 44);
    gfx_setvar(mv, "_root.rj_btn.background", gv_bool(1)); gfx_setvar(mv, "_root.rj_btn.backgroundColor", gv_num(0x1E1E1E));
    gfx_setvar(mv, "_root.rj_btn.border", gv_bool(1)); gfx_setvar(mv, "_root.rj_btn.borderColor", gv_num(0xF7A020));
    gfx_setvar(mv, "_root.rj_l1s._alpha", gv_num(85)); gfx_setvar(mv, "_root.rj_l2s._alpha", gv_num(85));
    logmsg("rejoin: the waiting lines made in the middle of the screen (movie %p): %s", mv, gfx_exists(mv, "rj_l1") ? "ok" : "NOT THERE");
}
static void rj_show(void *mv, int on) {
    const char *f[] = {"rj_l1s", "rj_l1", "rj_l2s", "rj_l2", "rj_btn", "rj_v"}; char p[64];
    for (int i = 0; i < 6; i++) { snprintf(p, sizeof p, "_root.%s._visible", f[i]); gfx_setvar(mv, p, gv_bool(on)); }
}
static int mouse_at(void *mv, double *x, double *y) {          // the mouse in stage units: the movie's own _xmouse / _ymouse
    GfxVal a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    GfxGetVar_t gv = *(GfxGetVar_t *)(*(char **)mv + 0x30);
    int ok = gv(mv, &a, "_root._xmouse") && gv(mv, &b, "_root._ymouse") && a.type == GV_NUMBER && b.type == GV_NUMBER;
    if (g_mouseOk < 0) { g_mouseOk = ok; logmsg("rejoin: the movie's mouse position %s (%g, %g)", ok ? "readable" : "NOT readable - no button", a.v.num, b.v.num); }
    if (!ok) return 0;
    *x = a.v.num; *y = b.v.num; return 1;
}
static void __thiscall rj_rn_upd(char *form, float dt) {
    o_rnUpd(form, dt);
    if (!g_wkOk) return;
    DWORD now = GetTickCount(); int idx = -1, more = 0; RjWait w;
    EnterCriticalSection(&g_wk);
    for (int i = 0; i < 32; i++) if (g_wait[i].at && now - g_wait[i].at <= 3000) { if (idx < 0) { idx = i; w = g_wait[i]; } else more++; }
    LeaveCriticalSection(&g_wk);
    if (idx < 0 && !g_rjShown) return;
    void *mv = gfx_movie_of_form(form); if (!mv) return;
    if (mv != g_rjMovie) { g_rjMovie = mv; g_rjShown = 0; rj_make(mv); }
    if (idx < 0) { rj_show(mv, 0); g_rjShown = 0; return; }
    if (!g_rjShown) rj_show(mv, 1);
    g_rjShown = 1;
    int L = game_lang(), el = (int)((now - w.at) / 1000), away = w.away + el, left = w.left == 0xFFFE ? 0 : w.left - el; if (left < 0) left = 0;
    wchar_t nm[200], t[256];
    html_esc(w.name[0] ? w.name : L"?", nm, 200);
    _snwprintf(t, 255, T_WAIT[L], nm); t[255] = 0;
    if (more) { size_t k = wcslen(t); _snwprintf(t + k, 255 - k, L" (+%d)", more); }
    set_html(mv, "rj_l1s", t, 44, "000000"); set_html(mv, "rj_l1", t, 44, "F7A020");
    if (w.left == 0xFFFE) _snwprintf(t, 255, T_SILENT[L], away / 60, away % 60);
    else _snwprintf(t, 255, T_LEFT[L], away / 60, away % 60, left / 60, left % 60);
    t[255] = 0;
    set_html(mv, "rj_l2s", t, 30, "000000"); set_html(mv, "rj_l2", t, 30, "FFFFFF");
    // the button and the click
    double mx, my; int over = mouse_at(mv, &mx, &my) && mx >= BX && mx <= BX + BW && my >= BY && my <= BY + BH;
    int voted = g_myVote == idx;
    set_html(mv, "rj_btn", T_BTN[L], 30, voted ? "777777" : over ? "FFFFFF" : "F7A020");
    if (over != g_hover) { g_hover = over; gfx_setvar(mv, "_root.rj_btn.backgroundColor", gv_num(over ? 0x4A3A1A : 0x1E1E1E)); }
    int lb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    DWORD fpid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &fpid);
    if (g_lbWas && !lb && over && !voted && fpid == GetCurrentProcessId()) {   // released over the button: the player's own click
        InterlockedExchange(&g_voteFor, idx); g_myVote = idx;
        logmsg("rejoin: \"don't wait\" for player %d (%ls) - sent to the server", idx, w.name);
    }
    g_lbWas = lb;
    if (w.votes > 0 || voted) _snwprintf(t, 255, T_VOTES[L], w.votes > 0 ? w.votes : 1, w.need > 0 ? w.need : 1); else t[0] = 0;
    t[255] = 0; set_html(mv, "rj_v", t, 26, "FFFFFF");
    if (g_rjSaid < 2) { g_rjSaid++; char u[300]; WideCharToMultiByte(CP_UTF8, 0, t, -1, u, sizeof u, NULL, NULL); logmsg("rejoin: waiting lines shown (votes line \"%s\")", u); }
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
    int ui = gfx_hook_slot(RJ_RN_SLOT, RJ_RN_FN, (void *)rj_rn_upd, (void **)&o_rnUpd);
    logmsg("rejoin: on - a match on our server: reconnect after %lu s of silence, given up after %lu s (Relic's relays: 15 / 40 s); "
           "the waiting lines and \"don't wait\" %s", g_reconnMs / 1000, g_giveupMs / 1000, ui ? "in the middle of the screen" : "NOT hooked (the resource notifications slot is someone else's)");
}
