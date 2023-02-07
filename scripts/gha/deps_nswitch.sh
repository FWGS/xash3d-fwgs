#!/bin/bash

cd $GITHUB_WORKSPACE

echo "Downloading devkitA64 docker container..."

docker pull devkitpro/devkita64:latest || exit 1

echo "Downloading libsolder..."

rm -rf libsolder
git clone https://github.com/fgsfdsfgs/libsolder.git || exit 1

echo "Downloading HLSDK..."

# TODO: change to FWGS/hlsdk-portable.git when changes are merged in
rm -rf hlsdk-xash3d hlsdk-portable
git clone --recursive https://github.com/fgsfdsfgs/hlsdk-xash3d.git || exit 1
