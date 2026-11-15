#!/bin/sh
# SPDX-License-Identifier: MIT
# Inspect the already-running Checkers ALSA capture stream without opening a
# PCM device or reading microphone samples.

set -eu

usage() {
    echo "usage: $0 --target checkers --read-only-status" >&2
    exit 2
}

[ "$#" -eq 3 ] || usage
[ "$1" = "--target" ] || usage
[ "$2" = "checkers" ] || usage
[ "$3" = "--read-only-status" ] || usage

project=$(zcat /proc/config.gz | sed -n 's/^CONFIG_ARCH_MTK_PROJECT="\([^"]*\)"$/\1/p')
[ "$project" = "checkers" ] || {
    echo "refusing non-Checkers target: $project" >&2
    exit 1
}

pcm=/proc/asound/card0/pcm22c/sub0
[ -r "$pcm/status" ] || {
    echo "Checkers capture status is unavailable" >&2
    exit 1
}
[ -r "$pcm/hw_params" ] || {
    echo "Checkers capture parameters are unavailable" >&2
    exit 1
}

state=$(awk '/^state:/{print $2}' "$pcm/status")
owner=$(awk '/^owner_pid/{print $3}' "$pcm/status")
[ "$state" = "RUNNING" ] || {
    echo "capture is not running: $state" >&2
    exit 1
}
case "$owner" in
    ''|*[!0-9]*) echo "invalid capture owner" >&2; exit 1 ;;
esac

format=$(awk '/^format:/{print $2}' "$pcm/hw_params")
channels=$(awk '/^channels:/{print $2}' "$pcm/hw_params")
rate=$(awk '/^rate:/{print $2}' "$pcm/hw_params")
period=$(awk '/^period_size:/{print $2}' "$pcm/hw_params")
buffer=$(awk '/^buffer_size:/{print $2}' "$pcm/hw_params")
owner_name=$(cat "/proc/$owner/comm")
parent=$(awk '{print $4}' "/proc/$owner/stat")
parent_name=$(cat "/proc/$parent/comm")

stdout_link=$(readlink "/proc/$owner/fd/1" || true)
pipe_connected=false
for descriptor in "/proc/$parent"/fd/*; do
    [ "$(readlink "$descriptor" 2>/dev/null || true)" = "$stdout_link" ] && {
        pipe_connected=true
        break
    }
done

downmix=false
audio_node=/sys/firmware/devicetree/base/soc/spi@1100a000/spi@0
[ -e "$audio_node/amzn,mic-downmix" ] && downmix=true

hw_start=$(awk '/^hw_ptr/{print $3}' "$pcm/status")
owner_ticks_start=$(awk '{print $14+$15}' "/proc/$owner/stat")
parent_ticks_start=$(awk '{print $14+$15}' "/proc/$parent/stat")
sleep 2
hw_end=$(awk '/^hw_ptr/{print $3}' "$pcm/status")
owner_ticks_end=$(awk '{print $14+$15}' "/proc/$owner/stat")
parent_ticks_end=$(awk '{print $14+$15}' "/proc/$parent/stat")

printf '{'
printf '"schema_version":1,'
printf '"target":"checkers",'
printf '"pcm":"hw:0,22",'
printf '"state":"%s",' "$state"
printf '"format":"%s",' "$format"
printf '"channels":%s,' "$channels"
printf '"rate_hz":%s,' "$rate"
printf '"period_frames":%s,' "$period"
printf '"buffer_frames":%s,' "$buffer"
printf '"owner":{"comm":"%s","parent_comm":"%s","pipe_connected":%s},' \
    "$owner_name" "$parent_name" "$pipe_connected"
printf '"two_second_delta":{"hardware_frames":%s,"owner_cpu_ticks":%s,"parent_cpu_ticks":%s},' \
    "$((hw_end - hw_start))" \
    "$((owner_ticks_end - owner_ticks_start))" \
    "$((parent_ticks_end - parent_ticks_start))"
printf '"device_tree_mic_downmix":%s,' "$downmix"
printf '"pcm_opened":false,"samples_read":false,"audio_saved":false'
printf '}\n'
