// Native test of own_rating.h (the Elite table of LobbyStats 0.12.0): make test. Fixture: own_all.json (the format of
// dow2-ladder docs/OWN_RATING_API.md "GET /ladder/api/own/all"), plus broken / cut answers that must be refused.
#include "../own_rating.h"
#include <stdio.h>
static int fails, checks;
#define OK(name, cond) do { checks++; if (!(cond)) { fails++; printf("FAIL %s\n", name); } else printf("ok   %s\n", name); } while (0)
static char *slurp(const char *fn, size_t *n) {
    FILE *f = fopen(fn, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(L + 1); *n = fread(b, 1, L, f); b[*n] = 0; fclose(f); return b;
}
static void tag(char *o, const OwnTable *t, unsigned long long s, int mode) { own_tag_utf8(o, 32, t, own_find(t, s), mode); }
int main(int argc, char **argv) {
    size_t n; char *j = slurp(argc > 1 ? argv[1] : "lobbystats/test/own_all.json", &n);
    if (!j) { printf("FAIL no fixture\n"); return 1; }
    OwnTable t; char s[32];
    OK("parse the fixture", own_parse(j, n, &t));
    OK("4 players (bad key dropped)", t.n == 4);
    OK("season / preseason / cal_games", !strcmp(t.season, "2026-pre") && t.preseason == 1 && t.cal_games == 5);
    const OwnRow *a = own_find(&t, 76561198005350431ULL), *b = own_find(&t, 76561198002971848ULL), *c = own_find(&t, 76561197960287930ULL);
    OK("rated 1v1: c rank g w l lv", a && a->m[OWN_1V1].rated && a->m[OWN_1V1].c == 1584 && a->m[OWN_1V1].rank == 1 &&
       a->m[OWN_1V1].g == 9 && a->m[OWN_1V1].w == 6 && a->m[OWN_1V1].l == 2 && a->m[OWN_1V1].lv == 1);
    OK("calibrating 2v2: no c, cal left 3", a && a->m[OWN_2V2].has && !a->m[OWN_2V2].rated && a->m[OWN_2V2].cal == 3 && a->m[OWN_2V2].g == 2);
    OK("mode not played: absent", a && !a->m[OWN_3V3].has);
    OK("fraction -> whole part, unknown keys skipped", b && b->m[OWN_3V3].rated && b->m[OWN_3V3].c == 1712 && b->m[OWN_3V3].rank == 3);
    OK("4v4 ignored", b && !b->m[OWN_1V1].has && !b->m[OWN_2V2].has);
    OK("\"c\": null = not rated", c && !c->m[OWN_2V2].rated && c->m[OWN_2V2].cal == 5);
    OK("empty player kept, unknown steam not found", own_find(&t, 76561198000000001ULL) && !own_find(&t, 76561198099999999ULL));
    tag(s, &t, 76561198005350431ULL, OWN_1V1); OK("tag 1v1 = 1584", !strcmp(s, "1584"));
    tag(s, &t, 76561198005350431ULL, OWN_2V2); OK("tag 2v2 = cal 2/5", !strcmp(s, "cal 2/5"));
    tag(s, &t, 76561198005350431ULL, OWN_3V3); OK("tag 3v3 = dash", !strcmp(s, OWN_DASH));
    tag(s, &t, 76561197960287930ULL, OWN_2V2); OK("tag cal 0/5", !strcmp(s, "cal 0/5"));
    tag(s, &t, 76561198099999999ULL, OWN_1V1); OK("tag unknown player = dash", !strcmp(s, OWN_DASH));
    tag(s, &t, 76561198005350431ULL, -1); OK("tag no mode (FFA/4v4) = dash", !strcmp(s, OWN_DASH));
    OK("mode: 2 players -> 1v1", own_mode_of(2, 0, 0) == OWN_1V1);
    OK("mode: 4 -> 2v2, 6 -> 3v3", own_mode_of(4, 0, 0) == OWN_2V2 && own_mode_of(6, 1, 0) == OWN_3V3);
    OK("mode: 8 -> none, FFA -> none", own_mode_of(8, 0, 0) == -1 && own_mode_of(2, 1, 1) == -1);
    OK("mode: no SetMaxPlayers -> the list's per-team number", own_mode_of(0, 3, 0) == OWN_3V3 && own_mode_of(0, 0, 0) == -1);
    own_free(&t);
    // refused answers: the table stays empty and the caller keeps its old one
    for (size_t cut = 1; cut < n; cut += 7) {
        char k = j[cut]; j[cut] = 0;
        int r = own_parse(j, cut, &t); j[cut] = k;
        if (r) { size_t e = cut; while (e < n && (j[e] == ' ' || j[e] == '\n' || j[e] == '\r')) e++; if (e < n) { OK("cut answer refused", 0); printf("     at %zu\n", cut); break; } }
        own_free(&t);
    }
    OK("every cut answer refused", 1);
    OK("404 html refused", !own_parse("<html>404</html>", 16, &t));
    OK("JSON without players refused", !own_parse("{\"season\": \"x\"}", 15, &t));
    OK("error JSON refused", !own_parse("{\"error\": \"not found\"}", 22, &t));
    OK("empty players accepted", own_parse("{\"players\": {}, \"cal_games\": 7}", 31, &t) && t.n == 0 && t.cal_games == 7);
    own_free(&t);
    // a big table: 20000 players, found by binary search
    { size_t cap = 20000 * 120 + 64, o = 0; char *big = malloc(cap);
      o += sprintf(big + o, "{\"players\": {");
      for (int i = 0; i < 20000; i++) o += sprintf(big + o, "%s\"%llu\": {\"1v1\": {\"c\": %d, \"rank\": %d, \"g\": 5, \"w\": 3, \"l\": 2, \"lv\": 0, \"cal\": 0}}",
                                                   i ? ", " : "", 76561198000000000ULL + (unsigned long long)(19999 - i) * 3, 1000 + i, i + 1);
      o += sprintf(big + o, "}}");
      int ok = own_parse(big, o, &t);
      const OwnRow *r = own_find(&t, 76561198000000000ULL + 3ULL * 19999);
      OK("20000 players parsed and found", ok && t.n == 20000 && r && r->m[OWN_1V1].c == 1000 && !own_find(&t, 76561198000000001ULL));
      own_free(&t); free(big); }
    free(j);
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails != 0;
}
