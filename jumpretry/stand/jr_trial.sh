#!/bin/bash
# jr_trial.sh <n> <bx> <by> [move|stop] - wait for READY n, then one HUD jump order (jr_click.sh)
. /mnt/dow2bench/stand/env1.sh; F="$GAMEDIR/Elite_DLLs/EliteAI.log"
grep -q jumpretry /mnt/dow2bench/dev/STAND_LOCK.owner || { echo "lock not mine"; exit 1; }
timeout 60 bash -c "until grep -q 'READY $1 ' '$F'; do sleep 0.3; done" || { echo "no READY $1"; exit 1; }
sleep 0.8; bash /mnt/dow2bench/dev/vanilla_scar_test/jr_click.sh $2 $3 ${4:-}
