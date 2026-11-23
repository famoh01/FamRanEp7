#!/usr/bin/env bash
# Taps the run toggle and the bag button in the Android emulator (smoke test).
# The emulator window can be offset from the input coordinates (e.g. a 2204-px
# window on a 2340-px display). The app logs where its first touch-down landed,
# and we swiped from (231, 849), so the difference is the offset. The HUD
# positions the app logged ("hud bag=X,Y run=X,Y") are then mapped back.
# Never fails the job: the check step decides pass or fail from the log.
# The smoke run's screenshot was 360x166 (portrait), so send the taps rotated (landscape portrait
# coordinates): ROOT=/tmp/device-tests.sh (staged by the workflow).
ROOT="${ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"

# Read the first touch this run (the script cleared logcat before starting the app).
down=$(adb logcat -d | grep 'touch down' | head -1 | grep -o 'down [0-9.]*,[0-9.]*' | sed 's/down //; s/,/ /')
if [ -z "$down" ]; then
    echo "taps: no touch-down logged; skipping taps"
    exit 0
fi
set -- $down
ox=$(( 231 - ${1%.*} ))
oy=$(( 849 - ${2%.*} ))

# The HUD re-logs its button positions on every resize; keep the last landscape one.
pos=$(adb logcat -d | grep 'hud bag=' | grep -v 'resized: 1080x2195' | tail -1 | grep -o 'bag=[0-9.]*,[0-9.]* run=[0-9.]*,[0-9.]*')
if [ -z "$pos" ]; then
    echo "taps: no hud position logged; skipping taps"
    exit 0
fi
set -- $(echo "$pos" | sed 's/[a-z]*=//g; s/,/ /g')
echo "taps: input offset=$ox,$oy bag=$1,$2 run=$3,$4"

# The device is in landscape while the screenshots/layout logs are quarter-turned: map a portrait
# (x, y) on a 1080x2204 screen to a landscape 2204x1080 screen -> (y, 2204 - x).
emit_tap() {
    local px=$1 py=$2
    local lx=$py ly=$(( 2204 - px ))
    adb shell input tap $(( lx + ox )) $(( ly + oy ))
}
emit_tap $(( ${3%.*} )) $(( ${4%.*} ))   # run / walk toggle
sleep 2
emit_tap $(( ${1%.*} )) $(( ${2%.*} ))   # bag
sleep 2
exit 0
