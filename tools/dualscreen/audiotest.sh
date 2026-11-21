#!/bin/bash
# Audio measurement run: load File 1, 30 s idle, 30 s of tab switches, then dump the DSAudio log.
# Usage: audiotest.sh <label> [noload]
T=~/android-dev/tools/ds.sh
export ANDROID_SERIAL=${ANDROID_SERIAL:-$(adb devices | awk '$2=="device"{print $1; exit}')}
OUT=${OUT:-${TMPDIR:-/tmp}/mmds}
mkdir -p "$OUT"
if [ "$2" != noload ]; then
    $T btn a; sleep 2; $T btn a; sleep 2; $T btn a; sleep 15
fi
adb logcat -c
echo "idle 30s"; sleep 30
adb logcat -d -s DSAudio > "$OUT/audio_$1_idle.log"
adb logcat -c
echo "tabs 30s"
for i in $(seq 1 15); do
    for x in 180 236 292 348 404 460; do $T tap $x 520; sleep 0.33; done
done
adb logcat -d -s DSAudio > "$OUT/audio_$1_tabs.log"
for f in idle tabs; do
    echo "== $1 $f"; grep -c DRY "$OUT/audio_$1_$f.log" | sed 's/^/dry lines: /'
    grep "5s:" "$OUT/audio_$1_$f.log" | sed 's/.*DSAudio *: //'
done
