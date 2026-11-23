#!/usr/bin/env bash
# .github/scripts/android-device-tests.sh
# Starts an Android emulator, installs the debug APK and runs the four on-device checks:
#   smoke (draw + move + HUD buttons), UI flow (real dialogs), reconnect (session lost -> resumed),
#   combat (lock a target and land hits).
#
# Why a script instead of an action: the workflow's job log is not always downloadable, so every
# interesting failure is echoed as a GitHub annotation (::error title=...::message), and each stage is
# timed so a machine-level failure (no KVM, image download) can be told apart from an app failure.
#
# Usage: .github/scripts/android-device-tests.sh <apk> <workspace-dir>
set -uo pipefail

APK_PATH="${1:?apk path}"
WS="${2:?workspace dir}"
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/usr/local/lib/android/sdk}}"
SDKMANAGER="$SDK/cmdline-tools/latest/bin/sdkmanager"
[ -x "$SDKMANAGER" ] || SDKMANAGER="$(ls -d "$SDK"/cmdline-tools/*/bin/sdkmanager 2>/dev/null | head -1)"
AVDMANAGER="$(dirname "$SDKMANAGER")/avdmanager"
IMAGE="system-images;android-30;google_apis;x86_64"
AVD="ci30"
LOG="$WS/device-setup.log"
: > "$LOG"

note() { echo "::notice title=$1::$2"; echo "[$1] $2" >> "$LOG"; }
fail() { echo "::error title=$1::$2"; echo "[$1] $2" >> "$LOG"; exit 1; }
step() { echo "::group::$1"; echo "=== $1" >> "$LOG"; }
endstep() { echo "::endgroup::"; }

step "install emulator packages"
yes | "$SDKMANAGER" --licenses >/dev/null 2>&1 || true
"$SDKMANAGER" --install "emulator" "platform-tools" "$IMAGE" >> "$LOG" 2>&1 \
    || fail sdk "sdkmanager could not install emulator/platform-tools/$IMAGE"
export PATH="$SDK/platform-tools:$SDK/emulator:$PATH"
command -v adb >/dev/null 2>&1 || fail sdk "adb is still not on PATH ($PATH)"
adb version >> "$LOG" 2>&1 || fail sdk "adb does not run"
note sdk "emulator + $IMAGE installed"
endstep

step "create the AVD"
echo no | "$AVDMANAGER" create avd -n "$AVD" -k "$IMAGE" --device pixel_5 --force >> "$LOG" 2>&1 \
    || fail avd "avdmanager could not create $AVD"
note avd "created $AVD"
endstep

step "boot the emulator"
[ -e /dev/kvm ] && sudo chmod 666 /dev/kvm 2>/dev/null || true
"$SDK/emulator/emulator" -avd "$AVD" -no-window -gpu swiftshader_indirect -no-audio -no-boot-anim \
    -no-snapshot -accel auto -netdelay none -netspeed full >> "$LOG" 2>&1 &
EMU_PID=$!
adb start-server >> "$LOG" 2>&1
booted=0
for _ in $(seq 1 90); do
    if [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = "1" ]; then booted=1; break; fi
    kill -0 "$EMU_PID" 2>/dev/null || break
    sleep 3
done
if [ "$booted" != "1" ]; then
    # Last 40 lines, newest last, so the tail of the emulator output reaches the annotations.
    mapfile -t LINES < <(tail -40 "$LOG")
    for line in "${LINES[@]}"; do echo "::error title=emulator-boot::${line:0:300}"; done
    fail emulator-boot "the emulator did not finish booting (see the annotations above)"
fi
note emulator-boot "booted"
adb shell input keyevent 82 >> "$LOG" 2>&1 || true
adb shell settings put global window_animation_scale 0 >/dev/null 2>&1 || true
endstep

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
bash "$(dirname "$0")/android-taps.sh" >> "$LOG" 2>&1 || note smoke "android-taps.sh reported a miss"
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

step "stop the emulator"
adb emu kill >/dev/null 2>&1 || kill "$EMU_PID" 2>/dev/null || true
note done "device tests finished (checks run in the next workflow steps)"
endstep
exit 0
