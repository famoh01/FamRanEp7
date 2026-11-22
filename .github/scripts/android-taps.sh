#!/usr/bin/env bash
# Taps the run toggle and the bag button in the Android emulator (smoke test).
# The emulator window can be offset from the input coordinates (e.g. a 2204-px
# window on a 2340-px display). The app logs where its first touch-down landed,
# and we swiped from (231, 849), so the difference is the offset. The HUD
# positions the app logged ("hud bag=X,Y run=X,Y") are then mapped back.
# Never fails the job: the check step decides pass or fail from the log.
cd "$(dirname "$0")/../.." || exit 0

down=$(adb logcat -d | grep 'touch down' | head -1 | grep -o 'down [0-9.]*,[0-9.]*' | sed 's/down //; s/,/ /')
if [ -z "$down" ]; then
    echo "taps: no touch-down logged; skipping taps"
    exit 0
fi
set -- $down
ox=$(( 231 - ${1%.*} ))
oy=$(( 849 - ${2%.*} ))

pos=$(adb logcat -d | grep 'hud bag=' | tail -1 | grep -o 'bag=[0-9.]*,[0-9.]* run=[0-9.]*,[0-9.]*')
if [ -z "$pos" ]; then
    echo "taps: no hud position logged; skipping taps"
    exit 0
fi
set -- $(echo "$pos" | sed 's/[a-z]*=//g; s/,/ /g')
echo "taps: input offset=$ox,$oy bag=$1,$2 run=$3,$4"

adb shell input tap $(( ${3%.*} + ox )) $(( ${4%.*} + oy ))
sleep 2
adb shell input tap $(( ${1%.*} + ox )) $(( ${2%.*} + oy ))
sleep 2
exit 0
