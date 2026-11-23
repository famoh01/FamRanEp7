#!/usr/bin/env bash
# .github/scripts/android-device-tests.sh
# Runs on the emulator started by the workflow, installs the debug APK and runs the four checks:
#   smoke (draw + move + HUD buttons), UI flow (real dialogs), reconnect (session lost -> resumed),
#   combat (lock a target and land hits).
#
# Why a script: the workflow's job log is not always downloadable, so every interesting failure is
# echoed as a GitHub annotation (::error title=...::message). Each stage is a ::group and timed, so a
# machine-level failure is distinguishable from an app failure.
#
# Usage: .github/scripts/android-device-tests.sh <apk> <workspace-dir>
set -uo pipefail

APK_PATH="${1:?apk path}"
WS="${2:?workspace dir}"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/usr/local/lib/android/sdk}}"
AVD="ci30"
LOG="$WS/device-setup.log"
: > "$LOG"

note() { echo "::notice title=$1::$2"; echo "[$1] $2" >> "$LOG"; }
fail() { echo "::error title=$1::$2"; echo "[$1] $2" >> "$LOG"; exit 1; }
step() { echo "::group::$1"; echo "=== $1" >> "$LOG"; }
endstep() { echo "::endgroup::"; }

# The workflow's emulator action starts the device; only the checks live here.
export PATH="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/usr/local/lib/android/sdk}}/platform-tools:$PATH"
command -v adb >/dev/null 2>&1 || fail sdk "adb is not on PATH"
adb wait-for-device >> "$LOG" 2>&1 || fail emulator "adb wait-for-device failed"
booted=0
for _ in $(seq 1 60); do
    if [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; then booted=1; break; fi
    sleep 2
done
[ "$booted" = "1" ] || fail emulator "the device never reported sys.boot_completed"
note emulator "device is up"
adb shell input keyevent 82 >> "$LOG" 2>&1 || true
adb shell settings put global window_animation_scale 0 >/dev/null 2>&1 || true

step "install the APK"
adb uninstall com.aiep7.app >/dev/null 2>&1 || true
adb install -r "$APK_PATH" >> "$LOG" 2>&1 || fail apk-install "adb install failed for $APK_PATH"
adb shell pm list packages 2>/dev/null | grep -q com.aiep7.app || fail apk-install "package missing after install"
note apk-install "installed $APK_PATH"
endstep

# ---------------------------------------------------------------- smoke test
step "smoke test"
adb logcat -c
adb shell am start -n com.aiep7.app/.MainActivity --es ci_user ci_smoke --es ci_pass CiSmokeTest2026 >> "$LOG" 2>&1 \
    || fail smoke "am start failed"
sleep 25
adb shell input swipe 231 849 340 849 600 >/dev/null 2>&1 || true    # joystick drag
sleep 3
ROOT="$(dirname "$0")" bash "$(dirname "$0")/android-taps.sh" >> "$LOG" 2>&1 \
    || note smoke "android-taps.sh reported a miss"
adb exec-out screencap -p > "$WS/smoke-screen.png" 2>>"$LOG" || true
adb logcat -d > "$WS/smoke-logcat.txt" 2>>"$LOG" || true
adb logcat -d -b crash > "$WS/smoke-crash.txt" 2>>"$LOG" || true
endstep

# ---------------------------------------------------------------- UI flow test
step "UI flow test"
adb shell am force-stop com.aiep7.app
adb logcat -c
adb shell am start -n com.aiep7.app/.MainActivity --ez ci_ui_test true >> "$LOG" 2>&1 \
    || fail ui-test "am start failed"
for _ in $(seq 1 120); do adb logcat -d 2>/dev/null | grep -q CI_TEST_SUMMARY && break; sleep 2; done
adb exec-out screencap -p > "$WS/ui-test-screen.png" 2>>"$LOG" || true
adb logcat -d > "$WS/ui-test-logcat.txt" 2>>"$LOG" || true
endstep

# ---------------------------------------------------------------- reconnect test
step "reconnect test"
adb shell am force-stop com.aiep7.app
adb logcat -c
adb shell am start -n com.aiep7.app/.MainActivity --es ci_user ci_smoke --es ci_pass CiSmokeTest2026 \
    --ez ci_reconnect true >> "$LOG" 2>&1 || fail reconnect "am start failed"
sleep 20
adb shell input keyevent 3 >/dev/null 2>&1 || true            # HOME -> onPause
sleep 3
adb shell am start -n com.aiep7.app/.MainActivity >> "$LOG" 2>&1 || true   # back -> onResume
sleep 8
adb exec-out screencap -p > "$WS/reconnect-screen.png" 2>>"$LOG" || true
adb logcat -d > "$WS/reconnect-logcat.txt" 2>>"$LOG" || true
adb logcat -d -b crash > "$WS/reconnect-crash.txt" 2>>"$LOG" || true
endstep

# ---------------------------------------------------------------- combat test
step "combat test"
adb shell am force-stop com.aiep7.app
adb logcat -c
adb shell am start -n com.aiep7.app/.MainActivity --es ci_user ci_smoke --es ci_pass CiSmokeTest2026 \
    --ez ci_combat true >> "$LOG" 2>&1 || fail combat "am start failed"
sleep 8
adb shell input swipe 231 849 340 780 600 >/dev/null 2>&1 || true    # walk towards the dummy
sleep 14
adb exec-out screencap -p > "$WS/combat-screen.png" 2>>"$LOG" || true
adb logcat -d > "$WS/combat-logcat.txt" 2>>"$LOG" || true
adb logcat -d -b crash > "$WS/combat-crash.txt" 2>>"$LOG" || true
endstep

note done "device tests finished (checks run in the next workflow steps)"
exit 0
