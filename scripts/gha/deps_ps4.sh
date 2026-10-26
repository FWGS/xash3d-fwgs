#!/bin/bash

cd "$GITHUB_WORKSPACE" || exit 1

# PS4-only submodules aren't checked out by default, see Documentation/ps4.md
git submodule update --init --checkout --recursive 3rdparty/opengnm/opengnm 3rdparty/opengnm-psbc 3rdparty/glslang || exit 1

# OpenOrbis builds the engine with LLVM 18 (newer lld picks musl's lite_malloc, which
# gives two mallocs in the process)
sudo apt-get update || exit 1
sudo apt-get install -y clang-18 lld-18 llvm-18 python3 python3-mako python3-yaml curl p7zip-full || exit 1

# PkgTool.Core is built for .NET Core 3.0, it can only load OpenSSL 1.0 or 1.1, and
# current Ubuntu images have just OpenSSL 3
SSL_URL=http://security.ubuntu.com/ubuntu/pool/main/o/openssl/libssl1.1_1.1.1f-1ubuntu2.24_amd64.deb
SSL_SHA256=7cf39d70a639017d1dd7c8d36daa2258063608688e449fddf40ffdd46f992a78

echo "Downloading OpenSSL 1.1..."

curl -L -o libssl1.1.deb "$SSL_URL" || exit 1
echo "$SSL_SHA256  libssl1.1.deb" | sha256sum -c - || exit 1
sudo dpkg -i libssl1.1.deb || exit 1
rm libssl1.1.deb

# OpenOrbis toolchain
OO_URL=https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases/download/v0.5.4/toolchain-llvm-18.tar.gz
OO_SHA256=3c7cd5bb593ca74fa1c13fd59f3938dc0fc07985167f7275063019e63abe4526

echo "Downloading OpenOrbis toolchain..."

curl -L -o oo.tar.gz "$OO_URL" || exit 1
echo "$OO_SHA256  oo.tar.gz" | sha256sum -c - || exit 1
rm -rf oo && mkdir oo || exit 1
tar -xzf oo.tar.gz -C oo || exit 1
rm oo.tar.gz

OO_LINKER=$(find oo -maxdepth 4 -name link.x | head -n 1)
[ -n "$OO_LINKER" ] || exit 1

echo "OO_PS4_TOOLCHAIN=$GITHUB_WORKSPACE/$(dirname "$OO_LINKER")" >> "$GITHUB_ENV"

echo "Downloading HLSDK..."

rm -rf hlsdk-portable
git clone --recursive https://github.com/FWGS/hlsdk-portable || exit 1
