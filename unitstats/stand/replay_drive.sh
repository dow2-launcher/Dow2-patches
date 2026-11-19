#!/bin/bash
# drive.sh: runs the lines appended to $T/cmd (after the session started) one by one; "quit" ends the session.
T=/mnt/dow2bench/dev/usreplay_1119; . /mnt/dow2bench/stand/env1.sh; : > $T/cmd; n=0
shot() { import -window root "$O/$1.png"; echo "shot $1"; }
win() { xdotool search --name "Dawn of War II" | head -1; }
move() { xdotool mousemove --window $(win) $1 $2; echo "move $1 $2"; }
click() { xdotool mousemove --window $(win) $1 $2; sleep 0.25; xdotool mousedown ${3:-1}; sleep 0.12; xdotool mouseup ${3:-1}; echo "click $1 $2"; }
key() { xdotool key --window $(win) "$@"; echo "key $*"; }
echo "$(date +%T) drive ready"
while :; do
  m=$(wc -l < $T/cmd)
  while [ $n -lt $m ]; do n=$((n+1)); l=$(sed -n "${n}p" $T/cmd); [ "$l" = quit ] && { echo "$(date +%T) quit"; touch $T/stop1; exit 0; }
    echo "$(date +%T) > $l"; eval "$l" 2>&1; done
  sleep 0.5
done
