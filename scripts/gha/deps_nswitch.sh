#!/bin/bash

cd $GITHUB_WORKSPACE

echo "Downloading and installing dkp-pacman..."

wget https://apt.devkitpro.org/install-devkitpro-pacman
chmod +x ./install-devkitpro-pacman
sudo ./install-devkitpro-pacman
sudo dkp-pacman --noconfirm -Sy

echo "Downloading and installing devkitA64..."

export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PORTLIBS=$DEVKITPRO/portlibs

sudo dkp-pacman --noconfirm -S devkitA64 dkp-toolchain-vars switch-cmake switch-pkg-config 

echo "Downloading and installing packaged dependencies..."

sudo dkp-pacman --noconfirm -S libnx switch-mesa switch-libdrm_nouveau switch-sdl2

echo "Downloading and installing libsolder..."

git clone https://github.com/fgsfdsfgs/libsolder.git

pushd ./libsolder
make
sudo make install
popd
