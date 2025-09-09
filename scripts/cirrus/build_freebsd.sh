#!/bin/sh

. scripts/lib.sh

build_engine()
{
	# Build engine
	cd "$CIRRUS_WORKING_DIR" || die

	./waf configure --enable-utils --enable-all-renderers --enable-tests --enable-dedicated || die_configure
	./waf build || die
}

rm -rf build # clean-up build directory

build_engine
