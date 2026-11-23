#!/usr/bin/env bash
# .github/scripts/android-taps.sh
# Taps the run/walk toggle and the bag button during the smoke test.
#
# The emulator's input coordinates do not equal the coordinates the app receives (the surface is
# letterboxed/rotated), so we measure the offset with one probe tap: send a tap at a known device
# point, read the "touch down X,Y" line the app logged for it, and use (probe - received) for every
# following tap. The HUD logs its own button positions ("hud bag=X,Y run=X,Y").
#
# Never fails the job: the check step decides pass or fail from the log. Prints one annotation line so
# the mapping is visible even when the job log is unavailable.
ROOT="${ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
note() { echo "::notice title=android-taps::$*"; }

probe_x=600
probe_y=300
adb shell input tap "$probe_x" "$probe_y"
sleep 1

down=$(adb logcat -d | grep 'touch down' | tail -1 | grep -o 'down [0-9.]*,[0-9.]*' | sed 's/down //; s/,/ /')
if [ -z "$down" ]; then
    note "no touch-down logged; skipping taps"
    exit 0
fi
set -- $down
rx=${1%.*}
ry=${2%.*}
ox=$(( probe_x - rx ))
oy=$(( probe_y - ry ))

pos=$(adb logcat -d | grep 'hud bag=' | tail -1 | grep -o 'bag=[0-9.]*,[0-9.]* run=[0-9.]*,[0-9.]*')
if [ -z "$pos" ]; then
    note "no hud position logged; skipping taps"
    exit 0
fi
set -- $(echo "$pos" | sed 's/[a-z]*=//g; s/,/ /g')
bag_x=${1%.*}; bag_y=${2%.*}
run_x=${3%.*}; run_y=${4%.*}

map_x() { echo $(( $1 + ox )); }
map_y() { echo $(( $1 + oy )); }
note "probe=($probe_x,$probe_y) received=($rx,$ry) offset=($ox,$oy) run=($run_x,$run_y)->($(map_x "$run_x"),$(map_y "$run_y")) bag=($bag_x,$bag_y)->($(map_x "$bag_x"),$(map_y "$bag_y"))"

adb shell input tap "$(map_x "$run_x")" "$(map_y "$run_y")"    # run / walk toggle
sleep 2
adb shell input tap "$(map_x "$bag_x")" "$(map_y "$bag_y")"    # bag window
sleep 2
exit 0
