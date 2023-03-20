#!/bin/bash

cd $GITHUB_WORKSPACE

echo "Downloading vitasdk..."

export VITASDK=/usr/local/vitasdk

VITAGL_SRCREV="c52391378c2bf1a00a0194c4fd88c35492d104b8" # lock vitaGL version to avoid compilation errors

install_package()
{
	./vdpm $1 || exit 1
}

git clone https://github.com/vitasdk/vdpm.git --depth=1 || exit 1
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

git clone https://github.com/Rinnegatamante/vitaGL.git -b "$VITAGL_SRCREV" --depth=1 || exit 1

echo "Downloading vitaGL fork of SDL2..."

git clone https://github.com/Northfear/SDL.git --depth=1 || exit 1

echo "Downloading vita-rtld..."

git clone https://github.com/fgsfdsfgs/vita-rtld.git --depth=1 || exit 1

echo "Downloading HLSDK..."

rm -rf hlsdk-xash3d hlsdk-portable
git clone --recursive https://github.com/FWGS/hlsdk-portable || exit 1
