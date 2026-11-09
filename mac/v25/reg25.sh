#!/bin/bash
# reg25.sh - all regressions (t13 run14, test reg13, mtest fixed, heapfix_test, t14..t17) with the BUILT v25 dylib.
# Only the Mac side is rerun: the Windows reference outputs (DOW2.exe / SimEngine.dll code, unchanged) are v17's in ../v17/o17.
# Each Mac output is compared with Windows (the tool of that test) and byte-compared with v17's Mac output.
H=/mnt/dow2bench/dev/native_run/macfix/v25; V=/mnt/dow2bench/dev/native_run/macfix/v17/o17; O=$H/o25; P4=/mnt/dow2bench/dev/native_run/macfix/v24/o24; P=/mnt/dow2bench/dev/native_run/macfix/v23/o23; Q=/mnt/dow2bench/dev/native_run/macfix/v22/o22; PY=/mnt/dow2bench/dev/re_venv/bin/python
N=1000000; M="gcc -O1 -ffp-contract=off"
echo "== t13 run14 (tdyl13)"; mkdir -p $O/r14; cd $H/t13
$M -o tdyl13 tdyl13.c ../msmath.c -lm -ldl 2>&1 | grep error
./tdyl13 $N $O/r14 2> $O/r14/tdyl13.err; echo "tdyl13 rc=$?"; grep -v "^$" $O/r14/tdyl13.err | tail -12
for p in aim scat cs ray obb hit cat pj clp hold dmg; do echo -n "$p: "; $PY cmpf.py $V/r14/w$p.txt $O/r14/d$p.txt | head -1; cmp -s $P4/r14/d$p.txt $O/r14/d$p.txt || echo "   (differs from v24 Mac output)"; done
echo "== test reg13 (DYL13=1, all inits)"; mkdir -p $O/reg; cd $H/test
for t in mtest mcost mcalc mturn mline mproj mfly mhom mhead mmove mcol mmesh tdylcf; do gcc -O1 -o $t $t.c ../msmath.c -lm -ldl 2>&1 | grep error; done
run() { local n=$1; shift; ( "$@" > $O/reg/$n.txt 2>$O/reg/$n.err ) ; }
export DYL13=1 NATIVEMATH=1
run mtest ./mtest & run mcost ./mcost & run mcalc ./mcalc & run mturn ./mturn & run mturn3 ./mturn 3pt & run mturnc ./mturn crt & run mturns ./mturn smooth & wait
run mline ./mline & run mproj ./mproj & run mfly ./mfly & run mhom ./mhom & run mhead ./mhead & run mmove ./mmove & run mcol ./mcol & wait
run mfi ./tdylcf fi & run mtr ./tdylcf tr & run minv ./tdylcf inv & run mmesh ./mmesh & wait
run mtest_fixed ./mtest fixed
unset DYL13 NATIVEMATH
for p in test cost calc turn turn3 turnc turns line proj fly hom head move col fi tr inv mesh; do
  echo -n "$p: "; $PY ../t13/cmpf.py $V/reg/w$p.txt $O/reg/m$p.txt | head -1; cmp -s $P4/reg/m$p.txt $O/reg/m$p.txt || echo "   (differs from v24 Mac output)"
done
echo -n "mtest fixed vs wtest: "; $PY cmp.py $V/reg/wtest.txt $O/reg/mtest_fixed.txt 2>&1 | tail -1
echo "module log lines (mcost run):"; grep -c ": OK" $O/reg/mcost.err; grep -h "NOT\|differ" $O/reg/*.err | sort | uniq -c | head
echo "== heapfix_test"; cd $H; gcc -O1 -o $O/heapfix_test heapfix_test.c 2>&1 | grep error
$O/heapfix_test /mnt/dow2bench/dev/ghidra_proj_mac/dow2mac.bin crossplay_fix.dylib 0x$(awk '$2=="_heapfix_siftdown"{print $3}' dylsyms.txt) 0x$(awk '$2=="_heapfix_update"{print $3}' dylsyms.txt) 2>&1 | tail -3
echo "== t14"; cd $H/t14; $M -o m14 m14.c ../msmath.c -lm -ldl 2>&1 | grep error; mkdir -p $O/t14m
./m14 $N $O/t14m dyl 2> $O/t14m.log; python3 cmp14.py $V/t14w $O/t14m | tail -20
for f in $P4/t14m/*.txt; do cmp -s $f $O/t14m/$(basename $f) || echo "   t14 $(basename $f) differs from v24 Mac output"; done
echo "== t15"; cd $H/t15; $M -o m15 m15.c ../msmath.c -lm -ldl 2>&1 | grep error; mkdir -p $O/t15
./m15 $N hmi dyl > $O/t15/m_hmi.txt 2>$O/t15/m_hmi.err; ./m15 $N qt dyl > $O/t15/m_qt.txt 2>$O/t15/m_qt.err
python3 cmp15.py $V/t15/w_hmi.txt $O/t15/m_hmi.txt | head -1; cmp -s $V/t15/w_qt.txt $O/t15/m_qt.txt && echo "qt: 0 differ" || echo "qt: DIFFERS"
echo "== t16"; cd $H/t16; $M -o m16 m16.c ../msmath.c -lm -ldl 2>&1 | grep error; mkdir -p $O/t16
for p in ch pw sk; do MELEEFIX=1 ./m16 $N $p dyl > $O/t16/d_$p.txt 2>$O/t16/d_$p.err; cmp -s $V/t16/w_$p.txt $O/t16/d_$p.txt && echo "$p: 0 differ" || echo "$p: DIFFERS"; done
echo "== t17"; cd $H/t17; $M -o m17 m17.c ../msmath.c -lm -ldl 2>&1 | grep error
for p in pt kf nd lv; do KNOCKFIX=1 WALKFIX=1 ./m17 $N $p dyl > $O/d_$p.txt 2>$O/d_$p.err; cmp -s $V/w_$p.txt $O/d_$p.txt && echo "$p: 0 differ" || echo "$p: DIFFERS"; done
echo "== t18"; cd $H/t18; $M -o m18 m18.c ../msmath.c -lm -ldl 2>&1 | grep error; ./m18 $N gt dyl > $O/m18_gt_dyl.txt 2>$O/m18.err; cmp -s /mnt/dow2bench/dev/native_run/macfix/v18/o18/w18_gt.txt $O/m18_gt_dyl.txt && echo "gt: 0 differ" || echo "gt: DIFFERS"
echo "== t19"; cd $H/t19; ./m19 $N dyl > $O/m19_dyl.txt 2>$O/m19_dyl.err; head -1 $O/m19_dyl.err; cmp -s /mnt/dow2bench/dev/native_run/macfix/v19/o19/w19_gt.txt $O/m19_dyl.txt && echo "sort: 0 differ" || echo "sort: DIFFERS"
for f in d_pt d_kf d_nd d_lv m18_gt_dyl m19_dyl; do cmp -s $P4/$f.txt $O/$f.txt || echo "   $f differs from v24 Mac output"; done
echo "== t20"; cd $H/t20; ./run20.sh $N
echo "== t21 (costfix / fowcellfix, 1M each)"; cd $H/t21; for t in cost cell; do ./m21 $t $N dyl > $O/m21_${t}_dyl.txt 2>/dev/null; echo "$t: $(diff /mnt/dow2bench/dev/native_run/macfix/v21/o21/w21_$t.txt $O/m21_${t}_dyl.txt | grep -c "^>") of $N differ"; done
echo "== t22 (homingfix / holdfix, 1M each)"; cd $H/t22; gcc -O1 -ffp-contract=off -o m22 m22.c ../msmath.c -lm -ldl 2>&1 | grep error
for t in hom prio; do ./m22 $t $N dyl > $O/m22_${t}_dyl.txt 2>$O/m22_${t}_dyl.err; echo "$t: $(diff $Q/w22_$t.txt $O/m22_${t}_dyl.txt | grep -c "^>") of $N differ"; done
echo "== t23 (recipfix, 1M)"; cd $H/t23; gcc -O1 -ffp-contract=off -o m23 m23.c ../msmath.c -lm -ldl 2>&1 | grep error
./m23 bag $N dyl > $O/m23_bag_dyl.txt 2>$O/m23_bag_dyl.err; echo "bag: $(diff $P/w23_bag.txt $O/m23_bag_dyl.txt | grep -c "^>") of $N differ"
for t in hom prio; do cmp -s $P4/m22_${t}_dyl.txt $O/m22_${t}_dyl.txt || echo "   m22_$t differs from v24 Mac output"; done
echo "== t23 m23p (holdfix OnModEvent copy, 1M)"; cd $H/t23; gcc -O1 -ffp-contract=off -o m23p m23p.c ../msmath.c -lm -ldl 2>&1 | grep error
./m23p $N dyl > $O/m23p_dyl.txt 2>$O/m23p_dyl.err; echo "prio2: $(diff $Q/w22_prio.txt $O/m23p_dyl.txt | grep -c "^>") of $N differ"

cmp -s $P4/m23_bag_dyl.txt $O/m23_bag_dyl.txt || echo "   m23_bag differs from v24 Mac output"; cmp -s $P4/m23p_dyl.txt $O/m23p_dyl.txt || echo "   m23p differs from v24 Mac output"
echo "== t24 (recipfix site 3 DamageAction::PostLoadValidate, artfix scatter points, cdownfix stuck-projectile follow + attach, 1M each; protfix)"; cd $H/t24; gcc -O1 -ffp-contract=off -o m24 m24.c ../msmath.c -lm -ldl 2>&1 | grep error
gcc -m32 -O1 -no-pie -msse2 -mfpmath=sse -ffp-contract=off -o w24 w24.c ../msmath.c -lm 2>&1 | grep error; [ -s $P4/w24_dact.txt ] || ./w24 dact $N > $P4/w24_dact.txt
[ -s $P4/m24_dact_orig.txt ] || ./m24 dact $N orig > $P4/m24_dact_orig.txt
./m24 dact $N dyl > $O/m24_dact_dyl.txt 2>$O/m24_dact_dyl.err; echo "dact: port $(diff $P4/w24_dact.txt $P4/m24_dact_orig.txt | grep -c "^>") -> v25 $(diff $P4/w24_dact.txt $O/m24_dact_dyl.txt | grep -c "^>") of $N differ"
[ -s $P4/w24_art.txt ] || ./w24 art $N > $P4/w24_art.txt; [ -s $P4/m24_art_orig.txt ] || ./m24 art $N orig > $P4/m24_art_orig.txt
./m24 art $N dyl > $O/m24_art_dyl.txt 2>$O/m24_art_dyl.err; echo "art: port $(diff $P4/w24_art.txt $P4/m24_art_orig.txt | grep -c "^>") -> v25 $(diff $P4/w24_art.txt $O/m24_art_dyl.txt | grep -c "^>") of $N differ"
[ -s $P4/w24_cdown.txt ] || ./w24 cdown $N > $P4/w24_cdown.txt; [ -s $P4/m24_cdown_orig.txt ] || ./m24 cdown $N orig > $P4/m24_cdown_orig.txt
./m24 cdown $N dyl > $O/m24_cdown_dyl.txt 2>$O/m24_cdown_dyl.err; echo "cdown: port $(diff $P4/w24_cdown.txt $P4/m24_cdown_orig.txt | grep -c "^>") -> v25 $(diff $P4/w24_cdown.txt $O/m24_cdown_dyl.txt | grep -c "^>") of $N differ"
[ -s $P4/w24_attach.txt ] || ./w24 attach $N > $P4/w24_attach.txt; [ -s $P4/m24_attach_orig.txt ] || ./m24 attach $N orig > $P4/m24_attach_orig.txt
./m24 attach $N dyl > $O/m24_attach_dyl.txt 2>$O/m24_attach_dyl.err; echo "attach: port $(diff $P4/w24_attach.txt $P4/m24_attach_orig.txt | grep -c "^>") -> v25 $(diff $P4/w24_attach.txt $O/m24_attach_dyl.txt | grep -c "^>") of $N differ"
gcc -O1 -o mprot mprot.c ../msmath.c -lm -ldl 2>&1 | grep error; ./mprot > $O/mprot.txt 2>&1; cat $O/mprot.txt
for t in dact art cdown attach; do cmp -s $P4/m24_${t}_dyl.txt $O/m24_${t}_dyl.txt || echo "   m24_$t differs from v24 Mac output"; done
echo "== t25 (rowfix RightOfWay::CollectLowerPriorityEntitiesOnLine direction, 1M)"; cd $H/t25; gcc -O1 -ffp-contract=off -o m25 m25.c ../msmath.c -lm -ldl 2>&1 | grep error
gcc -m32 -O1 -no-pie -msse2 -mfpmath=sse -ffp-contract=off -o w25 w25.c ../msmath.c -lm 2>&1 | grep error; [ -s $O/w25_row.txt ] || ./w25 row $N > $O/w25_row.txt
[ -s $O/m25_row_orig.txt ] || ./m25 row $N orig > $O/m25_row_orig.txt
./m25 row $N dyl > $O/m25_row_dyl.txt 2>$O/m25_row_dyl.err; echo "row: port $(diff $O/w25_row.txt $O/m25_row_orig.txt | grep -c "^>") -> v25 $(diff $O/w25_row.txt $O/m25_row_dyl.txt | grep -c "^>") of $N differ"
echo DONE
