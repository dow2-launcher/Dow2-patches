// retlock.h - the table behind eliteai_retlock (EliteAI.dll 0.16): which AI players have the engine AI's own retreats off.
// Kept apart from eliteai.c so that test/retlock_test.c compiles THE SAME LINES on the host with a faked engine.
// The includer defines, before the include:
//   RL_AIP(pid)     the AIPlayer object of a player id, or NULL          RL_OK(p, n)  n bytes at p are readable
//   RL_PLAYER(aip)  its simulation player                                RL_PROXY(aip) its CommandProxy
//   RL_WORLD()      the world of the simulation, or NULL                 RL_TICK(world) its tick (an int)
//   RL_SAY(why, n)  n locks were dropped, why (a log line; nothing comes back from it)
// The owner's rule: integers and pointer compares, a fixed order (slot 0..7), nothing from the wall clock - what ends a
// lock is the simulation's own state (the world object, its tick, the AI player objects) and a SCAR load, never a timer.
//
// 0.15 kept { player id, AIPlayer* } for the life of the PROCESS and forgot a pair only when a retreat of the engine AI
// passed the hook and the AIPlayer of that id was at another address. A later match of the same game session with an AI
// of the same id at the same address (usual for the allocator) inherited the lock: its retreats vanished silently - an
// old-layer or Elite AI, also on the host of a network game (the verifier of step 9, VERIFY_9_RU.md note 1). Now a lock
// ends
//   * when a match starts: at a SCAR script the game loads (eliteai.c load_hook -> ret_scar_load: a world loads its SCAR
//     before it makes its AI players; 0.17: only when the world is none / another one / its tick went back - the engine
//     loads SCAR at every AI player it makes, a host migration mid-match too), and here at every use of the table when
//     the world object is another one or its tick went BACK (ret_epoch);
//   * when what it names is gone: the AIPlayer of that id is another object or none, or its player / its proxy is not the
//     one the lock was given for (ret_alive) - checked for every lock at every use, not only on a retreat.
// The brain asks for its lock again at its start and every 30 s of the game (netorder.ai relock), so a lock dropped here
// for a living brain comes back; a lock nobody asks for again stays off.
typedef struct { int pid; void *aip, *player, *proxy; } RetLock;
#define RL_N 8
static RetLock g_retLock[RL_N];
static void *g_retWorld; static int g_retTick;     // the world and the tick the table was last used at
static long g_retCleared;                          // locks dropped so far (the log, the test)

static int ret_clear_all(const char *why) {        // -> how many were set
    int n = 0;
    for (int i = 0; i < RL_N; i++) { if (g_retLock[i].aip) n++; g_retLock[i].pid = 0; g_retLock[i].aip = g_retLock[i].player = g_retLock[i].proxy = NULL; }
    g_retWorld = NULL; g_retTick = 0;
    if (n) { g_retCleared += n; RL_SAY(why, n); }
    return n;
}
static void ret_epoch(void) {                      // another world, or the same address with the tick gone back: a new match
    char *w = (char *)RL_WORLD(); int t = w ? RL_TICK(w) : 0;
    if (w != (char *)g_retWorld) ret_clear_all("another world");
    else if (t < g_retTick) ret_clear_all("the world's tick went back");
    g_retWorld = w; g_retTick = t;
}
static int ret_alive(const RetLock *k) {           // does the lock still name the AI player it was given for?
    char *aip = (char *)RL_AIP(k->pid);
    return aip && aip == (char *)k->aip && RL_OK(aip, 0x18) && RL_PLAYER(aip) == k->player && RL_PROXY(aip) == k->proxy;
}
static void ret_sweep(void) {
    ret_epoch();
    for (int i = 0; i < RL_N; i++) if (g_retLock[i].aip && !ret_alive(&g_retLock[i])) {
        g_retLock[i].pid = 0; g_retLock[i].aip = g_retLock[i].player = g_retLock[i].proxy = NULL; g_retCleared++; RL_SAY("its AI player is gone", 1);
    }
}
// 0.17 (the network check of 15.11, MP_CHECK_1115_RU.md bug 2): a SCAR script is loaded not only when a match starts - the
// engine loads one whenever it MAKES an AI player, also in the middle of a match: a host migration made three AI players on
// the new host at tick 7824, and the old clear at every SCAR load dropped the lock of p1003, which had been computed there all
// along, and the lock p1001 had just taken (up to 30 s of the engine's own retreats until the brains asked again). The same
// at the start of a skirmish with several computers (each made one after another at tick 0). Now a SCAR load ends the locks
// only when it is a NEW MATCH by the simulation's own state - no world, another world object than the table's, or the tick
// gone back below the table's last use; otherwise (the same world, the tick not back) only the locks whose AI player is gone
// or another object / player / proxy end (ret_sweep), the others stay - an AI player the engine kept keeps its lock, one
// it made anew asks for its own. -> how many were dropped. Integers and pointer compares only.
static int ret_scar_load(void) {
    char *w = (char *)RL_WORLD(); int t = w ? RL_TICK(w) : 0;
    if (!w || w != (char *)g_retWorld || t < g_retTick) return ret_clear_all("a SCAR script is loaded: a match starts");
    long was = g_retCleared; ret_sweep(); return (int)(g_retCleared - was);
}
static int ret_locked(void *proxy) {               // the hook: is this proxy a locked player's?
    ret_sweep();
    for (int i = 0; i < RL_N; i++) if (g_retLock[i].aip && g_retLock[i].proxy == proxy) return 1;
    return 0;
}
static int ret_set(int pid, int on) {              // eliteai_retlock: 1 done, 0 no such AI player / no room
    ret_sweep();
    char *aip = (char *)RL_AIP(pid); int slot = -1;
    for (int i = 0; i < RL_N; i++) { if (g_retLock[i].aip && g_retLock[i].pid == pid) { slot = i; break; } if (slot < 0 && !g_retLock[i].aip) slot = i; }
    if (!aip || !RL_OK(aip, 0x18) || slot < 0) return 0;
    if (on && RL_PROXY(aip)) { g_retLock[slot].pid = pid; g_retLock[slot].aip = aip; g_retLock[slot].player = RL_PLAYER(aip); g_retLock[slot].proxy = RL_PROXY(aip); }
    else { g_retLock[slot].pid = 0; g_retLock[slot].aip = g_retLock[slot].player = g_retLock[slot].proxy = NULL; if (on) return 0; }
    return 1;
}
