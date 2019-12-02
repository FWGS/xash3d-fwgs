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
NDK_BUNDLE="ndk-bundle"
if [ "$1" = "r10e" ]; then
	NDK_BUNDLE=""
fi
sdk/tools/bin/sdkmanager --install build-tools\;29.0.1 platform-tools platforms\;android-28 $NDK_BUNDLE > /dev/null 2>/dev/null
if [ "$1" = "r10e" ]; then
	wget http://dl.google.com/android/ndk/android-ndk-r10e-linux-x86_64.bin >/dev/null 2>/dev/null
	7z x ./android-ndk-r10e-linux-x86_64.bin > /dev/null
	mv android-ndk-r10e sdk/ndk-bundle
fi
echo "Download Xash3D FWGS Android source"
git clone --depth 1 https://github.com/FWGS/xash3d-android-project -b waf android || exit 1
cd android

echo "Fetching submodules"
git submodule update --init xash-extras || exit 1

mv xash3d-fwgs xash3d-fwgs-sub
ln -s $TRAVIS_BUILD_DIR xash3d-fwgs
echo "Installed Xash3D FWGS source symlink"

mv hlsdk-xash3d hlsdk-xash3d-sub
ln -s $TRAVIS_BUILD_DIR/hlsdk hlsdk-xash3d
echo "Install HLSDK source symlink"

cd $TRAVIS_BUILD_DIR
