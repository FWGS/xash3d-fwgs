#!/bin/bash

set -eux

build() {
	NAME="$1"
	glslc -o "build-debug-amd64/lib/xash3d/valve/$NAME.spv" "ref_vk/shaders/$NAME"
}

for s in 2d.frag 2d.vert brush.vert brush.frag
do
	build "$s"
done
