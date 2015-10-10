#!/bin/sh

ndk-build NDK_TOOLCHAIN_VERSION=4.8 NDK_DEBUG=0 V=0 XASH_SDL=1 -j8 APP_CFLAGS="-w"
sh gen-version.sh default-release
ant release
jarsigner -verbose -sigalg SHA1withRSA -digestalg SHA1 -keystore ../myks.keystore bin/xashdroid-release-unsigned.apk xashdroid
rm bin/xashdroid-release.apk
/home/a1ba/.android/android-sdk-linux/build-tools/22.0.1/zipalign 4 bin/xashdroid-release-unsigned.apk bin/xashdroid-release.apk
