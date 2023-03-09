#!/bin/bash

cd $GITHUB_WORKSPACE

echo "Downloading vitasdk..."

export VITASDK=/usr/local/vitasdk

install_package()
{
	./vdpm $1 || exit 1
}

git clone https://github.com/vitasdk/vdpm.git || exit 1
pushd vdpm
./bootstrap-vitasdk.sh || exit 1
install_package taihen
install_package kubridge
install_package zlib
install_package SceShaccCgExt
install_package vitaShaRK
install_package libmathneon
popd

echo "Downloading vitaGL..."

git clone https://github.com/Rinnegatamante/vitaGL.git || exit 1

echo "Downloading vitaGL fork of SDL2..."

git clone https://github.com/Northfear/SDL.git || exit 1

echo "Downloading vita-rtld..."

git clone https://github.com/fgsfdsfgs/vita-rtld.git || exit 1

echo "Downloading HLSDK..."

rm -rf hlsdk-xash3d hlsdk-portable
# TODO: change to FWGS/hlsdk-portable.git when changes are merged in
git clone --recursive https://github.com/fgsfdsfgs/hlsdk-xash3d || exit 1
