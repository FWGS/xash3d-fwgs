#!/bin/bash
mkdir ../build/xash3d.app

cp -r dlls ../build/xash3d.app
cp -r cl_dlls ../build/xash3d.app
cp ./*.plist ../build/xash3d.app
cp -r ../SDL2.framework ../build/xash3d.app

cd ../build

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