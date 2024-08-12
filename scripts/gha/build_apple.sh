#!/bin/bash

. scripts/lib.sh

cd $GITHUB_WORKSPACE || die

pushd hlsdk || die
./waf configure build install --destdir=../bin || die
popd

./waf configure --enable-utils --enable-tests --enable-lto build install --destdir=bin || die_configure

mkdir -p artifacts/
tar -cJvf artifacts/ash3d-fwgs-apple-$ARCH.tar.xz -C bin . # skip the bin directory from resulting tar archive
