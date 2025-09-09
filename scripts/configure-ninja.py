#!/usr/bin/env python
# encoding: utf-8
# Copyright (C) 2025 Velaron
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

from __future__ import print_function

import argparse
import os
import subprocess
import sys
import io


def check_repo(name, branch, url, path):
	if not os.path.exists(path):
		print("{} not found. Cloning...".format(name))
		git_exec = ["git", "clone", "--branch", branch, url, path]
		git_process = subprocess.Popen(git_exec)
		git_process.communicate()


def run_cmake(root, out, toolchain, abi, build_type, ndk_root, min_sdk, *args):
	cmake_exec = ["cmake", "-H{}".format(root), "-DCMAKE_BUILD_TYPE={}".format(build_type),
				  "-DCMAKE_TOOLCHAIN_FILE={}".format(toolchain), "-DANDROID_ABI={}".format(abi),
				  "-DANDROID_NDK={}".format(ndk_root),
				  "-DANDROID_PLATFORM=android-{}".format(min_sdk),
				  "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
				  "-DCMAKE_SYSTEM_NAME=Android", "-DCMAKE_SYSTEM_VERSION={}".format(min_sdk),
				  "-B{}".format(out), "-GNinja"]

	cmake_exec.extend(args)
	cmake_process = subprocess.Popen(cmake_exec)
	cmake_process.communicate()


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("wscript_path")
	parser.add_argument("--variant")
	parser.add_argument("--abi")
	parser.add_argument("--configuration-dir")
	parser.add_argument("--ndk-version")
	parser.add_argument("--min-sdk-version")
	parser.add_argument("--ndk-root")

	args, unknown = parser.parse_known_args()

	abi = args.abi

	cmake_build_type = "Debug" if args.variant in ["debug", "asan"] else "Release"
	cmake_toolchain_path = os.path.join(args.ndk_root, "build", "cmake", "android.toolchain.cmake")

	# configure SDL2
	sdl_path = os.path.join(args.wscript_path, "3rdparty", "SDL")
	check_repo("SDL", "release-2.32.8", "https://github.com/libsdl-org/SDL", sdl_path)

	sdl_out_path = os.path.join(args.configuration_dir, "SDL")

	run_cmake(sdl_path, sdl_out_path, cmake_toolchain_path, abi, cmake_build_type, args.ndk_root, args.min_sdk_version,
			  "-DSDL_RENDER=OFF", "-DSDL_POWER=OFF", "-DSDL_VULKAN=OFF", "-DSDL_DISKAUDIO=OFF",
			  "-DSDL_DUMMYAUDIO=OFF", "-DSDL_DUMMYVIDEO=OFF",
			  "-DSDL_VULKAN=OFF", "-DSDL_OFFSCREEN=OFF", "-DSDL_STATIC=OFF")

	# configure hlsdk-portable
	hlsdk_path = os.path.join(args.wscript_path, "3rdparty", "hlsdk-portable")
	check_repo("hlsdk-portable", "mobile_hacks", "https://github.com/FWGS/hlsdk-portable", hlsdk_path)

	hlsdk_out_path = os.path.join(args.configuration_dir, "hlsdk-portable")

	run_cmake(hlsdk_path, hlsdk_out_path, cmake_toolchain_path, abi, cmake_build_type, args.ndk_root,
			  args.min_sdk_version, "-DANDROID_APK=ON")

	# waf configure
	waf_path = os.path.join(args.wscript_path, "waf")
	out_path = os.path.join(args.configuration_dir, "xash3d-fwgs")

	waf_build_type = "debug" if args.variant in ["debug", "asan"] else "release"

	env = os.environ.copy()
	env["WAFLOCK"] = ".lock-waf_android_{}_build".format(abi)
	env["ANDROID_NDK"] = args.ndk_root
	env["BUILD_CMAKE_LIBRARY_OUTPUT_DIRECTORY"] = sdl_out_path

	waf_exec = [sys.executable, waf_path, "configure", "-t", args.wscript_path, "-o", out_path,
				"-T", waf_build_type, "--android={},,{}".format(abi, args.min_sdk_version), "-s",
				sdl_path, "--skip-sdl2-sanity-check", "--enable-bundled-deps", "ninja"]

	process = subprocess.Popen(waf_exec, env=env)
	process.communicate()

	with io.open(os.path.join(args.configuration_dir, "build.ninja.txt"), "w", encoding="utf-8") as f:
		f.write(os.path.join(out_path, "build.ninja"))

	# required for Android Studio
	return 0


if __name__ == "__main__":
	sys.exit(main())
