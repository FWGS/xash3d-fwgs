#!/bin/bash

cd $GITHUB_WORKSPACE

# pinning cmake version to 3.28.3, because with cmake 4.x SDL vita fork doesn't building
# it is known problem and cmake 4.x update breaks many CI pipelines around the world :)
sudo apt-get update || exit 1
sudo apt-get install cmake=3.28.3-1build7
sudo ln -sf /usr/bin/cmake /usr/local/bin/cmake

echo "Downloading vitasdk..."

export VITASDK=/usr/local/vitasdk

VITAGL_SRCREV="064db9efb15833e18777a3e768b8b1fb2abee78f" # lock vitaGL version to avoid compilation errors

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

git clone https://github.com/Rinnegatamante/vitaGL.git || exit 1
pushd vitaGL
git checkout $VITAGL_SRCREV || exit 1
popd

echo "Downloading vitaGL fork of SDL2..."

git clone https://github.com/Northfear/SDL.git --depth=1 || exit 1

echo "Downloading vita-rtld..."

git clone https://github.com/fgsfdsfgs/vita-rtld.git --depth=1 || exit 1

echo "Downloading HLSDK..."

rm -rf hlsdk-xash3d hlsdk-portable
git clone --recursive https://github.com/FWGS/hlsdk-portable || exit 1
