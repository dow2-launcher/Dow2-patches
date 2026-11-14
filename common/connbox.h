// common/connbox.h - the game's own "Connecting" message box (DOW2.exe 3.19), shared by the add-ons that take the game somewhere by
// themselves: RoomJoin (a lobby join), MMFind (a launcher room's start), LiveReplay (a live replay), ReplayOpen (a replay file).
// Moved here from RoomJoin 13 (siritos, 14.11.2026): the player sees that something is going on while the game walks by itself.
//
// What it is: the box the game's own games list opens on Join (GameListForm, 0x0046BF34 -> 0x0046C1FC..0x0046C253): a message
// box of its MessageBoxManager, title 550001 "Connecting", text 550002 "Connecting to the game..." (or another DOW2.ucs id - the
// caller's line, in the player's language through Localizer::GetString), no buttons. The games list keeps its id in
// GameListForm+0x68 m_joiningMBID and closes it when the join ends (CancelConnection 0x0046B520); the add-ons do the same.
//   manager: 0x0045A3CD() -> EAX;  open: ESI = manager, push 0, 0, &text, &title (LocStrings), call 0x00AD5FE0 -> EAX = box id;
//   close:   EAX = manager, EDX = box id, call 0x00AD6200 (MessageBoxManager::DeactivateMessageBox).
// Rules for the caller: only from the game's own thread (its frame hook, shark::update), only in the front end (no world running or
// loading, the menu movie up); closed on success, failure, cancel and when a match / replay loads - never left up in a match.
// connbox_check() first (in the add-on's install): the three code places must be 3.19's, else connbox_open() never shows anything.
// The add-on gives its log function: #define CONNBOX_LOG logmsg before the include.
#ifndef COMMON_CONNBOX_H
#define COMMON_CONNBOX_H
#include <windows.h>
#include <string.h>
#ifndef CONNBOX_LOG
#error "#define CONNBOX_LOG <the add-on's printf-like log function> before including common/connbox.h"
#endif

#define CONNBOX_FN_MGR   0x0045A3CD
#define CONNBOX_FN_OPEN  0x00AD5FE0
#define CONNBOX_FN_CLOSE 0x00AD6200
#define CONNBOX_TITLE    550001                    // "Connecting"
#define CONNBOX_TEXT     550002                    // "Connecting to the game..."

static int connbox_ok = -1;                        // -1 not checked, 0 not 3.19 (never shown), 1 fine
static int connbox_id = -1;                        // the box that is up (ours), -1 none
static volatile int connbox_own;                   // 1 while our own box is being made (an Invoke hook: not a box of the game)
static char connbox_why[48];                       // what it is up for (the log)

// the code at the four places as DOW2.exe 3.19 has it (the box's functions are called directly: a wrong build must not get there)
static int connbox_check(void) {
    static const unsigned char mgr[6] = {0x8B, 0x0D, 0x88, 0x50, 0x33, 0x01};    // 0x45A3CD: mov ecx, [0x01335088]
    static const unsigned char opn[4] = {0x51, 0x83, 0xEC, 0x08};                // 0xAD5FE0: push ecx; sub esp, 8
    static const unsigned char cls[5] = {0x51, 0x53, 0x56, 0x8B, 0xF0};          // 0xAD6200: push ecx; push ebx; push esi; mov esi, eax
    const unsigned char *m = (const unsigned char *)CONNBOX_FN_MGR, *o = (const unsigned char *)CONNBOX_FN_OPEN, *c = (const unsigned char *)CONNBOX_FN_CLOSE;
    connbox_ok = !IsBadReadPtr(m, 6) && !IsBadReadPtr(o, 4) && !IsBadReadPtr(c, 5) && !memcmp(m, mgr, 6) && !memcmp(o, opn, 4) && !memcmp(c, cls, 5);
    if (!connbox_ok) CONNBOX_LOG("Connecting box: the game's message box code is not 3.19's - never shown");
    return connbox_ok;
}
static void *connbox_mgr(void) {
    void *m;
    __asm__ volatile("call *%1" : "=a"(m) : "r"((void *)CONNBOX_FN_MGR) : "ecx", "edx", "memory");
    return m;
}
static int connbox_is_open(void) { return connbox_id != -1; }
// the box with the game's title "Connecting" and the text of DOW2.ucs id textId (0 = 550002; an id this language has not = 550002)
static int connbox_open(int textId, const char *why) {
    if (connbox_id != -1) return 1;
    if (connbox_ok < 0) connbox_check();
    if (connbox_ok != 1) return 0;
    HMODULE l = GetModuleHandleA("Localizer.dll"); if (!l) { CONNBOX_LOG("Connecting box: no Localizer.dll - not shown"); return 0; }
    typedef char (__stdcall *GetStrFn)(int, void *);
    typedef void (__fastcall *CtorFn)(void *, void *);
    typedef const wchar_t *(__fastcall *CStrFn)(void *, void *);
    GetStrFn get = (GetStrFn)GetProcAddress(l, "?GetString@Localizer@@SG_NHAAVLocString@@@Z");
    CtorFn ctor = (CtorFn)GetProcAddress(l, "??0LocString@@QAE@XZ"), dtor = (CtorFn)GetProcAddress(l, "??1LocString@@UAE@XZ");
    CStrFn cstr = (CStrFn)GetProcAddress(l, "?c_str@LocString@@QBEPB_WXZ");
    if (!get || !ctor || !dtor) { CONNBOX_LOG("Connecting box: no Localizer::GetString - not shown"); return 0; }
    static __attribute__((aligned(16))) unsigned char title[64], text[64];
    memset(title, 0, sizeof title); memset(text, 0, sizeof text);
    ctor(title, NULL); ctor(text, NULL);
    int tid = textId > 0 ? textId : CONNBOX_TEXT, got = get(CONNBOX_TITLE, title) != 0;
    if (got && tid != CONNBOX_TEXT) {                                 // the caller's line: there and not empty in this language?
        int have = get(tid, text) != 0;
        const wchar_t *s = have && cstr ? cstr(text, NULL) : NULL;
        if (!have || (cstr && (!s || IsBadReadPtr(s, 2) || !s[0] || s[0] == L'$'))) {
            CONNBOX_LOG("Connecting box: text %d is not in this game's language - %d instead", tid, CONNBOX_TEXT);
            dtor(text, NULL); memset(text, 0, sizeof text); ctor(text, NULL); tid = CONNBOX_TEXT;
        }
    }
    if (got && tid == CONNBOX_TEXT) got = get(CONNBOX_TEXT, text) != 0;
    int shown = 0;
    if (got) {
        void *m = connbox_mgr();
        if (m) {
            int id; void *ti = title, *tx = text, *fn = (void *)CONNBOX_FN_OPEN;
            connbox_own = 1;
            __asm__ volatile(                                         // EBX keeps the stack (no operand in it), whoever cleans it
                "push %%ebx\n\tmov %%esp, %%ebx\n\t"
                "push $0\n\tpush $0\n\tpush %%ecx\n\tpush %%edi\n\t"
                "call *%%edx\n\t"
                "mov %%ebx, %%esp\n\tpop %%ebx"
                : "=a"(id), "+S"(m), "+c"(tx), "+d"(fn), "+D"(ti) : : "memory");
            connbox_own = 0;
            connbox_id = id; shown = 1;
            snprintf(connbox_why, sizeof connbox_why, "%s", why ? why : "");
            CONNBOX_LOG("the game's Connecting box shown (id %d, text %d) - %s", id, tid, connbox_why);
        } else CONNBOX_LOG("Connecting box: no message box manager - not shown");
    } else CONNBOX_LOG("Connecting box: texts %d/%d not found - not shown", CONNBOX_TITLE, tid);
    dtor(title, NULL); dtor(text, NULL);
    return shown;
}
static void connbox_close(const char *why) {
    if (connbox_id == -1) return;
    int id = connbox_id; connbox_id = -1;
    void *fn = (void *)CONNBOX_FN_CLOSE; void *m = connbox_mgr();
    if (!m) { CONNBOX_LOG("Connecting box %d: no manager to close it (%s)", id, why ? why : ""); return; }
    __asm__ volatile(
        "push %%ebx\n\tmov %%esp, %%ebx\n\t"
        "call *%%ecx\n\t"
        "mov %%ebx, %%esp\n\tpop %%ebx"
        : "+a"(m), "+d"(id), "+c"(fn) : : "memory");
    CONNBOX_LOG("the game's Connecting box closed (%s)", why ? why : "");
}
#endif
