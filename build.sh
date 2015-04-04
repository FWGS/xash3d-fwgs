#!/bin/sh

ndk-build NDK_TOOLCHAIN_VERSION=4.8 NDK_DEBUG=0 SUPPORT_WEBP=false V=1
ant release
#jarsigner -verbose -sigalg SHA1withRSA -digestalg SHA1 -keystore ../myks.keystore bin/xashdroid-release-unsigned.apk xashdroid
#adb install -r -f bin/xashdroid-debug.apk
rm bin/xashdroid-release.apk
/opt/android-sdk-update-manager/build-tools/19.1.0/zipalign 4 bin/xashdroid-release-unsigned.apk bin/xashdroid-release.apk
