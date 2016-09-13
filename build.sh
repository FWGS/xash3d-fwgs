#!/bin/sh

ndk-build NDK_TOOLCHAIN_VERSION=4.8 NDK_DEBUG=0 V=0 XASH_SDL=0 -j 8 APP_CFLAGS="-w -Wl,--no-undefined" APP_LDFLAGS="-Wl,--no-undefined"
./gen-config.sh release sign
./gen-version.sh default-release
rm assets/extras.pak 2>/dev/null
python2 makepak.py xash-extras assets/extras.pak
ant release
jarsigner -verbose -sigalg SHA1withRSA -digestalg SHA1 -keystore ../myks.keystore bin/xashdroid-release-unsigned.apk xashdroid
/home/a1ba/.android/android-sdk-linux/build-tools/24.0.1/zipalign -f 4 bin/xashdroid-release-unsigned.apk bin/xashdroid-release.apk
