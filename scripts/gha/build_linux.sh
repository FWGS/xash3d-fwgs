#!/bin/bash

. scripts/lib.sh

APP=xash3d-fwgs
APPDIR=$APP.AppDir
APPIMAGE=$APP-$ARCH.AppImage

DS=xashds-linux
DSDIR=$DS-$ARCH
DSTARGZ=$DS-$ARCH.tar.gz

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
		./waf configure -T release -d $AMD64 --enable-fs-tests || die_configure
	elif [ "$1" = "full" ]; then
		./waf configure --sdl2=SDL2_linux -T release --enable-stb $AMD64 --enable-utils --enable-fs-tests || die_confgure
	else
		die
	fi

	./waf build || die
}

build_appimage()
{
	cd "$BUILDDIR" || die

	./waf install --destdir="$APPDIR" || die

	cp SDL2_linux/lib/libSDL2-2.0.so.0 "$APPDIR/"
	if [ "$ARCH" = "i386" ]; then
		cp 3rdparty/vgui_support/vgui-dev/lib/vgui.so "$APPDIR/"
	fi

	cat > "$APPDIR"/AppRun << 'EOF'
#!/bin/sh

if [ "$XASH3D_BASEDIR" = "" ]; then
	export XASH3D_BASEDIR=$PWD
fi
echo "Xash3D FWGS installed as AppImage."
echo "Base directory is $XASH3D_BASEDIR. Set XASH3D_BASEDIR environment variable to override this"

export XASH3D_EXTRAS_PAK1="${APPDIR}"/valve/extras.pk3
${DEBUGGER} "${APPDIR}"/xash3d "$@"
exit $?
EOF

	chmod +x "$APPDIR"/xash3d "$APPDIR"/AppRun # Engine launcher & engine launcher script

	echo "Contents of AppImage: "
	ls -R "$APPDIR"

	wget "https://raw.githubusercontent.com/FWGS/fwgs-artwork/master/xash3d/icon_512.png" -O "$APPDIR/$APP.png"

	cat > "$APPDIR/$APP.desktop" <<EOF
[Desktop Entry]
Name=xash3d-fwgs
Icon=xash3d-fwgs
Type=Application
Exec=AppRun
Categories=Game;
EOF

	./appimagetool.AppImage "$APPDIR" "$APPIMAGE"
}

build_dedicated_tarball()
{
	cd "$BUILDDIR" || die

	./waf install --destdir=$DSDIR || die

	tar -czvf $DSTARGZ $DSDIR
}

mkdir -p artifacts/

rm -rf build # clean-up build directory
build_engine dedicated
build_dedicated_tarball
mv $DSTARGZ artifacts/

build_sdl2
build_engine full # don't rebuild some common parts twice
build_appimage
mv $APPIMAGE artifacts/
