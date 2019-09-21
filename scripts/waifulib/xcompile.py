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

try: from fwgslib import get_flags_by_compiler
except: from waflib.extras.fwgslib import get_flags_by_compiler
from waflib import Logs, TaskGen
from waflib.Tools import c_config
from collections import OrderedDict
import os
import sys

# This class does support ONLY r10e and r19c/r20 NDK
class Android:
	ctx            = None # waf context
	arch           = None
	toolchain      = None
	api            = None
	toolchain_path = None
	ndk_home       = None
	ndk_rev        = 0
	is_hardfloat   = False
	clang          = False

	def __init__(self, ctx, arch, toolchain, api):
		self.ctx = ctx
		self.api = api
		self.toolchain = toolchain
		self.arch = arch

		for i in ['ANDROID_NDK_HOME', 'ANDROID_NDK']:
			self.ndk_home = os.getenv(i)
			if self.ndk_home != None:
				break

		if not self.ndk_home:
			ctx.fatal('Set ANDROID_NDK_HOME environment variable pointing to the root of Android NDK!')

		# TODO: this were added at some point of NDK development
		# but I don't know at which version
		# r10e don't have it
		source_prop = os.path.join(self.ndk_home, 'source.properties')
		if os.path.exists(source_prop):
			with open(source_prop) as ndk_props_file:
				for line in ndk_props_file.readlines():
					tokens = line.split('=')
					trimed_tokens = [token.strip() for token in tokens]

					if 'Pkg.Revision' in trimed_tokens:
						self.ndk_rev = int(trimed_tokens[1].split('.')[0])
		else:
			self.ndk_rev = 10

		if self.ndk_rev not in [10, 19, 20]:
			ctx.fatal('Unknown NDK revision: %d' % (self.ndk_rev))

		if self.is_host() and self.ndk_rev < 20:
			ctx.fatal('Using host toolchain with this NDK revision is untested. You can comment this check, but you\'re on your own!')

		if self.ndk_rev >= 19 or 'clang' in self.toolchain:
			self.clang = True

		if self.arch == 'armeabi-v7a-hard':
			if self.ndk_rev <= 10:
				self.arch = 'armeabi-v7a' # Only armeabi-v7a have hard float ABI
				self.is_hardfloat = True
			else:
				ctx.fatal('NDK does not support hardfloat ABI')

		if self.is_arm64() or self.is_amd64() and self.api < 21:
			Logs.warn('API level for 64-bit target automatically was set to 21')
			self.api = 21
		elif self.ndk_rev >= 19 and self.api < 16:
			Logs.warn('API level automatically was set to 16 due to NDK support')
			self.api = 16
		self.toolchain_path = self.gen_toolchain_path()

	def is_host(self):
		'''
		Checks if we using host compiler(implies clang)
		'''
		return self.toolchain == 'host'

	def is_arm(self):
		'''
		Checks if selected architecture is **32-bit** ARM
		'''
		return self.arch.startswith('armeabi')

	def is_x86(self):
		'''
		Checks if selected architecture is **32-bit** or **64-bit** x86
		'''
		return self.arch == 'x86'

	def is_amd64(self):
		'''
		Checks if selected architecture is **64-bit** x86
		'''
		return self.arch == 'x86_64'

	def is_arm64(self):
		'''
		Checks if selected architecture is AArch64
		'''
		return self.arch == 'aarch64'

	def is_clang(self):
		'''
		Checks if selected toolchain is Clang (TODO)
		'''
		return self.clang

	def is_hardfp(self):
		return self.is_hardfloat

	def ndk_triplet(self, llvm_toolchain = False, toolchain_folder = False):
		if self.is_x86():
			if toolchain_folder:
				return 'x86'
			else:
				return 'i686-linux-android'
		elif self.is_arm():
			if llvm_toolchain:
				return 'armv7a-linux-androideabi'
			else:
				return 'arm-linux-androideabi'
		elif self.is_amd64() and toolchain_folder:
			return 'x86_64'
		else:
			return self.arch + '-linux-android'

	def gen_gcc_toolchain_path(self):
		path = 'toolchains'

		if sys.platform.startswith('win32') or sys.platform.startswith('cygwin'):
			toolchain_host = 'windows'
		elif sys.platform.startswith('darwin'):
			toolchain_host = 'darwin'
		elif sys.platform.startswith('linux') or self.is_host():
			toolchain_host = 'linux'
		else:
			self.ctx.fatal('Unsupported by NDK host platform')

		toolchain_host += '-'

		# Assuming we are building on x86
		if sys.maxsize > 2**32:
			toolchain_host += 'x86_64'
		else: toolchain_host += 'x86'

		if self.is_clang():
			if self.ndk_rev < 19:
				raise self.ctx.fatal('Clang is not supported for this NDK')

			toolchain_folder = 'llvm'
		else:
			if self.is_host():
				toolchain = '4.9'
			else:
				toolchain = self.toolchain

			toolchain_folder = '%s-%s' % (self.ndk_triplet(toolchain_folder = True), toolchain)

		return os.path.abspath(os.path.join(self.ndk_home, path, toolchain_folder, 'prebuilt', toolchain_host))

	def gen_toolchain_path(self):
		if self.is_clang():
			triplet = '%s%d-' % (self.ndk_triplet(llvm_toolchain = True), self.api)
		else:
			triplet = self.ndk_triplet() + '-'
		return os.path.join(self.gen_gcc_toolchain_path(), 'bin', triplet)

	def gen_binutils_path(self):
		return os.path.join(self.gen_gcc_toolchain_path(), self.ndk_triplet(), 'bin')

	def cc(self):
		if self.is_host():
			return 'clang --target=%s%d' % (self.ndk_triplet(), self.api)
		return self.toolchain_path + ('clang' if self.is_clang() else 'gcc')

	def cxx(self):
		if self.is_host():
			return 'clang++ --target=%s%d' % (self.ndk_triplet(), self.api)
		return self.toolchain_path + ('clang++' if self.is_clang() else 'g++')

	def strip(self):
		if self.is_host():
			return 'llvm-strip'
		return os.path.join(self.gen_binutils_path(), 'strip')

	def system_stl(self):
		# TODO: proper STL support
		return os.path.abspath(os.path.join(self.ndk_home, 'sources', 'cxx-stl', 'system', 'include'))

	def libsysroot(self):
		arch = self.arch
		if self.is_arm():
			arch = 'arm'
		elif self.is_arm64():
			arch = 'arm64'
		path = 'platforms/android-%s/arch-%s' % (self.api, arch)

		return os.path.abspath(os.path.join(self.ndk_home, path))

	def sysroot(self):
		if self.ndk_rev >= 19:
			return os.path.abspath(os.path.join(self.ndk_home, 'sysroot'))
		else:
			return self.libsysroot()

	def cflags(self):
		cflags = []
		if self.is_host():
			if self.ndk_rev >= 19:
				cflags += [
					'--sysroot=%s/sysroot' % (self.gen_gcc_toolchain_path()),
					'-I%s/usr/include/' % (self.sysroot())
				]
			else: cflags += ['--sysroot=%s' % (self.sysroot())]
		elif self.ndk_rev < 20:
			cflags += ['--sysroot=%s' % (self.sysroot())]

		cflags += ['-I%s' % (self.system_stl()), '-DANDROID', '-D__ANDROID__']
		if self.is_arm():
			if self.arch == 'armeabi-v7a':
				# ARMv7 support
				cflags += ['-mthumb', '-mfpu=neon', '-mcpu=cortex-a9', '-DHAVE_EFFICIENT_UNALIGNED_ACCESS', '-DVECTORIZE_SINCOS']

				if not self.is_clang() and not self.is_host():
					cflags += [ '-mvectorize-with-neon-quad' ]

				if self.is_hardfloat:
					cflags += ['-D_NDK_MATH_NO_SOFTFP=1', '-mhard-float', '-mfloat-abi=hard', '-DLOAD_HARDFP', '-DSOFTFP_LINK']
				else:
					cflags += ['-mfloat-abi=softfp']
			else:
				# ARMv5 support
				cflags += ['-march=armv5te', '-mtune=xscale', '-msoft-float']
		elif self.is_x86():
			cflags += ['-mtune=atom', '-march=atom', '-mssse3', '-mfpmath=sse', '-DVECTORIZE_SINCOS', '-DHAVE_EFFICIENT_UNALIGNED_ACCESS']
		return cflags

	# they go before object list
	def linkflags(self):
		linkflags = []
		if self.is_host():
			linkflags += ['--gcc-toolchain=%s' % self.gen_gcc_toolchain_path()]

		if self.ndk_rev < 20:
			linkflags += ['--sysroot=%s' % (self.sysroot())]
		elif self.is_host():
			linkflags += ['--sysroot=%s/sysroot' % (self.gen_gcc_toolchain_path())]

		if self.is_clang() or self.is_host():
			linkflags += ['-fuse-ld=lld']

		linkflags += ['-Wl,--hash-style=both']
		return linkflags

	def ldflags(self):
		ldflags = ['-stdlib=libstdc++', '-lgcc', '-no-canonical-prefixes']
		if self.is_arm():
			if self.arch == 'armeabi-v7a':
				ldflags += ['-march=armv7-a', '-mthumb']
				if not self.is_clang() and not self.is_host(): # lld only
					ldflags += ['-Wl,--fix-cortex-a8']
				if self.is_hardfloat:
					ldflags += ['-Wl,--no-warn-mismatch', '-lm_hard']
			else:
				ldflags += ['-march=armv5te']
		return ldflags

def options(opt):
	android = opt.add_option_group('Android options')
	android.add_option('--android', action='store', dest='ANDROID_OPTS', default=None,
		help='enable building for android, format: --android=<arch>,<toolchain>,<api>, example: --android=armeabi-v7a-hard,4.9,9')

def configure(conf):
	if conf.options.ANDROID_OPTS:
		values = conf.options.ANDROID_OPTS.split(',')
		if len(values) != 3:
			conf.fatal('Invalid --android paramater value!')

		valid_archs = ['x86', 'x86_64', 'armeabi', 'armeabi-v7a', 'armeabi-v7a-hard', 'aarch64']

		if values[0] not in valid_archs:
			conf.fatal('Unknown arch: %s. Supported: %r' % (values[0], ', '.join(valid_archs)))

		conf.android = android = Android(conf, values[0], values[1], int(values[2]))
		conf.environ['CC'] = android.cc()
		conf.environ['CXX'] = android.cxx()
		conf.environ['STRIP'] = android.strip()
		conf.env.CFLAGS += android.cflags()
		conf.env.CXXFLAGS += android.cflags()
		conf.env.LINKFLAGS += android.linkflags()
		conf.env.LDFLAGS += android.ldflags()

		conf.env.HAVE_M = True
		if android.is_hardfp():
			conf.env.LIB_M = ['m_hard']
		else: conf.env.LIB_M = ['m']

		conf.env.PREFIX = '/lib/%s' % (android.arch)

		conf.msg('Selected Android NDK', '%s, version: %d' % (android.ndk_home, android.ndk_rev))
		# no need to print C/C++ compiler, as it would be printed by compiler_c/cxx
		conf.msg('... C/C++ flags', ' '.join(android.cflags()).replace(android.ndk_home, '$NDK'))
		conf.msg('... link flags', ' '.join(android.linkflags()).replace(android.ndk_home, '$NDK'))
		conf.msg('... ld flags', ' '.join(android.ldflags()).replace(android.ndk_home, '$NDK'))

		# conf.env.ANDROID_OPTS = android
		conf.env.DEST_OS2 = 'android'

	MACRO_TO_DESTOS = OrderedDict({ '__ANDROID__' : 'android' })
	for k in c_config.MACRO_TO_DESTOS:
		MACRO_TO_DESTOS[k] = c_config.MACRO_TO_DESTOS[k] # ordering is important
	c_config.MACRO_TO_DESTOS  = MACRO_TO_DESTOS

def post_compiler_cxx_configure(conf):
	conf.msg('Target OS', conf.env.DEST_OS)
	conf.msg('Target CPU', conf.env.DEST_CPU)
	conf.msg('Target binfmt', conf.env.DEST_BINFMT)

	if conf.options.ANDROID_OPTS:
		if conf.android.ndk_rev == 19:
			conf.env.CXXFLAGS_cxxshlib += ['-static-libstdc++']
			conf.env.LDFLAGS_cxxshlib += ['-static-libstdc++']
	return

def post_compiler_c_configure(conf):
	conf.msg('Target OS', conf.env.DEST_OS)
	conf.msg('Target CPU', conf.env.DEST_CPU)
	conf.msg('Target binfmt', conf.env.DEST_BINFMT)

	return

from waflib.Tools import compiler_cxx, compiler_c

compiler_cxx_configure = getattr(compiler_cxx, 'configure')
compiler_c_configure = getattr(compiler_c, 'configure')

def patch_compiler_cxx_configure(conf):
	compiler_cxx_configure(conf)
	post_compiler_cxx_configure(conf)

def patch_compiler_c_configure(conf):
	compiler_c_configure(conf)
	post_compiler_c_configure(conf)

setattr(compiler_cxx, 'configure', patch_compiler_cxx_configure)
setattr(compiler_c, 'configure', patch_compiler_c_configure)

@TaskGen.feature('cshlib', 'cxxshlib', 'dshlib', 'fcshlib', 'vnum')
@TaskGen.after_method('apply_link', 'propagate_uselib_vars')
@TaskGen.before_method('apply_vnum')
def apply_android_soname(self):
	"""
	Enforce SONAME on Android
	"""
	if self.env.DEST_OS != 'android':
		return

	setattr(self, 'vnum', None) # remove vnum, so SONAME would not be overwritten
	link = self.link_task
	node = link.outputs[0]
	libname = node.name
	v = self.env.SONAME_ST % libname
	self.env.append_value('LINKFLAGS', v.split())
