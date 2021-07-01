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

export API=21
export TOOLCHAIN=host
export CC=clang-12
export CXX=clang++-12
export STRIP=llvm-strip-12
sh compile.sh release

if [[ "$GH_CPU_ARCH" == "64" ]]; then
	mv xashdroid.apk xashdroid-64-test.apk
fi
