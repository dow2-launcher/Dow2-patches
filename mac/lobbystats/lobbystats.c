// LobbyStats.dylib - Lobby ratings for the native Mac game (Feral 1.0.3): each player's rating after his name in the lobby (Relic's,
// or our Elite rating of the lobby's mode with Rating=own), the slot's tooltip with every rating, W/L and the Steam hours, and the
// Relic rating after your own name on the main menu. The lobby part of the Windows LobbyStats.dll 0.12.0 (lobbystats/lobbystats.c),
// same servers, same ini keys, same texts.
//
// Mac 1.0.3: every Flash call of the game goes through GFxMovieRoot::Invoke(view, "<movie path>.<method>", result, args, n) - vtable
// slot 0x102db3c38 (0x101289fd0); BaseForm::Invoke (0x1000e0de0) builds the path and calls it. PlayerListPanel::InvokePopulatePlayerList
// (0x10016acd0) sends Receive_SetMaxPlayers and then Receive_PopulatePlayerList with the same layout as DOW2.exe 3.19: 5 header
// values (players per team, host, ..., the teams' names) + 20 per slot ([0] has a user, [3] position, [4] slot type, [5] the name,
// [19] the tooltip). GFxValue 16 bytes as on Windows (type +0: 2 bool, 3 number, 5 wide string; value +8); wide strings are
// 16-bit (Feral builds with a 2-byte wchar_t: _feral_wcslen_2). The slot hook is wrapped: for those methods the arguments are
// copied, the copy gets our name and tooltip, and the game's Invoke gets the copy - the game's own vector is not touched.
// Players are looked up by name (the Windows add-on's fallback; the Mac lobby list gives no Steam ID): the stats service's lobby
// line (<Server>/stats/api/lobby?n=...: name, rating, W, L, hours, pid, SteamID64) and, with Rating=own, the Elite table
// (<Server>/ladder/api/own/all, own_rating.h, at most every 2 min), the player found in it by the SteamID of his lobby line.
// Network: one worker thread with the system's CFNetwork (http_mac.h), never the game's thread; the hosts are a list (Server=
// host;host, the one that answered kept, the next tried) - default rts, dow2, dow2ru as on Windows; the launcher writes its
// active host first (Windows: Elite_DLLs\LobbyStats.ini; the Mac launcher does not write mac_addons/lobbystats_mac/ yet).
// A name is first shown without numbers; they come on the next lobby update (join, ready, race) and right away in later
// lobbies (LobbyStats.cache next to the dylib). It only reads the lobby: no kick, no chat, nothing in the match.
//
// LobbyStats.ini [LobbyStats]: Enabled=1, Stats=1, ShowRatingInName=1, Rating=relic|own, Server=host;host. LobbyStats.log.
#include "../common/machook.h"
#include "../../lobbystats/own_rating.h"
#include "http_mac.h"
#include <pthread.h>

#define LS_VERSION "0.12.0-mac1"
#define VT_INVOKE  0x102db3c38UL         // GFxMovieRoot vtable +0x90: Invoke(view, path, result, args, n)
#define FN_INVOKE  0x101289fd0UL         // GFxMovieRoot::Invoke
#define AT_BF_CALL 0x1000e0e90UL         // BaseForm::Invoke: GetMovieView; mov rax,[rdi]; mov rax,[rax+0x90]; ... call rax
#define AT_PL_POP  0x10016c446UL         // PlayerListPanel::InvokePopulatePlayerList: lea rsi,"Receive_PopulatePlayerList"; ...; call BaseForm::Invoke
#define STR_POP    0x10258a942UL         // "Receive_PopulatePlayerList"
#define SERVER_PATH "/stats/api/lobby"
#define OWN_PATH    "/ladder/api/own/all"
#define LS_MIRRORS  "rts.fintebtc.de;dow2.fintebtc.de;dow2ru.fintebtc.de"
#define OWN_EVERY_MS 120000
#define OWN_RETRY_MS 60000

typedef uint16_t u16;
typedef struct { int32_t type, pad; union { double num; uint8_t b; const u16 *ws; const char *s; } v; } GFxValue;
enum { GFX_BOOL = 2, GFX_NUMBER = 3, GFX_STRING = 4, GFX_WSTRING = 5 };
enum { HEADER_ARGS = 5, SLOT_ARGS = 20, A_HASUSER = 0, A_SLOTINDEX = 3, A_SLOTTYPE = 4, A_NAME = 5, A_TOOLTIP = 19 };
enum { RATING_RELIC, RATING_OWN };
typedef char (*Invoke_t)(void *view, const char *path, void *result, const GFxValue *args, uint32_t n);
static Invoke_t o_invoke;

// ---------- UTF-16 (the game's) <-> UTF-8 (ours)
static int u16len(const u16 *s) { int n = 0; while (s && s[n]) n++; return n; }
static void u16_to_utf8(const u16 *s, int n, char *o, int cap) {
    int k = 0;
    for (int i = 0; i < n && k < cap - 4; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00); i++; }
        if (c < 0x80) o[k++] = (char)c;
        else if (c < 0x800) { o[k++] = (char)(0xC0 | c >> 6); o[k++] = (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o[k++] = (char)(0xE0 | c >> 12); o[k++] = (char)(0x80 | ((c >> 6) & 0x3F)); o[k++] = (char)(0x80 | (c & 0x3F)); }
        else { o[k++] = (char)(0xF0 | c >> 18); o[k++] = (char)(0x80 | ((c >> 12) & 0x3F)); o[k++] = (char)(0x80 | ((c >> 6) & 0x3F)); o[k++] = (char)(0x80 | (c & 0x3F)); }
    }
    o[k] = 0;
}
static void utf8_to_u16(const char *s, u16 *o, int cap) {
    const unsigned char *p = (const unsigned char *)s; int k = 0;
    while (*p && k < cap - 2) {
        uint32_t c = *p++;
        if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 0xF) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
        else if (c >= 0xC0 && p[0]) { c = ((c & 0x1F) << 6) | (p[0] & 0x3F); p += 1; }
        if (c >= 0x10000) { c -= 0x10000; o[k++] = (u16)(0xD800 + (c >> 10)); o[k++] = (u16)(0xDC00 + (c & 0x3FF)); }
        else o[k++] = (u16)c;
    }
    o[k] = 0;
}

// ---------- the players: by name (UTF-8), the stats service's lobby line
typedef struct { char name[192]; int known, absent, rating, wins, losses, hours; uint64_t fetched; unsigned long long steam; } Entry;
#define MAX_ENTRIES 256
static Entry g_cache[MAX_ENTRIES]; static int g_ncache;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER; static int g_woken;
static char g_pending[16][192]; static int g_npending;
static int g_enabled = 1, g_showInName = 1, g_stats = 1, g_rating = RATING_RELIC, g_started, g_nonet;
static OwnTable g_own; static int g_ownState; static volatile int g_ownWant; static uint64_t g_ownTry;
static int g_maxPlayers; static volatile int g_ffa, g_ffaSolo, g_ffaTeam;
static char g_server[192], g_hosts[4][64], g_cachep[1024]; static int g_nHosts, g_curHost;

static uint64_t now_ms(void) { return hm_ms(); }
static Entry *find(const char *name) { for (int i = 0; i < g_ncache; i++) if (!strcmp(g_cache[i].name, name)) return &g_cache[i]; return 0; }
static Entry *find_or_add(const char *name) {
    Entry *e = find(name); if (e) return e;
    if (g_ncache == MAX_ENTRIES) { memmove(g_cache, g_cache + 1, sizeof(Entry) * (MAX_ENTRIES - 1)); g_ncache--; }
    e = &g_cache[g_ncache++]; memset(e, 0, sizeof *e); snprintf(e->name, sizeof e->name, "%s", name); return e;
}
static unsigned long long steam_of(const char *s) {
    char *end; unsigned long long v = s ? strtoull(s, &end, 10) : 0;
    return s && *s && !*end && (v >> 32) == 0x01100001 ? v : 0;
}
// lines "name\trating\twins\tlosses\thours[\tpid\tsteam]" (the service's answer and the disk cache); under g_lock
static void parse_lines(char *buf, int from_disk) {
    char *sv = 0;
    for (char *line = strtok_r(buf, "\n", &sv); line; line = strtok_r(0, "\n", &sv)) {
        char *f[7] = {0}; int n = 0; char *p = line;
        while (n < 7) { f[n++] = p; p = strchr(p, '\t'); if (!p) break; *p++ = 0; }
        if (n < 2 || !*f[0]) continue;
        { size_t L = strlen(f[n - 1]); if (L && f[n - 1][L - 1] == '\r') f[n - 1][L - 1] = 0; }
        Entry *e = find_or_add(f[0]);
        e->fetched = from_disk ? 0 : now_ms();
        if (n >= 7 && steam_of(f[6])) e->steam = steam_of(f[6]);
        if (n >= 4 && strcmp(f[1], "-")) {
            e->known = 1; e->absent = 0; e->rating = atoi(f[1]); e->wins = atoi(f[2]); e->losses = atoi(f[3]);
            e->hours = (n >= 5 && strcmp(f[4], "-")) ? atoi(f[4]) : -1;
        } else if (!from_disk) { e->known = 0; e->absent = 1; }
    }
}
static void load_cache(void) {
    FILE *f = fopen(g_cachep, "rb"); if (!f) return;
    static char buf[64 * 1024]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
    pthread_mutex_lock(&g_lock); parse_lines(buf, 1); pthread_mutex_unlock(&g_lock);
}
static void save_cache(void) {                        // worker thread: a copy under the lock, the file written outside it
    static Entry snap[MAX_ENTRIES]; int n;
    pthread_mutex_lock(&g_lock); n = g_ncache; memcpy(snap, g_cache, sizeof(Entry) * n); pthread_mutex_unlock(&g_lock);
    FILE *f = fopen(g_cachep, "wb"); if (!f) return;
    for (int i = 0; i < n; i++) if (snap[i].known || snap[i].steam) {
        if (snap[i].known) fprintf(f, "%s\t%d\t%d\t%d\t%d", snap[i].name, snap[i].rating, snap[i].wins, snap[i].losses, snap[i].hours);
        else fprintf(f, "%s\t-\t0\t0\t-", snap[i].name);
        if (snap[i].steam) fprintf(f, "\t-\t%llu", snap[i].steam);
        fputc('\n', f);
    }
    fclose(f);
}

// ---------- network (worker thread only)
static void parse_hosts(void) {
    char b[192]; snprintf(b, sizeof b, "%s", g_server); g_nHosts = 0; char *sv = 0;
    for (char *t = strtok_r(b, ";, ", &sv); t && g_nHosts < 4; t = strtok_r(0, ";, ", &sv))
        if (strlen(t) < 64 && !strpbrk(t, "/:?#")) snprintf(g_hosts[g_nHosts++], 64, "%s", t);
}
static int http_get(const char *pathq, char **buf, int *cap, int maxlen, int *got) {
    *got = 0;
    if (g_nonet) return 0;
    for (int k = 0; k < g_nHosts; k++) {
        int h = (g_curHost + k) % g_nHosts; char url[4400];
        snprintf(url, sizeof url, "https://%s%s", g_hosts[h], pathq);
        int code = http_mac_get(url, "LobbyStats-mac/" LS_VERSION, buf, cap, maxlen, got, 15000);
        if (code && code < 500) {
            if (h != g_curHost) { mh_log("server: %s answered, staying on it", g_hosts[h]); g_curHost = h; }
            return code;
        }
        mh_log("server: %s did not answer (%d)%s", g_hosts[h], code, g_nHosts > 1 ? ", trying the next mirror" : "");
    }
    *got = 0; return 0;
}
static void urlenc(const char *u, char *out, int cap) {
    int o = 0;
    for (const unsigned char *c = (const unsigned char *)u; *c && o < cap - 4; c++) {
        if ((*c >= '0' && *c <= '9') || ((*c | 32) >= 'a' && (*c | 32) <= 'z') || strchr("-_.~", *c)) out[o++] = (char)*c;
        else o += snprintf(out + o, (size_t)(cap - o), "%%%02X", *c);
    }
    out[o] = 0;
}
static void own_fetch(void) {
    static char *buf; static int cap;
    uint64_t now = now_ms();
    if (g_ownTry && now - g_ownTry < (uint64_t)(g_ownState == 1 ? OWN_EVERY_MS : OWN_RETRY_MS)) return;
    g_ownTry = now;
    int got = 0, code = http_get(OWN_PATH, &buf, &cap, 16 * 1024 * 1024, &got);
    OwnTable t; memset(&t, 0, sizeof t);
    int ok = code == 200 && got > 0 && own_parse(buf, (size_t)got, &t);
    pthread_mutex_lock(&g_lock);
    if (ok) { own_free(&g_own); g_own = t; g_ownState = 1; } else if (g_ownState != 1) g_ownState = -1;
    pthread_mutex_unlock(&g_lock);
    if (ok) mh_log("elite rating: %d players, season %s%s, %d bytes, %llu ms", t.n, t.season[0] ? t.season : "?", t.preseason ? " (preseason)" : "",
                   got, (unsigned long long)(now_ms() - now));
    else mh_log("elite rating: no table (HTTP %d, %d bytes%s)%s", code, got, code == 200 ? ", not the expected JSON" : "", g_ownState == 1 ? ", the previous one stays" : "");
}
static void *worker(void *arg) {
    static char *resp, q[4096]; static int rcap; (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_lock);
        while (!g_woken) pthread_cond_wait(&g_wake, &g_lock);
        g_woken = 0;
        pthread_mutex_unlock(&g_lock);
        usleep(300000);                                // all names of one lobby update in one request
        char names[16][192]; int n;
        pthread_mutex_lock(&g_lock); n = g_npending; memcpy(names, g_pending, sizeof(names[0]) * n); g_npending = 0; pthread_mutex_unlock(&g_lock);
        if (n) {
            int o = snprintf(q, sizeof q, "%s?", SERVER_PATH);
            for (int i = 0; i < n && o < (int)sizeof q - 8; i++) { char e[600]; urlenc(names[i], e, sizeof e); o += snprintf(q + o, sizeof q - o, "%sn=%s", i ? "&" : "", e); }
            int got = 0, code = http_get(q, &resp, &rcap, 64 * 1024 - 1, &got);
            mh_log("lookup %d names -> %d bytes (HTTP %d)", n, got, code);
            pthread_mutex_lock(&g_lock);
            if (code == 200 && got > 0) parse_lines(resp, 0);
            else for (int i = 0; i < n; i++) find_or_add(names[i])->fetched = now_ms();   // don't hammer the server on an error
            pthread_mutex_unlock(&g_lock);
            if (code == 200 && got > 0) save_cache();
        }
        if (g_rating == RATING_OWN && __sync_lock_test_and_set(&g_ownWant, 0)) own_fetch();
    }
    return 0;
}
static void wake(void) {                               // the game's thread: no waiting, the worker starts at the first need
    pthread_mutex_lock(&g_lock); g_woken = 1; pthread_cond_signal(&g_wake); pthread_mutex_unlock(&g_lock);
    if (!g_started && !g_nonet) {
        g_started = 1; pthread_t t; pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &a, worker, 0)) { g_started = 0; mh_log("no worker thread - no numbers"); }
        pthread_attr_destroy(&a);
    }
}
// a name wanted from the service (none yet, or older than 10 min); under g_lock. 1 = queued
static int want_name(const char *name, Entry **pe) {
    Entry *e = find(name); int want = 0;
    if (!e || !e->fetched || now_ms() - e->fetched > 10 * 60 * 1000) {
        int dup = 0; for (int k = 0; k < g_npending; k++) if (!strcmp(g_pending[k], name)) dup = 1;
        if (!dup && g_npending < 16) { snprintf(g_pending[g_npending++], 192, "%s", name); want = 1; }
        if (!e) { e = find_or_add(name); e->fetched = 0; }
    }
    *pe = e; return want;
}

// ---------- the lobby list
static u16 g_nameBuf[16][160], g_tipBuf[16][600], g_tagBuf[160];
// a name with our "  [...]" after it (another copy higher in the chain): "  [1953]", "  [cal 2/5]", "  [—]" - taken off (UTF-8)
static void strip_tag(char *s) {
    char *br = strstr(s, "  ["), *nx; size_t L = strlen(s);
    while (br && (nx = strstr(br + 3, "  ["))) br = nx;
    if (!br || !L || s[L - 1] != ']') return;
    const char *in = br + 3; size_t n = (size_t)(s + L - 1 - in);
    int ok = n > 0 && n <= 12;
    if (ok && strcmp(in, OWN_DASH "]") && strncmp(in, "cal ", 4)) for (size_t k = 0; k < n; k++) if (in[k] < '0' || in[k] > '9') ok = 0;
    if (ok) *br = 0;
}
static void own_line(char *o, int cap, const OwnMode *m, const char *mname, int here) {
    const char *mark = here ? "  (this lobby)" : ""; char wl[48] = "";
    if (m && m->has && m->g > 0) snprintf(wl, sizeof wl, " - %d W / %d L", m->w, m->l);
    if (m && m->rated) { if (m->rank > 0) snprintf(o, cap, "%s: %d, place %d%s%s", mname, m->c, m->rank, wl, mark); else snprintf(o, cap, "%s: %d%s%s", mname, m->c, wl, mark); }
    else if (m && m->has && m->cal > 0) snprintf(o, cap, "%s: calibration %d/%d%s%s", mname, m->g, g_own.cal_games, wl, mark);
    else snprintf(o, cap, "%s: no games yet%s", mname, mark);
}
#define TIP_ADD(...) do { if (o < cap - 1) { int k_ = snprintf(t + o, (size_t)(cap - o), __VA_ARGS__); o = k_ < 0 ? cap - 1 : (o + k_ > cap - 1 ? cap - 1 : o + k_); } } while (0)
// Rating=own: the slot's text and tooltip (under g_lock); 0 = nothing to show yet
static int own_slot(GFxValue *a, int i, const char *name, const Entry *e, int mode) {
    unsigned long long steam = e ? e->steam : 0;
    if (!g_ownState) return 0;
    if (!steam && !(e && e->fetched)) return 0;
    const OwnRow *r = steam && g_ownState == 1 ? own_find(&g_own, steam) : 0;
    char tag[32], n8[256]; own_tag_utf8(tag, sizeof tag, &g_own, r, mode);
    if (g_showInName) { snprintf(n8, sizeof n8, "%s  [%s]", name, tag); utf8_to_u16(n8, g_nameBuf[i], 160); a[A_NAME].v.ws = g_nameBuf[i]; }
    char t[1200]; int cap = sizeof t, o = 0;
    TIP_ADD("%s", name);
    if (g_ownState != 1) TIP_ADD("\nElite rating: not available right now");
    else if (!steam) TIP_ADD("\nElite rating: not found (no Steam ID for this name)");
    else {
        if (g_own.preseason) TIP_ADD("\nElite rating - preseason"); else TIP_ADD("\nElite rating - season %s", g_own.season[0] ? g_own.season : "?");
        if (mode < 0) TIP_ADD("\n(none for this lobby: only 1v1, 2v2, 3v3)");
        for (int k = -1; k < OWN_NMODES; k++) {
            int mi = k < 0 ? mode : k; if (mi < 0 || (k >= 0 && k == mode)) continue;
            char ln[160]; own_line(ln, sizeof ln, r ? &r->m[mi] : 0, OWN_MODE_NAME[mi], mi == mode); TIP_ADD("\n%s", ln);
        }
    }
    if (e && e->known) { int g = e->wins + e->losses, wr = g ? (e->wins * 100 + g / 2) / g : 0; TIP_ADD("\nRelic rating: %d  (%d W / %d L, %d%%)", e->rating, e->wins, e->losses, wr); }
    else if (e && e->absent) TIP_ADD("\nRelic rating: none");
    { int hours = r && r->hours >= 0 ? r->hours : e && e->known ? e->hours : -1; if (hours >= 0) TIP_ADD("\nSteam hours: %d", hours); }
    utf8_to_u16(t, g_tipBuf[i], 600); a[A_TOOLTIP].type = GFX_WSTRING; a[A_TOOLTIP].v.ws = g_tipBuf[i];
    return 1;
}
static void relic_slot(GFxValue *a, int i, const char *name, const Entry *e) {
    int g = e->wins + e->losses, wr = g ? (e->wins * 100 + g / 2) / g : 0;
    char n8[256], t[600]; int cap = sizeof t, o = 0;
    if (g_showInName) { snprintf(n8, sizeof n8, "%s  [%d]", name, e->rating); utf8_to_u16(n8, g_nameBuf[i], 160); a[A_NAME].v.ws = g_nameBuf[i]; }
    TIP_ADD("%s\nRelic rating: %d\nWin rate: %d%%  (%d W / %d L)\nGames: %d", name, e->rating, wr, e->wins, e->losses, g);
    if (e->hours >= 0) TIP_ADD("\nSteam hours: %d", e->hours);
    utf8_to_u16(t, g_tipBuf[i], 600); a[A_TOOLTIP].type = GFX_WSTRING; a[A_TOOLTIP].v.ws = g_tipBuf[i];
}
static void decorate(GFxValue *args, uint32_t count) {
    if (count < HEADER_ARGS + SLOT_ARGS || (count - HEADER_ARGS) % SLOT_ARGS) {
        static int warned; if (!warned++) mh_log("PopulatePlayerList: unexpected arg count %u, not touching", count);
        return;
    }
    uint32_t slots = (count - HEADER_ARGS) / SLOT_ARGS; int want = 0, people = 0;
    int teamArg = args[0].type == GFX_NUMBER ? (int)args[0].v.num : 0;
    int mode = own_mode_of(g_maxPlayers, teamArg, g_ffa);
    if (g_rating == RATING_OWN && g_stats) {
        uint64_t now = now_ms();
        if (!g_ownTry || now - g_ownTry >= (uint64_t)(g_ownState == 1 ? OWN_EVERY_MS : OWN_RETRY_MS)) { g_ownWant = 1; want = 1; }
    }
    static unsigned long long lastSig; unsigned long long sig = (unsigned)(mode + 1) * 7919ULL + slots;
    char who[16][192]; int nwho = 0;
    pthread_mutex_lock(&g_lock);
    for (uint32_t i = 0; i < slots && i < 16; i++) {
        GFxValue *a = args + HEADER_ARGS + i * SLOT_ARGS;
        if (a[A_HASUSER].type != GFX_BOOL || !a[A_HASUSER].v.b || a[A_SLOTTYPE].type != GFX_NUMBER) continue;
        if (a[A_NAME].type != GFX_WSTRING || !a[A_NAME].v.ws || !a[A_NAME].v.ws[0] || a[A_TOOLTIP].type != GFX_WSTRING) continue;
        char name[192]; u16_to_utf8(a[A_NAME].v.ws, u16len(a[A_NAME].v.ws), name, sizeof name); strip_tag(name);
        if (!name[0]) continue;
        people++; for (const char *c = name; *c; c++) sig = sig * 31 + (unsigned char)*c;
        if (nwho < 16) snprintf(who[nwho++], 192, "%s", name);
        Entry *e; want |= want_name(name, &e);
        if (!g_stats) continue;
        if (g_rating == RATING_OWN) { own_slot(a, (int)i, name, e, mode); continue; }
        if (e->known) relic_slot(a, (int)i, name, e);
    }
    pthread_mutex_unlock(&g_lock);
    if (sig != lastSig) {
        lastSig = sig;
        mh_log("lobby: max players=%d (per team %d%s), people=%d%s", g_maxPlayers, teamArg, g_ffa ? ", FFA" : "", people,
               g_rating != RATING_OWN ? "" : mode < 0 ? ", elite rating: no mode" : mode == OWN_1V1 ? ", elite rating: 1v1" : mode == OWN_2V2 ? ", elite rating: 2v2" : ", elite rating: 3v3");
        for (int k = 0; k < nwho; k++) mh_log("lobby:   %s", who[k]);
    }
    if (want) wake();
}
// main menu: your name in the corner, Receive_SetGamerTag(name, n) - Relic's rating after it (not with Rating=own, as on Windows)
static void decorate_gamertag(GFxValue *args) {
    if (args[0].type != GFX_WSTRING || !args[0].v.ws || !args[0].v.ws[0] || args[0].v.ws == g_tagBuf) return;
    char name[192]; u16_to_utf8(args[0].v.ws, u16len(args[0].v.ws), name, sizeof name); strip_tag(name);
    Entry *e; int want;
    pthread_mutex_lock(&g_lock);
    want = want_name(name, &e);
    if (e->known && g_stats && g_showInName && g_rating != RATING_OWN) {
        char n8[256]; snprintf(n8, sizeof n8, "%s  [%d]", name, e->rating); utf8_to_u16(n8, g_tagBuf, 160); args[0].v.ws = g_tagBuf;
    }
    pthread_mutex_unlock(&g_lock);
    if (want) wake();
}

// ---------- the hook: every Flash call of the game (the game's UI thread)
static const char *method_of(const char *path) { const char *d = path ? strrchr(path, '.') : 0; return d ? d + 1 : path; }
static char hk_invoke(void *view, const char *path, void *result, const GFxValue *args, uint32_t n) {
    const char *m = method_of(path);
    if (!m || !g_enabled || m[0] != 'R' || strncmp(m, "Receive_", 8)) return o_invoke(view, path, result, args, n);
    m += 8;
    static int kicked;                                // Rating=own: the table asked once the game's UI runs (not in the constructor)
    if (!kicked && g_ownWant) { kicked = 1; wake(); }
    if (!strcmp(m, "SetMaxPlayers") && args && n >= 1 && args[0].type == GFX_NUMBER) g_maxPlayers = (int)args[0].v.num;
    else if ((!strcmp(m, "SetFFASolo") || !strcmp(m, "SetFFATeam")) && args && n >= 1 && args[0].type == GFX_BOOL) {
        if (m[5] == 'S') g_ffaSolo = args[0].v.b != 0; else g_ffaTeam = args[0].v.b != 0;
        g_ffa = g_ffaSolo || g_ffaTeam;
    } else if (!strcmp(m, "PopulatePlayerList") && args && n >= HEADER_ARGS + SLOT_ARGS && n <= HEADER_ARGS + SLOT_ARGS * 16) {
        GFxValue copy[HEADER_ARGS + SLOT_ARGS * 16];
        memcpy(copy, args, sizeof(GFxValue) * n);
        decorate(copy, n);
        return o_invoke(view, path, result, copy, n);
    } else if (!strcmp(m, "SetGamerTag") && args && n >= 1 && n <= 8) {
        GFxValue copy[8]; memcpy(copy, args, sizeof(GFxValue) * n);
        decorate_gamertag(copy);
        return o_invoke(view, path, result, copy, n);
    }
    return o_invoke(view, path, result, args, n);
}

// ---------- outside the game (macrun addontest): the data fed as the worker would put it, the lists decorated as in the game
__attribute__((visibility("default"))) int LobbyStats_TestFeed(const char *lines, const char *json, int rating, int maxPlayers, int ffa) {
    int n = 0;
    g_nonet = 1;                                       // nothing goes to the network in a test
    pthread_mutex_lock(&g_lock);
    if (lines) { size_t L = strlen(lines); char *b = malloc(L + 1); if (b) { memcpy(b, lines, L + 1); parse_lines(b, 0); free(b); } }
    if (json) { OwnTable t; memset(&t, 0, sizeof t); if (own_parse(json, strlen(json), &t)) { own_free(&g_own); g_own = t; g_ownState = 1; n = t.n; } else n = -1; }
    pthread_mutex_unlock(&g_lock);
    g_ownTry = now_ms(); g_rating = rating ? RATING_OWN : RATING_RELIC; g_maxPlayers = maxPlayers; g_ffa = ffa;
    return n;
}

__attribute__((constructor)) static void lobbystats_init(void) {
    if (!mh_start("LobbyStats", 12)) return;
    char r[16] = "relic";
    g_stats = mh_ini_int("LobbyStats", "Stats", 1);
    g_showInName = mh_ini_int("LobbyStats", "ShowRatingInName", 1);
    mh_ini_str("LobbyStats", "Rating", r, sizeof r);
    g_rating = !strcasecmp(r, "own") || !strcasecmp(r, "elite") ? RATING_OWN : RATING_RELIC;
    if (!mh_ini_str("LobbyStats", "Server", g_server, sizeof g_server) || !g_server[0] || !strcasecmp(g_server, "rts.fintebtc.de"))
        snprintf(g_server, sizeof g_server, "%s", LS_MIRRORS);
    parse_hosts();
    if (!g_nHosts) g_stats = 0;
    snprintf(g_cachep, sizeof g_cachep, "%.*sLobbyStats.cache", (int)(strrchr(mh_logp, '/') ? strrchr(mh_logp, '/') - mh_logp + 1 : 0), mh_logp);
    if (*(uint64_t *)mh_at(VT_INVOKE) == (uint64_t)hk_invoke) { mh_log("already hooked"); return; }
    // BaseForm::Invoke: mov rsi,[rbx+0x50]; lea rdi,[rbp-0x68]; call SFMovie::GetMovieView; mov rdi,[rbp-0x68]; mov rax,[rdi]; mov rax,[rax+0x90]
    static const uint8_t BFCALL[26] = { 0x48, 0x8B, 0x73, 0x50, 0x48, 0x8D, 0x7D, 0x98, 0xE8, 0xC3, 0xB3, 0x05, 0x02, 0x48, 0x8B, 0x7D, 0x98, 0x48, 0x8B, 0x07,
                                        0x48, 0x8B, 0x80, 0x90, 0x00, 0x00 };
    // lea rsi,[rip+...] ("Receive_PopulatePlayerList"); xor edx,edx; mov rdi,[rbp-0x1d60]; call BaseForm::Invoke
    static const uint8_t PLPOP[24] = { 0x48, 0x8D, 0x35, 0xF5, 0xE4, 0x41, 0x02, 0x31, 0xD2, 0x48, 0x8B, 0xBD, 0xA0, 0xE2, 0xFF, 0xFF, 0xE8, 0x85, 0x49, 0xF7, 0xFF, 0x48, 0x8D, 0xBD };
    static const uint8_t INVOKE[16] = { 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC };
    static const uint8_t STRPOP[27] = "Receive_PopulatePlayerList";
    if (*(uint64_t *)mh_at(VT_INVOKE) != FN_INVOKE + mh_slide || !mh_same(FN_INVOKE, INVOKE, 16) || !mh_same(AT_BF_CALL, BFCALL, 26)
        || !mh_same(AT_PL_POP, PLPOP, 24) || !mh_same(STR_POP, STRPOP, 27)) {
        mh_log("LobbyStats " LS_VERSION ": GFxMovieRoot::Invoke / the lobby list code is not as in 1.0.3 (or hooked by another add-on) - NOT installed"); return;
    }
    load_cache();
    o_invoke = (Invoke_t)mh_at(FN_INVOKE);
    uint64_t hk = (uint64_t)hk_invoke;
    if (!mh_write(mh_at(VT_INVOKE), &hk, 8, MH_DATA)) { mh_log("vm_protect failed - not installed"); return; }
    mh_log("LobbyStats " LS_VERSION " (Mac): rating %s (LobbyStats.ini [LobbyStats] Rating), server %s, %d cached players - installed "
           "(GFxMovieRoot::Invoke vtable slot %#lx)%s", g_rating == RATING_OWN ? "own (Elite, the lobby's mode; none on the main menu)" : "relic",
           g_server, g_ncache, VT_INVOKE, g_stats ? "" : " - Stats=0: names and tooltips left alone");
    if (g_stats && g_rating == RATING_OWN) g_ownWant = 1;               // the table before the first lobby (asked at the first UI call)
}
