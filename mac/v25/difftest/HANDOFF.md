# Mac↔Win replay difftest — handoff (08.11.2026, written by the coordinator; previous agent stopped by the owner for cost)

## What works (tool: difftest.sh in this dir)
- The REAL Mac simulation runs on this Linux server: macrun = Mac binary + crossplay dylib (dylibs/v16.dylib, v17.dylib), no Mac needed.
- Windows replays carry the per-frame sync CRC recorded by the Windows game → tools/difftest.py plays a replay in the Mac sim and
  reports the first frame whose CRC differs (Low level). Replays: /var/www/media/dow2_archive/mac/difftest/replays_cloud/ (owner's 60
  vanilla replays; 3 are Elite (ordo_malleus/t_tau) and temp.rec is 398 frames — skip those 4).
- tools/diagnose_high.py: Mac and Windows (private Wine prefixes wprefix*, win/win_run.sh) at High sync level up to F → first frame H
  whose STATE differs (often 30–800 frames earlier than F) → first differing entity id / blueprint / property path / field /
  component / mac hex / win hex (FIXED in step 1, see below).
- tools/diagnose_all.py: RNG traces of both around F → first differing random call + both call chains.
- tools/watch_writer.py (pass 3, unfinished): watchpoint on the first differing field → which Mac function writes it.
- tools/report.py → results_<tag>.md/.tsv in /var/www/media/dow2_archive/mac/difftest/.

## Results so far (v16 dylib, 60 replays) — runs in /var/www/media/dow2_archive/mac/difftest/runs/
- v16/: every replay diverges; Low first_diff median frame 935 (≈1.5 min), min ~767, max 2894.
- v16_diag/ (diagnose_all): first differing random-call sites: WeaponUtil::CanAutoTarget 16, ElementExt::CalculateFormationDrift 13,
  RangedWeapon::DoWorldObjectHits 4, CombatState::UpdateCrouching 4, WeaponUtil::GetHitPositionOnEntity 3,
  ShootingWeapon::UpdateState_WS_WindingUp 3, _WS_Idle 3, SquadMovement::ClearElementCover 2, others 1 each. These are where the
  divergence becomes visible in RNG; the cause is an earlier float difference.
- v16_high/: H frames found (e.g. 750 vs Low 785, 496 vs 1349) but the differing entity/property = None (extraction broken).
- watch1/watch2: pass-3 attempts; watch2 shows first_diff=1 on 48787 — the watch harness itself perturbs/miscompares; not usable yet.
- v17 (knockfix) not yet run in the mass test.

## Step 1 DONE (08.11.2026) — diagnose_high property extraction
- Changed: dump comparison rewritten (text, not running-CRC markers): both dumps normalised like ../v15/norm.py (CR/BOM, e+0dd),
  Mac "#n crc" markers and crctrace's older blocks (after 2nd "Total CRC") dropped, lines compared by hex only (decimal print
  ignored), dumps split into blocks (sections; entity/squad/player keyed by name+id) matched by key. Windows run retried up to 3x
  when it dies at start ("gone, 0 frames" — that is why calderis had no Windows dump in v16). New: --redump (recompare existing p2
  dumps, no game runs), H-1 equality check (dump_prev_equal). Old version: tools/diagnose_high.py.bak_step0.
- Run: python3 tools/difftest.py --dylib dylibs/v17.dylib --tag v17 --stopat 3000 REC... (Low F, ~70 s each), then
  python3 tools/diagnose_high.py --tag v17 --dylib dylibs/v17.dylib --jobs 3 REC... (~10 min for 3).
  Output: runs/v17_high/high_summary.tsv (replay, F, H, H-1 equal, n diffs, entity, blueprint, path, field, component, mac, win),
  runs/v17_high/<replay>/high.json (up to 12 diffs, first, first_state).
- Results (v17): /var/www/media/dow2_archive/mac/difftest/results_step1.md —
  lohmistral F1614 H324 csm_tactical_marine 16501 grounddynamicspath gpr:hlr:numsearched 57/58;
  6 F1349 H496 ig_sentinel 31465 position y 0x434816e6/0x434816e5;
  calderis 05-29 F921 H882 (v16 785/750, knockfix moved it) eld_warp_spider 30448 combatext/activeweapon horizonrotation
  0xc1d07d34/0x4168b524 (whole different aim). H-1 dumps equal in all 3.

## Step 2 DONE (08.11.2026) — who writes the first differing value (results_step2.md in the media difftest dir)
- How to run a watch (does NOT perturb: High CRC of the watch run == diagnose_high p2 run up to the stop frame, checked on all 3):
  macrun env MR_WATCH=H:entity:hexoffset:kind with --high, stop at H+1, e.g. via difftest.run_one(..., high=True, stopat=H+1,
  extra_env={'MR_WATCH': '496:31465:c4:e', 'MR_WATCH_TERRAIN': '1'}). Kinds: e = entity (+0xc0/c4/c8 position), w = first
  ShootingWeapon (+0x1b8/1bc rotations, +0xf8 target type), p = GroundDynamicsPath (+0x140 gpr:hlr:numsearched; new, hook on
  GetSyncTokenHigh 0x101e7edc0). Armed for exactly the sim step between the H-1 and H dumps; log "WATCH write #n: value .. rip .."
  + Mac backtrace in macrun.log. Diagnostics (env, read-only, after the write): MR_WATCH_TERRAIN=1 (ground-snap helper: x/y/z, every
  terrain source, the walkable triangle + height in port and SimEngine order), MR_WATCH_WEAPON=1 (target entity/pos/hit offset,
  desired angles). tools/watch_writer.py updated for the step-1 record format (kinds e/w/p, sets both diag envs). macrun backups:
  macrun/game.c.bak_step2, macrun.bak_step2 (rebuild: macrun/build.sh). Note: Mac symbol gaps — "GroundDynamics::GetDriverInfo+0x2a2"
  is really the unnamed snap helper 0x101e763e0 (DOW2 0x912680).
- Results:
  6 F496 sentinel y: Entity::SetTransform < snap helper (0x912680) < GroundDynamics::Drive (0x913eb0); value from
  CollisionSystem::QueryTerrainHeight -> WalkableSurfaceSystem::QueryHeight -> CollisionModel::QueryHeight ->
  CollisionModel::GetTriangleHeightNormal (Mac 0x102185cf0, SimEngine 0x10030f40): plane d = Mac (y*ny + x*nx) + z*nz,
  SimEngine (y*ny + z*nz) + x*nx. Reproduced exactly (e6 vs e5). Also normal length: Mac (ny²+nx²)+nz², Win (nz²+ny²)+nx².
  calderis F882 warp spider aim: ShootingWeapon::TryToMatchHeading (0xa4a250) < UpdateState (0xa4aa10); the target is different:
  Mac WeaponUtil::FindWeaponTarget picked hive tyrant 30464, Windows' angle fits ork boy 30526 -> candidate choice in
  FindWeaponTarget (GetCloseEnemies/QueryEntities list, CanAutoTarget RNG — same site as v16's first RNG diff).
  lohmistral F324 numsearched: PathHigh::FindPath (0xa23000) copies the A* counter; A* 0x101e991f0 ↔ 0xa23370 expands 57 vs 58
  nodes -> open-list tie order (libc++ vs STLport heap) or a 1-ulp cost (Win cost fn 0xa7a8f0); needs expansion traces.
- (done in step 3) Step 3 first: lift CollisionModel::GetTriangleHeightNormal in SimEngine order (+ check CollisionModel::Query, SimEngine
  0x10030a10, same plane math) as a v18 fix with self-test; rerun replay 6 -> next H. Then FindWeaponTarget candidates, then A*.

## Step 3 DONE (08.11.2026) — v18 = v17 + walkfix (walkable-surface height as SimEngine.dll)
- Fix: CollisionModel::GetTriangleHeightNormal (Mac 0x102185cf0 = SimEngine 0x10030f40): plane offset d -> (y*ny + z*nz) + x*nx
  (0x102185da9, 34 B) and |n|^2 -> (nz^2 + ny^2) + nx^2 (0x102185dd0, 29 B), in place; self-test 48 at load, DOW2_NOWALKFIX=1.
  Side by side 1M (t18): 10.44 % -> 0. All regressions 0 (v18/reg18.sh). CollisionModel::Query + its FindIntersection were
  already fixed by combatfix (v12); 2D point-in-triangle, QueryHeight wrappers equal. Details: ../v18/STATUS.md,
  package /var/www/media/dow2_archive/mac/crossplay_v18/. dylibs/v18.dylib (+ .map).
- Replays (runs/v18, v18_high; v17 baselines of the 5 new ones in runs/v17x, v17x_high; stop 4500): no Low F moved
  (6 1349, loh 1614, calderis 921, 48787 1088, angelgate 918, arena 2894, outskirts 780, refinery-06-01 1266). High H moved where
  the walkable height was first: 6 496 -> 1344, arena 612 -> 1131 (H-1 dumps not equal there), outskirts 498 -> 759.
  First High diffs now: activeweapon horizon/verticalrotation in 5 of 8 (6 cultist 1344, calderis warp spider 882, arena warp
  spider 1131, outskirts csm 759, 48787 sm vertical 1071); angelgate 859 coverspot pos 1 ulp; refinery-06-01 1053 sightext
  lastfow.cx 136/135; lohmistral 324 A* numsearched.
- Step 4 target: weapon target choice / heading — trace WeaponUtil::FindWeaponTarget candidates (GetCloseEnemies /
  QueryEntities order, CanAutoTarget, priority compares) on both sides on calderis F882 or 6 F1344 (watch kind w, MR_WATCH_WEAPON);
  check first whether 6's cultist and outskirts' csm are also "different target" (desired angle vs candidates) or a float aim
  difference. Cheap side target: angelgate coverspot pos (1 ulp, SquadMovement::ElementCover — watch who writes it).

## Step 4 DONE (08.11.2026) — weapon target choice = sort order of equal priorities -> v19 (sortfix)
- New tracing: macrun MR_FWT=lo:hi (game.c: hooks the sort call 0x101c96eff via a near page 0x10ff00000, chains to whatever the
  call pointed to - the stable_sort or the dylib's sortfix page; logs runs/<tag>/<rid>/fwt.log) and DiffTrace.dll DT_FWT=lo,hi
  (hooks DOW2.exe calls 0x98c7e0 sort and 0x98d047 select; winref/<tag>/<rid>/fwt.log; frame = last CRC key + 1).
  tools/fwt_run.py --tag T --dylib D REC:H ... runs both (High, dumps H-1/H, FWT log H-2..H+1). Backups: macrun/game.c.bak_step3,
  macrun.bak_step3, win/difftrace_win.c.bak_step3, win/DiffTrace.dll.bak_step3 (new DLL also in wgame/dow2-patches/).
- Cause: FindWeaponTarget (Mac 0x101c96b20 = DOW2.exe 0x98cde0, select 0x98c7c0, add 0x98c620, priority 0x98c390) - candidate
  lists before the sort identical on both (41/41 calls), mostly priority 0; Windows STLport std::sort (unstable for n > 16),
  Mac libc++ stable_sort -> another first candidate. calderis f882: n=20 all 0, Win 30528 / Mac 30464; 6 f1344 n=23; outskirts f759 n=21.
- Fix v19 = v18 + sortfix.c (call 0x101c96eff -> STLport sort transcribed, sortfix_impl.h); t19 side by side 1M: 58.5 % -> 0;
  reg19.sh all 0, Mac outputs = v18's. dylibs/v19.dylib (+ .map); package /var/www/media/dow2_archive/mac/crossplay_v19/.
- Replays (results_step4.md): Low F 6 1349->5035, calderis 921->7854, arena 2894->5902, outskirts 780->7966, refinery-06-01
  1266->6381, 48787 1088->1888, angelgate 918->1395, lohmistral 1614->1821. High H now: 6 3680 + arena 4053 formation spot map x
  (cell 2.0/1.0 apart), calderis 7729 + refinery-06-01 1053 sightext lastfow.cx (FOW cell 1 apart), 48787 1885 holdableext slotid
  2/3, outskirts 1072 holdableext priority 1 ulp, angelgate 859 coverspot 1 ulp, lohmistral 324 A* numsearched.
- Step 5 target: destinationformationstate spot map x (6 F3680 / arena F4053; watch who writes it; likely another tie order or
  rounding), then holdable slot/priority (48787 1885, outskirts 1072), FOW lastfow.cx. Audit idea: other STLport std::sort sites
  where the port uses stable_sort / libc++ sort (same tie class).

## Step 5 DONE (08.11.2026) - simulation sort audit -> v20 (gsortfix); results_step5.md (audit table) in the media difftest dir
- Tools: v20/sortaudit/ (static: libc++ sort / stable_sort instantiations + callers on Mac, STLport introsort entries + call sites
  in DOW2.exe/SimEngine.dll); macrun/sa.c: MR_SORTAUDIT=sites (per-site calls, ties, would STLport/stable order differ; sa_sum.txt,
  sa.log), MR_SORTAUDIT_ALL=1 (log every call), MR_ENTRYCOUNT=funcs.txt (per-function call counts + frames, ec_sum.txt; prologue
  table from sortaudit/ecgen.py; skips functions the dylib already hooked); DiffTrace.dll DT_SORTS=hi (every DOW2.exe std::sort
  call site per frame -> sorts.log; win/sort_sites.h); tools/sa_run.py; sortaudit/pair_dyn.py, pair_ec.py (pair by per-frame
  counts). Backups: macrun/game.c.bak_step4, macrun.bak_step4, win/difftrace_win.c.bak_step4, win/DiffTrace.dll.bak_step4.
- Finding: the port replaced std::sort with std::stable_sort in the simulation; all 12 paired Mac stable sites are DOW2.exe
  STLport std::sort calls (equal call counts per frame); libc++ std::sort sites are another algorithm too (ties differ even n<=5).
- v20 = v19 + gsortfix.c (45 sites -> STLport sort with the port's own comparator probed on 2-element scratch, runtime guard on the
  first 64 calls/site). Excluded: 7 sites with non-trivial elements (TurningBehaviorUtil::CalcTurnCost = DOW2.exe 0xa39c43, PlayerFOW,
  RetreatUtil x2, SplitCommand_Optimal, AI SortAndCapTargets, WargearExtInfo, FunctionalActions S=88) - need hand comparators.
  t20 1M: 0 differ (port 72 %); reg20 all 0, Mac outputs = v19's. dylibs/v20.dylib(.map); /var/www/media/dow2_archive/mac/crossplay_v20/.
- Replays v19 -> v20: Low F 6 5035=, lohmistral 1821->6857, calderis 7854=, 48787 1888->5427, angelgate 1395->4901, arena 5902=,
  outskirts 7966->8325, refinery-06-01 6381=. High H: 6 3680->5035 (krak missile offset x at F), arena 4053->5902 (ork missile
  offset x at F) - formation spot map gone (B solved by A); lohmistral 324 and 48787 3415 A* numsearched; angelgate 3023 and
  refinery-06-01 1053 sightext lastfow.cx; outskirts 1072 holdable priority 1 ulp; calderis: Windows High run ended at 6115.
- Step 6 target: A* numsearched (2 replays) and FOW lastfow.cx (2 replays; PlayerFOW sort is one of the excluded ones), then the
  excluded non-trivial sorts (TurningBehaviorUtil first), projectile offset x at F, holdable priority ulp.

## Step 6 DONE (08.11.2026) - A* cost + FOW cell rounding + turn-cost sort -> v21; results_step6.md in the media difftest dir
- New tracing: macrun MR_ASTAR=lo:hi (game.c: PathHigh A* step 0x101e99630 hooked; astar.log: per search "A f goal cost-object",
  per step popped node cell<parent g h f (hex) + heap size, then every neighbour cell:g, result, numsearched) and DiffTrace.dll
  DT_ASTAR=lo,hi (call 0xa233b9 -> step 0xa235a0, same format). tools/trace_run.py = generic fwt_run.py (--menv K=V --wenv K=V with
  {lo} {hi} {H}; --nowin / --nomac). Backups: macrun/game.c.bak_step5, macrun.bak_step5, win/difftrace_win.c.bak_step5,
  win/DiffTrace.dll.bak_step5 (new DLL also in wgame/dow2-patches/). Runs: runs/s6_astar, winref/s6_astar.
- A* numsearched cause: same pops, a neighbour's g 1 ulp apart (lohmistral sector 1010 0x423d540e / 0x423d540f). PathHighEnvironment::
  ActualCost (Mac 0x101e9a950 = DOW2.exe 0xa7a7f0): Win L = (diff*d)*0.1f, cost = (P(to)+P(from))+L; port L = (diff*0.1f)*d,
  cost = (P(from)+L)+P(to). Fix costfix.c (part 23, jump to hc_cost, self-test 256 from 0xa7a7f0, DOW2_NOCOSTFIX=1).
  (Step 2's "Win cost fn 0xa7a8f0" was wrong: that is FindBestCellWithinAP; the heuristic 0xa7a3d0 is equal.)
- FOW lastfow.cx: the PlayerFOW sort is order-independent (ties = same entity + frame, de-dup keeps OR of flags) - not the cause.
  Writer PlayerFOW::UpdateEntity -> world->cell DOW2.exe 0x868330: fistp(v + 0.50001f) (round half even) vs port
  roundf((float)((double)v + 0.50001)) (half away) -> cell edge = Mac +1. Fix fowcellfix.c (part 24: 16 inlined roundings,
  gen_fowcell.py, fc_round via near stub, self-test 256 from 0x868330, DOW2_NOFOWCELLFIX=1).
- TurningBehaviorUtil::CalcTurnCost sort: POD {float,int}, float less on both (0xa3c9f0); v20 exclusion was a false positive ->
  gsortfix site 46.
- t21 1M: cost port 113627 -> 0, cell port 16767 -> 0; reg21 all 0, Mac outputs = v20's; no gsortfix guard failures in 8 replays.
  dylibs/v21.dylib(.map); package /var/www/media/dow2_archive/mac/crossplay_v21/ (details ../v21/STATUS.md).
- Replays v20 -> v21: Low F unchanged except outskirts 8325 -> 8693. High H: lohmistral 324 -> 5719 (ammocase healthext health
  2 ulp), 48787 3415 -> 5427 (= F, sm_krak_missile offset x), refinery-06-01 1053 -> 6381 (= F, eld_haywire_grenade offset x),
  angelgate 3023 -> none up to 4657 (Windows High run ended there; F 4901), 6 / arena unchanged (= F, projectile offset x),
  outskirts 1072 holdable priority 1 ulp, calderis 05-29 none up to ~6400.
- Step 7 target: modprojectiledynamics offset x at F (6 F5035 sm_krak_missile 33280, 48787 F5427, arena F5902, refinery-06-01 F6381:
  whole different values -> hit position / target choice; watch the offset write at F, RNG trace around F), then lohmistral 5719
  health 2 ulp, outskirts 1072 holdable priority 1 ulp, remaining excluded sorts (RetreatUtil x2, SplitCommand_Optimal, AI, Wargear,
  FunctionalActions S=88).

## Step 7 DONE (08.11.2026) - homing random order, slot priority, early Windows runs -> v22; results_step7.md in the media difftest dir
- projectile offset at F (6/48787/arena/refinery 06-01): High randomflow showed the draw order swapped (Mac PRJX PRX2.., Win PRX2 PRJX..).
  ModProjectileDynamics::initHoming (Mac 0x101d43d60 = DOW2.exe 0x999090): off.c = GetRand(PRJc)*off.c*(GetRand(PRc2)>0.5?0.5:-0.5),
  MSVC evaluates the sign's GetRand first. homingfix.c (part 25, C replacement, self-test 256, DOW2_NOHOMINGFIX=1). Audit of all
  random-call tag orders (v22/t22/randorder.py): only this + CalculateScatterPosition (already aimfix) differ.
- outskirts H1072 holdableext priority: CombatSlotManager::OnAddEntity weapon priority t*(scale*10) vs DOW2.exe (scale*t)*10 (0xa586ed).
  holdfix.c (part 26, 20-byte patch at 0x101ceed70, self-test runs the patched loop in place, DOW2_NOHOLDFIX=1).
- Windows High runs "ending early" = win/win_run.sh gpid() prefix match: "-init dt_6" matched dt_6p_*, the end of replay 6's run killed
  the other worker's game. Now exact -init $ID.lua (backup win_run.sh.bak_step6). All Windows High runs reach the end now.
- New macrun watch kind x: MR_WATCH=H:id:off:x + MR_WATCH_EXT=<extension sync fn hex>:<prologue len> (HealthExt::GetSyncTokenLow
  101b656c0:17 -> health +0x70; HoldableExt::GetSyncToken 101b74880:17 -> priority +0x34). Backups game.c.bak_step6, macrun.bak_step6.
- t22 1M: hom port 872645 -> 0, prio port 188229 -> 0; reg22 all 0, Mac outputs = v21's. dylibs/v22.dylib(.map); package
  /var/www/media/dow2_archive/mac/crossplay_v22/ (details ../v22/STATUS.md). tools/report.py: median fix for "no differences".
- Replays v21 -> v22: Low CRC EQUAL TO THE END in all 8 (was 4901..8693). High (runs/v22e_high: tag v22e = result.json with
  first_diff = last frame, so diagnose_high compares the whole game): 6 H6237 squadenergyext currentenergyamount 4 ulp; lohmistral
  H5719 health 2 ulp (unchanged); calderis 05-29 H11599 health 2 ulp; 48787 H7342 health 2 ulp; angelgate H8060 squad ability target
  position z 1 ulp; arena H7268 health 1 ulp; outskirts H7498 construction plan axis; refinery 06-01 H6928 autarch plasma grenade axis.
- Step 8 target: healthext health 1-2 ulp after area hits (4 of 8). Writer SetHealth <- ApplyDamage <- ApplyFireAssessment <-
  AreaEffectManager::OnImpact; damage 1 ulp. Read equal: distance sum, RangeUtil::GetBaseValue, AreaEffect::GetDamageInfo
  (0x101ed9490 / 0x9464b0), damage-action multipliers (0x101c904a0 / 0xa279e0), DistanceSquaredShifted (0x101936e60 / SE 0x1004ed60).
  Do a two-sided dynamic trace of the FireAssessment damage (Mac +0x30, ctor call 0x101edb850; Windows +0x24, helpers 0xa27720,
  0xa27dd0, 0xa279e0) at the H frame. Note: all_pairs.tsv pairs for HealthExt::SetHealth / GetWeaponDamageBag are WRONG (verify pairs).
  Then the other High classes above, then a mass Low run of the remaining 52 replays with v22.

## Step 8 DONE (09.11.2026) - mass Low run (56 vanilla), recipfix + holdfix 2nd copy -> v23; results_step8.md in the media difftest dir
- Mass Low run with v22 (runs/v22, v22all, reruns v22all_r2/_r3): 53 of 56 vanilla replays equal to the end. v23 (run on the 8 +
  the 3 failing): 54 of 56. Still failing: angelgate 2025-12-13 F1335 (H1130 control flow only: weapon-target FOW visibility
  query order, entity 16493/16492) and calderis_outskirts 2026-03-15 F18771 (H15163 eld_haywire_grenade entity y axis 1-2 ulp).
- Health class (4 of 8 High): new two-sided single-step tracer (macrun MR_STEP/MR_STEP_FN/MR_STEP_X, DiffTrace DT_STEP/_FN/_X,
  through tools/trace_run.py; compare tools/stcmp2.py, gmvcmp.py). The damage base WeaponBag damage_hp differs: PostLoadValidate
  Mac hp/d, courage/d vs DOW2.exe r=1/d, hp*r (0x101c876ed / 0x821f8b; weights a/s,b/s vs 1/s at 0x101c87916 / 0x82216e).
  recipfix.c (part 27): 1M port 410939 -> 0. lohmistral, calderis 05-29, arena, 48787 now High-equal for the WHOLE game.
- holdableext priority (outskirts 05-24 Low F9846, 48787 H7689): DOW2.exe 0xa58650 has 2 callers; the Mac inlines it twice -
  OnAddEntity (v22 holdfix) and OnModEvent 0x101cf1740 (missed). holdfix patches both now; t23/m23p 1M: port 188229 -> 0.
  outskirts 05-24 now equal to the end.
- reg23.sh all 0, Mac outputs = v22's. dylibs/v23.dylib (v23a.dylib = recipfix-only build); package
  /var/www/media/dow2_archive/mac/crossplay_v23/ (details ../v23/STATUS.md).
- HARNESS: Feral tamper timers (CFeralGame::RezLib_Callback_ShowHide arms "localhost kernel: internal ICX failure" exit(1) and a
  call to 0xcafebabe after 780 + rand%180 s) killed every macrun run > ~13-16 min (17 of 48 mass runs cut short, rc 1/139).
  macrun nops both AddTimer calls (tamper_timers_off; backups game.c.bak_step7, macrun.bak_step7). Check whether a real Mac hits
  them in long matches. difftest.py: DIFFTEST_TIMEOUT_MUL for loaded CPU. diagnose_high.py --reuse-win (link winref p1 of an
  older tag; backups *.bak_step7). DiffTrace.dll rebuilt (difftrace_win.c.bak_step7).
- High on the 8 with v23 (runs/v23h_high): none in lohmistral, calderis 05-29, 48787, arena; 6 H13896 (construction move target
  position z 1 ulp), angelgate H8060 (ability target position z 1 ulp), outskirts 05-30 H7498 (construction plan axis),
  refinery 06-01 H6928 (plasma grenade axis).
- Step 9 targets: (1) grenade/projectile orientation axis (refinery 06-01 High, outskirts 03-15 LOW F18771 - the only Low class
  with state); (2) angelgate 2025-12-13 F1335 weapon target candidate / FOW query order; (3) target position z 1 ulp (6, angelgate);
  (4) the recipfix class elsewhere: o23/mac_samediv_sites.txt (Mac sim functions dividing twice by one value, e.g.
  HealthExt::ApplyDamage 0x101b68172, DamageAction::PostLoadValidate) vs o23/win_recip_sites.txt (246 DOW2.exe 1/x sites);
  (5) a v23 mass Low run of all 56 (only 11 run with v23).

## Step 9 DONE (09.11.2026) - owner's live crash = Feral protection timer -> protfix; grenades -> artfix, cdownfix; v24; results_step9.md in the media difftest dir
- LIVE CRASH (v22, 02:12:31 -> 02:28:39 = 968 s): .ips thread 0 = exit() <- CFeralGame::RezLib_Callback_Zoomed <- NRunLoop timer,
  running static destructors while WinMain renders -> dca ActionUpdater allocator mutex destroyed -> NMutex::Lock(NULL) at 0x8.
  CFeralGame::Execute arms a 120 s repeating timer -> DoAMTask (0x100898450): if CDynProfUtils::CollectInfo() (obfuscated integrity
  check; says "modified" for the patched game) -> RezLib_Callback_ShowHide arms exit(1) at +780..959 s and call 0xcafebabe at
  +1080..1259 s => every patched game ends 15-18 min after start. protfix.c (part 28): DoAMTask entry -> same logic without ShowHide
  (log line when it fires), self-test through the patched entry, DOW2_NOPROTFIX=1. Proof in macrun with MR_KEEP_TAMPER=1 (new: macrun
  leaves the timers to the dylib): 35-41 min replays run to the end; DOW2_NOPROTFIX=1 dies at 1350 s "rip cafebabe" (runs/v24p_noprot).
- refinery 06-01 grenade axis: not artillery/Fire - OffMapArtilleryActionState::Update (Autarch ability) random start/target points:
  DOW2.exe x87 fcos/fsin (extended) * r with ONE rounding (PC_24) vs Mac cosf (float) * r. artfix.c (part 29, 4 spans -> x87 stubs
  under PC_24 CW). t24 1M: 52504 -> 0. refinery 06-01 now High-equal for the whole game.
- outskirts 03-15 haywire grenade: grenade stuck to an entity, StateProjectile::ProjectileCountdown world = local * parent; DOW2.exe
  0xa5c687 per-element sum orders. cdownfix.c (part 30). t24 1M: 876603 -> 0. (Found by macrun MR_WATCH=15163:32286:c8:e.)
- recipfix site 3: DamageAction::PostLoadValidate a/s,b/s -> r=1/s (DOW2.exe 0x94a38b); 1M 211244 -> 0. HealthExt::ApplyDamage
  (min(h/m,1)-d/m, only a comparison) left: DOW2.exe counterpart not found.
- Tools: macrun MR_STEP_NOENT=1 (step tracer crashed reading rcx as entity in DoStep), MR_KEEP_TAMPER=1; t24/ (w24 trampolines that run
  DOW2.exe's own byte ranges incl. x87 under PC_24; m24 runs the Mac spans in place). Backups game.c/macrun .bak_step8.
- NOTE t13/rcase.h rc_r() is 24-bit (>>8): use rc_f() for [-1,1) - my first art cases covered only tiny angles (fixed).
- v24 = v23 + protfix + artfix + cdownfix + recipfix site 3. reg24.sh all as v23 + t24 dact/art/cdown 0; dylibs/v24.dylib
  (v24b.dylib = build without cdownfix, used for runs/v24all). Package /var/www/media/dow2_archive/mac/crossplay_v24/.
- v24f mass Low run (runs/v24f, all 56): 54 of 56 equal to the end, all ran to the last frame. Failing: angelgate 2025-12-13
  F1335, calderis_outskirts 2026-03-15 F18771 (still, after cdownfix site 2). Package crossplay_v24 built from v24f
  (pkg24.sh, 09.11 08:26). Report: media difftest/results_step9.md (sent to the owner).
- Step 10 targets: (1) outskirts 03-15 - rerun High with v24f, find the remaining grenade writer; (2) angelgate 12-13 F1335
  FOW query order; (3) 1-ulp target position z (6, angelgate 05-29), construction plan axis (outskirts 05-30).

## Next steps (small, one at a time)
   Step-2 first target: replay 6, frame 496, entity 31465 ig_sentinel position y (last-bit, H-1 equal) — watch its write in frame 496.
2. Make watch_writer work (no perturbation: hardware watchpoint via gdb on the Mac-sim process, or a write-trap on the page) →
   the Mac function + callers that write the first differing value → map to DOW2.exe address (pairs in ../v12/audit/all_pairs.tsv).
3. Run with v17.dylib on ~10 replays → ranked list of sites to fix → v18 (same method as v14–v17: lifted C / reordered sums, self-test,
   1M side-by-side, regressions).
Rules from the owner: Mac work continues autonomously; keep agent contexts small (one step per agent, HANDOFF updates); no live tests
with the owner until replays pass clean.

## Step 10 (target 1) - IN PROGRESS (09.11.2026): outskirts 03-15 Low F18771 is now a RightOfWay evacuate decision (not the grenade)
- High with v24f (diagnose_high.py --tag v24f --reuse-win; Windows p1 linked to winref/v22all_r2_high p1; driver log
  runs/v24f_high_driver.log, result runs/v24f_high/<rid>/high.json): first High difference moved H15163 -> **H18746** (Low F18771),
  H-1 dumps equal, ONLY ONE entity differs (179 lines): 32465 sm_terminator_assault (owner 1003, squad 50072). Windows: stands at
  exactly (-35.5, 39.5), state 2, idle. Mac: got entity command id 14 (Evacuate, sender 1003) -> StateEvacuate (stateid 4) with
  target (-36.90052, 40.92777) = 0xc2139a23 / 0x4223b60a, moved to (-35.57003, 39.57139), pace 2. So a CONTROL decision: on the Mac
  RightOfWay asks this unit to step aside, on Windows not (or not in this frame). Entity block diffs: scratchpad only (re-extract).
- Mac step trace (tools/trace_run.py --tag s10_evac --nowin --span 1, MR_STEP_FN=101cd73c0:20:0:0:20 MR_STEP_NOENT=1; out
  runs/s10_evac/<rid>/step.log): RightOfWay::IssueEvacuateOrder (0x101cd73c0, prologue 20) called ONCE in f18746 (and once in
  f18747), ret 0x101cd87fc = RightOfWay::EvacuatePath (0x101cd8180, call site 0x101cd87f7). Chain on the Mac:
  StateMove::MoveToTargetState 0x101bcb3e0 (site 0x101bcb55e) or StateOutOfControl::Moving 0x101bcef30 -> RightOfWay::EvacuateFor
  0x101cd8fb0 -> (tail jmp) DoEvacuateForMovingUnit 0x101cd89a0 (CalculateLookAheadDist 0x101cd7070, GetLowPath, sqrtf x4,
  0x10203e5a0 x6) -> EvacuatePath 0x101cd8180 (CollectLowerPriorityEntitiesOnLine 0x101cd78b0 per path segment, floorf cell
  rounding at 0x101cd8630.., GetEntitiesForPivotPoint 0x101cd77d0, VerifyRightOfWayEntities 0x101cd7d70, sqrtf, GetRightOfWayPriority
  0x101e82240, GetEvacuationPosition 0x101cd7170 -> IssueEvacuateOrder). The mover is some other unit moving past 32465 (unknown yet).
- DOW2.exe counterparts NOT found yet: none of RightOfWay::* is in audit/all_pairs.tsv; win_cg.tsv is sparse there. Known nearby pairs:
  StateMove vtable 0x97e6d0..0x980000 (Windows functions 0x97f150 size 1443 and 0x97fb30 size 507 are MoveToTargetState candidates),
  GroundDynamicsUtils::CalcRightOfWayPriority 0x9dbe90 (Mac 0x101e821d0), GetGroundAttributes 0x973190 (Mac 0x101e81fa0; Windows
  callers 0x973e50 <- 0x974080, 0x974470).
- NEXT (one run each ~30 min Mac / ~5 min Windows):
  1. Mac: trace EvacuatePath itself to get the mover (rdi entity) and which list produced 32465: MR_STEP_FN=101cd8180:<prologue>:0:...
     (check the prologue length with dz.py: needs >= 14 bytes, whole instructions, no rip-relative), MR_STEP_X on the
     CollectLowerPriorityEntitiesOnLine / floorf region for float values; or trace CollectLowerPriorityEntitiesOnLine 0x101cd78b0.
  2. Find DOW2.exe RightOfWay::EvacuatePath / CollectLowerPriorityEntitiesOnLine (search the Windows image for the
     DoEvacuateForMovingUnit structure: calls through MoveToTargetState candidate 0x97f150, or scan for the cell rounding
     floor(x + off) + the 4 sqrt pattern), then DT_STEP on it (win_run.sh via trace_run.py --nomac --wenv DT_STEP={lo},{hi}
     --wenv DT_STEP_FN=<fn>,<plen>,<arg>,<off>,<len>) and compare with the Mac (stcmp2.py) -> the differing comparison
     (likely a point-to-segment distance / look-ahead length sum order or floorf cell index).
  3. Then fix as usual (new part in v25, self-test, DOW2_NO<NAME>FIX=1), t25 1M, reg25, dylibs/v25.dylib, Low rerun + the 8.
- Work dir /mnt/dow2bench/dev/native_run/macfix/v25 = plain copy of v24 (nothing changed yet; build.sh/pkg/reg still v24 names).

## Step 10b (09.11.2026, stopped at the context limit) - outskirts 03-15 cause found and fixed in v25 (rowfix, part 31); replay runs NOT done yet
- DOW2.exe RightOfWay counterparts FOUND (anchor: IssueEvacuateOrder = the only caller of StateEvacuate::OverrideEvacuationPoint
  0xa2b910, push 0xe + WorldDoCommandEntity; new tool v25/callscan.py ADDR... = raw E8 call sites in DOW2.exe, win_cg is sparse):
  IssueEvacuateOrder 0xa37cf0 | GetEvacuationPosition 0xa37820 (+ loop wrapper 0xa37e20 = Mac's inline loop in EvacuatePath) |
  GivePivotEvacuateOrders 0xa37f20 | GetEntitiesForPivotPoint 0xa38070 | CollectLowerPriorityEntitiesOnLine 0xa38140 |
  VerifyRightOfWayEntities 0xa383f0 | EvacuatePath 0xa38610 | DoEvacuateForMovingUnit 0xa38af0 | EvacuateFor 0xa39050.
- CAUSE (static, not yet confirmed on the replay): CollectLowerPriorityEntitiesOnLine walks the grid cells along a path segment
  (DDA; step x if |(bx-sx)/dirx| < |(by-sy)/diry|); its direction is Mac 0x101cd7a27 dirx = dx/len, diry = dy/len vs DOW2.exe
  0xa38233 r = 1/len, r*dx, r*dy (both SSE). t25 1M: 456910 differ. Path points on cell centres/corners -> ties -> another cell ->
  other units "on the line" -> Evacuate for 32465 on the Mac only. Rest of the loop (tie direction, boundary +-1, dirx==0 branch)
  matches. The mover unit and the deciding values were NOT traced (skipped: the arithmetic difference was found directly).
- FIX v25 = v24 + rowfix.c (part 31): span 0x101cd7a27..0x101cd7a39 (18 bytes) -> stub r=1/len (xmm0), xmm3=dx*r, xmm2=r*dy, back
  0x101cd7a39; self-test 256 DOW2.exe cases (rowfix_st.inc from t25/w25 rowst) through the patched code (entry 0x101cd79cb, fake
  rbp frame, temporary landing on 0x101cd7a39); DOW2_NOROWFIX=1 off. build.sh updated (v25 names + rowfix.c), dylsyms.txt
  regenerated, crossplay_fix.dylib built (v24 copy kept as crossplay_fix.dylib.v24) -> difftest/dylibs/v25.dylib (+ .map made
  by difftest.py). Log line "rowfix: OK ... self-test 256" seen in a macrun replay.
- t25 (v25/t25: c25.h, w25.c runs DOW2.exe bytes 0xa381d6..0xa38220 + 0xa38233..0xa38274, m25.c runs the Mac span in place):
  1M port 456910 -> v25 0 (outputs v25/o25/w25_row.txt, m25_row_orig.txt, m25_row_dyl.txt).
- reg25.sh written (= reg24 with v24/o24 as the "previous Mac output" and Windows t24 refs, + t25). Started, then KILLED at the
  coordinator's stop (only t13 run14 done: all 0 differ). pkg25.sh not made yet (copy pkg24.sh).
- Low run v25 (9 replays) started and KILLED after ~1 min (runs/v25/ partial - delete or overwrite).
- Other Windows notes for later: EvacuatePath DOW2.exe uses cvttss2si at 0xa388ad/0xa388e6 for the pivot cell where the Mac does
  floorf (0x101cd863a/0x101cd8651) then cvttss2si - same only for non-negative offset coords (check if a replay still differs);
  divss sites 0xa389b7 (EvacuatePath), 0xa38e80 (DoEvacuateForMovingUnit), 0xa37ab8 (GetEvacuationPosition) vs Mac reciprocal
  forms 0x101cd8742 / 0x101cd8e43 / 0x101cd72db - not compared yet (Mac multiplies by 1/x there; Windows divides? check order).
- NEXT (exact commands):
  1. cd /mnt/dow2bench/dev/native_run/macfix/v25 && nohup ./reg25.sh > o25/reg25.log 2>&1 &     (expect all 0 + row 456910 -> 0)
  2. cd ../difftest && R=/var/www/media/dow2_archive/mac/difftest/replays_cloud; nohup python3 tools/difftest.py --dylib
     dylibs/v25.dylib --tag v25 --jobs 9 $R/6p_calderis_outskirts.2026-03-15.00-22-29.rec $R/48787.rec $R/6.rec $R/lohmistral.rec
     $R/6p_angelgate.2026-05-29.20-51-52.rec $R/6p_arena.2026-05-29.22-05-58.rec $R/6p_calderis_outskirts.2026-05-30.01-15-30.rec
     $R/6p_calderisrefinery.2026-05-29.21-24-35.rec $R/6p_calderisrefinery.2026-06-01.00-50-15.rec
     > /var/www/media/dow2_archive/mac/difftest/runs/v25_driver.log 2>&1 &      (~50 min; outskirts 03-15 was F18771 with v24f)
  3. If outskirts 03-15 still differs: High (diagnose_high.py --tag v25 --reuse-win, Windows p1 from winref/v22all_r2_high) and
     check the floorf/cvttss2si and divss notes above. Then pkg25.sh, results_step10.md, step 10 targets 2-3.
