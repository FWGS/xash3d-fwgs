#!/bin/sh

if [ "$1" = "dedicated" ]; then
	APP=xashds
else # elif [ "$1" = "full" ]; then
	APP=xash3d-fwgs
fi

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

build_engine
