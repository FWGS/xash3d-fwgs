#!/bin/bash

. scripts/lib.sh

cd "$BUILDDIR" || die

./waf configure -T release --ps4 || die_configure
./waf build || die

mkdir -p artifacts || die
cp build/engine/IV0000-XASH00001_00-XASH3DFWGSPS4000.pkg artifacts/xash3d-fwgs-ps4.pkg || die
