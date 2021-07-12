#!/bin/bash

cd $GITHUB_WORKSPACE

sudo dpkg --add-architecture i386
sudo apt update
sudo apt install libc6:i386 libstdc++6:i386 gcc-multilib g++-multilib p7zip-full

sudo mkdir -p /opt/toolchains

pushd /opt/toolchains/
sudo git clone https://github.com/a1batross/motomagx_toolchain motomagx --depth=1
popd

git clone https://github.com/FWGS/hlsdk-xash3d hlsdk -b mobile_hacks --depth=1
