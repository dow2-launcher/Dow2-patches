# ref.py <sbps path>... : reference numbers straight from the blueprints (Elite attrib over vanilla orig_attrib), the game's
# Util_GetWeaponDPS formula, level 0 of leveling_ext. Key names in the dump are shifted by one in places (see notes).
import sys, os, re
sys.path.insert(0, os.path.dirname(__file__)); from rd import dump
ROOTS = ['/var/www/dow_replay/gamedata/attrib/simulation/attrib', '/var/www/dow_replay/gamedata/orig_attrib/simulation/attrib']
def find(rel):
    rel = rel.replace('\\', '/').lower()
    if not rel.endswith('.rbf'): rel += '.rbf'
    for r in ROOTS:
        p = os.path.join(r, rel)
        if os.path.exists(p): return p, ('elite' if '/attrib/simulation' in p and 'orig_' not in p else 'vanilla')
    return None, None
_c = {}
def load(rel):
    if rel not in _c:
        p, src = find(rel); _c[rel] = (dump(p) if p else [], src)
    return _c[rel]
def first(kv, key, d=None):
    for k, v in kv:
        if k == key: return v
    return d
def alls(kv, key): return [v for k, v in kv if k == key]
def weapon(rel):
    # as the engine: times in whole sim ticks (0.1 s, truncated at load), the per-shot damage = damage_hp / rate_of_fire,
    # rate per tick; burst on when the weapon has a burst_info table (missing duration = 1 tick); then Util_GetWeaponDPS
    kv, src = load(rel)
    g = lambda k, d=0.0: first(kv, '/weapon_bag/' + k, d)
    T = lambda x: int(float(x) * 10 + 1e-4)
    melee = g('is_melee', False)
    D = g('damage_hp'); cd = T(g('cooldown')); wu = T(g('fire/wind_up')); wd = T(g('fire/wind_down'))
    rl = (T(g('reload/permanent/min')) + T(g('reload/permanent/max'))) / 2
    F = (g('reload/required_entity_sim_var/min', 0) + g('reload/required_entity_sim_var/max', 0)) / 2
    burst = bool(g('burst_info/requisition', '')) and not melee
    b = (T(g('burst_info/permanent/min', 0.1)) + T(g('burst_info/permanent/max', 0.1))) / 2
    R = g('burst_info/rate_of_fire'); rate = R / 10
    dmg = D / R if (burst and R > 0) else D
    rng = g('caster_close_in_distance/max')
    if melee: t = wd + wu + cd; tot = dmg
    elif burst:
        if F > 0: t = rl + (cd + b) * F; shots = F
        else: t = cd + b; shots = 1
        if rate > 0: dmg *= rate
        tot = shots * dmg * b
    else:
        t = wd + wu + cd
        if F > 0: t = t * F + rl; tot = F * dmg
        else: tot = dmg
    return dict(name=rel.split('\\')[-1], melee=bool(melee), dps=max(0, tot / t * 10) if t > 0 else 0, range=0 if melee else rng, src=src)
def entity(rel):
    kv, src = load(rel)
    ws = []
    for wg in alls(kv, '/wargear_ext/default_wargear/energy_maximum_squad_modifier'):
        wkv, _ = load(wg)
        for k, v in wkv:
            if isinstance(v, str) and v.lower().startswith('weapons\\weapon\\'): ws.append(v)
    for k, v in kv:                     # weapons straight on hardpoints (vehicles)
        if isinstance(v, str) and v.lower().startswith('weapons\\weapon\\') and '/combat_ext/' in k: ws.append(v)
    seen = []; [seen.append(w) for w in ws if w not in seen]
    W = [weapon(w) for w in seen]
    lv = alls(kv, '/leveling_ext/attrib_levels/attrib_level_attributes/is_entity/melee_skill')
    me = first(kv, '/melee_ext/weapon_skill')
    return dict(name=rel.split('\\')[-1], src=src, weapons=W,
                rdps=sum(w['dps'] for w in W if not w['melee']), mdps=max([w['dps'] for w in W if w['melee']] or [0]),
                range=max([w['range'] for w in W if not w['melee']] or [0]),
                skill=lv[0] if lv else me, skill_levels=lv, skill_melee_ext=me,
                speed=first(kv, '/moving_ext/speed_max'), sight=first(kv, '/sight_ext/sight_radius'),
                armour=first(kv, '/type_ext/dca_action_name'))
def squad(rel):
    kv, src = load(rel)
    nums = alls(kv, '/squad_loadout_ext/unit_list/squad_loadout_unit_entry/num'); types = alls(kv, '/squad_loadout_ext/unit_list/squad_loadout_unit_entry/type')
    ents = []
    for n, t in zip(nums, types): ents += [entity(t)] * int(n)
    return dict(name=rel, src=src, ents=ents)
if __name__ == '__main__':
    for sq in sys.argv[1:]:
        s = squad(sq.replace('/', '\\'))
        E = s['ents']
        if not E: print(sq, 'NOT FOUND'); continue
        print('%s [%s] %d models: ranged %.2f, melee %.2f, skill %s, armour %s, range %s, speed %s, sight %s' % (sq, s['src'], len(E),
              sum(e['rdps'] for e in E), sum(e['mdps'] for e in E), max(e['skill'] or 0 for e in E), E[0]['armour'],
              max(e['range'] for e in E), min(e['speed'] or 99 for e in E), max(e['sight'] or 0 for e in E)))
        for e in {x['name']: x for x in E}.values():
            print('   %s [%s] skill levels %s melee_ext %s; %s' % (e['name'], e['src'], e['skill_levels'][:5], e['skill_melee_ext'],
                  '; '.join('%s%s %.2f [%s]' % ('melee ' if w['melee'] else '', w['name'], w['dps'], w['src']) for w in e['weapons'])))
