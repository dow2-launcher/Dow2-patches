// test_net.c - CI (the "mac" job, macOS runner): LobbyStats.dylib's HTTPS code (http_mac.h, the system's CFNetwork) against our
// stats hosts, as the add-on asks them: the stats service's lobby line and the Elite table (own_rating.h parses it). Informational
// (the job does not fail on it: a host may be down) - it shows that the request code works on a real macOS.
#include "http_mac.h"
#include "../../lobbystats/own_rating.h"
#include <stdio.h>
int main(void) {
    const char *hosts[] = { "rts.fintebtc.de", "dow2.fintebtc.de", "dow2ru.fintebtc.de" };
    int okLobby = 0, okOwn = 0;
    for (int i = 0; i < 3; i++) {
        char url[256], *buf = 0; int cap = 0, got = 0;
        snprintf(url, sizeof url, "https://%s/stats/api/lobby?n=SirItos", hosts[i]);
        uint64_t t = hm_ms(); int code = http_mac_get(url, "LobbyStats-mac/ci", &buf, &cap, 64 * 1024 - 1, &got, 15000);
        printf("%-20s lobby line: HTTP %d, %d bytes, %llu ms: %.80s\n", hosts[i], code, got, (unsigned long long)(hm_ms() - t), buf ? buf : "");
        okLobby += code == 200 && got > 0;
        snprintf(url, sizeof url, "https://%s/ladder/api/own/all", hosts[i]);
        t = hm_ms(); got = 0; code = http_mac_get(url, "LobbyStats-mac/ci", &buf, &cap, 16 * 1024 * 1024, &got, 15000);
        OwnTable tb; memset(&tb, 0, sizeof tb); int parsed = code == 200 && got > 0 && own_parse(buf, (size_t)got, &tb);
        printf("%-20s elite table: HTTP %d, %d bytes, %llu ms, %s (%d players, season %s)\n", hosts[i], code, got, (unsigned long long)(hm_ms() - t),
               parsed ? "parsed" : "NOT parsed", tb.n, tb.season[0] ? tb.season : "?");
        okOwn += parsed; own_free(&tb); free(buf);
    }
    // a host that does not exist: an answer of 0 within the time limit, no hang
    { char *buf = 0; int cap = 0, got = 0; uint64_t t = hm_ms();
      int code = http_mac_get("https://no-such-host.invalid/x", "LobbyStats-mac/ci", &buf, &cap, 1024, &got, 5000);
      printf("no-such-host.invalid: HTTP %d after %llu ms (want 0, < 6 s)\n", code, (unsigned long long)(hm_ms() - t)); free(buf); }
    printf("%s: lobby line from %d of 3 hosts, elite table from %d of 3\n", okLobby && okOwn ? "OK" : "NOT OK", okLobby, okOwn);
    return okLobby && okOwn ? 0 : 1;
}
