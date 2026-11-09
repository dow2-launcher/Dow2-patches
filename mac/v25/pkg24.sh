#!/bin/bash
# pkg24.sh - v24 package: zip (dow2mac/ dylib + run scripts + README_RU), src tgz, STATUS/README to the media archive.
# (no copy to the rts stage folder here: publishing is the owner's step)
set -e
cd "$(dirname "$0")"
OUT=/var/www/media/dow2_archive/mac/crossplay_v24; mkdir -p $OUT
cp crossplay_fix.dylib README_RU.txt pkg/dow2mac/
rm -f dow2mac_crossplay_v24.zip; python3 mkzip.py dow2mac_crossplay_v24.zip pkg/dow2mac
cp dow2mac_crossplay_v24.zip README_RU.txt STATUS.md $OUT/
tar czf $OUT/crossplay_v24_src.tgz --exclude='*.txt' --exclude='*.bin' --exclude='*.img' --exclude='*.dll' --exclude='*.pkl' \
  --exclude='__pycache__' --exclude='pkg' --exclude='*.dylib' --exclude='*.zip' --exclude='*.o' \
  $(ls *.c *.h *.inc *.py *.sh 2>/dev/null) lift/*.py lift/*.h lift/*.c lift/*.sh t13/*.c t13/*.h t13/*.py t13/*.sh t14/*.c t14/*.h t14/*.py t15/*.c t15/*.h t15/*.py t16/*.c t16/*.h t17/*.c t17/*.h t18/*.c t18/*.h t19/*.c t19/*.h t20/*.c t20/*.h t20/*.sh t21/*.c t21/*.h t22/*.c t22/*.h t22/*.py t23/*.c t23/*.h t24/*.c t24/*.h sortaudit/*.py sortaudit/*.sh sortaudit/*.txt test/*.c test/*.h test/*.py test/*.sh test/shim audit/*.py
cp x87scan_sim.txt $OUT/x87scan_sim.txt
cp ../v12/dow2win_crctrace_v12.zip $OUT/ 2>/dev/null || cp /var/www/media/dow2_archive/mac/crossplay_v12/dow2win_crctrace_v12.zip $OUT/
sha256sum $OUT/dow2mac_crossplay_v24.zip crossplay_fix.dylib
ls -la $OUT
