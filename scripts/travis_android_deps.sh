#!/bin/bash

echo "Download HLSDK"

cd $TRAVIS_BUILD_DIR
git clone --depth 1 --recursive https://github.com/FWGS/hlsdk-xash3d hlsdk || exit 1

echo "Download Android SDK"
mkdir -p sdk && cd sdk
wget https://dl.google.com/android/repository/sdk-tools-linux-4333796.zip -qO sdk.zip > /dev/null 2>/dev/null || exit 1

echo "Unpack Android SDK"
unzip sdk.zip > /dev/null 2>/dev/null || exit 1
cd $TRAVIS_BUILD_DIR

echo "Download all needed tools and NDK"
yes | sdk/tools/bin/sdkmanager --licenses > /dev/null 2>/dev/null # who even reads licenses? :)
sdk/tools/bin/sdkmanager --install build-tools\;29.0.1 platform-tools platforms\;android-19 ndk-bundle > /dev/null 2>/dev/null

echo "Download Xash3D FWGS Android source"
git clone --depth 1 https://github.com/FWGS/xash3d-android-project -b waf android || exit 1
cd android

echo "Fetching submodules"
git submodule update --init xash-extras || exit 1

ln -s $TRAVIS_BUILD_DIR xash3d-fwgs-sl
echo "Installed Xash3D FWGS source symlink"

ln -s $TRAVIS_BUILD_DIR/hlsdk hlsdk-xash3d-sl
echo "Install HLSDK source symlink"

cd $TRAVIS_BUILD_DIR
