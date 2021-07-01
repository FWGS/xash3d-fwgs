#!/bin/bash

export ANDROID_SDK_HOME=$GITHUB_WORKSPACE/sdk
export ANDROID_NDK_HOME=$ANDROID_SDK_HOME/ndk-bundle

pushd android
export ARCHS=$GH_CPU_ARCH
export API=21
export TOOLCHAIN=host
sh compile.sh release

if [[ $ARCHS == *"aarch64"* ]]; then
	mv xashdroid.apk xashdroid-64-test.apk
fi
