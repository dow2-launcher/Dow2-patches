# editor_dps.py <weapon blueprint>... : the attribute editor's DPS by hand from the attribute files (Elite attrib over vanilla, ref.py), seconds as written - the check of UnitStats 0.5
import sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.dont_write_bytecode = True
import ref
def W(rel):
    kv,src=ref.load(rel); g=lambda k,d=0.0: ref.first(kv,'/weapon_bag/'+k,d)
    melee=bool(g('is_melee',False)); D=g('damage_hp'); cd=g('cooldown'); wu=g('fire/wind_up'); wd=g('fire/wind_down')
    rl=(g('reload/permanent/min')+g('reload/permanent/max'))/2; n=(g('reload/required_entity_sim_var/min',0)+g('reload/required_entity_sim_var/max',0))/2
    burst=bool(g('burst_info/requisition','')); rof=g('burst_info/rate_of_fire',0) or 0
    bd=(g('burst_info/permanent/min',0.1)+g('burst_info/permanent/max',0.1))/2
    proj=bool(g('projectile_info/requisition','')); pps=g('projectile_info/projectiles_per_shot',1) or 1
    dF=D; fire=0
    if melee: per=wu+wd+cd; n=0; rl=0
    else:
        if proj: dF=D*pps
        elif burst and rof>0: fire=bd; dF=D*bd      # damage_hp per second of firing
        per=wu+fire+wd+cd
    if n>0: t=n*per+rl; tot=n*dF
    else: t=per; tot=dF
    s='%s [%s] dmg %s cd %s wu %s wd %s reload %s shots %s burst %s rof %s dur %s proj %s pps %s' % (rel.split('\\')[-1],src,D,cd,wu,wd,rl,n,int(burst),rof,bd if burst else '-',int(proj),pps)
    return tot/t if t>0 else 0, s, '%g x %g / (%g x (%g+%g+%g+%g) + %g)' % (n or 1, dF, n or 1, wu, fire, wd, cd, rl)
for w in sys.argv[1:]:
    d,s,f=W(w); print('%8.4f  %s\n          = %s' % (d,s,f))
