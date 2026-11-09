#!/usr/bin/env python3
"""difftest.py [options] REPLAY.rec... - play Windows replays in the REAL Mac simulation (Mac binary + crossplay dylib, run on
Linux by macrun) and compare its per-frame sync CRC with the hashes the Windows game recorded in the replay.
For each replay: the first frame where the Mac simulation differs from Windows (or "equal to the end").

  --dylib PATH   crossplay dylib to load (default: newest released, dylibs/v16.dylib); 'none' = the bare Mac port
  --tag NAME     results subfolder (default: dylib file name)
  --jobs N       parallel runs (default 4)
  --speed X      game-time speed-up after the mission starts (default 50)
  --rng          keep the dylib's RNG trace (/tmp/dow2_rng.log of each run, big)
  --dumpat F,..  also dump the sync block of these frames (dump_<F>.txt; the dylib's crctrace adds older kept blocks)
  --high         High sync level (entity state in the CRC; the replay hashes are Low, so compare with a Windows High run)
Results: /var/www/media/dow2_archive/mac/difftest/runs/<tag>/<replay>/ (result.json, crc.log, macrun.log) and
         /var/www/media/dow2_archive/mac/difftest/runs/<tag>/summary.tsv
"""
import sys, os, re, json, shutil, subprocess, time, argparse, concurrent.futures as cf
T = os.path.dirname(os.path.abspath(__file__)); B = os.path.dirname(T)
sys.path.insert(0, T)
from crccmp import compare
sys.path.insert(0, '/var/www/dow_replay'); import dow2rec
MEDIA = '/var/www/media/dow2_archive/mac/difftest'
DATAROOT = MEDIA + '/macgame/DawnOfWar2RetributionData/retribution'
USERDIR = 'Users/macrun/Library/Application Support/Feral Interactive/Dawn of War II - Retribution/VFS/User/AppData/Roaming/My Games/Dawn of War II - Retribution'
INIT = '''-- macrun (difftest): play a replay headless
local function log(s) print("MACRUN " .. tostring(s)) end
function SCRIPT_Before() local ok, r = pcall(SetGamePlayback, "playback:%s.rec"); log("SetGamePlayback " .. tostring(ok) .. " " .. tostring(r)); return 1 end
function SCRIPT_Connect() return 1 end
function SCRIPT_GameStart() log("GameStart") return 1 end
'''

def run_one(rec, a):
    rid = re.sub(r'[^A-Za-z0-9_.-]', '_', os.path.basename(rec)[:-4])
    rd = os.path.join(MEDIA, 'runs', a.tag, rid); root = os.path.join(rd, 'macroot')
    if os.path.isdir(rd): shutil.rmtree(rd)
    shutil.copytree(MEDIA + '/skel', root, symlinks=True)
    pb = os.path.join(root, USERDIR, 'Playback'); os.makedirs(pb, exist_ok=True)
    shutil.copy(rec, os.path.join(pb, rid + '.rec'))
    with open(os.path.join(DATAROOT, 'mr_%s.lua' % rid), 'w') as f: f.write(INIT % rid)
    info, _ = dow2rec.parse(rec)
    dur = info.get('duration_s', 1800) or 1800
    env = dict(os.environ, MR_ROOT=root, MR_SPEED=str(a.speed), MR_CRC=os.path.join(rd, 'crc_own.log'))
    env.update(getattr(a, 'extra_env', None) or {})
    if not a.rng: env['MR_NORNG'] = '1'
    if a.high: env['MR_SYNC_HIGH'] = '1'
    if getattr(a, 'stopat_map', None) and rid in a.stopat_map: env['MR_STOPAT'] = str(a.stopat_map[rid])
    elif a.stopat: env['MR_STOPAT'] = str(a.stopat)
    if getattr(a, 'dumpat_map', None) and rid in a.dumpat_map: env['MR_DUMPAT'] = a.dumpat_map[rid]; env['MR_DUMPDIR'] = rd
    env['MR_NOVIS'] = '1'
    if a.dumpat: env['MR_DUMPAT'] = a.dumpat; env['MR_DUMPDIR'] = rd
    cmd = [B + '/macrun/macrun', '--dir', B + '/macrun', '--log', os.path.join(rd, 'macrun.log')]
    if a.dylib != 'none': cmd += ['--dylib', a.dylib]
    cmd += ['--', '-dev', '-nomovies', '-init', 'mr_%s.lua' % rid, '-norender', '-nosound', '-window']
    t0 = time.time(); to = 420 + dur * float(os.environ.get("DIFFTEST_TIMEOUT_MUL", "1.0"))   # step 8: raise under heavy CPU load
    try: p = subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, timeout=to); rc = p.returncode
    except subprocess.TimeoutExpired: rc = 'timeout'
    el = time.time() - t0
    try: os.remove(os.path.join(DATAROOT, 'mr_%s.lua' % rid))
    except OSError: pass
    crc = os.path.join(root, 'private/tmp/dow2_crc.log')
    if not os.path.exists(crc) or os.path.getsize(crc) == 0: crc = os.path.join(rd, 'crc_own.log')
    res = {'replay': os.path.basename(rec), 'rc': rc, 'seconds': round(el), 'duration_s': dur, 'map': info.get('map'),
           'players': [(p.get('name'), p.get('race'), p.get('team')) for p in info.get('players', [])]}
    if os.path.exists(crc):
        shutil.copy(crc, os.path.join(rd, 'crc.log')); res.update(compare(rec, crc))
    log = open(os.path.join(rd, 'macrun.log'), errors='replace').read() if os.path.exists(os.path.join(rd, 'macrun.log')) else ''
    res['finished'] = 'playback finished' in log or 'game over after' in log or 'stop at frame' in log
    m = re.search(r'\*\*\* signal (\d+) at rip (\w+)', log); res['crash'] = m.group(0) if m else None
    json.dump(res, open(os.path.join(rd, 'result.json'), 'w'), indent=1)
    for d in ('macroot/private/tmp',):                          # keep the run small: drop the replay copy and caches
        pass
    shutil.rmtree(os.path.join(root, 'Users/macrun/Library/Application Support/Feral Interactive/Dawn of War II - Retribution/FeralTemp'), ignore_errors=True)
    return res

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('recs', nargs='+'); ap.add_argument('--dylib', default=B + '/dylibs/v16.dylib')
    ap.add_argument('--tag'); ap.add_argument('--jobs', type=int, default=4); ap.add_argument('--speed', type=float, default=50)
    ap.add_argument('--rng', action='store_true'); ap.add_argument('--dumpat'); ap.add_argument('--high', action='store_true'); ap.add_argument('--stopat', type=int)
    a = ap.parse_args(); a.tag = a.tag or (os.path.basename(a.dylib).replace('.dylib', '') if a.dylib != 'none' else 'nodylib')
    if a.dylib != 'none' and not os.path.exists(a.dylib + '.map'):
        subprocess.run(['/mnt/dow2bench/dev/re_venv/bin/python', B + '/macrun/mkdylmap.py', a.dylib, a.dylib + '.map'], check=True)
    os.makedirs(os.path.join(MEDIA, 'runs', a.tag), exist_ok=True)
    out = open(os.path.join(MEDIA, 'runs', a.tag, 'summary.tsv'), 'a')
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for r in ex.map(lambda x: run_one(x, a), a.recs):
            line = '%s\t%s\t%s\tframes=%s/%s\tfirst_diff=%s\tmac=%s\twin=%s\tfinished=%s\t%ss\t%s' % (
                r['replay'], r.get('map'), ','.join(sorted(set(p[1] or '?' for p in r['players']))), r.get('mac_frames'), r.get('win_ticks'),
                r.get('first_diff'), r.get('mac'), r.get('win'), r['finished'], r['seconds'], r.get('crash') or '')
            print(line, flush=True); out.write(line + '\n'); out.flush()

if __name__ == '__main__': main()
