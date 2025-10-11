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

ANDROID_NDK_ENVVARS = ['ANDROID_NDK_HOME', 'ANDROID_NDK']
ANDROID_NDK_SUPPORTED = [10, 19, 20, 23, 25, 27, 28]
ANDROID_NDK_HARDFP_MAX = 11 # latest version that supports hardfp
ANDROID_NDK_GCC_MAX = 17 # latest NDK that ships with GCC
ANDROID_NDK_UNIFIED_SYSROOT_MIN = 15
ANDROID_NDK_SYSROOT_FLAG_MAX = 19 # latest NDK that need --sysroot flag
ANDROID_NDK_BUGGED_LINKER_MAX = 22
ANDROID_NDK_API_MIN = {
	10: 3,
	19: 16,
	20: 16,
	23: 16,
	25: 19,
	27: 19,
	28: 21,
} # minimal API level ndk revision supports

ANDROID_STPCPY_API_MIN = 21 # stpcpy() introduced in SDK 21
ANDROID_64BIT_API_MIN = 21 # minimal API level that supports 64-bit targets

NSWITCH_ENVVARS = ['DEVKITPRO']

PSVITA_ENVVARS = ['VITASDK']

# This class does support ONLY r10e and r19c/r20 NDK
class Android:
	ctx            = None # waf context
	arch           = None
	toolchain      = None
	api            = None
	ndk_home       = None
	ndk_rev        = 0
	is_hardfloat   = False
	clang          = False

	def __init__(self, ctx, arch, toolchain, api):
		self.ctx = ctx
		self.api = api
		self.toolchain = toolchain
		self.arch = arch
		self.exe = '.exe' if sys.platform.startswith('win32') or sys.platform.startswith('cygwin') else ''

		for i in ANDROID_NDK_ENVVARS:
			self.ndk_home = os.getenv(i)
			if self.ndk_home != None:
				break
		else:
			ctx.fatal('Set %s environment variable pointing to the root of Android NDK!' %
				' or '.join(ANDROID_NDK_ENVVARS))

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

			if self.ndk_rev not in ANDROID_NDK_SUPPORTED:
				ctx.fatal('Unknown NDK revision: %d' % (self.ndk_rev))
		else:
			self.ndk_rev = ANDROID_NDK_SUPPORTED[0]

		if 'clang' in self.toolchain or self.ndk_rev > ANDROID_NDK_GCC_MAX:
			self.clang = True

		if self.arch == 'armeabi-v7a-hard':
			if self.ndk_rev <= ANDROID_NDK_HARDFP_MAX:
				self.arch = 'armeabi-v7a' # Only armeabi-v7a have hard float ABI
				self.is_hardfloat = True
			else:
				ctx.fatal('NDK does not support hardfloat ABI')

		if self.api < ANDROID_NDK_API_MIN[self.ndk_rev]:
			self.api = ANDROID_NDK_API_MIN[self.ndk_rev]
			Logs.warn('API level automatically was set to %d due to NDK support' % self.api)

		if self.is_arm64() or self.is_amd64() and self.api < ANDROID_64BIT_API_MIN:
			self.api = ANDROID_64BIT_API_MIN
			Logs.warn('API level for 64-bit target automatically was set to %d' % self.api)

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

	def apk_arch(self):
		if self.is_arm64():
			return 'arm64-v8a'
		return self.arch

	def gen_host_toolchain(self):
		# With host toolchain we don't care about OS
		# so just download NDK for Linux x86_64
		if 'HOST_TOOLCHAIN' in self.ctx.environ:
			return self.ctx.environ['HOST_TOOLCHAIN']
		if self.is_host():
			return 'linux-x86_64'

		if sys.platform.startswith('win32') or sys.platform.startswith('cygwin'):
			osname = 'windows'
		elif sys.platform.startswith('darwin'):
			osname = 'darwin'
		elif sys.platform.startswith('linux'):
			osname = 'linux'
		else:
			self.ctx.fatal('Unsupported by NDK host platform')

		if sys.maxsize > 2**32:
			arch = 'x86_64'
		else: arch = 'x86'

		return '%s-%s' % (osname, arch)

	def gen_gcc_toolchain_path(self):
		path = 'toolchains'
		toolchain_host = self.gen_host_toolchain()

		if self.is_clang():
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
			base = ''
		else:
			base = self.ndk_triplet() + '-'
		return os.path.join(self.gen_gcc_toolchain_path(), 'bin', base)

	def gen_binutils_path(self):
		if self.ndk_rev >= 23:
			return os.path.join(self.gen_gcc_toolchain_path(), 'bin')
		return os.path.join(self.gen_gcc_toolchain_path(), self.ndk_triplet(), 'bin')

	def cc(self):
		if self.is_host():
			s = 'clang'
			environ = getattr(self.ctx, 'environ', os.environ)

			if 'CC' in environ:
				s = environ['CC']

			return '%s --target=%s%d' % (s, self.ndk_triplet(), self.api)

		if self.is_clang():
			return '%s --target=%s%d' % (self.gen_toolchain_path() + 'clang' + self.exe, self.ndk_triplet(), self.api)

		return self.gen_toolchain_path() + 'gcc'

	def cxx(self):
		if self.is_host():
			s = 'clang++'
			environ = getattr(self.ctx, 'environ', os.environ)

			if 'CXX' in environ:
				s = environ['CXX']

			return '%s --target=%s%d' % (s, self.ndk_triplet(), self.api)

		if self.is_clang():
			return '%s --target=%s%d' % (self.gen_toolchain_path() + 'clang++' + self.exe, self.ndk_triplet(), self.api)

		return self.gen_toolchain_path() + 'g++'

	def strip(self):
		if self.is_host():
			environ = getattr(self.ctx, 'environ', os.environ)
			if 'STRIP' in environ:
				return environ['STRIP']
			return 'llvm-strip'

		if self.ndk_rev >= 23:
			return os.path.join(self.gen_binutils_path(), 'llvm-strip' + self.exe)
		return os.path.join(self.gen_binutils_path(), 'strip' + self.exe)

	def ar(self):
		if self.is_host():
			environ = getattr(self.ctx, 'environ', os.environ)
			if 'AR' in environ:
				return environ['AR']
			return 'llvm-ar'

		if self.ndk_rev >= 23:
			return os.path.join(self.gen_binutils_path(), 'llvm-ar' + self.exe)
		return os.path.join(self.gen_binutils_path(), 'ar' + self.exe)

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
		if self.ndk_rev >= ANDROID_NDK_UNIFIED_SYSROOT_MIN:
			return os.path.abspath(os.path.join(self.ndk_home, 'sysroot'))
		else:
			return self.libsysroot()

	def cflags(self, cxx = False):
		cflags = []

		if self.ndk_rev <= ANDROID_NDK_SYSROOT_FLAG_MAX:
			cflags += ['--sysroot=%s' % (self.sysroot())]
		else:
			if self.is_host():
				cflags += [
					'--sysroot=%s/sysroot' % (self.gen_gcc_toolchain_path()),
					'-isystem', '%s/usr/include/' % (self.sysroot())
				]

		cflags += ['-I%s' % (self.system_stl())]
		if not self.is_clang():
			cflags += ['-DANDROID', '-D__ANDROID__']

		if cxx and not self.is_clang() and self.toolchain not in ['4.8','4.9']:
			cflags += ['-fno-sized-deallocation']

		if self.is_clang():
			# stpcpy() isn't available in early Android versions
			# disable it here so Clang won't use it
			if self.api < ANDROID_STPCPY_API_MIN:
				cflags += ['-fno-builtin-stpcpy']

		if self.is_arm():
			if self.arch == 'armeabi-v7a':
				# ARMv7 support
				cflags += ['-mthumb', '-mfpu=neon', '-mcpu=cortex-a9']

				if self.is_hardfp():
					cflags += ['-D_NDK_MATH_NO_SOFTFP=1', '-mfloat-abi=hard', '-DLOAD_HARDFP', '-DSOFTFP_LINK']

					if self.is_host():
						# Clang builtin redefine w/ different calling convention bug
						# NOTE: I did not added complex.h functions here, despite
						# that NDK devs forgot to put __NDK_FPABI_MATH__ for complex
						# math functions
						# I personally don't need complex numbers support, but if you want it
						# just run sed to patch header
						for f in ['strtod', 'strtof', 'strtold']:
							cflags += ['-fno-builtin-%s' % f]
				else:
					cflags += ['-mfloat-abi=softfp']
			else:
				# ARMv5 support
				cflags += ['-march=armv5te', '-msoft-float']
		elif self.is_x86():
			cflags += ['-mtune=atom', '-march=atom', '-mssse3', '-mfpmath=sse']
		return cflags

	# they go before object list
	def linkflags(self):
		linkflags = []
		if self.is_host():
			linkflags += ['--gcc-toolchain=%s' % self.gen_gcc_toolchain_path()]

		if self.ndk_rev <= ANDROID_NDK_SYSROOT_FLAG_MAX:
			linkflags += ['--sysroot=%s' % (self.sysroot())]
		elif self.is_host():
			linkflags += ['--sysroot=%s/sysroot' % (self.gen_gcc_toolchain_path())]

		if self.is_clang() or self.is_host():
			linkflags += ['-fuse-ld=lld']
		else: linkflags += ['-no-canonical-prefixes']

		linkflags += ['-Wl,--hash-style=sysv', '-Wl,--no-undefined']

		linkflags += ["-Wl,-z,max-page-size=16384"]

		if self.ndk_rev <= ANDROID_NDK_BUGGED_LINKER_MAX:
			linkflags += ["-Wl,-z,common-page-size=16384"]

		return linkflags

	def ldflags(self):
		ldflags = []

		if self.ndk_rev < 23:
			ldflags += ['-lgcc']

		if self.is_clang() or self.is_host():
			ldflags += ['-stdlib=libstdc++', '-lc++abi']
		else: ldflags += ['-no-canonical-prefixes']

		if self.is_arm():
			if self.arch == 'armeabi-v7a':
				ldflags += ['-march=armv7-a', '-mthumb']

				if not self.is_clang() and not self.is_host(): # lld only
					ldflags += ['-Wl,--fix-cortex-a8']

				if self.is_hardfp():
					ldflags += ['-Wl,--no-warn-mismatch', '-lm_hard']
			else:
				ldflags += ['-march=armv5te']
		return ldflags

class NintendoSwitch:
	ctx          = None # waf context
	arch         = "arm64"
	dkp_dir      = None
	portlibs_dir = None
	dka64_dir    = None
	libnx_dir    = None

	def __init__(self, ctx):
		self.ctx = ctx

		for i in NSWITCH_ENVVARS:
			self.dkp_dir = os.getenv(i)
			if self.dkp_dir != None:
				break
		else:
			ctx.fatal('Set %s environment variable pointing to the DEVKITPRO home!' %
				' or '.join(NSWITCH_ENVVARS))

		self.dkp_dir = os.path.abspath(self.dkp_dir)

		self.dka64_dir = os.path.join(self.dkp_dir, 'devkitA64')
		if not os.path.exists(self.dka64_dir):
			ctx.fatal('devkitA64 not found in `%s`. Install devkitA64!' % self.dka64_dir)

		self.libnx_dir = os.path.join(self.dkp_dir, 'libnx')
		if not os.path.exists(self.libnx_dir):
			ctx.fatal('libnx not found in `%s`. Install libnx!' % self.libnx_dir)

		self.portlibs_dir = os.path.join(self.dkp_dir, 'portlibs', 'switch')
		if not os.path.exists(self.portlibs_dir):
			ctx.fatal('No Switch libraries found in `%s`!' % self.portlibs_dir)

	def gen_toolchain_prefix(self):
		return 'aarch64-none-elf-'

	def gen_gcc_toolchain_path(self):
		return os.path.join(self.dka64_dir, 'bin', self.gen_toolchain_prefix())

	def cc(self):
		return self.gen_gcc_toolchain_path() + 'gcc'

	def cxx(self):
		return self.gen_gcc_toolchain_path() + 'g++'

	def strip(self):
		return self.gen_gcc_toolchain_path() + 'strip'

	def pkgconfig(self):
		# counter-intuitively, this motherfucker is in $DEVKITPRO/portlibs/switch/bin
		return os.path.join(self.portlibs_dir, 'bin', self.gen_toolchain_prefix() + 'pkg-config')

	def cflags(self, cxx = False):
		cflags = []
		# arch flags
		cflags += ['-D__SWITCH__', '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft', '-ftls-model=local-exec', '-fPIE']
		# help the linker out
		cflags += ['-ffunction-sections', '-fdata-sections']
		# base include dirs
		cflags += ['-isystem %s/include' % self.libnx_dir, '-I%s/include' % self.portlibs_dir]
		# the game wants GNU extensions
		if cxx:
			cflags += ['-std=gnu++17', '-D_GNU_SOURCE']
		else:
			cflags += ['-std=gnu11', '-D_GNU_SOURCE']
		return cflags

	# they go before object list
	def linkflags(self):
		linkflags = ['-fPIE', '-specs=%s/switch.specs' % self.libnx_dir]
		# libsolder only supports sysv hashes and we need to build everything with -rdynamic
		linkflags += ['-Wl,--hash-style=sysv', '-rdynamic']
		# avoid pulling in and exposing mesa's internals, that crashes it for some god forsaken reason
		linkflags += ['-Wl,--exclude-libs=libglapi.a', '-Wl,--exclude-libs=libdrm_nouveau.a']
		return linkflags

	def ldflags(self):
		# NOTE: shared libraries should be built without standard libs, so that they could import their contents from the NRO,
		# but executables, including the SDL2 sanity check, will generally require libstdc++ and libm, which we will add manually
		ldflags = [] # ['-lm', '-lstdc++']
		return ldflags

class PSVita:
	ctx          = None # waf context
	arch         ='armeabi-v7a-hard'
	vitasdk_dir  = None

	def __init__(self, ctx):
		self.ctx = ctx

		for i in PSVITA_ENVVARS:
			self.vitasdk_dir = os.getenv(i)
			if self.vitasdk_dir != None:
				break
		else:
			ctx.fatal('Set %s environment variable pointing to the VitaSDK directory!' %
				' or '.join(PSVITA_ENVVARS))

	def gen_toolchain_prefix(self):
		return 'arm-vita-eabi-'

	def gen_gcc_toolchain_path(self):
		return os.path.join(self.vitasdk_dir, 'bin', self.gen_toolchain_prefix())

	def cc(self):
		return self.gen_gcc_toolchain_path() + 'gcc'

	def cxx(self):
		return self.gen_gcc_toolchain_path() + 'g++'

	def strip(self):
		return self.gen_gcc_toolchain_path() + 'strip'

	def ar(self):
		return self.gen_gcc_toolchain_path() + 'ar'

	def pkgconfig(self):
		return self.gen_gcc_toolchain_path() + 'pkg-config'

	def cflags(self, cxx = False):
		cflags = []
		# arch flags
		cflags += ['-D__vita__', '-mtune=cortex-a9', '-mfpu=neon']
		# necessary linker flags
		cflags += ['-Wl,-q', '-Wl,-z,nocopyreloc']
		# this optimization is broken in vitasdk
		cflags += ['-fno-optimize-sibling-calls']
		# disable some ARM bullshit
		cflags += ['-fno-short-enums', '-Wno-attributes']
		# base include dir
		cflags += ['-isystem %s/arm-vita-eabi/include' % self.vitasdk_dir]
		# SDL include dir
		cflags += ['-I%s/arm-vita-eabi/include/SDL2' % self.vitasdk_dir]
		return cflags

	# they go before object list
	def linkflags(self):
		linkflags = ['-Wl,--hash-style=sysv', '-Wl,-q', '-Wl,-z,nocopyreloc', '-mtune=cortex-a9', '-mfpu=neon']
		# enforce no-short-enums again
		linkflags += ['-Wl,-no-enum-size-warning', '-fno-short-enums']
		return linkflags

	def ldflags(self):
		ldflags = []
		return ldflags

def options(opt):
	xc = opt.add_option_group('Cross compile options')
	xc.add_option('--android', action='store', dest='ANDROID_OPTS', default=None,
		help='enable building for android, format: --android=<arch>,<toolchain>,<api>, example: --android=armeabi-v7a-hard,4.9,9')
	xc.add_option('--enable-magx', action='store_true', dest='MAGX', default=False,
		help='enable building for Motorola MAGX [default: %(default)s]')
	xc.add_option('--enable-msvc-wine', action='store_true', dest='MSVC_WINE', default=False,
		help='enable building with MSVC using Wine [default: %(default)s]')
	xc.add_option('--nswitch', action='store_true', dest='NSWITCH', default = False,
		help='enable building for Nintendo Switch [default: %(default)s]')
	xc.add_option('--psvita', action='store_true', dest='PSVITA', default = False,
		help='enable building for PlayStation Vita [default: %(default)s]')
	xc.add_option('--sailfish', action='store', dest='SAILFISH', default = None,
		help='enable building for Sailfish/Aurora')
	xc.add_option('--emscripten', action='store_true', dest='EMSCRIPTEN', default = None,
		help='enable building for Emscripten')

def configure(conf):
	if 'CROSS_COMPILE' in conf.environ:
		toolchain_path = conf.environ['CROSS_COMPILE']
		conf.environ['CC'] = toolchain_path + 'cc'
		conf.environ['CXX'] = toolchain_path + 'c++'
		conf.environ['STRIP'] = toolchain_path + 'strip'
		conf.environ['OBJDUMP'] = toolchain_path + 'objdump'
		conf.environ['AR'] = toolchain_path + 'ar'
	elif conf.options.ANDROID_OPTS:
		values = conf.options.ANDROID_OPTS.split(',')
		if len(values) != 3:
			conf.fatal('Invalid --android paramater value!')

		valid_archs = ['x86', 'x86_64', 'armeabi', 'armeabi-v7a', 'armeabi-v7a-hard', 'aarch64']

		if values[0] == 'arm64-v8a':
			values[0] = 'aarch64'

		if values[0] not in valid_archs:
			conf.fatal('Unknown arch: %s. Supported: %r' % (values[0], ', '.join(valid_archs)))

		conf.android = android = Android(conf, values[0], values[1], int(values[2]))

		conf.environ['CC'] = android.cc()
		conf.environ['CXX'] = android.cxx()
		conf.environ['STRIP'] = android.strip()
		conf.environ['AR'] = android.ar()
		conf.env.CFLAGS += android.cflags()
		conf.env.CXXFLAGS += android.cflags(True)
		conf.env.LINKFLAGS += android.linkflags()
		conf.env.LDFLAGS += android.ldflags()

		from waflib.Tools.compiler_c import c_compiler
		from waflib.Tools.compiler_cxx import cxx_compiler
		c_compiler['win32'] = ['clang' if android.is_clang() or android.is_host() else 'gcc']
		cxx_compiler['win32'] = ['clang++' if android.is_clang() or android.is_host() else 'gxx']

		conf.env.HAVE_M = True
		if android.is_hardfp():
			conf.env.LIB_M = ['m_hard']
		else: conf.env.LIB_M = ['m']

		conf.msg('Selected Android NDK', '%s, version: %d' % (android.ndk_home, android.ndk_rev))
		# no need to print C/C++ compiler, as it would be printed by compiler_c/cxx
		conf.msg('... C/C++ flags', ' '.join(android.cflags()).replace(android.ndk_home, '$NDK/'))
		conf.msg('... link flags', ' '.join(android.linkflags()).replace(android.ndk_home, '$NDK/'))
		conf.msg('... ld flags', ' '.join(android.ldflags()).replace(android.ndk_home, '$NDK/'))
	elif conf.options.MAGX:
		# useless to change toolchain path, as toolchain meant to be placed in this path
		toolchain_path = '/opt/toolchains/motomagx/arm-eabi2/lib/'
		conf.env.INCLUDES_MAGX = [toolchain_path + i for i in ['ezx-z6/include', 'qt-2.3.8/include']]
		conf.env.LIBPATH_MAGX  = [toolchain_path + i for i in ['ezx-z6/lib', 'qt-2.3.8/lib']]
		conf.env.LINKFLAGS_MAGX = ['-Wl,-rpath-link=' + i for i in conf.env.LIBPATH_MAGX]
	elif conf.options.MSVC_WINE:
		try:
			toolchain_path = conf.environ['MSVC_WINE_PATH']
		except KeyError:
			conf.fatal('Set MSVC_WINE_PATH environment variable to the MSVC toolchain root!')

		conf.environ['CC'] = conf.environ['CXX'] = os.path.join(toolchain_path, 'bin', conf.env.MSVC_TARGETS[0], 'cl')
		conf.environ['LINK_CXX'] = os.path.join(toolchain_path, 'bin', conf.env.MSVC_TARGETS[0], 'link')
		conf.environ['AR'] = os.path.join(toolchain_path, 'bin', conf.env.MSVC_TARGETS[0], 'lib')
		conf.environ['WINRC'] = os.path.join(toolchain_path, 'bin', conf.env.MSVC_TARGETS[0], 'rc')
		conf.env.DEST_OS = 'win32'
		conf.env.DEST_CPU = conf.env.MSVC_TARGETS[0]
		conf.env.COMPILER_CXX = conf.env.COMPILER_CC = 'msvc'
	elif conf.options.NSWITCH:
		conf.nswitch = nswitch = NintendoSwitch(conf)
		conf.environ['CC'] = nswitch.cc()
		conf.environ['CXX'] = nswitch.cxx()
		conf.environ['STRIP'] = nswitch.strip()
		conf.env.PKGCONFIG = nswitch.pkgconfig()
		conf.env.CFLAGS += nswitch.cflags()
		conf.env.CXXFLAGS += nswitch.cflags(True)
		conf.env.LINKFLAGS += nswitch.linkflags()
		conf.env.LDFLAGS += nswitch.ldflags()
		conf.env.HAVE_M = True
		conf.env.LIB_M = ['m']
		conf.env.DEST_OS = 'nswitch'
	elif conf.options.PSVITA:
		conf.psvita = psvita = PSVita(conf)
		conf.environ['CC'] = psvita.cc()
		conf.environ['CXX'] = psvita.cxx()
		conf.environ['STRIP'] = psvita.strip()
		conf.environ['AR'] = psvita.ar()
		conf.env.PKGCONFIG = psvita.pkgconfig()
		conf.env.CFLAGS += psvita.cflags()
		conf.env.CXXFLAGS += psvita.cflags(True)
		conf.env.LINKFLAGS += psvita.linkflags()
		conf.env.LDFLAGS += psvita.ldflags()
		conf.env.HAVE_M = True
		conf.env.LIB_M = ['m']
		conf.env.VRTLD = ['vrtld']
		conf.env.DEST_OS = 'psvita'
	elif conf.options.EMSCRIPTEN:
		# Emscripten compiler is just wrapper to clang
		# But we need to setup platform modifiers, they all are contained inside c_emscripten.py for now
		# In future, that could be upstreamed to waf itself and this wouldn't be needed
		conf.environ['CC'] = 'emcc'
		conf.environ['CXX'] = 'em++'
		conf.environ['AR'] = 'emar'
		conf.environ['STRIP'] = 'emstrip'
		conf.environ['OBJCOPY'] = 'llvm-objcopy'
		conf.load('c_emscripten')

	conf.env.MAGX = conf.options.MAGX
	conf.env.MSVC_WINE = conf.options.MSVC_WINE
	conf.env.SAILFISH = conf.options.SAILFISH
	MACRO_TO_DESTOS = OrderedDict({ '__ANDROID__' : 'android', '__SWITCH__' : 'nswitch', '__vita__' : 'psvita', '__wasi__': 'wasi', '__EMSCRIPTEN__' : 'emscripten' })
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
	elif conf.options.MAGX:
		for lib in ['qte-mt', 'ezxappbase', 'ezxpm', 'log_util']:
			conf.check_cc(lib=lib, use='MAGX', uselib_store='MAGX')

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
	if not conf.env.MSVC_WINE:
		compiler_cxx_configure(conf)
	else:
		conf.load('msvc', funs='no_autodetect')
	post_compiler_cxx_configure(conf)

def patch_compiler_c_configure(conf):
	if not conf.env.MSVC_WINE:
		compiler_c_configure(conf)
	else:
		conf.load('msvc', funs='no_autodetect')
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
