# encoding: utf-8
# xcompile.py -- crosscompiling utils
# Copyright (C) 2018 a1batross
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

from fwgslib import get_flags_by_compiler
import os
import sys

# Output:
#  CROSSCOMPILING -- set to true, if crosscompiling is enabled
#  DEST_OS2 -- as some operating systems is built on top of another, it's better to not change DEST_OS,
#              instead of this DEST_OS2 is defined with target value
#              For example: android is built on top of linux and have many things in common,
#              but it can't be considered as default GNU/Linux.
#              Possible values:
#                 DEST_OS2    DEST_OS
#                 'android'   'linux'

class Android:
	arch = None
	toolchain = None
	api = None
	toolchain_path = None
	ndk_home = None

	# TODO: Clang support?
	# TODO: New Android NDK support?
	# TODO: Crystax support?
	# TODO: Support for everything else than linux-x86_64?
	# TODO: Determine if I actually need to implement listed above
	def is_arm(self):
		return self.arch.startswith('armeabi')

	def gen_toolchain_path(self):
		path = 'toolchains/'
		if self.arch.startswith('x86'):
			path += self.arch + '-' + self.toolchain
		elif self.is_arm():
			path += 'arm-linux-androideabi-' + self.toolchain
		else:
			path += self.arch + '-linux-android-' + self.toolchain
		path += '/prebuilt/linux-x86_64/bin/'

		if self.arch == 'x86':
			path += 'i686-linux-android-'
		elif self.is_arm():
			path += 'arm-linux-androideabi-'
		else:
			path += self.arch + '-linux-android-'

		return path

	def cc(self):
		return os.path.abspath(os.path.join(self.ndk_home, self.toolchain_path + 'gcc'))

	def cxx(self):
		return os.path.abspath(os.path.join(self.ndk_home, self.toolchain_path + 'g++'))

	def link(self):
		return os.path.abspath(os.path.join(self.ndk_home, self.toolchain_path + 'ld'))

	def sysroot(self):
		arch = self.arch
		if self.is_arm():
			arch = 'arm'
		elif self.arch == 'aarch64':
			arch = 'arm64'
		path = 'platforms/android-{0}/arch-{1}'.format(self.api, arch)

		return os.path.abspath(os.path.join(self.ndk_home, path))

	def cflags(self):
		cflags = ['--sysroot={0}'.format(self.sysroot()), '-DANDROID']
		if self.is_arm():
			if self.arch.startswith('armeabi-v7a'):
				cflags += ['-march=armv7-a', '-mfpu=vfpv3-d16']
				if self.arch == 'armeabi-v7a-hard':
					cflags += ['-mhard-float', '-D_NDK_MATH_NO_SOFTFP=1']
				else:
					cflags += ['-mfloat-abi=softfp']
			else:
				cflags += ['-march=armv5te', '-mtune=xscale', '-msoft-float']
		return cflags

	def ldflags(self):
		ldflags = ['--sysroot={0}'.format(self.sysroot())]
		if self.is_arm():
			if self.arch.startswith('armeabi-v7a'):
				ldflags += ['-march=armv7-a', '-Wl,--fix-cortex-a8']
				if self.arch == 'armeabi-v7a-hard':
					ldflags += ['-Wl,--no-warn-mismatch', '-lm_hard']
			else:
				ldflags += ['-march=armv5te']
		return ldflags

	def __init__(self, ndk_home, arch, toolchain, api):
		self.ndk_home = ndk_home
		self.arch = arch
		self.toolchain = toolchain
		self.api = api
		self.toolchain_path = self.gen_toolchain_path()

def options(opt):
	android = opt.add_option_group('Android options')
	android.add_option('--android', action='store', dest='ANDROID_OPTS', default=None,
		help='enable building for android, format: --android=<arch>,<toolchain>,<api>, example: --android=arm,4.9,26')

def configure(conf):
	if conf.options.ANDROID_OPTS:
		android_ndk_path = os.getenv('ANDROID_NDK_HOME')
		if not android_ndk_path:
			conf.fatal('Set ANDROID_NDK_HOME environment variable pointing to the root of Android NDK!')

		values = conf.options.ANDROID_OPTS.split(',')
		if len(values) != 3:
			conf.fatal('Invalid --android paramater value!')

		android = Android(android_ndk_path, values[0], values[1], values[2])
		conf.options.ALLOW64 = True # skip pointer length check
		conf.options.NO_VGUI = True # skip vgui
		conf.environ['CC'] = android.cc()
		conf.environ['CXX'] = android.cxx()
		conf.environ['LD'] = android.link()
		conf.env.CFLAGS += android.cflags()
		conf.env.CXXFLAGS += android.cflags()
		conf.env.LINKFLAGS += android.ldflags()
		# conf.env.ANDROID_OPTS = android
		conf.env.DEST_OS2 = 'android'
#	else:
#		conf.load('compiler_c compiler_cxx') # Use host compiler :)
