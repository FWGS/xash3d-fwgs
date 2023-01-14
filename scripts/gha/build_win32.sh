#!/bin/bash

. scripts/lib.sh

# Build engine
cd $BUILDDIR

if [ "$ARCH" = "amd64" ]; then # we need enabling 64-bit target only on Intel-compatible CPUs
	AMD64="-8"
fi

# NOTE: to build with other version use --msvc_version during configuration
# NOTE: sometimes you may need to add WinSDK to %PATH%
./waf.bat configure -s "SDL2_VC" -T "release" --enable-utils --enable-fs-tests --prefix=`pwd` $AMD64 || die_configure
./waf.bat build || die
./waf.bat install || die

if [ "$ARCH" = "i386" ]; then
	cp SDL2_VC/lib/x86/SDL2.dll . # Install SDL2
	cp 3rdparty/vgui_support/vgui-dev/lib/win32_vc6/vgui.dll .
elif [ "$ARCH" = "amd64" ]; then
	cp SDL2_VC/lib/x64/SDL2.dll .
else
	die
fi

mkdir -p artifacts/
7z a -t7z artifacts/xash3d-fwgs-win32-$ARCH.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on \
	*.dll *.exe *.pdb activities.txt \
	valve/
