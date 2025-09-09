#!/bin/bash

unset ANDROID_SDK_ROOT
export JAVA_HOME=$GITHUB_WORKSPACE/jdk-17.0.15+6
export ANDROID_HOME=$GITHUB_WORKSPACE/sdk
export PATH=$PATH:$JAVA_HOME/bin:$ANDROID_HOME/tools:$ANDROID_HOME/tools/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/tools/bin

pushd android

./gradlew assembleContinuous --no-daemon || exit 1

pushd app/build/outputs/apk/continuous

$ANDROID_HOME/build-tools/36.0.0/apksigner sign \
	--ks $GITHUB_WORKSPACE/android/debug.keystore \
	--ks-key-alias androiddebugkey \
	--ks-pass pass:android \
	--key-pass pass:android \
	--out app-continuous-signed.apk app-continuous-unsigned.apk || exit 1

popd
popd

mkdir -p artifacts/

mv android/app/build/outputs/apk/continuous/app-continuous-signed.apk artifacts/xash3d-fwgs-android.apk
tar -cJvf artifacts/xash3d-fwgs-android-mappings.tar.zst -C android/app/build/outputs/mapping/continuous '.'

