#!/bin/bash
set -e
D=~/android-dev; cd $D
if [ ! -d jdk17 ]; then
  curl -sSL -o jdk.tar.gz "https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jdk/hotspot/normal/eclipse"
  mkdir jdk17 && tar xzf jdk.tar.gz -C jdk17 --strip-components=1 && rm jdk.tar.gz
fi
export JAVA_HOME=$D/jdk17 PATH=$D/jdk17/bin:$PATH
if [ ! -d sdk/cmdline-tools/latest ]; then
  curl -sSL -o clt.zip https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
  mkdir -p sdk/cmdline-tools && unzip -q clt.zip -d sdk/cmdline-tools && mv sdk/cmdline-tools/cmdline-tools sdk/cmdline-tools/latest && rm clt.zip
fi
SM=$D/sdk/cmdline-tools/latest/bin/sdkmanager
yes | $SM --sdk_root=$D/sdk --licenses >/dev/null
$SM --sdk_root=$D/sdk "platform-tools" "platforms;android-33" "build-tools;33.0.2" "ndk;26.0.10792818" "cmake;3.30.3" | tail -2
echo SETUP_DONE
