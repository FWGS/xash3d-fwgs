#!/bin/sh
# One-step PS4 build: patches submodules, configures, builds and packages Xash3D FWGS.
#
# Usage: scripts/ps4/build.sh [PS4 IP address]
#   With IP address, uploads resulting .pkg to /data/pkg on the console through FTP (GoldHEN, port 2121).
#
# Environment:
#   OO_PS4_TOOLCHAIN - path to OpenOrbis PS4 toolchain (required)
#   WAF_CONFIGURE    - extra arguments for ./waf configure
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

if [ -z "$OO_PS4_TOOLCHAIN" ] || [ ! -f "$OO_PS4_TOOLCHAIN/link.x" ]; then
	echo "Set OO_PS4_TOOLCHAIN to OpenOrbis toolchain directory" >&2
	exit 1
fi

echo "=== Applying patches"
scripts/ps4/apply-patches.sh

# opengnm (GPU command buffers, used by the GNM backend of OpenGL renderers),
# pinned commit, not a submodule because upstream engine doesn't need it
OPENGNM_REPO=https://github.com/PS4-OpenGNM/opengnm
OPENGNM_COMMIT=4b295ca54c82c83acf308d1c646a2dfa9ae57350
if [ "$(git -C 3rdparty/opengnm rev-parse HEAD 2>/dev/null)" != "$OPENGNM_COMMIT" ]; then
	echo "=== Fetching opengnm"
	rm -rf 3rdparty/opengnm
	git init -q 3rdparty/opengnm
	git -C 3rdparty/opengnm fetch -q --depth 1 "$OPENGNM_REPO" "$OPENGNM_COMMIT"
	git -C 3rdparty/opengnm checkout -q FETCH_HEAD
fi

echo "=== Configuring"
./waf configure --ps4 -T release $WAF_CONFIGURE > build-ps4-configure.log 2>&1 || {
	tail -n 30 build-ps4-configure.log
	echo "Configure failed, full log: build-ps4-configure.log" >&2
	exit 1
}

echo "=== Building"
./waf build

echo "=== Packaging"
scripts/ps4/package.sh "$ROOT/build"

PKG=$(ls "$ROOT"/build/IV0000-*.pkg | head -n 1)

if [ -n "$1" ]; then
	echo "=== Uploading to $1"
	curl --ftp-create-dirs -T "$PKG" "ftp://$1:2121/data/pkg/"
	echo "Uploaded. Install it with Debug Settings -> Game -> Package Installer"
fi
