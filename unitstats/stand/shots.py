# shots.py ShotProbe.log [UnitStats.log] - the DPS the engine really delivers per weapon (ShotProbe.dll events), next to
# UnitStats' number for the same weapon (its log: "<file> <dps> dps (tooltip <old>; cycle ...") .
# Window per weapon: from the first windup after its first reload to the first windup after its last reload (whole
# magazines); without reloads from its 2nd windup to its last one. Shots (projectiles, melee strikes) in the window x the
# damage per shot / the window's length.
import sys, re, collections, statistics
W = {}; ev = collections.defaultdict(list)
for ln in open(sys.argv[1], errors='replace'):
    if ln.startswith('WEAPON'):
        m = re.match(r'WEAPON (\S+) vt (\S+) (\S+) dmg (\S+) .* squad \S+ (\S+)', ln)
        if m: W[m.group(1)] = dict(file=m.group(3).split('\\')[-1], dmg=float(m.group(4)), squad=m.group(5).split('\\')[-1], line=ln.strip())
        continue
    p = ln.split()
    if len(p) == 3 and p[1] in 'SWRM' and p[0].lstrip('-').isdigit(): ev[p[2]].append((int(p[0]), p[1]))
pred = {}
if len(sys.argv) > 2:
    for ln in open(sys.argv[2], errors='replace'):
        for m in re.finditer(r' (?:melee )?(\S+?) (\d+\.\d+) dps \(tooltip (\d+\.\d+); cycle (\w+) ([\d.]+) ticks, ([\d.]+) shots x([\d.]+), reload ([\d.]+), cooldown ([\d.]+)', ln):
            pred.setdefault(m.group(1), (float(m.group(2)), float(m.group(3)), m.group(4), float(m.group(5)), float(m.group(6)), float(m.group(7)), float(m.group(8)), float(m.group(9))))
rows = collections.defaultdict(list)
for w, es in ev.items():
    if w not in W: continue
    es.sort()
    shots = [t for t, k in es if k in 'SM']; wind = [t for t, k in es if k == 'W']; rel = [t for t, k in es if k == 'R']
    # cycle starts: the windups; a ranged weapon with no wind_up goes 10 -> 6 -> 7 inside one update (no 'W' seen):
    # then the first shot of each burst (a shot more than one tick after the previous one) instead
    starts = wind if len(wind) >= 3 else [t for i, t in enumerate(shots) if i == 0 or t - shots[i - 1] > 1]
    if len(shots) < 4 or len(starts) < 3: continue
    rel = [r for r in rel if starts[-1] > r]              # reloads with a burst after them (the run may end in one)
    if len(rel) >= 2:
        a = next((t for t in starts if t > rel[0]), None); b = next((t for t in starts if t > rel[-1]), None)
    else:
        a, b = starts[1], starts[-1]
    if a is None or b is None or b <= a: continue
    n = sum(1 for t in shots if a <= t < b)
    cyc = [y - x for x, y in zip(starts, starts[1:]) if a <= x < b]
    info = W[w]
    rows[(info['squad'], info['file'])].append(dict(dps=n * info['dmg'] / (b - a) * 10, n=n, ticks=b - a, mags=len([t for t in rel if a <= t < b]),
                                                 cyc=statistics.median(cyc) if cyc else 0, shots_per_cycle=n / max(len(cyc), 1), dmg=info['dmg']))
for k in rows:
    m = max(r['n'] for r in rows[k]); rows[k] = [r for r in rows[k] if r['n'] >= 0.4 * m]
print('%-28s %-34s %5s %8s %8s %8s %7s %6s %6s' % ('squad', 'weapon', 'shots', 'measured', 'v0.4', 'tooltip', 'ticks', 'reloads', 'cycle'))
for (sq, f), rs in sorted(rows.items()):
    d = statistics.mean(r['dps'] for r in rs); p = pred.get(f.replace('.rgd', ''), pred.get(f))
    print('%-28s %-34s %5d %8.2f %8s %8s %7d %6d %6.1f   (%d weapons: %s)' % (sq[:28], f[:34], sum(r['n'] for r in rs), d,
          '%.2f' % p[0] if p else '-', '%.2f' % p[1] if p else '-', sum(r['ticks'] for r in rs), sum(r['mags'] for r in rs),
          statistics.median([r['cyc'] for r in rs]), len(rs), ' '.join('%.2f' % r['dps'] for r in rs)))
    if p: print('%66s predicted cycle: %s, %.2f ticks per burst, %.2f shots, F %.2f, reload %.1f, cooldown %.2f' % ('', p[2], p[3], p[4], p[5], p[6], p[7]))
