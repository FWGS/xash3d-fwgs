#!/bin/bash

unset ANDROID_SDK_ROOT
export JAVA_HOME=$GITHUB_WORKSPACE/java
export ANDROID_HOME=$GITHUB_WORKSPACE/sdk
export PATH=$PATH:$JAVA_HOME/bin:$ANDROID_HOME/tools:$ANDROID_HOME/tools/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmdline-tools/tools/bin

pushd android || exit 1

./gradlew assembleContinuous --no-daemon || exit 1

popd || exit 1

mkdir -p artifacts/

mv android/app/build/outputs/apk/continuous/app-continuous.apk artifacts/xash3d-fwgs-android.apk
tar -cJvf artifacts/xash3d-fwgs-android-mappings.tar.zst -C android/app/build/outputs/mapping/continuous '.'

