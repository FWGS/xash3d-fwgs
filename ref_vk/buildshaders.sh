#!/bin/bash

build() {
	NAME="$1"
	glslc -o "build-debug-amd64/lib/xash3d/valve/$NAME.spv" "ref_vk/shaders/$NAME"
}

build 2d.frag
build 2d.vert
