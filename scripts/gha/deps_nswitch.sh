#!/bin/bash

cd $GITHUB_WORKSPACE

echo "Downloading devkitA64 docker container..."

docker pull devkitpro/devkita64:latest || exit 1

echo "Downloading libsolder..."

rm -rf libsolder
git clone https://github.com/fgsfdsfgs/libsolder.git --depth=1 || exit 1

echo "Downloading HLSDK..."

rm -rf hlsdk-xash3d hlsdk-portable
git clone --recursive https://github.com/FWGS/hlsdk-portable || exit 1
