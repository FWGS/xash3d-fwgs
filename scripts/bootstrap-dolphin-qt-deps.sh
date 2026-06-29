#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps="$root/3rdparty/dolphin/.deps/apt"
mkdir -p "$deps"
cd "$deps"

fetch_and_extract() {
	local package="$1"
	if [[ ! -f "${package}_"*.deb ]]; then
		apt download "$package"
	fi
	local deb
	deb="$(ls -1 "${package}_"*.deb | tail -1)"
	rm -rf "$package"
	mkdir -p "$package"
	dpkg-deb -x "$deb" "$package"
}

fetch_and_extract qt6-svg-dev
fetch_and_extract qt6-base-private-dev

libdir="$deps/qt6-svg-dev/usr/lib/x86_64-linux-gnu"
mkdir -p "$libdir"
for lib in libQt6Svg.so.6.10.2 libQt6Svg.so.6 libQt6SvgWidgets.so.6.10.2 libQt6SvgWidgets.so.6; do
	if [[ -e "/usr/lib/x86_64-linux-gnu/$lib" && ! -e "$libdir/$lib" ]]; then
		ln -sf "/usr/lib/x86_64-linux-gnu/$lib" "$libdir/$lib"
	fi
done

echo "Dolphin Qt deps ready under $deps"
