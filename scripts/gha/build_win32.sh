#!/bin/bash

. scripts/lib.sh

# Build engine
cd $BUILDDIR

WAF_EXTRA_ARGS=""

if [ "$ARCH" = "amd64" ]; then # we need enabling 64-bit target only on Intel-compatible CPUs
	WAF_EXTRA_ARGS="-8"
fi

if [ -d "ffmpeg" ]; then
	export PKGCONFIG="$PWD/pkgconf/bin/pkgconf.exe"
	export PKG_CONFIG_PATH="$PWD/ffmpeg/lib/pkgconfig"
	WAF_EXTRA_ARGS+=" --enable-ffmpeg --enable-ffmpeg-dlopen"
fi

# NOTE: to build with other version use --msvc_version during configuration
# NOTE: sometimes you may need to add WinSDK to %PATH%
# NOTE: --enable-msvcdeps only used for CI builds, enabling it non-English versions of MSVC causes useless console spam
./waf.bat configure -s "SDL2_VC" -T release --enable-utils --enable-tests --enable-lto --enable-msvcdeps --enable-tui $WAF_EXTRA_ARGS || die_configure
./waf.bat build || die
./waf.bat install --destdir=. || die

if [ "$ARCH" = "i386" ]; then
	cp -v SDL2_VC/lib/x86/SDL2.dll . # Install SDL2
	cp -v SDL2_VC/lib/x86/SDL2.pdb .
elif [ "$ARCH" = "amd64" ]; then
	cp -v SDL2_VC/lib/x64/SDL2.dll .
	cp -v SDL2_VC/lib/x64/SDL2.pdb .
else
	die
fi

if [ -d "ffmpeg" ]; then
	cp -v ffmpeg/bin/av* ffmpeg/bin/sw* .
fi

WINSDK_LATEST=$(ls -1 "C:/Program Files (x86)/Windows Kits/10/bin" | grep -E '^10' | sort -rV | head -n1)
echo "Latest installed Windows SDK is $WINSDK_LATEST"

"C:/Program Files (x86)/Windows Kits/10/bin/$WINSDK_LATEST/x64/signtool.exe" \
	sign //f scripts/fwgs.pfx //fd SHA256 //p "$FWGS_PFX_PASSWORD" *.dll *.exe

if [ "$ARCH" = "i386" ]; then # VGUI is already signed
	cp 3rdparty/vgui_support/vgui-dev/lib/win32_vc6/vgui.dll .
fi

mkdir -p artifacts/
7z a -t7z artifacts/xash3d-fwgs-win32-$ARCH.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on \
	*.dll *.exe *.pdb activities.txt \
	valve/
