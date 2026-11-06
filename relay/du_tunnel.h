// du_tunnel.h - Relay 23 (05.11.2026): the delivery of our UDP tunnel "DU" - one game connection carried in UDP packets to
// a node's udp_gateway. The C twin of gameserver/du_proto.py (dow2-ladder), which udp_gateway.py, du_client.py and
// du_egress.py use: the same rules, the same numbers. Included by relay.c; relay/test/du_harness.c builds it on Linux for the
// network tests (relay/test/README.md).
//
// The wire format did not change (Relay 15-22, udp_gateway since 01.11) - an old gateway works with Relay 23, an old
// Relay.dll with a new gateway:
//   "DU" + type + connection id (u32 LE) + body
//   1 SYN version | 2 SYNACK [version] | 3 DATA seq ack sack bytes | 4 ACK ack sack | 5 FIN | 6 PING | 7 PONG
//   8 DATA2 seq ack sack k k*(seq u32, len u16, bytes) bytes | 9 ACK2 ack sack k copies     (version 2, Relay 20)
//   ack = the next number expected, sack bit i = ack+1+i arrived
//
// Why (05.11.2026): a 2v2 on our server went to ~650 packets/s per player for 10 minutes (Relay.log: sent 399559, sent again
// 16495, copies 1071947). The flood itself came from the game (battle_relay.py handed the game's proxy messages to every
// peer, fixed there), but the tunnel had no brakes: every overdue packet had its own timer (the whole window resent, 60 ms
// floor), copies rode on every packet and every ack while anything was unacknowledged, nothing ever gave up. Now (the
// owner's limits):
//  1. one resend timer per connection (as TCP, Karn): on expiry only the OLDEST unacknowledged packet goes again, plus the
//     holes the peer's sack shows below the newest packet it has; the timer doubles, floor 200 ms, ceiling 3 s; the round
//     trip is taken only from a packet sent once, from an ack that came cleanly (no hole before it). Before it, a tail probe
//     (TCP's TLP): after RTT + 2 x RTTVAR + 10 ms (20 ms at least) without an ack the newest packet goes once more - a lost LAST packet
//     has no later one to carry its copy - once per ack that moves on.
//  2. a packet sent 11 times (10 resends, ~20 s with the doubling) = the connection is dead: FIN, "ev=dead" in Relay.log.
//  3. resends are budgeted (token bucket): max(20/s, 25% of the fresh packets/s); a packet is copied at most twice in its
//     life and only while younger than max(2 x round trip, 200 ms); an ACK2 (an ack with copies) at most once per RTT/4 (10 ms
//     at least) - with one per round trip the copy of a lost packet waited for the next game packet (~100 ms: p99 of a turn
//     +10 ms at 5 % loss in the tests); the copies stay bounded by the 2 per packet either way.
//  4. at most 64 packets / 64 KB in flight; full -> the game's socket is not read (back pressure: its bytes wait there and
//     come out later in bigger packets); fresh packets paced to 250/s (burst 20) - a game sends ~10/s.
//  5. in-order data: at most one plain ack per 10 ms (a hole is told at once); a duplicate at most once per 20 ms; an ACK / ACK2 is never answered at once (a hole an ACK2 filled is
//     told with the next packet, or with a plain ACK at the next tick - a plain ACK nobody answers, so no bouncing).
//  6. a fuse: resends + copies > 3 x fresh in 5 s (copies alone are at most 2 x) (and 100+), or more than 300 packets/s out -> this connection's copies off,
//     resends cut to 20/s, "ev=storm" with the numbers in Relay.log.
//  7. what goes to the game waits in a queue (up to 1 MB) when the game does not read for a while - the tunnel keeps
//     acknowledging instead of blocking in send() (Relay 20-22 blocked there: no acks at all while the game hung).

enum { U_SYN = 1, U_SYNACK, U_DATA, U_ACK, U_FIN, U_PING, U_PONG, U_DATA2, U_ACK2 };
#define U_MAXP 1200
#define U_VER 2
#define U_COPIES 2
#define U_BUDGET 1240
#define U_RWIN 512                  // receive ring: accepted up to this far ahead of the next expected (old ends keep 512 in flight)
#define U_SRING 128                 // send ring (in flight is at most DU_WIN_PKTS)
#define DU_WIN_PKTS 64
#define DU_WIN_BYTES 65536
#define DU_RTO_MIN 0.2
#define DU_RTO_MAX 3.0
#define DU_MAX_SENDS 11
#define DU_RESEND_FLOOR 20.0
#define DU_RESEND_SHARE 0.25
#define DU_COPY_MAX 2
#define DU_PACE 250.0
#define DU_PACE_BURST 20.0          // 20 x 1240 B = 25 KB at once at most
#define DU_DUP_ACK_GAP 0.02
#define DU_TLP_MIN 0.02              // tail probe after max(RTT + 2 x RTTVAR + 10 ms, 20 ms) of silence, once per ack that moves on
#define DU_ACK_DELAY 0.01            // in-order data: at most one plain ack per 10 ms (a hole is told at once)
#define DU_KEEPALIVE 1.0
#define DU_IDLE 30.0
#define DU_FUSE_WINDOW 5.0
#define DU_FUSE_EXTRA 3.0           // copies alone are at most DU_COPY_MAX = 2 x fresh by design: the fuse wants resends on top
#define DU_FUSE_RATE 300.0
#define DU_FUSE_MIN 100
#define DU_OUTQ_MAX (1 << 20)

#ifndef DU_NFDS
#define DU_NFDS(a, b) 0             // Windows select() ignores the first argument
#endif

static void u_hdr(unsigned char *b, int t, DWORD cid) { b[0] = 'D'; b[1] = 'U'; b[2] = (unsigned char)t; memcpy(b + 3, &cid, 4); }

static double du_now(void) {
#ifdef _WIN32
    static LARGE_INTEGER f; LARGE_INTEGER c; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c); return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9;
#endif
}

typedef struct { unsigned char d[U_MAXP]; int n, live, sends, copies; double first, last; } USnd;
typedef struct { double rate, cap, tok, t; } UBucket;
static void ub_init(UBucket *b, double rate, double cap, double now) { b->rate = rate; b->cap = cap; b->tok = cap; b->t = now; }
static void ub_fill(UBucket *b, double now) { b->tok += (now - b->t) * b->rate; if (b->tok > b->cap) b->tok = b->cap; b->t = now; }
static int ub_take(UBucket *b, double now) { ub_fill(b, now); if (b->tok >= 1.0) { b->tok -= 1.0; return 1; } return 0; }

typedef struct {
    SOCKET u; DWORD cid; int ver;
    USnd *snd; unsigned char (*rcv)[U_MAXP]; int *rlen; char *rhave;
    DWORD snext, sbase, rnext; int inflight;              // inflight = bytes of the live packets
    double srtt, rttvar; int have_rtt, backoff, tlp_done; double rto_at;
    DWORD last_ack_seen, peer_ack, peer_sack, fast_done; int dups, fast_valid;
    double seen, sent, last_ack_out, last_ack2; int ack_due;
    UBucket pace, rb; double fresh_rate, fresh_t; unsigned fresh_n0;
    int copies_on, fuse; double win_t; unsigned win_fresh, win_extra, win_out;
    unsigned sentn, gotn, resent, gapn, copies, saved, acks, dupin, paced, bskip;
    const char *ended;
    unsigned char *outq; int outq_len;                    // in order, for the game, not taken by it yet
    int filt; unsigned char *fq; int fq_len;              // Relay 24: the server's own frames (0x7e) taken out of the stream
    unsigned char pk[U_BUDGET + 64];
} DuConn;

static void du_out(DuConn *c, const unsigned char *b, int n, double now, int kind) {   // kind: 0 fresh, 1 resend, 2 ack
    send(c->u, (const char *)b, n, 0); c->sent = now; c->win_out++;
    if (kind == 0) c->win_fresh++; else if (kind == 1) c->win_extra++;
}
static double du_rto(DuConn *c) {
    double r = c->srtt + 4 * c->rttvar; if (r < DU_RTO_MIN) r = DU_RTO_MIN; if (r > DU_RTO_MAX) r = DU_RTO_MAX;
    r *= c->backoff; return r > DU_RTO_MAX ? DU_RTO_MAX : r;
}
static int du_room(DuConn *c) { return c->snext - c->sbase < DU_WIN_PKTS && c->inflight < DU_WIN_BYTES; }
static DWORD du_sack(DuConn *c) {
    DWORD m = 0; for (int i = 0; i < 32; i++) if (c->rhave[(c->rnext + 1 + i) % U_RWIN]) m |= 1u << i; return m;
}
// copies of the newest unacknowledged packets below `below` into pk + o, within the budget (`own` bytes of the packet counted)
static int du_copies(DuConn *c, unsigned char *pk, int o, DWORD below, int own, int *k, double now) {
    *k = 0; if (!c->copies_on) return o;
    double age = 2 * c->srtt; if (age < DU_RTO_MIN) age = DU_RTO_MIN;
    for (DWORD p = below; p-- > c->sbase && below - p <= 16 && *k < U_COPIES; ) {
        USnd *x = &c->snd[p % U_SRING];
        if (!x->live || x->copies >= DU_COPY_MAX || now - x->first > age || o + 6 + x->n + own > U_BUDGET) continue;
        unsigned short L = (unsigned short)x->n; memcpy(pk + o, &p, 4); memcpy(pk + o + 4, &L, 2); memcpy(pk + o + 6, x->d, x->n);
        o += 6 + x->n; (*k)++; x->copies++;
    }
    c->copies += *k; c->win_extra += *k;
    return o;
}
static int du_build(DuConn *c, DWORD sq, double now) {        // DATA (version 1) / DATA2 with copies; carries our ack
    USnd *q = &c->snd[sq % U_SRING]; unsigned char *pk = c->pk; DWORD sk = du_sack(c);
    u_hdr(pk, c->ver >= 2 ? U_DATA2 : U_DATA, c->cid); memcpy(pk + 7, &sq, 4); memcpy(pk + 11, &c->rnext, 4); memcpy(pk + 15, &sk, 4);
    c->ack_due = 0; c->last_ack_out = now;
    if (c->ver < 2) { memcpy(pk + 19, q->d, q->n); return 19 + q->n; }
    int k, o = du_copies(c, pk, 20, sq, q->n, &k, now);
    pk[19] = (unsigned char)k; memcpy(pk + o, q->d, q->n); return o + q->n;
}
static void du_ack(DuConn *c, double now, int copies_ok) {
    unsigned char *pk = c->pk; DWORD sk = du_sack(c);
    u_hdr(pk, U_ACK, c->cid); memcpy(pk + 7, &c->rnext, 4); memcpy(pk + 11, &sk, 4);
    c->ack_due = 0; c->last_ack_out = now; c->acks++;
    int len = 15; double gap = c->srtt / 4 > DU_ACK_DELAY ? c->srtt / 4 : DU_ACK_DELAY;   // an ACK2 at most every RTT/4 (10 ms at least)
    if (c->ver >= 2 && copies_ok && now - c->last_ack2 >= gap) {
        int k, o = du_copies(c, pk, 16, c->snext, 0, &k, now);
        if (k) { pk[2] = U_ACK2; pk[15] = (unsigned char)k; len = o; c->last_ack2 = now; }
    }
    du_out(c, pk, len, now, 2);
}
static int du_fresh(DuConn *c, const unsigned char *d, int n, double now) {   // a new packet of game bytes (room and pace checked)
    USnd *q = &c->snd[c->snext % U_SRING];
    memcpy(q->d, d, n); q->n = n; q->live = 1; q->sends = 1; q->copies = 0; q->first = q->last = now; c->inflight += n;
    int len = du_build(c, c->snext, now); du_out(c, c->pk, len, now, 0);
    if (!c->rto_at) c->rto_at = now + du_rto(c);
    c->snext++; c->sentn++;
    return 1;
}
static int du_resend(DuConn *c, DWORD sq, double now) {      // 1 sent, 0 not now (budget), -1 dead
    USnd *q = &c->snd[sq % U_SRING];
    if (q->sends >= DU_MAX_SENDS) {
        logmsg("udp: %08lX - packet %lu sent %d times without an answer, %lu waiting: the connection is dead (ev=dead)",
               c->cid, sq, q->sends, c->snext - c->sbase);
        c->ended = "dead"; return -1;
    }
    if (!ub_take(&c->rb, now)) { c->bskip++; return 0; }
    int len = du_build(c, sq, now); du_out(c, c->pk, len, now, 1);
    q->last = now; q->sends++; c->resent++;
    return 1;
}
static void du_release(DuConn *c, USnd *x, int *clean) { if (x->sends > 1 || x->copies > 0) *clean = 0; x->live = 0; c->inflight -= x->n; }
static int du_on_ack(DuConn *c, DWORD ack, DWORD sack, double now) {   // -1 = dead
    int clean = c->peer_sack == 0, advanced = 0; long newest = -1; double nfirst = 0; int nsends = 0;
    if (ack > c->snext) ack = c->snext;                          // never past what we sent
    while (c->sbase < ack) {
        USnd *x = &c->snd[c->sbase % U_SRING];
        if (x->live) { if ((long)c->sbase > newest) { newest = c->sbase; nfirst = x->first; nsends = x->sends; } du_release(c, x, &clean); }
        c->sbase++; advanced = 1;
    }
    for (int i = 0; i < 32; i++) if (sack >> i & 1) {
        DWORD sq = ack + 1 + i; if (sq < c->sbase || sq >= c->snext) continue;
        USnd *x = &c->snd[sq % U_SRING];
        if (x->live) { if ((long)sq > newest) { newest = sq; nfirst = x->first; nsends = x->sends; } du_release(c, x, &clean); }
    }
    while (c->sbase < c->snext && !c->snd[c->sbase % U_SRING].live) { c->sbase++; advanced = 1; }
    if (clean && newest >= 0 && nsends == 1) {                   // the round trip (Karn, and only from a clean ack)
        double r = now - nfirst;
        if (!c->have_rtt) { c->srtt = r; c->rttvar = r / 2; c->have_rtt = 1; }
        else { c->rttvar = 0.75 * c->rttvar + 0.25 * fabs(c->srtt - r); c->srtt = 0.875 * c->srtt + 0.125 * r; }
    }
    if (advanced) { c->backoff = 1; c->tlp_done = 0; c->rto_at = c->sbase < c->snext ? now + du_rto(c) : 0; }
    else if (c->sbase == c->snext) c->rto_at = 0;
    // two duplicate acks while later packets got there: the oldest once more, once per ack value
    if (ack == c->last_ack_seen && sack && ack >= c->sbase && ack < c->snext && c->snd[ack % U_SRING].live) {
        if (++c->dups >= 2 && !(c->fast_valid && c->fast_done == ack)) {
            c->fast_done = ack; c->fast_valid = 1; if (du_resend(c, ack, now) < 0) return -1;
        }
    } else c->dups = 0;
    c->last_ack_seen = ack; c->peer_ack = ack; c->peer_sack = sack;
    return 0;
}
static void du_to_game(DuConn *c, SOCKET g, int *gameOpen) {    // what waits for the game, as far as it takes it now
    int off = 0;
    while (off < c->outq_len) {
        int w = g_sendOrig(g, (const char *)c->outq + off, c->outq_len - off, 0);
        if (w > 0) { off += w; continue; }
#ifdef _WIN32
        if (w < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
#else
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
#endif
        *gameOpen = 0; c->outq_len = 0; return;
    }
    if (off) { memmove(c->outq, c->outq + off, c->outq_len - off); c->outq_len -= off; }
}
// Relay 24: the stream from the server is u16 BE length + message; a message starting with 0x7e is the server's own (who is
// away and for how long) - it goes to du_notice, never to the game; every other frame goes on whole, in order
#ifndef DU_FQ_MAX
#define DU_FQ_MAX (DU_OUTQ_MAX + 70000)
#endif
static void du_notice(const unsigned char *m, int n);
static void du_filter(DuConn *c, const unsigned char *p, int len) {
    if (c->fq_len + len > DU_FQ_MAX) return;                       // can't be: du_on_data takes nothing while outq + fq is full
    if (len) { memcpy(c->fq + c->fq_len, p, len); c->fq_len += len; }
    int o = 0;
    while (c->fq_len - o >= 2) {
        int n = c->fq[o] << 8 | c->fq[o + 1];
        if (c->fq_len - o < 2 + n) break;
        if (n >= 1 && c->fq[o + 2] == 0x7e) du_notice(c->fq + o + 2, n);
        else if (c->outq_len + 2 + n <= DU_OUTQ_MAX) { memcpy(c->outq + c->outq_len, c->fq + o, 2 + n); c->outq_len += 2 + n; }
        else break;                                              // the game hangs: the rest waits here
        o += 2 + n;
    }
    if (o) { memmove(c->fq, c->fq + o, c->fq_len - o); c->fq_len -= o; }
}
// one data packet's bytes (its own or a copy): 1 = new
static int du_on_data(DuConn *c, DWORD seq, const unsigned char *p, int len, int copy) {
    if (seq < c->rnext || seq - c->rnext >= U_RWIN || c->rhave[seq % U_RWIN]) return 0;
    if (c->outq_len + c->fq_len + len > DU_OUTQ_MAX) return 0;   // the game hangs that long: not taken (it comes again)
    memcpy(c->rcv[seq % U_RWIN], p, len); c->rlen[seq % U_RWIN] = len; c->rhave[seq % U_RWIN] = 1;
    if (copy) c->saved++; else if (seq != c->rnext) c->gapn++;
    while (c->rhave[c->rnext % U_RWIN]) {
        int i = c->rnext % U_RWIN; c->rhave[i] = 0;
        if (c->filt) du_filter(c, c->rcv[i], c->rlen[i]);
        else if (c->outq_len + c->rlen[i] <= DU_OUTQ_MAX) { memcpy(c->outq + c->outq_len, c->rcv[i], c->rlen[i]); c->outq_len += c->rlen[i]; }
        c->rnext++; c->gotn++;
    }
    return 1;
}
// one datagram from the node: 1 FIN, -1 dead, 0 go on
static int du_datagram(DuConn *c, const unsigned char *pk, int n, double now) {
    if (n < 7 || pk[0] != 'D' || pk[1] != 'U' || memcmp(pk + 3, &c->cid, 4)) return 0;
    int t = pk[2]; c->seen = now;
    if (t == U_FIN) { logmsg("udp: %08lX closed by the server", c->cid); c->ended = "server"; return 1; }
    if (!((t == U_DATA && n >= 19) || (t == U_DATA2 && n >= 20) || (t == U_ACK && n >= 15) || (t == U_ACK2 && n >= 16))) return 0;
    DWORD ack, sack; int o = t == U_ACK || t == U_ACK2 ? 7 : 11; memcpy(&ack, pk + o, 4); memcpy(&sack, pk + o + 4, 4);
    if (du_on_ack(c, ack, sack, now) < 0) return -1;
    if (t == U_ACK) return 0;
    struct { DWORD seq; const unsigned char *p; int len; } in[U_COPIES + 2]; int nin = 0, ncopy = 0, bad = 0;
    if (t == U_DATA) { memcpy(&in[0].seq, pk + 7, 4); in[0].p = pk + 19; in[0].len = n - 19; nin = 1; if (n - 19 > U_MAXP) nin = 0; }
    else {
        int k = pk[t == U_ACK2 ? 15 : 19]; o = t == U_ACK2 ? 16 : 20;
        for (int i = 0; i < k; i++) {                            // more copies than we keep: skipped, the offset follows them
            unsigned short L; if (o + 6 > n) { bad = 1; break; } memcpy(&L, pk + o + 4, 2); if (L > U_MAXP || o + 6 + L > n) { bad = 1; break; }
            if (nin < U_COPIES + 1) { memcpy(&in[nin].seq, pk + o, 4); in[nin].p = pk + o + 6; in[nin].len = L; nin++; }
            o += 6 + L;
        }
        ncopy = nin;
        for (int a = 0, b = ncopy - 1; a < b; a++, b--) { __typeof__(in[0]) x = in[a]; in[a] = in[b]; in[b] = x; }   // oldest first
        if (t == U_DATA2 && !bad && n - o <= U_MAXP) { memcpy(&in[nin].seq, pk + 7, 4); in[nin].p = pk + o; in[nin].len = n - o; nin++; }
    }
    int took = 0;
    for (int j = 0; j < nin; j++) took += du_on_data(c, in[j].seq, in[j].p, in[j].len, j < ncopy);
    if (t == U_ACK2) { if (took) c->ack_due = 1; return 0; }      // an ACK2 is never answered at once (rule 5)
    if (took) {                                                  // new bytes: ack now - in order at most one per 10 ms, a hole at once
        int hole = 0; for (int i = 1; i < 32 && !hole; i++) hole = c->rhave[(c->rnext + i) % U_RWIN];
        if (hole || now - c->last_ack_out >= DU_ACK_DELAY) du_ack(c, now, 1); else c->ack_due = 1;
    }
    else { c->dupin++; if (now - c->last_ack_out >= DU_DUP_ACK_GAP) du_ack(c, now, 0); else if (!c->ack_due) c->ack_due = 2; }
    return 0;
}
// the clock: resend timer, budgets, keepalive, the fuse. -1 = dead / silent
static int du_tick(DuConn *c, double now) {
    if (now - c->seen > DU_IDLE) { logmsg("udp: %08lX - nothing from the server for 30 s", c->cid); c->ended = "silent"; return -1; }
    if (now - c->fresh_t >= 1.0) {
        c->fresh_rate = 0.7 * c->fresh_rate + 0.3 * (c->sentn - c->fresh_n0) / (now - c->fresh_t); c->fresh_t = now; c->fresh_n0 = c->sentn;
        double r = c->fuse ? DU_RESEND_FLOOR : (DU_RESEND_SHARE * c->fresh_rate > DU_RESEND_FLOOR ? DU_RESEND_SHARE * c->fresh_rate : DU_RESEND_FLOOR);
        c->rb.rate = c->rb.cap = r;
    }
    if (c->sbase < c->snext && !c->tlp_done && c->rto_at && now < c->rto_at) {   // the tail probe: the newest once more (TCP's TLP)
        DWORD nw = c->snext; while (nw-- > c->sbase && !c->snd[nw % U_SRING].live) ;
        double w = c->srtt + 2 * c->rttvar + DU_ACK_DELAY; if (w < DU_TLP_MIN) w = DU_TLP_MIN;
        if (c->snd[nw % U_SRING].live && now - c->snd[nw % U_SRING].last >= w) { c->tlp_done = 1; if (du_resend(c, nw, now) < 0) return -1; }
    }
    if (c->rto_at && now >= c->rto_at && c->sbase < c->snext) {
        DWORD base = c->sbase;
        if (du_resend(c, base, now) < 0) return -1;
        int hb = 0; for (DWORD m = c->peer_sack; m; m >>= 1) hb++;   // the peer has peer_ack + hb: what we hold below that is a hole
        for (DWORD s = base + 1; s < c->peer_ack + hb && s < c->snext; s++) {
            USnd *x = &c->snd[s % U_SRING];
            if (x->live && now - x->last > c->srtt) { int r = du_resend(c, s, now); if (r < 0) return -1; if (!r) break; }
        }
        double b = c->srtt + 4 * c->rttvar; if (b < DU_RTO_MIN) b = DU_RTO_MIN;
        if (b * c->backoff < DU_RTO_MAX) c->backoff *= 2;
        c->rto_at = now + du_rto(c);
    }
    if (c->ack_due && now - c->last_ack_out >= (c->ack_due == 2 ? DU_DUP_ACK_GAP : DU_ACK_DELAY)) du_ack(c, now, c->ack_due == 1);   // 2 = only duplicates
    else if (now - c->sent > DU_KEEPALIVE) du_ack(c, now, 1);
    if (now - c->win_t >= DU_FUSE_WINDOW) {
        double secs = now - c->win_t;
        if (!c->fuse && ((c->win_extra > DU_FUSE_EXTRA * (c->win_fresh ? c->win_fresh : 1) && c->win_extra >= DU_FUSE_MIN)
                         || c->win_out / secs > DU_FUSE_RATE)) {
            c->fuse = 1; c->copies_on = 0; c->rb.rate = c->rb.cap = DU_RESEND_FLOOR;
            logmsg("udp: %08lX - storm (ev=storm): in %.1f s %u fresh, %u resent+copies, %u out (%.0f/s), %lu waiting - copies off, resends cut to %d/s",
                   c->cid, secs, c->win_fresh, c->win_extra, c->win_out, c->win_out / secs, c->snext - c->sbase, (int)DU_RESEND_FLOOR);
        }
        c->win_t = now; c->win_fresh = c->win_extra = c->win_out = 0;
    }
    return 0;
}

static void du_nonblock(SOCKET s) {
#ifdef _WIN32
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}

// one game connection: the game's socket g <-> the node over UDP u (handshake done, version ver); pre = our preamble (DWR4)
static void udp_carry(SOCKET g, SOCKET u, DWORD cid, int ver, const unsigned char *pre, int plen) {
    DuConn *c = calloc(1, sizeof *c); double now = du_now();
    c->snd = calloc(U_SRING, sizeof(USnd)); c->rcv = calloc(U_RWIN, U_MAXP); c->rlen = calloc(U_RWIN, sizeof(int));
    c->rhave = calloc(U_RWIN, 1); c->outq = malloc(DU_OUTQ_MAX);
    c->u = u; c->cid = cid; c->ver = ver; c->srtt = 0.15; c->rttvar = 0.05; c->backoff = 1; c->copies_on = ver >= 2;
    c->seen = c->sent = c->win_t = c->fresh_t = now; c->last_ack_seen = (DWORD)-1;
    ub_init(&c->pace, DU_PACE, DU_PACE_BURST, now); ub_init(&c->rb, DU_RESEND_FLOOR, DU_RESEND_FLOOR, now);
    du_nonblock(g); du_nonblock(u);
    int gameOpen = 1; unsigned char buf[U_MAXP], in[U_BUDGET + 64];
    if (plen > 0 && plen <= U_MAXP) { ub_take(&c->pace, now); du_fresh(c, pre, plen, now); }   // Relay 22: the DWR4 preamble first
    if (plen >= 4 && !memcmp(pre, "DWR5", 4)) { c->filt = 1; c->fq = malloc(DU_FQ_MAX); if (!c->fq) c->filt = 0; }   // Relay 24
    for (;;) {
        now = du_now();
        ub_fill(&c->pace, now);
        int canread = gameOpen && du_room(c) && c->pace.tok >= 1.0;
        fd_set rs, ws; FD_ZERO(&rs); FD_ZERO(&ws); FD_SET(u, &rs);
        if (canread) FD_SET(g, &rs);
        if (c->outq_len) FD_SET(g, &ws);
        long us = 10000;                                         // the tick: 10 ms; sooner when only the pace holds the game back
        if (gameOpen && du_room(c) && !canread) { long w = (long)((1.0 - c->pace.tok) / DU_PACE * 1e6) + 500; if (w < us) us = w; }
        struct timeval tv = {0, us};
        int sel = select(DU_NFDS(g, u), &rs, c->outq_len ? &ws : NULL, NULL, &tv); now = du_now();
        if (sel > 0 && canread && FD_ISSET(g, &rs)) {           // game -> server
            int n = g_recvOrig(g, (char *)buf, U_MAXP, 0);
            if (n > 0) { ub_take(&c->pace, now); du_fresh(c, buf, n, now); }
            else {
#ifdef _WIN32
                if (!(n < 0 && WSAGetLastError() == WSAEWOULDBLOCK)) gameOpen = 0;
#else
                if (!(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) gameOpen = 0;
#endif
            }
        } else if (gameOpen && du_room(c) && !canread) c->paced++;
        if (sel > 0 && FD_ISSET(u, &rs)) {                       // server -> game: everything that is here (at most 256 at once)
            int r = 0;
            for (int i = 0; i < 256; i++) {
                int n = recv(u, (char *)in, sizeof in, 0); if (n <= 0) break;
                if ((r = du_datagram(c, in, n, now)) != 0) break;
            }
            if (r) break;
        }
        if (c->outq_len) du_to_game(c, g, &gameOpen);
        if (c->filt && c->fq_len) du_filter(c, NULL, 0);           // frames that waited for room in outq
        if (du_tick(c, now) < 0) break;
        if (!gameOpen && c->sbase == c->snext && !c->outq_len) { c->ended = "game"; break; }   // the game closed and all got there
    }
    unsigned char fin[8]; u_hdr(fin, U_FIN, cid);
    if (!c->ended || strcmp(c->ended, "server")) for (int i = 0; i < 3; i++) send(u, (char *)fin, 7, 0);
    logmsg("udp: %08lX over: sent %u, got %u, sent again %u, out of order in %u, round trip %d ms; copies sent %u, lost ones filled by a copy %u; "
           "acks %u, duplicates in %u, pace waits %u, resends held by the budget %u, storm %d, ended by %s",
           cid, c->sentn, c->gotn, c->resent, c->gapn, (int)(c->srtt * 1000), c->copies, c->saved,
           c->acks, c->dupin, c->paced, c->bskip, c->fuse, c->ended ? c->ended : "?");
    closesocket(u); closesocket(g);
    free(c->snd); free(c->rcv); free(c->rlen); free(c->rhave); free(c->outq); free(c->fq); free(c);
}
