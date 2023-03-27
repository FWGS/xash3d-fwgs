#!/bin/bash

. scripts/lib.sh

cd "$BUILDDIR" || die

rm -rf artifacts build pkgtemp

mkdir -p pkgtemp/xash3d/{valve,gearbox,bshift}/{dlls,cl_dlls} || die
mkdir -p artifacts/ || die

echo "Running build script in Docker container..."

docker run --name xash-build --rm -v `pwd`:`pwd` -w `pwd` devkitpro/devkita64:latest bash ./scripts/gha/build_nswitch_docker.sh || die

echo "Packaging artifacts..."

pushd pkgtemp || die
7z a -t7z ../artifacts/xash3d-fwgs-nswitch.7z -m0=lzma2 -mx=9 -mfb=64 -md=32m -ms=on -r xash3d/
popd
