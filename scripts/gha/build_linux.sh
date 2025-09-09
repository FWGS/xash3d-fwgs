#!/bin/bash

# As e2k builds for distro that's vastly different from Ubuntu/Debian and specially handles cross-compiling
# keep it in separate script for now
if [[ $GH_CPU_ARCH == e2k* ]]; then
	exec bash scripts/gha/build_linux-e2k.sh
fi

. scripts/lib.sh

# "booo, bash feature!"
declare -A ARCH_TRIPLET CROSS_COMPILE_CC CROSS_COMPILE_CXX
ARCH_TRIPLET[amd64]=x86_64-linux-gnu
ARCH_TRIPLET[i386]=i386-linux-gnu
ARCH_TRIPLET[arm64]=aarch64-linux-gnu
ARCH_TRIPLET[armhf]=arm-linux-gnueabihf
ARCH_TRIPLET[riscv64]=riscv64-linux-gnu
ARCH_TRIPLET[ppc64el]=powerpc64le-linux-gnu
CROSS_COMPILE_CC[amd64]=cc
CROSS_COMPILE_CC[i386]="cc -m32"
CROSS_COMPILE_CXX[amd64]=c++
CROSS_COMPILE_CXX[i386]="c++ -m32"
for i in arm64 armhf riscv64 ppc64el; do
	CROSS_COMPILE_CC[$i]=${ARCH_TRIPLET[$i]}-gcc
	CROSS_COMPILE_CXX[$i]=${ARCH_TRIPLET[$i]}-g++
done
export PKG_CONFIG_PATH=$PWD/ffmpeg/lib/pkgconfig:${ARCH_TRIPLET[$GH_CPU_ARCH]}
export CC=${CROSS_COMPILE_CC[$GH_CPU_ARCH]}
export CXX=${CROSS_COMPILE_CXX[$GH_CPU_ARCH]}

APP=xash3d-fwgs
APPDIR=$APP.AppDir
APPIMAGE=$APP-$ARCH.AppImage

APPDIR2=$APP-linux-$ARCH # FIXME: not conforms to libpublic's build arch strings but in parity with xashds directory name
APPTARGZ=$APP-linux-$ARCH.tar.gz

DS=xashds-linux
DSDIR=$DS-$ARCH
DSTARGZ=$DS-$ARCH.tar.gz
N=$(nproc)

build_sdl2()
{
	cd "$BUILDDIR"/SDL2_src || die

	# a1ba: let's make something different. Rather than removing features
	# let's enable everything we can
	mkdir -p build || die
	pushd build || die
		cmake ../ -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$BUILDDIR"/SDL2_linux -DCMAKE_C_FLAGS=-O3 -DSDL_STATIC=OFF -DSDL_RENDER=OFF || die
		ninja install -j$((N+1)) || die
	popd || die
}

build_engine()
{
	# Build engine
	cd "$BUILDDIR" || die

	if [ "$ARCH" = "amd64" ]; then # we need enabling 64-bit target only on Intel-compatible CPUs
		WAF_EXTRA_ARGS="-8"
	fi

	if [ -d "ffmpeg" ]; then
		WAF_EXTRA_ARGS+=" --enable-ffmpeg --enable-ffmpeg-dlopen"
	fi

	if [ "$GH_CROSSCOMPILING" != "true" ]; then
		WAF_EXTRA_ARGS+=" --enable-tests"
	fi

	./waf configure $WAF_EXTRA_ARGS --enable-lto --enable-bundled-deps -s SDL2_linux --enable-stbtt --enable-utils --enable-tui --enable-dedicated || die_configure

	./waf build || die_configure
}

deploy_engine()
{
	cd "$BUILDDIR" || die
	./waf install --destdir="$APPDIR" || die

	cp -av SDL2_linux/lib/libSDL2.so SDL2_linux/lib/libSDL2-* "$APPDIR/"

	if [ "$GH_CPU_ARCH" = "i386" ]; then
		cp -av 3rdparty/vgui_support/vgui-dev/lib/vgui.so "$APPDIR/"
	fi

	if [ -d "ffmpeg" ]; then
		cp -av ffmpeg/lib/libav* ffmpeg/lib/libsw* "$APPDIR/"
	fi
}

build_appimage()
{
	deploy_engine

	cp scripts/gha/linux/AppRun "$APPDIR/AppRun"
	cp scripts/gha/linux/xash3d-fwgs.desktop "$APPDIR/$APP.desktop"
	wget "https://raw.githubusercontent.com/FWGS/fwgs-artwork/master/xash3d/icon_512.png" -O "$APPDIR/$APP.png"

	chmod +x "$APPDIR"/AppRun # Engine launcher & engine launcher script
	echo "Contents of AppImage: "
	ls -R "$APPDIR"
	./appimagetool.AppImage "$APPDIR" "artifacts/$APPIMAGE"
}

build_engine_tarball()
{
	mv "$APPDIR" "$APPDIR2"
	tar -czvf "artifacts/$APPTARGZ" "$APPDIR2"
}

build_dedicated_tarball()
{
	cd "$BUILDDIR" || die
	# FIXME: make an option for Waf to only install dedicated
	mkdir -p "$DSDIR"
	cp -v "$APPDIR"/filesystem_stdio.so "$DSDIR"
	mv -v "$APPDIR"/xash "$DSDIR"
	tar -czvf "artifacts/$DSTARGZ" "$DSDIR"
}

mkdir -p artifacts/

rm -rf build # clean-up build directory
build_sdl2
build_engine
deploy_engine
build_dedicated_tarball

if [ -x appimagetool.AppImage ]; then
	build_appimage
else
	build_engine_tarball
fi
