#!/bin/bash

export ANDROID_SDK_HOME=$GITHUB_WORKSPACE/sdk
export ANDROID_NDK_HOME=$ANDROID_SDK_HOME/ndk-bundle

pushd android
if [[ "$GH_CPU_ARCH" == "32" ]]; then
	export ARCHS="armeabi armeabi-v7a x86"
elif [[ "$GH_CPU_ARCH" == "64" ]]; then
	export ARCHS="aarch64 x86_64"
elif [[ "$GH_CPU_ARCH" == "32&64" ]]; then
	export ARCHS="armeabi armeabi-v7a x86 aarch64 x86_64"
fi

sh compile.sh release

popd

mkdir -p artifacts/

if [[ "$GH_CPU_ARCH" == "64" ]]; then
	mv android/xashdroid.apk artifacts/xashdroid-64.apk
else
	mv android/xashdroid.apk artifacts/xashdroid-32.apk
fi
