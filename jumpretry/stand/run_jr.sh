#!/bin/bash
# run_jr.sh <secs> - stand 1: JumpRetry.dll in dow2-patches + the autarch UI scenario (run.sh); removes the DLL after
T=/mnt/dow2bench/dev/vanilla_scar_test; . /mnt/dow2bench/stand/env1.sh; P="$GAMEDIR/dow2-patches"
cp "$T/JumpRetry.dll" "$P/JumpRetry.dll"; printf '[JumpRetry]\r\nEnabled=1\r\nVerbose=1\r\n' > "$P/JumpRetry.ini"; rm -f "$P/JumpRetry.log"
cleanup() { cp "$P/JumpRetry.log" "$T/jumpretry_run.log" 2>/dev/null; rm -f "$P/JumpRetry.dll" "$P/JumpRetry.ini" "$P/JumpRetry.log"; echo "JumpRetry removed"; }
trap cleanup EXIT
bash "$T/run.sh" autarch_ui.scar 0 "${1:-900}"
