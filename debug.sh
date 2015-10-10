#!/bin/sh

ndk-build NDK_TOOLCHAIN_VERSION=4.8 NDK_DEBUG=1 SUPPORT_WEBP=false V=1 -j3 APP_CFLAGS="-gdwarf-3" XASH_SDL=1
sh gen-version.sh default-debug
ant debug
#jarsigner -verbose -sigalg SHA1withRSA -digestalg SHA1 -keystore ../myks.keystore bin/xashdroid-release-unsigned.apk xashdroid -storepass 529459
adb install -r -f bin/xashdroid-debug.apk
#rm bin/xashdroid-release.apk
#/opt/android-sdk-update-manager/build-tools/19.1.0/zipalign 4 bin/xashdroid-release-unsigned.apk bin/xashdroid-release.apk
