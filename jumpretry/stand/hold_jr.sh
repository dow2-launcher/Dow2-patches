#!/bin/bash
# hold_jr.sh - inside stand_lock: JumpRetry sessions one after another (touch stop = next session with the current DLL,
# touch jr_done = finish), so the lock isn't given away between sessions
T=/mnt/dow2bench/dev/vanilla_scar_test; rm -f $T/jr_done
for i in 1 2 3 4 5 6; do rm -f $T/stop; bash $T/run_jr.sh 900; [ -f $T/jr_done ] && break; done
rm -f $T/stop $T/jr_done
