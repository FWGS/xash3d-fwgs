#!/bin/bash

cd "$GITHUB_WORKSPACE" || exit 1

ANDROID_COMMANDLINE_TOOLS_VER="14742923"
ANDROID_BUILD_TOOLS_VER="36.0.0"
ANDROID_PLATFORM_VER="android-35"
ANDROID_NDK_VERSION="29.0.14206865"

BUILD_ONCE_RUN_WITH_SPECIFIC_RUNTIME_VERSION="jbrsdk-21.0.10-linux-x64-b1163.110"

echo "Download JDK 17"
wget https://cache-redirector.jetbrains.com/intellij-jbr/$BUILD_ONCE_RUN_WITH_SPECIFIC_RUNTIME_VERSION.tar.gz -qO- | tar -xzf - || exit 1
mv $BUILD_ONCE_RUN_WITH_SPECIFIC_RUNTIME_VERSION java

export JAVA_HOME=$GITHUB_WORKSPACE/java
export PATH=$PATH:$JAVA_HOME/bin

echo "Download hlsdk-portable"
git clone --depth 1 --recursive https://github.com/FWGS/hlsdk-portable -b mobile_hacks 3rdparty/hlsdk-portable || exit 1

echo "Download SDL"
pushd 3rdparty || exit 1
wget "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-$SDL_VERSION.tar.gz" -qO- | tar -xzf - || exit 1
mv "SDL2-$SDL_VERSION" SDL
popd || exit 1

echo "Download Android SDK"
mkdir -p sdk || exit 1
pushd sdk || exit 1
wget https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_COMMANDLINE_TOOLS_VER}_latest.zip -qO sdk.zip || exit 1
unzip -q sdk.zip || exit 1
mv cmdline-tools tools
mkdir -p cmdline-tools
mv tools cmdline-tools/tools
unset ANDROID_SDK_ROOT
export ANDROID_HOME=$GITHUB_WORKSPACE/sdk
export PATH=$PATH:$ANDROID_HOME/tools:$ANDROID_HOME/tools/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/tools/bin
popd || exit 1

echo "Download all needed tools and Android NDK"
yes | sdkmanager --licenses > /dev/null 2>/dev/null # who even reads licenses? :)
sdkmanager --install build-tools\;${ANDROID_BUILD_TOOLS_VER} platform-tools platforms\;${ANDROID_PLATFORM_VER} ndk\;${ANDROID_NDK_VERSION}
