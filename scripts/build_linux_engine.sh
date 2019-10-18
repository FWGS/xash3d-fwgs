#!/bin/bash

. scripts/lib.sh

if [ "$1" = "dedicated" ]; then
	APP=xashds
	APPNAME=$APP-linux-$ARCH # since we have no extension, mark executable name that it for linux
else # elif [ "$1" = "full" ]; then
	APP=xash3d-fwgs
	APPNAME=$APP-$ARCH
fi

if [ ! "$ARCH" ]; then
	ARCH=i686
fi

# set up ccache
export CC="ccache gcc"
export CXX="ccache g++"


build_sdl2()
{
	cd "$TRAVIS_BUILD_DIR"/SDL2_src || die
	if [ "$ARCH" = "i686" ]; then
		export CFLAGS="-msse2 -march=i686 -m32 -ggdb -O2"
		export LDFLAGS="-m32"
	fi
	./configure --disable-render --disable-haptic --disable-power --disable-filesystem \
		--disable-file --disable-libudev --disable-dbus --disable-ibus \
		--disable-ime --disable-fcitx \
		--enable-alsa-shared --enable-pulseaudio-shared \
		--enable-wayland-shared --enable-x11-shared \
		--prefix / || die # get rid of /usr/local stuff
	make -j2 || die
	mkdir -p "$TRAVIS_BUILD_DIR"/SDL2_linux
	make install DESTDIR="$TRAVIS_BUILD_DIR"/SDL2_linux || die
	export CFLAGS=""
	export LDFLAGS=""
}

build_engine()
{
	# Build engine
	cd "$TRAVIS_BUILD_DIR" || die

	if [ "$ARCH" = "x86_64" ]; then # we need enabling 64-bit target only on Intel-compatible CPUs
		AMD64="-8"
	fi

	if [ "$APP" = "xashds" ]; then
		./waf configure -T release -d -W $AMD64 || die
	elif [ "$APP" = "xash3d-fwgs" ]; then
		APPDIR=$APPNAME.AppDir
		./waf configure --sdl2=SDL2_linux -T release --enable-stb --prefix="$APPDIR" -W $AMD64 || die
	else
		die
	fi

	./waf build || die
}

build_appimage()
{
	APPDIR=$APPNAME.AppDir
	APPIMAGE=$APPNAME.AppImage

	cd "$TRAVIS_BUILD_DIR" || die

	./waf install || die

	# Generate extras.pak
	python3 scripts/makepak.py xash-extras/ "$APPDIR/extras.pak"

	cp SDL2_linux/lib/libSDL2-2.0.so.0 "$APPDIR/"
	if [ "$ARCH" = "i686" ]; then
		cp vgui-dev/lib/vgui.so "$APPDIR/"
	fi

	cat > "$APPDIR"/AppRun << 'EOF'
#!/bin/sh

if [ "$XASH3D_BASEDIR" = "" ]; then
	export XASH3D_BASEDIR=$PWD
fi
echo "Xash3D FWGS installed as AppImage."
echo "Base directory is $XASH3D_BASEDIR. Set XASH3D_BASEDIR environment variable to override this"

export XASH3D_EXTRAS_PAK1="${APPDIR}"/extras.pak
export LD_LIBRARY_PATH="${APPDIR}":$LD_LIBRARY_PATH
${DEBUGGER} "${APPDIR}"/xash3d "$@"
exit $?
EOF

	chmod +x "$APPDIR"/xash3d "$APPDIR"/AppRun # Engine launcher & engine launcher script

	echo "Contents of AppImage: "
	ls -R "$APPDIR"

	wget "https://raw.githubusercontent.com/FWGS/fwgs-artwork/master/xash3d/icon_512.png" -O "$APPDIR/$APP.png"

	cat > "$APPDIR/$APP.desktop" <<EOF
[Desktop Entry]
Name=$APP
Icon=$APP
Type=Application
Exec=AppRun
Categories=Game;
EOF

	wget "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-$ARCH.AppImage"
	chmod +x "appimagetool-$ARCH.AppImage"
	./appimagetool-$ARCH.AppImage "$APPDIR" "$APPIMAGE"
}

rm -rf build # clean-up build directory

if [ $APP != "xashds" ]; then
	build_sdl2
fi
build_engine
if [ $APP != "xashds" ]; then
	build_appimage
else
	mv build/engine/xash $APPNAME
fi
