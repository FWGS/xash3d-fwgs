#!/bin/bash


if [ "$1" = "dedicated" ]; then
	APP=xashds
else # elif [ "$1" = "full" ]; then
	APP=xash3d-fwgs
fi

build_sdl2()
{
	cd "$CIRRUS_WORKING_DIR"/SDL2_src || die
	./configure --disable-render --disable-haptic --disable-power --disable-filesystem \
		--disable-file --disable-libudev --disable-dbus --disable-ibus \
		--disable-ime --disable-fcitx \
		--enable-alsa-shared --enable-pulseaudio-shared \
		--enable-wayland-shared --enable-x11-shared \
		--prefix / || die # get rid of /usr/local stuff
	make -j2 || die
	mkdir -p "$CIRRUS_WORKING_DIR"/SDL2_bsd
	make install DESTDIR="$CIRRUS_WORKING_DIR"/SDL2_bsd || die
}

build_engine()
{
	# Build engine
	cd "$CIRRUS_WORKING_DIR" || die

	if [ "$APP" = "xashds" ]; then
		./waf configure -T release --single-binary -d -W || die
	elif [ "$APP" = "xash3d-fwgs" ]; then
		./waf configure --sdl2=SDL2_bsd -T release --enable-stb -W || die
	else
		die
	fi

	./waf build || die
}

rm -rf build # clean-up build directory

if [ $APP != "xashds" ]; then
	build_sdl2
fi
build_engine
