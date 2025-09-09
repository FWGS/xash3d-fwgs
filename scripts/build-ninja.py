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

import argparse
import os
import shutil
import subprocess
import sys


def run_cmake(bin_path, libs, inst_path):
	cmake_exec = ["cmake", "--build", bin_path]
	cmake_process = subprocess.Popen(cmake_exec)
	cmake_process.communicate()

	if libs:
		for lib in libs:
			src = os.path.join(bin_path, *lib.split("/"))
			dest = os.path.join(inst_path, lib.split("/")[-1])

			dest_dir = os.path.dirname(dest)

			if not os.path.exists(dest_dir):
				os.makedirs(dest_dir)

			shutil.copyfile(src, dest)
	else:
		cmake_exec = ["cmake", "--install", bin_path, "--prefix", inst_path]
		cmake_process = subprocess.Popen(cmake_exec)
		cmake_process.communicate()

def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("cmd")
	parser.add_argument("top_dir")
	parser.add_argument("out_dir")
	parser.add_argument("waflock")
	parser.add_argument("--targets", type=str, default="")

	args = parser.parse_args()

	waf_path = os.path.join(args.top_dir, "waf")

	env = os.environ.copy()
	env["WAFLOCK"] = args.waflock

	waf_exec = [sys.executable, waf_path, args.cmd, "-t", args.top_dir]

	if args.targets:
		waf_exec += ["--targets={}".format(args.targets)]
	else:
		# build SDL2 and hlsdk-portable with cmake
		sdl_bin_path = os.path.join(args.out_dir, "SDL")
		hlsdk_bin_path = os.path.join(args.out_dir, "hlsdk-portable")

		abi = args.waflock.replace(".lock-waf_android_", "").replace("_build", "")
		inst_path = os.path.join(args.top_dir, "android", "app", "src", "main", "jniLibs", abi)

		if not os.path.exists(inst_path):
			os.makedirs(inst_path)

		run_cmake(sdl_bin_path, ["libSDL2.so"], inst_path)
		run_cmake(hlsdk_bin_path, None, inst_path)

	process = subprocess.Popen(waf_exec, env=env)
	process.communicate()

	return 0

if __name__ == "__main__":
	sys.exit(main())
