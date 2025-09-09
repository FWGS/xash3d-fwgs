#!/bin/bash

set -e

. scripts/lib.sh
. scripts/lib-e2k.sh

export CC=${E2K_CROSS_COMPILER_PATH[$GH_CPU_ARCH]}/bin/lcc
export CXX=${E2K_CROSS_COMPILER_PATH[$GH_CPU_ARCH]}/bin/l++

APP=xash3d-fwgs

APPDIR=$APP-linux-$ARCH # FIXME: not conforms to libpublic's build arch strings but in parity with xashds directory name
APPTARGZ=$APP-linux-$ARCH.tar.gz

DS=xashds-linux
DSDIR=$DS-$ARCH
DSTARGZ=$DS-$ARCH.tar.gz

build_engine()
{
	# Build engine
	cd "$BUILDDIR"

	./waf configure --enable-dedicated -s usr --enable-stbtt --enable-utils --enable-bundled-deps --enable-all-renderers || die_configure
	./waf build || die_configure
}

make_client_tarball()
{
	cd "$BUILDDIR"
	./waf install --destdir="$APPDIR"
	tar -czvf "artifacts/$APPTARGZ" "$APPDIR"
}

make_server_tarball()
{
	cd "$BUILDDIR"

	# FIXME: make an option for Waf to only install dedicated
	mkdir -p "$DSDIR"
	cp -v "$APPDIR"/xash "$APPDIR"/filesystem_stdio.so "$DSDIR"
	tar -czvf "artifacts/$DSTARGZ" "$DSDIR"
}

mkdir -p artifacts/

rm -rf build # clean-up build directory
build_engine

make_client_tarball
make_server_tarball
