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
import subprocess

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
	arch           = None
	toolchain      = None
	api            = None
	toolchain_path = None
	ndk_home       = None
	is_hardfloat   = False

	# TODO: New Android NDK support?
	# TODO: Crystax support?
	# TODO: Support for everything else than linux-x86_64?
	# TODO: Determine if I actually need to implement listed above

	def is_arm(self):
		'''
		Checks if selected architecture is **32-bit** ARM
		'''
		return self.arch.startswith('armeabi')

	def is_x86(self):
		'''
		Checks if selected architecture is **32-bit** or **64-bit** x86
		'''
		return self.arch.startswith('x86')

	def is_arm64(self):
		'''
		Checks if selected architecture is AArch64
		'''
		return self.arch == 'aarch64'

	def is_clang(self):
		'''
		Checks if selected toolchain is Clang (TODO)
		'''
		return self.toolchain.startswith('clang')

	def is_hardfp(self):
		return self.is_hardfloat

	def gen_toolchain_path(self):
		path = 'toolchains'
		if self.is_clang():
			raise Exception('Clang is not supported yet')
		else:
			if self.is_x86():
				toolchain_folder = self.arch + '-' + self.toolchain
			elif self.is_arm():
				toolchain_folder = 'arm-linux-androideabi-' + self.toolchain
			else:
				toolchain_folder = self.arch + '-linux-android-' + self.toolchain

			if sys.platform.startswith('linux'):
				toolchain_host = 'linux'
			elif sys.platform.startswith('darwin'):
				toolchain_host = 'darwin'
			elif sys.platform.startswith('win32') or sys.platform.startswith('cygwin'):
				toolchain_host = 'windows'
			else: raise Exception('Unsupported by NDK host platform')

			toolchain_host += '-'

			# Assuming we are building on x86
			if sys.maxsize > 2**32: 
				toolchain_host += 'x86_64'
			else: toolchain_host += 'x86'

			if self.arch == 'x86':
				triplet = 'i686-linux-android-'
			elif self.is_arm():
				triplet = 'arm-linux-androideabi-'
			else:
				triplet = self.arch + '-linux-android-'

			return os.path.join(path, toolchain_folder, 'prebuilt', toolchain_host, 'bin', triplet)

	def cc(self):
		return os.path.abspath(os.path.join(self.ndk_home, self.toolchain_path + 'gcc'))

	def cxx(self):
		return os.path.abspath(os.path.join(self.ndk_home, self.toolchain_path + 'g++'))

	def system_stl(self):
		# TODO: proper STL support
		return os.path.abspath(os.path.join(self.ndk_home, 'sources', 'cxx-stl', 'system', 'include'))

	def sysroot(self):
		arch = self.arch
		if self.is_arm():
			arch = 'arm'
		elif self.is_arm64():
			arch = 'arm64'
		path = 'platforms/android-{0}/arch-{1}'.format(self.api, arch)

		return os.path.abspath(os.path.join(self.ndk_home, path))

	def cflags(self):
		cflags = ['--sysroot={0}'.format(self.sysroot()), '-DANDROID', '-D__ANDROID__']
		cflags += ['-I{0}'.format(self.system_stl())]
		if self.is_arm():
			if self.arch == 'armeabi-v7a':
				# ARMv7 support
				cflags += ['-mthumb', '-mfpu=neon', '-mcpu=cortex-a9', '-mvectorize-with-neon-quad', '-DHAVE_EFFICIENT_UNALIGNED_ACCESS', '-DVECTORIZE_SINCOS']
				if self.is_hardfloat:
					cflags += ['-D_NDK_MATH_NO_SOFTFP=1', '-mhard-float', '-mfloat-abi=hard', '-DLOAD_HARDFP', '-DSOFTFP_LINK']
				else:
					cflags += ['-mfloat-abi=softfp'] # Tegra 2 sucks
			else:
				# ARMv5 support
				cflags += ['-march=armv5te', '-mtune=xscale', '-msoft-float']
		elif self.is_x86():
			cflags += ['-mtune=atom', '-march=atom', '-mssse3', '-mfpmath=sse', '-DVECTORIZE_SINCOS', '-DHAVE_EFFICIENT_UNALIGNED_ACCESS']
		return cflags

	def ldflags(self):
		ldflags = ['--sysroot={0}'.format(self.sysroot())]
		if self.is_arm():
			if self.arch == 'armeabi-v7a':
				ldflags += ['-march=armv7-a', '-Wl,--fix-cortex-a8']
				if self.is_hardfloat:
					ldflags += ['-Wl,--no-warn-mismatch']
			else:
				ldflags += ['-march=armv5te']
		return ldflags

	def __init__(self, ndk_home, arch, toolchain, api):
		self.ndk_home = ndk_home
		self.arch = arch
		if self.arch == 'armeabi-v7a-hard':
			self.arch = 'armeabi-v7a' # Only armeabi-v7a have hard float ABI
			self.is_hardfloat = True
		self.toolchain = toolchain
		self.api = api
		self.toolchain_path = self.gen_toolchain_path()

def options(opt):
	android = opt.add_option_group('Android options')
	# ios = opt.add_option_group('iOS options')
	android.add_option('--android', action='store', dest='ANDROID_OPTS', default=None,
		help='enable building for android, format: --android=<arch>,<toolchain>,<api>, example: --android=armeabi-v7a-hard,4.9,9')
	opt.add_option('--ios', action='store_true', dest='IOS_OPTS', default=None)

def configure(conf):
	if conf.options.IOS_OPTS:
		environment = conf.env
		
		# Setup common defines for ios
		environment['DEFINES'] += [ 'APPLE', 'IOS', 'MOBILE', 'APPLE_BUNDLE', 'TARGET_OS_IPHONE', 'TARGET_OS_IOS' ]
		
		# Set Minimum ios version and the path to the current sdk
		conf.options.min_iphoneos_version = "12.2"
		sdk_path = subprocess.check_output(["xcrun", "--sdk", "iphoneos", "--show-sdk-path"]).strip()
		environment['CFLAGS'] += [ '-miphoneos-version-min=' + conf.options.min_iphoneos_version, '-isysroot' + sdk_path, '-Wno-shorten-64-to-32' ]
		environment['CXXFLAGS'] += [ '-miphoneos-version-min=' + conf.options.min_iphoneos_version, '-isysroot' + sdk_path, '-Wno-shorten-64-to-32' ]
		environment['MMFLAGS'] = environment['CXXFLAGS'] + ['-x', 'objective-c++']

		environment['LINKFLAGS'] += [ '-Wl,-dead_strip', '-isysroot' + sdk_path, '-miphoneos-version-min='+conf.options.min_iphoneos_version]
		environment['LIB'] += [ 'pthread' ]
		
		# For now, only support arm64
		environment['ARCH'] = ['arm64']
		
		# Pattern to transform outputs
		environment['cprogram_PATTERN']   = '%s'
		environment['cxxprogram_PATTERN'] = '%s'
		environment['cshlib_PATTERN']     = 'lib%s.dylib'
		environment['cxxshlib_PATTERN']   = 'lib%s.dylib'
		environment['cstlib_PATTERN']     = 'lib%s.a'
		environment['cxxstlib_PATTERN']   = 'lib%s.a'
		environment['macbundle_PATTERN']  = 'lib%s.dylib'
		
		# Specify how to translate some common operations for a specific compiler   
		environment['FRAMEWORK_ST']     = ['-framework']
		environment['FRAMEWORKPATH_ST'] = '-F%s'
		
		# Default frameworks to always link
		environment['FRAMEWORK'] = [ 'Foundation', 'UIKit', 'QuartzCore', 'GameController', 'CoreMotion', 'SystemConfiguration', 'CoreFoundation', 'CFNetwork', 'AVFoundation' ]

		# Setup compiler and linker settings for mac bundles
		environment['CFLAGS_MACBUNDLE'] = environment['CXXFLAGS_MACBUNDLE'] = '-fpic'
		environment['LINKFLAGS_MACBUNDLE'] = [
			'-bundle', 
			'-undefined', 
			'dynamic_lookup'
			]
		
		# Since we only support a single ios target (clang-64bit), we specify all tools directly here    
		environment['AR'] = 'ar'
		environment['CC'] = 'clang'
		environment['CXX'] = 'clang++'
		environment['LINK'] = environment['LINK_CC'] = environment['LINK_CXX'] = 'clang++'
		environment.DEST_OS2 = 'ios'
		conf.env = environment
	elif conf.options.ANDROID_OPTS:
		for i in ['ANDROID_NDK_HOME', 'ANDROID_NDK']:
			android_ndk_path = os.getenv(i)
			if android_ndk_path != None:
				break

		if not android_ndk_path:
			conf.fatal('Set ANDROID_NDK_HOME environment variable pointing to the root of Android NDK!')		

		values = conf.options.ANDROID_OPTS.split(',')
		if len(values) != 3:
			conf.fatal('Invalid --android paramater value!')

		valid_archs = ['x86', 'x86_64', 'armeabi', 'armeabi-v7a', 'armeabi-v7a-hard', 'aarch64', 'mipsel', 'mips64el']

		if values[0] not in valid_archs:
			conf.fatal('Unknown arch: {0}. Supported: {1}'.format(values[0], ', '.join(valid_archs)))

		android = Android(android_ndk_path, values[0], values[1], values[2])
		conf.environ['CC'] = android.cc()
		conf.environ['CXX'] = android.cxx()
		conf.env.CFLAGS += android.cflags()
		conf.env.CXXFLAGS += android.cflags()
		conf.env.LINKFLAGS += android.ldflags()

		conf.env.HAVE_M = True
		if android.is_hardfp():
			conf.env.LIB_M = ['m_hard']
		else: conf.env.LIB_M = ['m']

		conf.env.PREFIX = '/lib/{0}'.format(android.arch)

		conf.msg('Selected Android NDK', android_ndk_path)
		# no need to print C/C++ compiler, as it would be printed by compiler_c/cxx
		conf.msg('... C/C++ flags', ' '.join(android.cflags()).replace(android_ndk_path, '$NDK'))
		conf.msg('... linker flags', ' '.join(android.ldflags()).replace(android_ndk_path, '$NDK'))

		# conf.env.ANDROID_OPTS = android
		conf.env.DEST_OS2 = 'android'
#	else:
#		conf.load('compiler_c compiler_cxx') # Use host compiler :)

