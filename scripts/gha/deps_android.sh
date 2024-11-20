#!/bin/bash

cd $GITHUB_WORKSPACE

ANDROID_COMMANDLINE_TOOLS_VER="11076708"
ANDROID_BUILD_TOOLS_VER="34.0.0"
ANDROID_PLATFORM_VER="android-34"

echo "Download JDK 17"
wget https://github.com/adoptium/temurin17-binaries/releases/download/jdk-17.0.7%2B7/OpenJDK17U-jdk_x64_linux_hotspot_17.0.7_7.tar.gz -qO- | tar -xzf - || exit 1
export JAVA_HOME=$GITHUB_WORKSPACE/jdk-17.0.7+7
export PATH=$PATH:$JAVA_HOME/bin

echo "Download hlsdk-portable"
git clone --depth 1 --recursive https://github.com/FWGS/hlsdk-portable -b mobile_hacks 3rdparty/hlsdk-portable || exit 1

echo "Download SDL"
pushd 3rdparty
wget https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz -qO- | tar -xzf - || exit 1
mv SDL2-$SDL_VERSION SDL
popd

echo "Download Android SDK"
mkdir -p sdk || exit 1
pushd sdk
wget https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_COMMANDLINE_TOOLS_VER}_latest.zip -qO sdk.zip || exit 1
unzip -q sdk.zip || exit 1
mv cmdline-tools tools
mkdir -p cmdline-tools
mv tools cmdline-tools/tools
unset ANDROID_SDK_ROOT
export ANDROID_HOME=$GITHUB_WORKSPACE/sdk
export PATH=$PATH:$ANDROID_HOME/tools:$ANDROID_HOME/tools/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/tools/bin
popd

echo "Download all needed tools and Android NDK"
yes | sdkmanager --licenses > /dev/null 2>/dev/null # who even reads licenses? :)
sdkmanager --install build-tools\;${ANDROID_BUILD_TOOLS_VER} platform-tools platforms\;${ANDROID_PLATFORM_VER}
