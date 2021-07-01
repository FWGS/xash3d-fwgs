#!/bin/bash

. scripts/lib.sh

APP=xash3d-fwgs
APPDIR=$APP
APPIMAGE=$APP-$ARCH.tar.xz

build_sdl2()
{
	cd "$BUILDDIR"/SDL2_src || die
	if [ "$ARCH" = "i386" ]; then
		export CFLAGS="-msse2 -march=i686 -m32 -ggdb -O2"
		export LDFLAGS="-m32"
	fi
	./configure \
		--disable-render \
		--disable-haptic \
		--disable-power \
		--disable-filesystem \
		--disable-file \
		--disable-libudev \
		--disable-dbus \
		--disable-ibus \
		--disable-ime \
		--disable-fcitx \
		--enable-alsa-shared \
		--enable-pulseaudio-shared \
		--enable-wayland-shared \
		--enable-x11-shared \
		--prefix / || die # get rid of /usr/local stuff
	make -j2 || die
	mkdir -p "$BUILDDIR"/SDL2_linux
	make install DESTDIR="$BUILDDIR"/SDL2_linux || die
	export CFLAGS=""
	export LDFLAGS=""
}

build_engine()
{
	# Build engine
	cd "$BUILDDIR" || die

	if [ "$ARCH" = "amd64" ]; then # we need enabling 64-bit target only on Intel-compatible CPUs
		AMD64="-8"
	fi

	if [ "$1" = "dedicated" ]; then
		./waf configure -T release -d -W $AMD64 || die
	elif [ "$1" = "full" ]; then
		./waf configure --sdl2=SDL2_linux -T release --enable-stb --prefix="$APPDIR" -W $AMD64 --enable-utils || die
	else
		die
	fi

	./waf build || die
}

build_archive()
{
	cd "$BUILDDIR" || die

	./waf install || die

	# Generate extras.pak
	python3 scripts/makepak.py xash-extras/ "$APPDIR/extras.pak"

	cp SDL2_linux/lib/libSDL2-2.0.so.0 "$APPDIR/"
	if [ "$ARCH" = "i386" ]; then
		cp vgui-dev/lib/vgui.so "$APPDIR/"
	fi

	cat > "$APPDIR"/run.sh << 'EOF'
#!/bin/sh

if [ "$XASH3D_BASEDIR" = "" ]; then
	export XASH3D_BASEDIR=$PWD
fi
echo "Xash3D FWGS installed as tarball."
echo "Base directory is $XASH3D_BASEDIR. Set XASH3D_BASEDIR environment variable to override this"

export XASH3D_EXTRAS_PAK1="${XASH3D_BASEDIR}"/extras.pak
export LD_LIBRARY_PATH="${XASH3D_BASEDIR}":$LD_LIBRARY_PATH
${DEBUGGER} "${XASH3D_BASEDIR}"/xash3d "$@"
exit $?
EOF

	chmod +x "$APPDIR"/xash3d "$APPDIR"/run.sh # Engine launcher & engine launcher script

	echo "Contents of tarball: "
	ls -R "$APPDIR"

	tar -cJvf $APPIMAGE $APPDIR/*
}

rm -rf build # clean-up build directory
build_engine dedicated
mv build/engine/xash xashds-linux-$ARCH

rm -rf build
build_sdl2
build_engine full
build_archive
