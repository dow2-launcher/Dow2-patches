// own_rating.h - the Elite rating table for LobbyStats 0.12.0 (LobbyStats.ini [LobbyStats] Rating=own).
//
// Source: GET <mirror>/ladder/api/own/all (dow2-ladder docs/OWN_RATING_API.md, launcher 1.6.20 reads the same answer):
//   {"season": "2026-pre", "preseason": true, "cal_games": 5, "info": "https://...",
//    "players": {"7656...": {"1v1": {"c": 1584, "rank": 1, "g": 9, "w": 6, "l": 2, "lv": 1, "cal": 0},
//                            "2v2": {"g": 2, "w": 1, "l": 1, "lv": 0, "cal": 3}}}}
//   c = the conservative number (rating - 2*RD, the site's table), rank = the place in that mode's table; both absent while
//   calibrating (cal = calibration games left). l is without the leaves (lv).
//   Per player, optional: "hours_played" (or "h"): Retribution's Steam hours, a number or null (the ladder's /c/me name for it,
//   17.11.2026: not in own/all yet - when it comes, the tooltip takes it before the stats service's lobby line).
// Plain C without Windows, integers only (a number with a fraction keeps its whole part): tested natively by
// lobbystats/test_own.c (make test). A small JSON reader just for this answer - unknown keys and values are skipped, a broken
// or cut answer is refused as a whole (the old table stays).
#ifndef OWN_RATING_H
#define OWN_RATING_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { OWN_1V1, OWN_2V2, OWN_3V3, OWN_NMODES };
static const char *const OWN_MODE_NAME[OWN_NMODES] = {"1v1", "2v2", "3v3"};
typedef struct { int has, rated, c, rank, g, w, l, lv, cal; } OwnMode;     // has: the mode is in his entry; rated: c is there
typedef struct { unsigned long long steam; OwnMode m[OWN_NMODES]; int hours; } OwnRow;   // hours: -1 = not in the answer
typedef struct { OwnRow *rows; int n, cap; char season[16]; int preseason, cal_games; } OwnTable;

typedef struct { const char *p, *e; } OwnJ;
static void oj_ws(OwnJ *j) { while (j->p < j->e && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++; }
static int oj_ch(OwnJ *j, char c) { oj_ws(j); if (j->p < j->e && *j->p == c) { j->p++; return 1; } return 0; }
// a string; out may be NULL (skipped). Escapes: \uXXXX becomes '?', the rest their character. 1 = ok
static int oj_str(OwnJ *j, char *out, int cap) {
    int o = 0;
    if (!oj_ch(j, '"')) return 0;
    while (j->p < j->e && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\') {
            if (j->p >= j->e) return 0;
            c = *j->p++;
            if (c == 'u') { if (j->e - j->p < 4) return 0; j->p += 4; c = '?'; }
            else if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'r') c = '\r';
            else if (c == 'b') c = '\b'; else if (c == 'f') c = '\f';
        }
        if (out && o < cap - 1) out[o++] = c;
    }
    if (out && cap > 0) out[o] = 0;
    return j->p < j->e && *j->p++ == '"';
}
static int oj_word(OwnJ *j, const char *w) {
    size_t n = strlen(w); oj_ws(j);
    if ((size_t)(j->e - j->p) >= n && !memcmp(j->p, w, n)) { j->p += n; return 1; }
    return 0;
}
// a number as a whole number (-12, 1584, 1584.7 -> 1584, 1e3 -> 1); true/false -> 1/0. 1 = ok
static int oj_int(OwnJ *j, long long *v) {
    oj_ws(j);
    if (oj_word(j, "true")) { *v = 1; return 1; }
    if (oj_word(j, "false")) { *v = 0; return 1; }
    int neg = 0, digits = 0; long long x = 0;
    if (j->p < j->e && *j->p == '-') { neg = 1; j->p++; }
    while (j->p < j->e && *j->p >= '0' && *j->p <= '9') { if (x < 100000000000LL) x = x * 10 + (*j->p - '0'); j->p++; digits++; }
    if (!digits) return 0;
    if (j->p < j->e && *j->p == '.') { j->p++; while (j->p < j->e && *j->p >= '0' && *j->p <= '9') j->p++; }
    if (j->p < j->e && (*j->p == 'e' || *j->p == 'E')) {
        j->p++; if (j->p < j->e && (*j->p == '+' || *j->p == '-')) j->p++;
        while (j->p < j->e && *j->p >= '0' && *j->p <= '9') j->p++;
    }
    *v = neg ? -x : x; return 1;
}
static int oj_skip(OwnJ *j, int depth) {
    oj_ws(j);
    if (j->p >= j->e || depth > 32) return 0;
    char c = *j->p;
    if (c == '"') return oj_str(j, NULL, 0);
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']'; j->p++;
        if (oj_ch(j, close)) return 1;
        do {
            if (c == '{') { if (!oj_str(j, NULL, 0) || !oj_ch(j, ':')) return 0; }
            if (!oj_skip(j, depth + 1)) return 0;
        } while (oj_ch(j, ','));
        return oj_ch(j, close);
    }
    if (oj_word(j, "null")) return 1;
    long long v; return oj_int(j, &v);
}
static int own_mode_index(const char *k) {
    for (int i = 0; i < OWN_NMODES; i++) if (!strcmp(k, OWN_MODE_NAME[i])) return i;
    return -1;
}
// one mode of a player: {"c": .., "rank": .., ...}
static int oj_mode(OwnJ *j, OwnMode *m) {
    if (!oj_ch(j, '{')) return 0;
    memset(m, 0, sizeof *m); m->has = 1;
    if (oj_ch(j, '}')) return 1;
    do {
        char k[16]; long long v;
        if (!oj_str(j, k, sizeof k) || !oj_ch(j, ':')) return 0;
        int *f = !strcmp(k, "c") ? &m->c : !strcmp(k, "rank") ? &m->rank : !strcmp(k, "g") ? &m->g : !strcmp(k, "w") ? &m->w :
                 !strcmp(k, "l") ? &m->l : !strcmp(k, "lv") ? &m->lv : !strcmp(k, "cal") ? &m->cal : NULL;
        oj_ws(j);
        if (f && j->p < j->e && *j->p != 'n') {            // "c": null is the same as no "c"
            if (!oj_int(j, &v)) return 0;
            if (v > 1000000000LL) v = 1000000000LL;
            if (v < -1000000000LL) v = -1000000000LL;
            *f = (int)v; if (f == &m->c) m->rated = 1;
        } else if (!oj_skip(j, 3)) return 0;
    } while (oj_ch(j, ','));
    return oj_ch(j, '}');
}
static int own_add(OwnTable *t, const OwnRow *r) {
    if (t->n == t->cap) {
        int nc = t->cap ? t->cap * 2 : 1024; OwnRow *nr = (OwnRow *)realloc(t->rows, sizeof(OwnRow) * nc);
        if (!nr) return 0;
        t->rows = nr; t->cap = nc;
    }
    t->rows[t->n++] = *r; return 1;
}
static int own_cmp(const void *a, const void *b) {
    unsigned long long x = ((const OwnRow *)a)->steam, y = ((const OwnRow *)b)->steam;
    return x < y ? -1 : x > y;
}
static void own_free(OwnTable *t) { free(t->rows); memset(t, 0, sizeof *t); }
// the whole answer into *t (emptied first). 1 = ok (the table has "players", maybe none), 0 = not this answer / broken / cut
static int own_parse(const char *buf, size_t len, OwnTable *t) {
    OwnJ j = {buf, buf + len}; int players = 0;
    memset(t, 0, sizeof *t); t->cal_games = 5;
    if (!oj_ch(&j, '{')) return 0;
    if (!oj_ch(&j, '}')) { do {
        char k[32];
        if (!oj_str(&j, k, sizeof k) || !oj_ch(&j, ':')) goto bad;
        if (!strcmp(k, "season")) {
            oj_ws(&j);
            if (j.p < j.e && *j.p == '"') { if (!oj_str(&j, t->season, sizeof t->season)) goto bad; }
            else if (!oj_skip(&j, 1)) goto bad;
        } else if (!strcmp(k, "preseason") || !strcmp(k, "cal_games")) {
            long long v; oj_ws(&j);
            if (j.p < j.e && (*j.p == '-' || (*j.p >= '0' && *j.p <= '9') || *j.p == 't' || *j.p == 'f')) {
                if (!oj_int(&j, &v)) goto bad;
                if (k[0] == 'p') t->preseason = v != 0; else if (v > 0 && v < 100) t->cal_games = (int)v;
            } else if (!oj_skip(&j, 1)) goto bad;
        } else if (!strcmp(k, "players")) {
            if (!oj_ch(&j, '{')) goto bad;
            players = 1;
            if (!oj_ch(&j, '}')) { do {
                char sk[32]; OwnRow r; memset(&r, 0, sizeof r); r.hours = -1;
                if (!oj_str(&j, sk, sizeof sk) || !oj_ch(&j, ':') || !oj_ch(&j, '{')) goto bad;
                char *end; r.steam = strtoull(sk, &end, 10); if (*end) r.steam = 0;
                if (!oj_ch(&j, '}')) {
                    do {
                        char mk[16]; OwnMode m;
                        if (!oj_str(&j, mk, sizeof mk) || !oj_ch(&j, ':')) goto bad;
                        int mi = own_mode_index(mk); oj_ws(&j);
                        if (mi >= 0 && j.p < j.e && *j.p == '{') { if (!oj_mode(&j, &m)) goto bad; r.m[mi] = m; }
                        else if ((!strcmp(mk, "hours_played") || !strcmp(mk, "h")) && j.p < j.e && (*j.p == '-' || (*j.p >= '0' && *j.p <= '9'))) {
                            long long v; if (!oj_int(&j, &v)) goto bad;
                            r.hours = v < 0 ? -1 : v > 1000000 ? 1000000 : (int)v;   // a fraction keeps its whole part
                        } else if (!oj_skip(&j, 2)) goto bad;
                    } while (oj_ch(&j, ','));
                    if (!oj_ch(&j, '}')) goto bad;
                }
                if (r.steam && !own_add(t, &r)) goto bad;
            } while (oj_ch(&j, ','));
            if (!oj_ch(&j, '}')) goto bad; }
        } else if (!oj_skip(&j, 1)) goto bad;
    } while (oj_ch(&j, ','));
    if (!oj_ch(&j, '}')) goto bad; }
    if (!players) goto bad;
    if (t->n > 1) qsort(t->rows, t->n, sizeof(OwnRow), own_cmp);
    return 1;
bad:
    own_free(t); t->cal_games = 5; return 0;
}
static const OwnRow *own_find(const OwnTable *t, unsigned long long steam) {
    int lo = 0, hi = t->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2; unsigned long long s = t->rows[mid].steam;
        if (s == steam) return &t->rows[mid];
        if (s < steam) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}
// the lobby's mode from its size: players per team 1..3 -> OWN_1V1..OWN_3V3; FFA, 4v4 or not known -> -1 (no Elite rating)
static int own_mode_of(int maxPlayers, int teamSizeArg, int ffa) {
    int ts = maxPlayers >= 2 ? maxPlayers / 2 : teamSizeArg;
    if (ffa || ts < 1 || ts > OWN_NMODES) return -1;
    return ts - 1;
}
// the short text after the name (ASCII + the dash; 24 chars at most): "1584", "cal 2/5", "—" (no rating in this mode)
// cal k/N: k = his games in the mode (as launcher 1.6.20: "calibration: 2/5"), N = the season's calibration length
#define OWN_DASH "\xE2\x80\x94"
static void own_tag_utf8(char *out, int cap, const OwnTable *t, const OwnRow *r, int mode) {
    const OwnMode *m = r && mode >= 0 ? &r->m[mode] : NULL;
    if (m && m->rated) snprintf(out, cap, "%d", m->c);
    else if (m && m->has && m->cal > 0) snprintf(out, cap, "cal %d/%d", m->g, t->cal_games);
    else snprintf(out, cap, "%s", OWN_DASH);
}
#endif
