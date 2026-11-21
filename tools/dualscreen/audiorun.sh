#!/bin/bash
# One audio measurement: config overrides as JSON, fresh launch, load File 1, record 40 s of DSAudio summaries.
# Usage: audiorun.sh <label> '<json of CVars to merge>'
T=~/android-dev/tools/ds.sh
export ANDROID_SERIAL=${ANDROID_SERIAL:-$(adb devices | awk '$2=="device"{print $1; exit}')}
OUT=/tmp/claude-1000/-home-deck-Projects/9d82db8f-49d9-475a-8428-229fad8d827f/scratchpad
BASE=$HOME/Backups/2S2H_saves/1120_before_audio_tests/config.json
adb shell am force-stop com.twoshipfork.mm.dualscreen
python3 -I -c "
import json,sys
def merge(a,b):
    for k,v in b.items():
        if isinstance(v,dict):
            if not isinstance(a.get(k),dict): a[k]={}
            merge(a[k],v)
        else: a[k]=v
c=json.load(open(sys.argv[1])); merge(c['CVars'],json.loads(sys.argv[2]))
json.dump(c,open(sys.argv[3],'w'),indent=4)" "$BASE" "$2" "$OUT/cfg_run.json"
adb push "$OUT/cfg_run.json" /sdcard/2S2H/2ship2harkinian.json >/dev/null
adb shell monkey -p com.twoshipfork.mm.dualscreen -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 14; $T btn a; sleep 2; $T btn a; sleep 2; $T btn a; sleep 15
adb logcat -c; sleep 40
adb logcat -d -s DSAudio > "$OUT/run_$1.log"
echo "== $1: $(grep -c DRY "$OUT/run_$1.log") dry, $(grep -c SLOW "$OUT/run_$1.log") slow frames"
grep "5s:" "$OUT/run_$1.log" | sed 's/.*5s: //; s/ synth avg=[^ ]* max=[^ ]*//'
