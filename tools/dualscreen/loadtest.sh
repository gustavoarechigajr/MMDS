#!/bin/bash
# Loads the slot-2 TEST save only: moves the cursor to File 2, checks on screen that File 2 is the highlighted row,
# and refuses to press A otherwise (the user's real save is File 1 and must never be loaded by the tools).
T=~/android-dev/tools/ds.sh
export ANDROID_SERIAL=${ANDROID_SERIAL:-$(adb devices | awk '$2=="device"{print $1; exit}')}
OUT=${OUT:-${TMPDIR:-/tmp}/mmds}
mkdir -p "$OUT"
adb shell "sendevent /dev/input/event9 3 1 32000; sendevent /dev/input/event9 0 0 0; sleep 0.08; sendevent /dev/input/event9 3 1 0; sendevent /dev/input/event9 0 0 0"
sleep 1
$T shot top 4 >/dev/null
sel=$(python3 -I - "$OUT/top.png" <<'PY'
import struct,zlib,sys
d=open(sys.argv[1],'rb').read();pos=8;idat=b''
while pos<len(d):
    l,k=struct.unpack('>I4s',d[pos:pos+8]);c=d[pos+8:pos+8+l]
    if k==b'IHDR': w,h=struct.unpack('>II',c[:8])
    if k==b'IDAT': idat+=c
    pos+=12+l
raw=zlib.decompress(idat);st=w*3
def px(x,y): o=y*(st+1)+1+x*3; return sum(raw[o:o+3])
r1,r2=px(150,92),px(150,110)
print('file2' if r2>r1+60 else 'other')
PY
)
if [ "$sel" != file2 ]; then echo "NOT_ON_FILE2: not loading anything"; exit 1; fi
$T btn a; sleep 2; $T btn a; sleep 18
echo "loaded test save (File 2)"
