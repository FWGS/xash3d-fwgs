#!/bin/bash

set -e

cd "$GITHUB_WORKSPACE"

# not checked out by default, needed for --enable-all-renderers
git submodule update --init --checkout 3rdparty/nanogl 3rdparty/gl-wes-v2 3rdparty/gl4es/gl4es

. scripts/lib-e2k.sh

wget "${E2K_CROSS_COMPILER_URL[$GH_CPU_ARCH]}" -O- | sudo tar -C / -xzvf -

for i in ${E2K_PACKAGES_URLS[$GH_CPU_ARCH]}; do
	wget "$i" -O package.deb
	ar x package.deb data.tar.xz
	tar -xvf data.tar.xz
	rm package.deb data.tar.xz
done
