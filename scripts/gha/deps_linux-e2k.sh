#!/bin/bash

set -e

cd "$GITHUB_WORKSPACE"

. scripts/lib-e2k.sh

wget "${E2K_CROSS_COMPILER_URL[$GH_CPU_ARCH]}" -O- | sudo tar -C / -xzvf -

for i in ${E2K_PACKAGES_URLS[$GH_CPU_ARCH]}; do
	wget "$i" -O package.deb
	ar x package.deb data.tar.xz
	tar -xvf data.tar.xz
	rm package.deb data.tar.xz
done
