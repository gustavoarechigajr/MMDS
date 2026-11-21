#!/bin/bash
set -e
D=~/android-dev
export JAVA_HOME=$D/jdk17 PATH=$D/bin:$D/jdk17/bin:$PATH PKG_CONFIG=$D/bin/pkg-config ANDROID_HOME=$D/sdk ANDROID_SDK_ROOT=$D/sdk
export ANDROID_NDK_HOME=$D/sdk/ndk/26.0.10792818 ANDROID_NDK_ROOT=$D/sdk/ndk/26.0.10792818
export VCPKG_ROOT=$D/vcpkg
if [ ! -x $VCPKG_ROOT/vcpkg ]; then
  git clone -q https://github.com/microsoft/vcpkg.git $VCPKG_ROOT --depth 1
  git -C $VCPKG_ROOT fetch -q --depth 1 origin a073c28e52d920cb2465a17512324b3b6684d03a
  git -C $VCPKG_ROOT checkout -q --detach a073c28e52d920cb2465a17512324b3b6684d03a
  $VCPKG_ROOT/bootstrap-vcpkg.sh -disableMetrics
fi
$VCPKG_ROOT/vcpkg install --triplet arm64-android --host-triplet arm64-android zlib libpng libogg libvorbis opus opusfile 2>&1 | tail -3
cd $D/2ship/Android
echo "sdk.dir=$ANDROID_HOME" > local.properties
./gradlew --no-daemon -q :app:assembleRelease -PtwoShipApplicationId=com.twoshipfork.mm.dualscreen "-PtwoShipAppLabel=2 Ship Dual Screen" 2>&1 | grep -E 'error:|FAILED|What went wrong|BUILD SUCC' | head -20
ls -la app/build/outputs/apk/release/
echo BUILD_DONE
