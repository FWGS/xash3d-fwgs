#!/bin/bash

echo "Download HLSDK"

cd $GITHUB_WORKSPACE
git clone --depth 1 --recursive https://github.com/FWGS/hlsdk-xash3d -b mobile_hacks hlsdk || exit 1

echo "Download and unpack Android SDK"
mkdir -p sdk || exit 1
pushd sdk
wget https://dl.google.com/android/repository/sdk-tools-linux-4333796.zip -qO sdk.zip > /dev/null || exit 1
unzip -q sdk.zip || exit 1
popd

echo "Download all needed tools and NDK"
yes | sdk/tools/bin/sdkmanager --licenses > /dev/null 2>/dev/null # who even reads licenses? :)
sdk/tools/bin/sdkmanager --install build-tools\;29.0.1 platform-tools platforms\;android-29 > /dev/null 2>/dev/null
wget https://dl.google.com/android/repository/android-ndk-r25-linux.zip -qO ndk.zip > /dev/null || exit 1
unzip -q ndk.zip || exit 1
mv android-ndk-r25 sdk/ndk-bundle || exit 1

echo "Download Xash3D FWGS Android source"
git clone --depth 1 https://github.com/FWGS/xash3d-android-project -b waf android || exit 1
pushd android

mv xash3d-fwgs xash3d-fwgs-sub
ln -s $GITHUB_WORKSPACE xash3d-fwgs
echo "Installed Xash3D FWGS source symlink"

mv hlsdk-xash3d hlsdk-xash3d-sub
ln -s $GITHUB_WORKSPACE/hlsdk hlsdk-xash3d
echo "Installed HLSDK source symlink"

mv xash-extras xash-extras-sub
ln -s $GITHUB_WORKSPACE/xash-extras xash-extras
echo "Installed xash-extras symlink"

popd
