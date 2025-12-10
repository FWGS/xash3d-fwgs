#!/bin/bash

#cd into script directory
cd "${0%/*}"
BUILDDIR=$(realpath ../../build)
echo $BUILDDIR
cd "../../engine/platform/ios/bundle"

if [ -d $BUILDDIR ]; then
    mkdir -p "$BUILDDIR/ios/xash3d.app"

    cp -r "$BUILDDIR/ios/dlls" "$BUILDDIR/ios/xash3d.app"
    cp -r "$BUILDDIR/ios/cl_dlls" "$BUILDDIR/ios/xash3d.app"
    find "$BUILDDIR/ios/xash3d.app/cl_dlls" -name "*.dylib" -type f -exec dsymutil {} \;
    find "$BUILDDIR/ios/xash3d.app/dlls" -name "*.dylib" -type f -exec dsymutil {} \;
    cp *.plist "$BUILDDIR/ios/xash3d.app"
    if [ ! -d $BUILDDIR/SDL2.framework ]; then
        echo "Couldn't find SDL2.framework, place it in the build directory"
        exit 1
    fi
    cp -r $BUILDDIR/SDL2.framework "$BUILDDIR/ios/xash3d.app"

    cd ../../../../

    ./waf install --destdir="$BUILDDIR/ios/xash3d.app"
    find "$BUILDDIR/ios/xash3d.app" -name "*.dylib" -type f -exec dsymutil {} \;
    dsymutil "$BUILDDIR/ios/xash3d.app/xash"

    cd $BUILDDIR

    rm -r "$BUILDDIR/ios/Payload/"
    mkdir "$BUILDDIR/ios/Payload"

    cp -r "$BUILDDIR/ios/xash3d.app" ios/Payload/
    rm -r "$BUILDDIR/ios/xash3d.app"
    cd ios
    zip -q -r ../xash3d.ipa Payload
    else
    echo "Couldn't find the build directory, compile the engine before running this script!"
    exit 1
fi
exit 0
