#!/bin/bash
# jr_click.sh <ability button x> <y> [move] - one jump order through the HUD on stand 1: the ability button, then the
# screen centre (the camera looks at the target); "move": a right-click move 1.2 s later (the player's new order)
export DISPLAY=:99
BX=$1; BY=$2
xdotool mousemove $BX $BY; sleep 0.15; xdotool click 1; sleep 0.3
xdotool mousemove 640 400; sleep 0.15; xdotool click 1
if [ "${3:-}" = move ]; then sleep 1.2; xdotool mousemove 520 470; sleep 0.1; xdotool click 3; fi
if [ "${3:-}" = stop ]; then sleep 1.2; xdotool key s; fi
