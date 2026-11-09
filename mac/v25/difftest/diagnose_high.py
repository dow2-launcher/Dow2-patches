#!/usr/bin/env python3
"""diagnose_high.py [--tag TAG] [--dylib D] [--jobs N] REPLAY.rec... - root cause of each Mac/Windows difference at the state level.
The Low sync CRC (what the replay records) only shows the random stream; floats can drift for hundreds of frames before a decision
(and a random call) changes. So both sides are replayed with the sync level forced to HIGH (every entity's state in the CRC):
 pass 1: Mac (macrun + dylib, MR_SYNC_HIGH) and Windows (win_run.sh, DT_HIGH) per-frame High CRC up to the Low difference F
         -> H = the first frame whose High CRC differs (both sides start equal);
 pass 2: both dump the sync block of H-1 and H -> the differing lines, each with its entity / squad / player and property path.
Writes runs/<TAG>_high/<replay>/high.json and runs/<TAG>_high/high_summary.tsv. Needs runs/<TAG>/<replay>/result.json (difftest.py).
Summary columns: replay, Low F, High H, H-1 dumps equal, n differing lines at H, entity id, blueprint, property path, field,
component (x/y/z), mac hex, win hex - the first STATE difference (control/random flow logs skipped; full list in high.json).
--redump: recompute the dump comparison from existing p2 dumps (no game runs). --reuse-win (step 8): Windows outputs already in
winref/<TAG>_high/<replay>/p1|p2 are kept (the Windows game does not change between dylib versions; link them from an older tag)."""
import sys, os, re, json, subprocess, argparse, queue, threading, concurrent.futures as cf
T = os.path.dirname(os.path.abspath(__file__)); B = os.path.dirname(T); sys.path.insert(0, T)
import difftest
from diagnose import load_crc, crc_part
MEDIA = difftest.MEDIA

def lines(p):
    if not p or not os.path.exists(p): return None
    L = [re.sub(r'e([+-])0(\d\d)', r'e\1\2', l.rstrip('\r\n').lstrip('﻿')) for l in open(p, errors='replace')]
    idx = [i for i, x in enumerate(L) if x.startswith('Total CRC')]
    return L[:idx[1]] if len(idx) > 1 else L

def context(L, i):
    """property path of line i (enclosing block headers) and the enclosing entity/squad/player name"""
    ind = len(L[i]) - len(L[i].lstrip()); path = []; owner = None
    for j in range(i - 1, -1, -1):
        s = L[j]; t = s.strip()
        if t in ('{', '}') or not t: continue
        k = len(s) - len(s.lstrip())
        if k < ind and t.endswith(':'):
            path.append(t[:-1]); ind = k
            if t in ('entity:', 'squad:', 'player:') and owner is None:
                for q in range(j + 1, min(j + 8, len(L))):
                    m = re.match(r'\s*(name|id): \[ (.*) \]', L[q])
                    if m and m.group(1) == 'name': owner = (t[:-1], m.group(2))
                    if m and m.group(1) == 'id': owner = (owner or (t[:-1], '?')) + (m.group(2),)
            if k == 0: break
    return '/'.join(reversed(path)), owner

# ---- dump comparison (text, step 1 of HANDOFF 08.11) ----
# Both dumps are normalised like ../v15/norm.py (strip CR / BOM, Windows' 3-digit exponents e+001 -> e+01); the Mac dump also
# loses its "#<n> <running crc>" markers and everything after the 2nd "Total CRC" (crctrace's older kept blocks). Lines are then
# compared by their HEX values only: the decimal print in front of every <hex> is dropped, so print-only differences are skipped.
# Dumps are split into blocks (top-level sections; entity / squad / player objects keyed by name+id) and blocks are matched by key,
# so one inserted line in a log section or a missing entity does not shift the comparison of everything after it.
NORM_EXP = re.compile(r'e([+-])0(\d\d)\b')
DEC = re.compile(r'(?:-?(?:\d+\.\d*|\d*\.\d+)(?:e[+-]\d+)?|-?(?:nan|inf)\w*|true|false) ?(?=<)')
HEXV = re.compile(r'<([0-9a-fA-F]+)>')
FLOW = ('control flow', 'randomflow')

def load_dump(p):
    if not p or not os.path.exists(p): return None
    L = []; tot = 0
    for l in open(p, 'rb'):
        l = l.replace(b'\r', b'').rstrip(b'\n').decode('utf-8', 'replace').lstrip('﻿')
        if l.startswith('Total CRC'):
            tot += 1
            if tot > 1: break
            continue
        if re.match(r'#\d+ [0-9a-f]{8}$', l): continue
        L.append(NORM_EXP.sub(r'e\1\2', l))
    return L

def hexkey(l): return DEC.sub('', l)

def blocks(L):
    """[(key, start, end)] - top-level sections, and inside them each 2-indent entity/squad/player object as its own block"""
    B = []; top = None; topn = {}; objn = {}
    def close(e):
        if B and B[-1][2] is None: B[-1][2] = e
    for i, l in enumerate(L):
        t = l.strip()
        if l and not l.startswith(' ') and t not in ('{', '}'):
            close(i); nm = t.split(':')[0]; topn[nm] = topn.get(nm, 0) + 1; top = (nm, topn[nm]); B.append([top, i, None])
        elif l.startswith('  ') and not l.startswith('   ') and t in ('entity:', 'squad:', 'player:'):
            nm = idv = None
            for q in range(i + 1, min(i + 8, len(L))):
                m = re.match(r'\s*(name|id): \[ (.*) \]', L[q])
                if m and m.group(1) == 'name' and nm is None: nm = m.group(2)
                if m and m.group(1) == 'id' and idv is None: idv = m.group(2)
            k = (top, t[:-1], nm, idv)
            if idv is None: objn[k] = objn.get(k, 0) + 1; k = k + (objn[k],)
            close(i); B.append([k, i, None])
        elif l.startswith('  ') and not l.startswith('   ') and t == '}' and B and B[-1][0] != top and B[-1][2] is None:
            B[-1][2] = i + 1; objn[top] = objn.get(top, 0) + 1; B.append([top + ('tail', objn[top]), i + 1, None])
    close(len(L))
    return [(k, s, e) for k, s, e in B if e > s]

def describe(L, i, mline, wline):
    path, owner = context(L, i)
    field = mline.strip().split(': [')[0].rstrip(':').strip()
    mh, wh = HEXV.findall(mline), HEXV.findall(wline)
    d = {'line': i, 'path': path, 'field': field, 'mac_line': mline.strip()[:220], 'win_line': wline.strip()[:220],
         'entity': owner[2] if owner and len(owner) > 2 else None, 'kind': owner[0] if owner else None,
         'blueprint': owner[1] if owner else None}
    if mh and len(mh) == len(wh):
        comp = [k for k in range(len(mh)) if mh[k] != wh[k]]
        names = 'xyz' if len(mh) == 3 else ('xyzw' if len(mh) == 4 else None)
        d['component'] = ','.join(names[k] if names else str(k) for k in comp) if len(mh) > 1 else None
        d['mac'] = ' '.join('0x%08x' % int(mh[k], 16) for k in comp); d['win'] = ' '.join('0x%08x' % int(wh[k], 16) for k in comp)
    else:
        g = lambda s: s[s.index('[') + 1:s.rindex(']')].strip() if '[' in s and ']' in s else s.strip()
        d['mac'], d['win'] = g(mline)[:120], g(wline)[:120]
    return d

def dump_diff(mp, wp, maxd=12):
    mL, wL = load_dump(mp), load_dump(wp)
    if mL is None or wL is None: return {'error': 'dump missing (mac %s win %s)' % (mL is not None, wL is not None)}
    mB, wB = blocks(mL), blocks(wL); wk = {k: (s, e) for k, s, e in wB}; mk = {k for k, s, e in mB}
    r = {'mac_lines': len(mL), 'win_lines': len(wL), 'diffs': [], 'only_mac': [], 'only_win': [str(k) for k, s, e in wB if k not in mk][:10]}
    n = 0
    for k, s, e in mB:
        if k not in wk: r['only_mac'].append(str(k)); continue
        ws, we = wk[k]; lm, lw = e - s, we - ws
        for q in range(min(lm, lw) + (1 if lm != lw else 0)):
            a = mL[s + q] if q < lm else '<end of block>'; b = wL[ws + q] if q < lw else '<end of block>'
            if q < min(lm, lw) and hexkey(a) == hexkey(b): continue
            n += 1
            if len(r['diffs']) < maxd:
                d = describe(mL, min(s + q, len(mL) - 1), a, b); d['section'] = k[0][0] if isinstance(k[0], tuple) else k[0]
                if lm != lw: d['block_len'] = '%d/%d' % (lm, lw)
                r['diffs'].append(d)
            if lm != lw and q >= min(lm, lw): break
    r['n_value_diffs'] = n
    r['first'] = r['diffs'][0] if r['diffs'] else None
    r['first_state'] = next((d for d in r['diffs'] if d['section'] not in FLOW), None)
    r['equal'] = n == 0 and not r['only_mac'] and not r['only_win']
    return r

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recs', nargs='+'); ap.add_argument('--tag', default='v16')
    ap.add_argument('--dylib', default=B + '/dylibs/v16.dylib'); ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--redump', action='store_true', help='only redo the dump comparison of existing runs (no game runs)')
    ap.add_argument('--reuse-win', dest='reuse_win', action='store_true', help='keep existing Windows outputs (winref/<tag>_high/<rid>/p1|p2 with the needed files)')
    a = ap.parse_args()
    todo = []
    for rec in a.recs:
        rid = re.sub(r'[^A-Za-z0-9_.-]', '_', os.path.basename(rec)[:-4])
        rj = os.path.join(MEDIA, 'runs', a.tag, rid, 'result.json')
        if not os.path.exists(rj): continue
        F = json.load(open(rj)).get('first_diff')
        if F is None: continue
        todo.append((rec, rid, F))
    htag = a.tag + '_high'; os.makedirs(os.path.join(MEDIA, 'runs', htag), exist_ok=True); os.makedirs(os.path.join(MEDIA, 'winref', htag), exist_ok=True)
    wq = queue.Queue()
    for k in range(a.jobs): wq.put(k)
    def win(rec, rid, stop, dumpat, sub):
        k = wq.get()
        try:
            out = os.path.join(MEDIA, 'winref', htag, rid, sub)
            env = dict(os.environ, HIGH='1', STOPAT=str(stop), WORKER=str(k)); env.pop('RNG', None)
            os.makedirs(out, exist_ok=True)
            need = [os.path.join(out, 'dump_%s.txt' % f) for f in dumpat.split(',') if f] or [os.path.join(out, 'crc.log')]
            if a.reuse_win and all(os.path.exists(f) and os.path.getsize(f) > 0 for f in need): return out   # step 8: Windows unchanged
            for attempt in range(3):   # the Windows run sometimes dies at start ("gone, 0 frames") - retry
                subprocess.run([B + '/win/win_run.sh', rec, out, dumpat], env=env, stdout=open(out + '.log', 'a'), stderr=subprocess.STDOUT)
                if all(os.path.exists(f) and os.path.getsize(f) > 0 for f in need): break
            return out
        finally: wq.put(k)
    def mac(rec, rid, stop, dumpat, sub):
        ns = argparse.Namespace(tag=htag + '/' + rid + '_' + sub, dylib=a.dylib, speed=50, rng=False, dumpat=dumpat or None, high=True, stopat=stop)
        os.makedirs(os.path.join(MEDIA, 'runs', ns.tag), exist_ok=True)
        difftest.run_one(rec, ns)
        return os.path.join(MEDIA, 'runs', ns.tag, rid)
    summ = open(os.path.join(MEDIA, 'runs', htag, 'high_summary.tsv'), 'a'); lock = threading.Lock()
    def one(item):
        rec, rid, F = item; stop = max(F, 1) + 2; r = {'replay': rid, 'low_first_diff': F}
        hj = os.path.join(MEDIA, 'runs', htag, rid, 'high.json')
        if a.redump:
            if not os.path.exists(hj): return
            r = json.load(open(hj)); H = r['high'].get('first_frame')
            md2, wd2 = os.path.join(MEDIA, 'runs', htag, rid + '_p2', rid), os.path.join(MEDIA, 'winref', htag, rid, 'p2')
        else:
            H = None
        if not a.redump:
          with cf.ThreadPoolExecutor(2) as ex:
              fm = ex.submit(mac, rec, rid, stop, '', 'p1'); fw = ex.submit(win, rec, rid, stop, '', 'p1'); md, wd = fm.result(), fw.result()
          c = crc_part(load_crc(os.path.join(md, 'crc.log')), load_crc(os.path.join(wd, 'crc.log'))) if os.path.exists(os.path.join(md, 'crc.log')) and os.path.exists(os.path.join(wd, 'crc.log')) else {'first_frame': None, 'error': 'crc missing'}
          r['high'] = c; H = c.get('first_frame')
        if H is not None:
            dl = '%d,%d' % (max(H - 1, 0), H)
            if not a.redump:
             with cf.ThreadPoolExecutor(2) as ex:
                fm = ex.submit(mac, rec, rid, H + 2, dl, 'p2'); fw = ex.submit(win, rec, rid, H + 2, dl, 'p2'); md2, wd2 = fm.result(), fw.result()
            r['dump'] = dump_diff(os.path.join(md2, 'dump_%d.txt' % H), os.path.join(wd2, 'dump_%d.txt' % H))
            r['dump_prev_equal'] = dump_diff(os.path.join(md2, 'dump_%d.txt' % (H - 1)), os.path.join(wd2, 'dump_%d.txt' % (H - 1))).get('equal') if H > 0 else None
        od = os.path.join(MEDIA, 'runs', htag, rid); os.makedirs(od, exist_ok=True)
        json.dump(r, open(os.path.join(od, 'high.json'), 'w'), indent=1)
        dd = r.get('dump', {}); d0 = dd.get('first_state') or dd.get('first') or {}
        line = '\t'.join(str(x) for x in (rid, F, H, r.get('dump_prev_equal'), dd.get('n_value_diffs', dd.get('error')), d0.get('entity'),
                                            d0.get('blueprint'), d0.get('path'), d0.get('field'), d0.get('component'), d0.get('mac'), d0.get('win')))
        with lock: print(line, flush=True); summ.write(line + '\n'); summ.flush()
    with cf.ThreadPoolExecutor(a.jobs) as ex: list(ex.map(one, todo))

if __name__ == '__main__': main()
