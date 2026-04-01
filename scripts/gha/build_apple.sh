#!/bin/bash

. scripts/lib.sh

cd "$GITHUB_WORKSPACE" || die

pushd hlsdk || die
./waf configure build install --destdir=../bin || die
popd || die

./waf configure --enable-utils --enable-tests --enable-lto --enable-tui || die_configure
./waf build -v || die
./waf install --destdir=bin || die

cp -vr /Library/Frameworks/SDL2.framework bin

mkdir -p artifacts/
tar -cJvf "artifacts/xash3d-fwgs-apple-$ARCH.tar.xz" -C bin . # skip the bin directory from resulting tar archive
