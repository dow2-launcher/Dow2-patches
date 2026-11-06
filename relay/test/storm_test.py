#!/usr/bin/env python3
"""storm_test.py - the UDP tunnel (Relay.dll's du_tunnel.h, built for Linux by build.sh) + udp_gateway.py + battle_relay.py
with 4 (or more) model players, in two network namespaces with tc netem between them. Root needed (ip netns, tc).

  ns du_cl (10.77.0.2) : the players' game model + one du_harness per player (the client tunnel) [+ the fault proxy]
  ns du_gw (10.77.0.1) : udp_gateway.py (UDP 47100) -> battle_relay.py (TCP 127.0.0.1:47100)
  veth du_vcl <-> du_vgw, netem on both ends (delay / jitter / loss as given)

The game model speaks the relay protocol of DoW2 (battle_relay.py's docstring): hello 15, turns 09 every 100 ms, ticks 0a
from the server, and the game's reliable peer messages as RLink ProxyMessages (0x10 <src> <n u16> n*dst <len u16> data +
n*15-byte infos): each player sends one every 100 ms to all the others (window 30 per peer, retry of everything older
than 500 ms every 50 ms, acks piggybacked in the infos, a forced ack after 250 ms); a non-host that gets an info for
another peer drops it (counted as `misdelivered`, the game's "dropping PROXY message ... (we're not host)"), the host
passes it on (`forwarded`). Optionally a flood: extra small 13-messages (the 650/s a player sent on 05.11).

Usage (from relay/test, after build.sh):
  python3 storm_test.py --ladder /path/to/dow2-ladder/gameserver --client new|old22 --secs 40 [--loss 1] [--delay 15]
        [--jitter 3] [--players 4] [--flood 0] [--fault none|eternal_seq|ack_blackhole|down_silence] [--gwver 2] --out DIR
Prints one JSON line with the results (also DIR/result.json).
"""
import argparse, asyncio, json, os, random, signal, socket, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
NS_GW, NS_CL = 'du_gw', 'du_cl'
GW_IP, CL_IP = '10.77.0.1', '10.77.0.2'


def sh(cmd, check=True):
    r = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if check and r.returncode: raise RuntimeError('%s: %s' % (cmd, r.stderr.strip()))
    return r.stdout


def net_up(a):
    net_down()
    sh(f'ip netns add {NS_GW}; ip netns add {NS_CL}')
    sh(f'ip link add du_vgw type veth peer name du_vcl')
    sh(f'ip link set du_vgw netns {NS_GW}; ip link set du_vcl netns {NS_CL}')
    for ns, dev, ip in ((NS_GW, 'du_vgw', GW_IP), (NS_CL, 'du_vcl', CL_IP)):
        sh(f'ip -n {ns} addr add {ip}/24 dev {dev}; ip -n {ns} link set {dev} up; ip -n {ns} link set lo up')
        q = f'delay {a.delay}ms {a.jitter}ms' if a.delay else ''
        if a.loss: q += f' loss {a.loss}%'
        if q: sh(f'ip netns exec {ns} tc qdisc add dev {dev} root netem {q} limit 10000')


def net_down():
    sh(f'ip netns del {NS_GW}', check=False); sh(f'ip netns del {NS_CL}', check=False)


# ---------------------------------------------------------------- the game model (runs inside du_cl)
def frame(m): return len(m).to_bytes(2, 'big') + m


def proxy_msg(src, dsts, data, infos):
    body = data + b''.join(infos)
    return bytes([0x10, src]) + len(dsts).to_bytes(2, 'little') + bytes(dsts) + len(body).to_bytes(2, 'little') + body


def info(src, dst, seq, ack, flags=0):          # 15 bytes, as the game's per-destination header
    return struct.pack('<BBIIB4x', src, dst, seq, ack, flags)


class Player:
    def __init__(self, g, port, n):
        self.g, self.port, self.n = g, port, n
        self.idx = 0; self.r = self.w = None
        self.st = dict(turns_out=0, ticks=0, turn_lat=[], proxy_out=0, proxy_in=0, misdelivered=0, forwarded=0, old_retry=0,
                       accepted=0, retries=0, flood_out=0, flood_in=0, bytes_out=0, bytes_in=0, bad=0)
        self.out_seq = {}; self.unacked = {}; self.expect = {}; self.last_to = {}; self.ack_pending = {}
        self.closed = False; self.started = None; self.tick_t = []

    def send(self, m):
        if self.closed: return
        b = frame(m); self.w.write(b); self.st['bytes_out'] += len(b)

    async def connect(self, sid, pid):
        for _ in range(100):
            try: self.r, self.w = await asyncio.open_connection('127.0.0.1', self.port); break
            except OSError: await asyncio.sleep(0.05)
        self.w.get_extra_info('socket').setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.send(b'\x15' + struct.pack('<I', sid) + b'\0\0\0\0' + pid + b'\x00')
        n = int.from_bytes(await self.r.readexactly(2), 'big'); m = await self.r.readexactly(n)
        assert m[0] == 0x15
        d = struct.pack('<IId', 0, 0, time.monotonic()) + b'\x00' * 8   # a first turn: the relay decides "host" (no 5 s wait) and joins it now
        self.send(b'\x09\xfe' + len(d).to_bytes(2, 'little') + d)

    async def reader(self):
        try:
            while True:
                n = int.from_bytes(await self.r.readexactly(2), 'big'); m = await self.r.readexactly(n)
                self.st['bytes_in'] += n + 2; self.on_msg(m)
        except (asyncio.IncompleteReadError, ConnectionError): pass
        self.closed = True

    def on_msg(self, m):
        now = time.monotonic(); t = m[0]
        if t == 0x0a:
            self.st['ticks'] += 1; k = m[2]; o = 3
            if self.started and now - self.started > 3: self.tick_t.append(now)
            for _ in range(k):
                i = m[o]; L = m[o + 1] | m[o + 2] << 8; d = m[o + 3:o + 3 + L]; o += 3 + L
                if i == self.idx and len(d) >= 16:
                    sent = struct.unpack('<d', d[8:16])[0]
                    if self.started and sent - self.started > 3: self.st['turn_lat'].append(now - sent)   # after a 3 s warm-up
        elif t == 0x10:
            n = m[2] | m[3] << 8; dsts = m[4:4 + n]; o = 4 + n; L = m[o] | m[o + 1] << 8; body = m[o + 2:o + 2 + L]
            if len(body) != L or L < 15 * n: self.st['bad'] += 1; return
            data, infos = body[:L - 15 * n], body[L - 15 * n:]
            self.st['proxy_in'] += 1
            for i in range(n):
                inf = infos[15 * i:15 * i + 15]; src, dst, seq, ack, flags = struct.unpack('<BBIIB4x', inf)
                if dst != self.idx:
                    if self.idx == 1: self.st['forwarded'] += 1; self.send(proxy_msg(1, [dst], data, [inf]))   # the host passes it on
                    else: self.st['misdelivered'] += 1
                    continue
                for s in [s for s in self.unacked.get(src, {}) if s < ack]: del self.unacked[src][s]   # our messages it has
                if flags & 1: continue                       # an ack only
                e = self.expect.get(src, 0)
                if seq == e: self.expect[src] = e + 1; self.st['accepted'] += 1
                elif seq < e: self.st['old_retry'] += 1
                self.ack_pending[src] = now
        elif t == 0x13: self.st['flood_in'] += 1
        elif t in (0x15, 0x14): pass
        else: self.st['bad'] += 1

    def others(self): return [i for i in range(1, self.n + 1) if i != self.idx]

    async def turns(self, end):
        t = 0
        while time.monotonic() < end and not self.closed:
            d = struct.pack('<IId', self.idx, t, time.monotonic()) + b'\x00' * 8
            self.send(b'\x09\xfe' + len(d).to_bytes(2, 'little') + d); t += 1; self.st['turns_out'] += 1
            await asyncio.sleep(0.1)

    async def peer_msgs(self, end):
        k = 0
        while time.monotonic() < end and not self.closed:
            now = time.monotonic(); dsts = []; infos = []
            for d in self.others():
                u = self.unacked.setdefault(d, {})
                if len(u) >= 30: continue
                s = self.out_seq.get(d, 0); self.out_seq[d] = s + 1
                inf = info(self.idx, d, s, self.expect.get(d, 0)); u[s] = [inf, now, now]
                dsts.append(d); infos.append(inf); self.last_to[d] = now; self.ack_pending.pop(d, None)
            if dsts:
                self.send(proxy_msg(self.idx, dsts, b'G' * 40 + struct.pack('<I', k), infos)); self.st['proxy_out'] += 1; k += 1
            for _ in range(2):                               # retries and forced acks, every 50 ms
                await asyncio.sleep(0.05); now = time.monotonic()
                for d, u in self.unacked.items():
                    if u and now - min(v[1] for v in u.values()) > 0.5 and now - max(v[2] for v in u.values()) > 0.05:
                        for s, v in sorted(u.items()):
                            v[2] = now
                            self.send(proxy_msg(self.idx, [d], b'G' * 40, [info(self.idx, d, s, self.expect.get(d, 0))]))
                            self.st['retries'] += 1
                for d, t in list(self.ack_pending.items()):
                    if now - t > 0.25:
                        self.send(proxy_msg(self.idx, [d], b'', [info(self.idx, d, 0, self.expect.get(d, 0), 1)]))
                        del self.ack_pending[d]

    async def flood(self, end, rate):
        if not rate: return
        while time.monotonic() < end and not self.closed:
            for _ in range(max(1, int(rate / 100))):
                d = random.choice(self.others())
                self.send(bytes([0x13, d, self.idx]) + b'\x0f' + b'F' * 28); self.st['flood_out'] += 1
            await asyncio.sleep(0.01)


async def game(a):
    ports = [int(p) for p in a.ports.split(',')]
    ps = [Player(None, p, len(ports)) for p in ports]
    sid = random.getrandbits(32)
    for i, p in enumerate(ps):                               # one after the other: the relay numbers them in this order
        await p.connect(sid, struct.pack('<Q', 0x1000 + i)); p.idx = i + 1
        await asyncio.sleep(0.4)
    end = time.monotonic() + a.secs
    for p in ps: p.started = time.monotonic()
    tasks = [asyncio.ensure_future(p.reader()) for p in ps]
    await asyncio.gather(*[p.turns(end) for p in ps], *[p.peer_msgs(end) for p in ps], *[p.flood(end, a.flood) for p in ps])
    await asyncio.sleep(1.0)
    for p in ps:
        try: p.w.close()
        except Exception: pass
    await asyncio.sleep(1.5)
    res = []
    for p in ps:
        st = dict(p.st); lat = sorted(st.pop('turn_lat'))
        st['turn_lat_ms'] = dict({q: round(lat[min(len(lat) - 1, int(len(lat) * q / 100))] * 1000, 1) for q in (50, 90, 99)}, max=round(lat[-1] * 1000, 1)) if lat else None
        st['turns_seen'] = len(lat); st['idx'] = p.idx
        gaps = sorted(b - a for a, b in zip(p.tick_t, p.tick_t[1:]))      # the server's ticks come every 100 ms: a longer gap = a stall on the way down
        st['tick_gap_ms'] = dict(p99=round(gaps[int(len(gaps) * 0.99)] * 1000, 1), max=round(gaps[-1] * 1000, 1), over150=sum(1 for g in gaps if g > 0.15)) if gaps else None
        res.append(st)
    print(json.dumps(res))


# ---------------------------------------------------------------- the fault proxy (inside du_cl, between harness and gateway)
class Fault(asyncio.DatagramProtocol):
    """client <-> 127.0.0.1:47199 <-> gateway. modes: eternal_seq (every datagram carrying the client's packet N, as its own
    or as a copy, is dropped - forever), ack_blackhole (every ACK / ACK2 from the gateway dropped after T s), down_silence
    (nothing from the gateway after T s)"""
    def __init__(self, mode, at):
        self.mode, self.at, self.t0 = mode, at, time.monotonic(); self.client = None; self.dropped = 0; self.target = None

    def connection_made(self, tr): self.tr = tr

    def carries(self, d, seq):
        if len(d) < 11: return False
        t = d[2]
        if t in (3, 8) and struct.unpack('<I', d[7:11])[0] == seq: return True
        if t in (8, 9):
            o = 20 if t == 8 else 16; k = d[o - 1]
            for _ in range(k):
                if o + 6 > len(d): break
                s, n = struct.unpack('<IH', d[o:o + 6])
                if s == seq: return True
                o += 6 + n
        return False

    def datagram_received(self, d, addr):
        late = time.monotonic() - self.t0 > self.at
        if addr[0] == GW_IP:
            if self.mode == 'ack_blackhole' and late and len(d) > 2 and d[2] in (4, 9): self.dropped += 1; return
            if self.mode == 'down_silence' and late: self.dropped += 1; return
            if self.client: self.tr.sendto(d, self.client)
        else:
            self.client = addr
            if self.mode == 'eternal_seq':
                if self.target is None and late and len(d) > 11 and d[2] in (3, 8): self.target = struct.unpack('<I', d[7:11])[0] + 1
                if self.target is not None and self.carries(d, self.target): self.dropped += 1; return
            self.tr.sendto(d, (GW_IP, 47100))


async def fault(a):
    loop = asyncio.get_running_loop()
    tr, f = await loop.create_datagram_endpoint(lambda: Fault(a.fault, a.fault_at), local_addr=('0.0.0.0', 47199))
    def stop(*_): print(json.dumps(dict(fault=a.fault, dropped=f.dropped, target=f.target)), flush=True); os._exit(0)
    loop.add_signal_handler(signal.SIGTERM, stop)
    await asyncio.Event().wait()


# ---------------------------------------------------------------- the orchestrator
def parse_kv(line):
    out = {}
    for tok in line.split():
        if '=' in tok:
            k, v = tok.split('=', 1)
            try: out[k] = float(v) if '.' in v else int(v)
            except ValueError: out[k] = v
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--role', default='run')
    ap.add_argument('--ladder', help='gameserver dir with udp_gateway.py + battle_relay.py (+ du_proto.py)')
    ap.add_argument('--relay-dir', help='battle_relay.py from here instead (old relay with the new gateway, ...)')
    ap.add_argument('--client', default='new', choices=['new', 'old22'])
    ap.add_argument('--players', type=int, default=4)
    ap.add_argument('--secs', type=float, default=40)
    ap.add_argument('--loss', type=float, default=0); ap.add_argument('--delay', type=float, default=15); ap.add_argument('--jitter', type=float, default=3)
    ap.add_argument('--flood', type=float, default=0)
    ap.add_argument('--fault', default='none'); ap.add_argument('--fault-at', type=float, default=8)
    ap.add_argument('--gwver', type=int, default=2)
    ap.add_argument('--addr-rate', type=float, default=20000, help='UDPGW_ADDR_RATE: all model players share one address here')
    ap.add_argument('--ports'); ap.add_argument('--out', default='/tmp/du_out')
    ap.add_argument('--pcap', action='store_true', help='tcpdump both ends of the veth into OUT/cl.pcap, OUT/gw.pcap')
    a = ap.parse_args()
    if a.role == 'game': asyncio.run(game(a)); return
    if a.role == 'fault': asyncio.run(fault(a)); return

    os.makedirs(a.out, exist_ok=True); procs = []
    def start(ns, cmd, log, env=None):
        f = open(os.path.join(a.out, log), 'w')
        p = subprocess.Popen(['ip', 'netns', 'exec', ns] + cmd, stdout=f, stderr=subprocess.STDOUT, env=dict(os.environ, **(env or {})))
        procs.append(p); return p
    try:
        net_up(a)
        env = dict(RELAY_PEERS='', RELAY_LOG=os.path.join(a.out, 'battle.jsonl'), UDPGW_TRUSTED='', UDPGW_VERSION=str(a.gwver), UDPGW_ADDR_RATE=str(a.addr_rate),
                   PYTHONUNBUFFERED='1')
        relay_dir = a.relay_dir or a.ladder
        start(NS_GW, ['python3', os.path.join(relay_dir, 'battle_relay.py'), '47100', '127.0.0.1'], 'battle.out', env)
        start(NS_GW, ['python3', os.path.join(a.ladder, 'udp_gateway.py'), '47100', '127.0.0.1:47100'], 'gateway.out', env)
        if a.pcap:                                           # both ends of the veth: what the client sent (before netem) / what arrived
            start(NS_CL, ['tcpdump', '-i', 'du_vcl', '-U', '-s', '2000', '-w', os.path.join(a.out, 'cl.pcap'), 'udp'], 'tcpdump_cl.out')
            start(NS_GW, ['tcpdump', '-i', 'du_vgw', '-U', '-s', '2000', '-w', os.path.join(a.out, 'gw.pcap'), 'udp'], 'tcpdump_gw.out')
        time.sleep(1.0)
        fp = None
        if a.fault != 'none':
            fp = subprocess.Popen(['ip', 'netns', 'exec', NS_CL, sys.executable, __file__, '--role', 'fault', '--fault', a.fault,
                                   '--fault-at', str(a.fault_at)], stdout=subprocess.PIPE, text=True)
            time.sleep(0.5)
        gw_ip, gw_port = ('127.0.0.1', 47199) if fp else (GW_IP, 47100)
        exe = os.path.join(HERE, 'du_harness' if a.client == 'new' else 'du_harness_old22')
        ports = [5001 + i for i in range(a.players)]
        hs = [start(NS_CL, [exe, str(p), gw_ip, str(gw_port)], 'client%d.log' % (i + 1)) for i, p in enumerate(ports)]
        time.sleep(0.3)
        g = subprocess.run(['ip', 'netns', 'exec', NS_CL, sys.executable, __file__, '--role', 'game', '--ports', ','.join(map(str, ports)),
                            '--secs', str(a.secs), '--flood', str(a.flood)], capture_output=True, text=True, timeout=a.secs + 120)
        players = json.loads(g.stdout.strip().splitlines()[-1]) if g.stdout.strip() else []
        if g.returncode: print(g.stderr[-2000:], file=sys.stderr)
        for h in hs:                                         # the clients end by themselves once the game socket is closed
            try: h.wait(timeout=40)
            except subprocess.TimeoutExpired: h.terminate()
        fres = None
        if fp:
            fp.terminate()
            try: fres = json.loads(fp.communicate(timeout=5)[0].strip().splitlines()[-1])
            except Exception: fres = None
        time.sleep(1.0)
    finally:
        for p in procs:
            if p.poll() is None: p.terminate()
        for p in procs:
            try: p.wait(timeout=5)
            except subprocess.TimeoutExpired: p.kill()
        net_down()
    # the results: the clients' "over" lines, the gateway's close lines
    clients = []
    for i in range(a.players):
        txt = open(os.path.join(a.out, 'client%d.log' % (i + 1))).read()
        over = [l for l in txt.splitlines() if ' over: ' in l]
        c = dict(storm=('ev=storm' in txt), dead=('ev=dead' in txt), over=over[-1].split(' over: ', 1)[1] if over else None)
        if over:
            import re
            nums = dict(re.findall(r'(sent again|sent|got|copies sent|lost ones filled by a copy|out of order in) (\d+)', over[-1]))
            c.update({k.replace(' ', '_'): int(v) for k, v in nums.items()})
        clients.append(c)
    gw = [parse_kv(l) for l in open(os.path.join(a.out, 'gateway.out')) if 'ev=close' in l]
    gwev = [l.strip() for l in open(os.path.join(a.out, 'gateway.out')) if 'ev=storm' in l or 'ev=dead' in l or 'addr_limit' in l]
    res = dict(args={k: v for k, v in vars(a).items() if k not in ('role', 'ports')}, players=players, clients=clients,
               gateway=[{k: c.get(k) for k in ('secs', 'srtt_ms', 'rx', 'tx', 'resent', 'lost_in', 'copies', 'filled', 'acks', 'storm', 'by')} for c in gw],
               gateway_events=gwev[:20], fault=fres)
    json.dump(res, open(os.path.join(a.out, 'result.json'), 'w'), indent=1)
    print(json.dumps(res))


if __name__ == '__main__':
    main()
