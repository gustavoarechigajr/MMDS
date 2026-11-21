#!/bin/bash
# Dual-screen dev loop, quiet by default. Usage:
#   ds.sh build                build; prints errors or the new APK path
#   ds.sh install [force]      install the latest APK + launch (refuses if the app is in front, unless "force")
#   ds.sh shot [bot|top] [k]   capture a screen, downscaled by k (default 2) -> $OUT/<screen>.png
#   ds.sh tap X Y              tap the bottom screen in canvas units (640 wide)
#   ds.sh swipe X1 Y1 X2 Y2    swipe on the bottom screen in canvas units
#   ds.sh log                  DualScreen log lines
PKG=com.twoshipfork.mm.dualscreen
OUT=${OUT:-/tmp/claude-1000/-home-deck-Projects/9d82db8f-49d9-475a-8428-229fad8d827f/scratchpad}
APK_DIR=$HOME/android-dev
TOP=4630946441858561667
BOT=4630946482288158084
S=1.9375 # 1240 px / 640 canvas units
mkdir -p "$OUT"
# Several mDNS entries can point at the same Thor; pin one so adb doesn't refuse
export ANDROID_SERIAL=${ANDROID_SERIAL:-$(adb devices | awk '$2=="device"{print $1; exit}')}

case "$1" in
build)
    log=$(bash "$APK_DIR/build.sh" 2>&1)
    if echo "$log" | grep -qE 'error:|FAILED|What went wrong'; then
        echo "$log" | grep -E 'error:|FAILED|What went wrong' | head -20
        exit 1
    fi
    n=$(ls "$APK_DIR"/2Ship-DualScreen-v*.apk 2>/dev/null | sed 's/.*-v\([0-9]*\)\.apk/\1/' | sort -n | tail -1)
    dst="$APK_DIR/2Ship-DualScreen-v$((n + 1)).apk"
    cp "$APK_DIR/2ship/Android/app/build/outputs/apk/release/app-release.apk" "$dst" && echo "$dst"
    ;;
install)
    if [ "$2" != force ] && adb shell dumpsys activity activities | grep -m1 topResumedActivity | grep -q $PKG; then
        echo "APP_IN_FRONT: ask the user before installing"
        exit 2
    fi
    apk=$(ls -v "$APK_DIR"/2Ship-DualScreen-v*.apk | tail -1)
    adb install -r "$apk" | tail -1 | grep -q Success || { echo "INSTALL_FAILED"; exit 1; }
    adb shell monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
    sleep 15
    echo "installed $(basename "$apk")"
    ;;
shot)
    id=$BOT; [ "$2" = top ] && id=$TOP
    adb exec-out screencap -p -d $id >"$OUT/raw.png"
    python3 -I "$APK_DIR/tools/shrink.py" "$OUT/raw.png" "$OUT/${2:-bot}.png" "${3:-2}" && echo "$OUT/${2:-bot}.png"
    ;;
tap)
    adb shell input -d 4 tap $(echo "$2*$S" | bc) $(echo "$3*$S" | bc)
    ;;
swipe)
    adb shell input -d 4 swipe $(echo "$2*$S;$3*$S;$4*$S;$5*$S" | bc | tr '\n' ' ') 150
    ;;
btn)
    # Press a button on the Thor's built-in controller (raw evdev on "Odin Controller"):
    # a=304 b=305 x=307 y=308 start=315, D-pad up/down/left/right=544-547; hold time in seconds as $3 (default 0.15)
    case "$2" in a) c=304 ;; b) c=305 ;; x) c=307 ;; y) c=308 ;; start) c=315 ;;
        up) c=544 ;; down) c=545 ;; left) c=546 ;; right) c=547 ;; *) c=$2 ;; esac
    adb shell "sendevent /dev/input/event9 1 $c 1; sendevent /dev/input/event9 0 0 0; sleep ${3:-0.15}; sendevent /dev/input/event9 1 $c 0; sendevent /dev/input/event9 0 0 0"
    ;;
log)
    adb logcat -d -s DualScreen | tail -20
    ;;
esac
