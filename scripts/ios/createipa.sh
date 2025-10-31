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
    cp *.plist "$BUILDDIR/ios/xash3d.app"
    if [ ! -d $BUILDDIR/SDL2.framework ]; then
        echo "Couldn't find SDL2.framework, place it in the build directory"
        exit 1
    fi
    cp -r $BUILDDIR/SDL2.framework "$BUILDDIR/xash3d.app"

    cd $BUILDDIR

    if [ ! -e engine/xash ]; then 
        echo "Couldn't find engine executable, ensure that compiliation finished successfully!"
        exit 1
    fi 
    cp engine/xash "$BUILDDIR/ios/xash3d.app"
    cp filesystem/filesystem_stdio.dylib "$BUILDDIR/ios/xash3d.app"
    cp ref/soft/libref_* "$BUILDDIR/ios/xash3d.app"
    cp ref/gl/libref_* "$BUILDDIR/ios/xash3d.app"
    cp 3rdparty/mainui/libmenu.dylib "$BUILDDIR/ios/xash3d.app"

    rm -r "$BUILDDIR/ios/Payload/"
    mkdir "$BUILDDIR/ios/Payload"

    cp -r "$BUILDDIR/ios/xash3d.app" ios/Payload/
    rm -r "$BUILDDIR/ios/xash3d.app"
    zip -r xash3d.ipa "ios/Payload"
    else
    echo "Couldn't find the build directory, compile the engine before running this script!"
    exit 1
fi
exit 0
