#!/bin/bash

#cd into script directory
cd "${0%/*}/../ios"

if [ -d ../build ]; then
    mkdir ../build/xash3d.app

    cp -r dlls ../build/xash3d.app
    cp -r cl_dlls ../build/xash3d.app
    cp ./*.plist ../build/xash3d.app
    if [ ! -d SDL2.framework ]; then
        echo "Couldn't find SDL2.framework, place it in the 'ios' directory"
        exit 1
    fi
    cp -r SDL2.framework ../build/xash3d.app

    cd ../build

    if [ ! -e engine/xash ]; then 
        echo "Couldn't find engine executable, ensure that compiliation finished successfully!"
        exit 1
    fi 
    cp engine/xash ./xash3d.app
    cp filesystem/filesystem_stdio.dylib ./xash3d.app
    cp ref/soft/libref_* ./xash3d.app
    cp ref/gl/libref_* ./xash3d.app
    cp 3rdparty/mainui/libmenu.dylib ./xash3d.app

    rm -r Payload/
    mkdir Payload

    cp -r ./xash3d.app Payload/
    rm -r ./xash3d.app
    zip -r xash3d.ipa Payload 
    else
    echo "Couldn't find the build directory, compile the engine before running this script!"
    exit 1
fi
exit 0