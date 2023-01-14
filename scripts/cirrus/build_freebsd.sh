#!/bin/sh

. scripts/lib.sh

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
		./waf configure -T release -d --enable-fs-tests || die_configure
	elif [ "$APP" = "xash3d-fwgs" ]; then
		./waf configure -T release --enable-stb --enable-utils --enable-gl4es --enable-gles1 --enable-gles2 --enable-fs-tests || die_configure
	else
		die
	fi

	./waf build || die
}

rm -rf build # clean-up build directory

build_engine
