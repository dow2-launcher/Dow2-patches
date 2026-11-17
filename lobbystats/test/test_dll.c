// LobbyStats.dll (0.12.0) and LoadingRatings.dll (5) under Wine, no game: the lobby list's names and tooltips, the loading
// screen's names and the main menu's own name, for Rating=relic and Rating=own.
//   make build/LobbyStats.dll build/LoadingRatings.dll build/test_lobbystats.exe
//   wine build/test_lobbystats.exe build/LobbyStats.dll lobbystats/test/own_all.json
//   wine build/test_lobbystats.exe build/LoadingRatings.dll lobbystats/test/own_all.json
// Feeds lobby lines and the own/all answer through LobbyStats_TestOwn, then runs the PopulatePlayerList substitution
// (LobbyStats_Test) on a fake argument set as the game sends it: 5 header values + 20 per slot. Nothing goes to the network
// (every name is in the fed lines; the table is fed). Exit code = the failed checks.
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

typedef struct { int type; int pad; union { double num; unsigned char b; const wchar_t *ws; const char *s; } v; } GFxValue;
enum { GFX_BOOL = 2, GFX_NUMBER = 3, GFX_WSTRING = 5 };
typedef int (*Test_t)(GFxValue *, unsigned);
typedef int (*TestOwn_t)(const char *, const char *, int, int, int);
typedef const wchar_t *(*TestText_t)(const wchar_t *);
static Test_t T; static TestOwn_t TO; static TestText_t TL, TM;
static int fails, checks;
static GFxValue args[5 + 20 * 8];

static void slot(int i, int pos, const wchar_t *name) {
    GFxValue *a = args + 5 + i * 20;
    memset(a, 0, sizeof(GFxValue) * 20);
    a[0].type = GFX_BOOL; a[0].v.b = 1;
    a[3].type = GFX_NUMBER; a[3].v.num = pos;
    a[4].type = GFX_NUMBER; a[4].v.num = i ? 1 : 0;
    a[5].type = GFX_WSTRING; a[5].v.ws = name;
    a[19].type = GFX_WSTRING; a[19].v.ws = L"Left click for more options.";
}
static unsigned lobby(int perTeam, const wchar_t **names, int n) {
    memset(args, 0, sizeof args);
    args[0].type = GFX_NUMBER; args[0].v.num = perTeam;
    args[1].type = GFX_BOOL; args[1].v.b = 1;
    args[3].type = GFX_WSTRING; args[3].v.ws = L"Your Team";
    args[4].type = GFX_WSTRING; args[4].v.ws = L"Enemy Team";
    // positions as the game numbers them: team 1 from 0, team 2 from 4 (a 1v1 is 0 and 4; 1..3 would be observer seats)
    for (int i = 0; i < n; i++) slot(i, i < (n + 1) / 2 ? i : 4 + i - (n + 1) / 2, names[i]);
    return 5 + 20 * n;
}
static void u8(const wchar_t *w, char *o, int cap) { WideCharToMultiByte(CP_UTF8, 0, w ? w : L"(null)", -1, o, cap, NULL, NULL); }
static void check(const char *what, int i, const wchar_t *wantName, const wchar_t *tipHas) {
    GFxValue *a = args + 5 + i * 20; char n8[256], t8[1200], w8[256];
    u8(a[5].v.ws, n8, sizeof n8); u8(a[19].v.ws, t8, sizeof t8); u8(wantName, w8, sizeof w8);
    int ok = !wcscmp(a[5].v.ws, wantName) && (!tipHas || wcsstr(a[19].v.ws, tipHas));
    checks++; if (!ok) fails++;
    printf("%s %s: name \"%s\"%s\n", ok ? "ok  " : "FAIL", what, n8, ok ? "" : "");
    if (!ok) printf("     want \"%s\"\n", w8);
    if (tipHas || !ok) printf("     tooltip: %s\n", t8);
}
// a text through the loading screen's widget (TL) or the main menu's SetGamerTag (TM): what the game would show
static void check_text(const char *what, TestText_t f, const wchar_t *in, const wchar_t *want) {
    const wchar_t *got = f(in); char g8[256], w8[256]; u8(got, g8, sizeof g8); u8(want, w8, sizeof w8);
    int ok = got && !wcscmp(got, want); checks++; if (!ok) fails++;
    printf("%s %s: \"%s\"\n", ok ? "ok  " : "FAIL", what, g8);
    if (!ok) printf("     want \"%s\"\n", w8);
}
static char *slurp(const char *fn) {
    FILE *f = fopen(fn, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(L + 1); size_t n = fread(b, 1, L, f); b[n] = 0; fclose(f); return b;
}
int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: test_lobbystats.exe LobbyStats.dll own_all.json\n"); return 2; }
    HMODULE m = LoadLibraryA(argv[1]); if (!m) { printf("FAIL LoadLibrary %s: %lu\n", argv[1], GetLastError()); return 2; }
    T = (Test_t)GetProcAddress(m, "LobbyStats_Test"); TO = (TestOwn_t)GetProcAddress(m, "LobbyStats_TestOwn");
    TL = (TestText_t)GetProcAddress(m, "LobbyStats_TestLoad"); TM = (TestText_t)GetProcAddress(m, "LobbyStats_TestMenuName");
    typedef int (*Build_t)(void); Build_t B = (Build_t)GetProcAddress(m, "LobbyStats_Build");
    if (!T || !TO || !TL || !TM || !B) { printf("FAIL no test exports\n"); return 2; }
    int loading = B() == 1;
    printf("%s\n", loading ? "LoadingRatings: the loading screen (the lobby untouched)" : "LobbyStats: the lobby");
    char *json = slurp(argv[2]); if (!json) { printf("FAIL no %s\n", argv[2]); return 2; }
    // lobby lines as /stats/api/lobby answers: name, rating, W, L, hours, pid[, steam]
    const char *lines =
        "SirItos\t1953\t143\t128\t812\t11526814\t76561198005350431\n"
        "grantovich\t1801\t40\t30\t-\t555\t76561198002971848\n"
        "newbie\t-\n"
        "oldline\t1600\t10\t10\t-\t777\n"
        "calguy\t1500\t1\t1\t-\t778\t76561197960287930\n";
    const wchar_t *two[] = {L"SirItos", L"grantovich"};
    const wchar_t *six[] = {L"SirItos", L"grantovich", L"newbie", L"oldline", L"calguy", L"stranger"};
    if (loading) {
        // LoadingRatings 5: Rating comes from the same key; the lobby list itself is never changed
        TO(lines, NULL, 0, 2, 0); T(args, lobby(1, two, 2));
        check("loading relic: lobby untouched", 0, L"SirItos", NULL);
        check_text("loading relic: the Relic number", TL, L"SirItos", L"SirItos  [1953]");
        check_text("loading relic: main menu name as in 4", TM, L"SirItos", L"SirItos  [1953]");
        TO(NULL, NULL, 1, 2, 0); T(args, lobby(1, two, 2));
        check_text("loading own, no table yet: untouched", TL, L"SirItos", L"SirItos");
        int n = TO(NULL, json, 1, 2, 0); checks++; if (n != 4) fails++; printf("%s own/all fixture: %d players\n", n == 4 ? "ok  " : "FAIL", n);
        T(args, lobby(1, two, 2));
        check("loading own: lobby untouched", 0, L"SirItos", L"Left click");
        check_text("loading own 1v1: the Elite number", TL, L"SirItos", L"SirItos  [1584]");
        check_text("loading own 1v1: no games in 1v1", TL, L"grantovich", L"grantovich  [\x2014]");
        check_text("loading own: another text untouched", TL, L"Loading...", L"Loading...");
        check_text("loading own: no rating on the main menu", TM, L"SirItos", L"SirItos");
        TO(NULL, NULL, 1, 4, 0); T(args, lobby(2, two, 2));
        check_text("loading own 2v2: calibration", TL, L"SirItos", L"SirItos  [cal 2/5]");
        TO(NULL, NULL, 1, 6, 0); T(args, lobby(3, six, 6));
        check_text("loading own 3v3: rated", TL, L"grantovich", L"grantovich  [1712]");
        check_text("loading own 3v3: no Steam ID", TL, L"newbie", L"newbie  [\x2014]");
        check_text("loading own 3v3: not looked up yet", TL, L"stranger", L"stranger");
        TO(NULL, NULL, 1, 2, 1); T(args, lobby(1, two, 2));
        check_text("loading own FFA: a dash", TL, L"SirItos", L"SirItos  [\x2014]");
        TO(NULL, NULL, 0, 2, 0); T(args, lobby(1, two, 2));
        check_text("loading relic again", TL, L"SirItos", L"SirItos  [1953]");
        printf("%d/%d checks passed\n", checks - fails, checks);
        fflush(stdout);
        ExitProcess(fails);
    }

    // Rating=relic: as 0.11 (the table is not even asked)
    TO(lines, NULL, 0, 2, 0); T(args, lobby(1, two, 2));
    check("relic: rating after the name", 0, L"SirItos  [1953]", L"Relic rating: 1953");
    check_text("relic: main menu name with the rating", TM, L"SirItos", L"SirItos  [1953]");
    check_text("LobbyStats leaves the loading screen to LoadingRatings", TL, L"SirItos", L"SirItos");

    // Rating=own before the table came: the names stay as the game sent them
    TO(NULL, NULL, 1, 2, 0); T(args, lobby(1, two, 2));
    check("own, no table yet: untouched", 0, L"SirItos", NULL);

    int n = TO(NULL, json, 1, 2, 0); checks++; if (n != 4) fails++; printf("%s own/all fixture: %d players\n", n == 4 ? "ok  " : "FAIL", n);
    T(args, lobby(1, two, 2));
    check("own 1v1: the number", 0, L"SirItos  [1584]", L"1v1: 1584, place 1 - 6 W / 2 L  (this lobby)");
    check("own 1v1: tooltip keeps Relic", 0, L"SirItos  [1584]", L"Relic rating: 1953  (143 W / 128 L, 53%)");
    check("own 1v1: hours_played null -> the lobby line's hours", 0, L"SirItos  [1584]", L"\nSteam hours: 812");
    check("own 1v1: hours_played from own/all", 1, L"grantovich  [\x2014]", L"\nSteam hours: 2400");
    check_text("own: no rating on the main menu", TM, L"SirItos", L"SirItos");
    check("own 1v1: no games in 1v1", 1, L"grantovich  [\x2014]", L"1v1: no games yet  (this lobby)");

    TO(NULL, NULL, 1, 4, 0); T(args, lobby(2, two, 2));
    check("own 2v2: calibration", 0, L"SirItos  [cal 2/5]", L"2v2: calibration 2/5 - 1 W / 1 L  (this lobby)");

    TO(NULL, NULL, 1, 6, 0); T(args, lobby(3, six, 6));
    check("own 3v3: rated", 1, L"grantovich  [1712]", L"3v3: 1712, place 3 - 10 W / 4 L  (this lobby)");
    check("own 3v3: not in this mode", 0, L"SirItos  [\x2014]", L"1v1: 1584, place 1");
    check("own 3v3: no Relic profile, no Steam ID", 2, L"newbie  [\x2014]", L"Elite rating: not found");
    check("own 3v3: old server line without Steam ID", 3, L"oldline  [\x2014]", L"Relic rating: 1600");
    check("own 3v3: cal 0/5 is 2v2 only", 4, L"calguy  [\x2014]", L"2v2: calibration 0/5");
    check("own 3v3: hours from own/all's \"h\"", 4, L"calguy  [\x2014]", L"\nSteam hours: 7");
    check("own 3v3: not looked up yet: untouched", 5, L"stranger", NULL);

    TO(NULL, NULL, 1, 0, 0); T(args, lobby(3, two, 2));
    check("own: no SetMaxPlayers -> the list's per-team 3", 1, L"grantovich  [1712]", NULL);

    TO(NULL, NULL, 1, 2, 1); T(args, lobby(1, two, 2));
    check("own FFA: no mode", 0, L"SirItos  [\x2014]", L"(none for this lobby");

    TO(NULL, NULL, 1, 8, 0); T(args, lobby(4, two, 2));
    check("own 4v4: no mode", 0, L"SirItos  [\x2014]", NULL);

    // a name a LobbyStats higher in the chain already tagged: the old tag goes
    { const wchar_t *tagged[] = {L"SirItos  [cal 2/5]", L"grantovich  [\x2014]"};
      TO(NULL, NULL, 1, 2, 0); T(args, lobby(1, tagged, 2));
      check("chain: old tag replaced", 0, L"SirItos  [1584]", NULL);
      check("chain: old dash replaced", 1, L"grantovich  [\x2014]", NULL); }
    { const wchar_t *brackets[] = {L"[RUS] Vasya  [abc]", L"x"};
      TO(NULL, NULL, 1, 2, 0); T(args, lobby(1, brackets, 1));
      check("a name with its own brackets is not cut", 0, L"[RUS] Vasya  [abc]", NULL); }

    // a broken answer later keeps the table
    n = TO(NULL, "{\"players\": {\"1\": ", 1, 2, 0); checks++; if (n != -1) fails++; printf("%s broken answer refused (%d)\n", n == -1 ? "ok  " : "FAIL", n);
    T(args, lobby(1, two, 2));
    check("old table kept", 0, L"SirItos  [1584]", NULL);

    // back to relic: as before
    TO(NULL, NULL, 0, 2, 0); T(args, lobby(1, two, 2));
    check("relic again", 0, L"SirItos  [1953]", L"Win rate: 53%");
    check_text("relic again: main menu name with the rating", TM, L"SirItos", L"SirItos  [1953]");

    printf("%d/%d checks passed\n", checks - fails, checks);
    fflush(stdout);
    ExitProcess(fails);
}
