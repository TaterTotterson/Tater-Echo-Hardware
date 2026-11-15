#!/bin/sh
# Owner-approved temporary hardware40/software0 comparison. No installation.
set -eu
trial_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "$trial_dir" in /tmp/tater-biscuit-hw40.*) ;; *) echo 'refusing non-temporary location'; exit 1 ;; esac
[ "$(hostname)" = tater-biscuit ] && [ "$(uname -r)" = 3.18.19-bt ] || exit 1
[ "${1:-}" = --owner-approved-hardware40-trial ] || exit 2
[ ! -e /data/tater-linux/app/pending.env ] || exit 1
[ ! -e /run/tater-biscuit-gain-trial.lock ] || exit 1
[ "$(tr '\000' ' ' </proc/459/cmdline)" = '/bin/sh /usr/local/sbin/tater-run ' ] || exit 1
[ "$(readlink /proc/1433/exe)" = /data/tater-linux/app/slots/a/tater-echo ] || exit 1
[ "$(sha256sum /proc/1433/exe | awk '{print $1}')" = 35ac4ce58eb7e6d6ba5de2b16438cf2305e7bb6ecec0224fc1f0eb3aa8b23bd0 ] || exit 1
[ "$(sed -n 's/^owner_pid *: *//p' /proc/asound/card0/pcm24c/sub0/status)" = 1448 ] || exit 1
[ -x "$trial_dir/tater-echo-hw40-trial" ] && [ -x "$trial_dir/biscuit_gain_trial" ] || exit 1
umask 077
app_pid=
gain_pid=
supervisor_paused=0
cleanup_done=0

cleanup() {
    [ "$cleanup_done" -eq 0 ] || return
    cleanup_done=1
    trap '' HUP INT TERM
    if [ -n "$app_pid" ]; then
        kill -TERM "$app_pid" 2>/dev/null || true
        wait "$app_pid" 2>/dev/null || true
    fi
    if [ -n "$gain_pid" ]; then
        kill -TERM "$gain_pid" 2>/dev/null || true
        wait "$gain_pid" 2>/dev/null || true
    fi
    # A gain helper that never reached "trial" changed nothing unless it
    # produced a restore result. An existing trial lock always blocks restart.
    safe=0
    if [ ! -e /run/tater-biscuit-gain-trial.lock ]; then
        if [ -z "$gain_pid" ] || grep -Fq '"phase":"restored","verified":true' "$trial_dir/gain.log"; then
            safe=1
        fi
    fi
    # Do not resume with a second recorder holding the PCM after its parent exits.
    owner=$(sed -n 's/^owner_pid *: *//p' /proc/asound/card0/pcm24c/sub0/status)
    if [ -n "$owner" ] && [ -n "$app_pid" ]; then
        case "$owner" in *[!0-9]*) safe=0 ;;
        *)
            if [ "$(cat "/proc/$owner/comm" 2>/dev/null)" = arecord ]; then
                kill -TERM "$owner" 2>/dev/null || true
                sleep 1
            else safe=0; fi
            ;;
        esac
    fi
    if [ "$supervisor_paused" -eq 1 ] && [ "$safe" -eq 1 ]; then
        kill -CONT 459
        echo 'ORIGINAL_VOICE_SUPERVISOR_RESUMED'
    elif [ "$supervisor_paused" -eq 1 ]; then
        echo 'RESTORE_NOT_VERIFIED_ORIGINAL_SUPERVISOR_LEFT_PAUSED'
    fi
    if [ "$(tr '\000' ' ' </proc/461/cmdline 2>/dev/null)" = '/bin/sh /usr/local/sbin/tater-usb-ethernet ' ]; then
        kill -CONT 461
        echo 'ORIGINAL_ETHERNET_SUPERVISOR_RESUMED'
    fi
}
trap cleanup EXIT
trap 'exit 0' HUP INT TERM

kill -STOP 459
supervisor_paused=1
kill -TERM 1433
count=0
while [ -e /proc/1433 ] && [ "$count" -lt 8 ]; do sleep 1; count=$((count+1)); done
# A reaped zombie is safe; a still-running original daemon is not.
if [ -r /proc/1433/status ] && ! grep -q '^State:.*Z' /proc/1433/status; then
    echo 'ORIGINAL_DAEMON_DID_NOT_STOP'; exit 1
fi
if [ -e /proc/1448/comm ] && [ "$(cat /proc/1448/comm)" = arecord ]; then
    kill -TERM 1448
    sleep 1
fi

export TATER_NATIVE_CONFIG=/data/local/etc/tater/native.json
export TATER_MWW_DIR=/data/local/share/tater/microwakeword
export MIC_GAIN_DB=0
export TATER_BISCUIT_HW_GAIN_TRIAL=40
"$trial_dir/tater-echo-hw40-trial" >"$trial_dir/daemon.log" 2>&1 </dev/null &
app_pid=$!
printf 'TRIAL_DAEMON_PID=%s\n' "$app_pid"
count=0
while ! grep -Fq '[gain-trial] hardware PGA=40dB software=0dB extraction=1.00 threshold_calibration=100.00' "$trial_dir/daemon.log"; do
    kill -0 "$app_pid" 2>/dev/null || { echo 'TRIAL_DAEMON_EXITED'; exit 1; }
    count=$((count+1)); [ "$count" -lt 20 ] || { echo 'TRIAL_DAEMON_NOT_READY'; exit 1; }
    sleep 1
done

TATER_GAIN_TRIAL_DAEMON_PID=$app_pid "$trial_dir/biscuit_gain_trial" --owner-approved-hardware40-trial >"$trial_dir/gain.log" 2>&1 </dev/null &
gain_pid=$!
printf 'GAIN_HELPER_PID=%s\n' "$gain_pid"
sleep 1
if ! grep -Fq '"phase":"trial","analog_delta_db":40,"digital_delta_db":0' "$trial_dir/gain.log"; then
    echo 'HARDWARE_GAIN_DID_NOT_ARM'; exit 1
fi
echo 'HARDWARE40_SOFTWARE0_TRIAL_ACTIVE'
while kill -0 "$app_pid" 2>/dev/null && kill -0 "$gain_pid" 2>/dev/null; do
    [ ! -e "$trial_dir/stop" ] || break
    sleep 1
done
