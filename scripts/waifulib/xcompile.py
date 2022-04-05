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
ANDROID_NDK_SUPPORTED = [10, 19, 20]
ANDROID_NDK_HARDFP_MAX = 11 # latest version that supports hardfp
ANDROID_NDK_GCC_MAX = 17 # latest NDK that ships with GCC
ANDROID_NDK_UNIFIED_SYSROOT_MIN = 15
ANDROID_NDK_SYSROOT_FLAG_MAX = 19 # latest NDK that need --sysroot flag
ANDROID_NDK_API_MIN = { 10: 3, 19: 16, 20: 16 } # minimal API level ndk revision supports
ANDROID_64BIT_API_MIN = 21 # minimal API level that supports 64-bit targets

PSP_SDK_ENVVARS = ['PSPDEV', 'PSPSDK', 'PSPTOOLCHAIN']

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
			triplet = '%s%d-' % (self.ndk_triplet(llvm_toolchain = True), self.api)
		else:
			triplet = self.ndk_triplet() + '-'
		return os.path.join(self.gen_gcc_toolchain_path(), 'bin', triplet)

	def gen_binutils_path(self):
		return os.path.join(self.gen_gcc_toolchain_path(), self.ndk_triplet(), 'bin')

	def cc(self):
		if self.is_host():
			return 'clang --target=%s%d' % (self.ndk_triplet(), self.api)
		return self.gen_toolchain_path() + ('clang' if self.is_clang() else 'gcc')

	def cxx(self):
		if self.is_host():
			return 'clang++ --target=%s%d' % (self.ndk_triplet(), self.api)
		return self.gen_toolchain_path() + ('clang++' if self.is_clang() else 'g++')

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

		cflags += ['-I%s' % (self.system_stl()), '-DANDROID', '-D__ANDROID__']

		if cxx and not self.is_clang() and self.toolchain not in ['4.8','4.9']:
			cflags += ['-fno-sized-deallocation']

		def fixup_host_clang_with_old_ndk():
			cflags = []
			# Clang builtin redefine w/ different calling convention bug
			# NOTE: I did not added complex.h functions here, despite
			# that NDK devs forgot to put __NDK_FPABI_MATH__ for complex
			# math functions
			# I personally don't need complex numbers support, but if you want it
			# just run sed to patch header
			for f in ['strtod', 'strtof', 'strtold']:
				cflags += ['-fno-builtin-%s' % f]
			return cflags


		if self.is_arm():
			if self.arch == 'armeabi-v7a':
				# ARMv7 support
				cflags += ['-mthumb', '-mfpu=neon', '-mcpu=cortex-a9', '-DHAVE_EFFICIENT_UNALIGNED_ACCESS', '-DVECTORIZE_SINCOS']

				if not self.is_clang() and not self.is_host():
					cflags += [ '-mvectorize-with-neon-quad' ]

				if self.is_host() and self.ndk_rev <= ANDROID_NDK_HARDFP_MAX:
					cflags += fixup_host_clang_with_old_ndk()

				if self.is_hardfp():
					cflags += ['-D_NDK_MATH_NO_SOFTFP=1', '-mfloat-abi=hard', '-DLOAD_HARDFP', '-DSOFTFP_LINK']
				else:
					cflags += ['-mfloat-abi=softfp']
			else:
				if self.is_host() and self.ndk_rev <= ANDROID_NDK_HARDFP_MAX:
					cflags += fixup_host_clang_with_old_ndk()

				# ARMv5 support
				cflags += ['-march=armv5te', '-msoft-float']
		elif self.is_x86():
			cflags += ['-mtune=atom', '-march=atom', '-mssse3', '-mfpmath=sse', '-DVECTORIZE_SINCOS', '-DHAVE_EFFICIENT_UNALIGNED_ACCESS']
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

		linkflags += ['-Wl,--hash-style=sysv', '-Wl,--no-undefined', '-no-canonical-prefixes']
		return linkflags

	def ldflags(self):
		ldflags = ['-lgcc', '-no-canonical-prefixes']
		if self.is_clang() or self.is_host():
			ldflags += ['-stdlib=libstdc++']
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



class PSP:
	ctx               = None # waf context
	sdk_home          = None
	psptoolchain_path = None
	pspsdk_path       = None
	binutils_path     = None
	module_type       = None
	fw_version        = None
	render_type       = None

	def __init__(self, ctx, moduletype, fwversion, rendertype):
		self.ctx = ctx
		self.module_type = moduletype
		self.fw_version = fwversion
		self.render_type = rendertype

		for i in PSP_SDK_ENVVARS:
			self.sdk_home = os.getenv(i)
			if self.sdk_home != None:
				break
		else:
			ctx.fatal('Set %s environment variable pointing to the root of PSP SDK!' %
				' or '.join(PSP_SDK_ENVVARS))

		self.psptoolchain_path = os.path.join(self.sdk_home, 'psp')
		self.pspsdk_path = os.path.join(self.psptoolchain_path, 'sdk')
		self.binutils_path  = os.path.join(self.sdk_home, 'bin')

	def cflags(self, cxx = False):
		cflags = []
		cflags += ['-I%s' % (os.path.join(self.pspsdk_path, 'include'))]
		cflags += ['-I.']
		cflags += ['-DNDEBUG', '-D_PSP_FW_VERSION=%s' % self.fw_version, '-G0']
		return cflags
	# they go before object list
	def linkflags(self):
		linkflags = []
#		if self.module_type == 'prx':
#			linkflags += ['-specs=%s' % os.path.join(self.pspsdk_path, 'lib/prxspecs')]
#			linkflags += ['-Wl,-q,-T%s' % os.path.join(self.pspsdk_path, 'lib/linkfile.prx')]
#		linkflags += ['-Wl,--hash-style=sysv', '-Wl,--no-undefined', '-no-canonical-prefixes']
		return linkflags

	def ldflags(self):
		ldflags = []
		if self.module_type == 'prx':
			ldflags += ['-specs=%s' % os.path.join(self.pspsdk_path, 'lib/prxspecs')]
			ldflags += ['-Wl,-q,-T%s' % os.path.join(self.pspsdk_path, 'lib/linkfile.prx')]
		ldflags += ['-L%s' % os.path.join(self.pspsdk_path, 'lib')]
		ldflags += ['-L.']
		return ldflags

	def stdlibs(self):
		stdlibs = []
		stdlibs += ['-lpspdisplay', '-lpspgum_vfpu', '-lpspgu','-lpspge', '-lpspvfpu']
		stdlibs += ['-lpspaudiolib', '-lpspaudio']	
		stdlibs += ['-lstdc++', '-lc', '-lm']
		stdlibs += ['-lpspctrl', '-lpspdebug', '-lpsppower', '-lpsputility',  '-lpspsdk', '-lpsprtc']
		stdlibs += ['-lpspuser', '-lpspkernel']
		return stdlibs

def options(opt):
	android = opt.add_option_group('Android options')
	android.add_option('--android', action='store', dest='ANDROID_OPTS', default=None,
		help='enable building for android, format: --android=<arch>,<toolchain>,<api>, example: --android=armeabi-v7a-hard,4.9,9')

	psp = opt.add_option_group('Sony PSP options')
	psp.add_option('--psp', action='store', dest='PSP_OPTS', default=None, 
		help='enable building for Sony PSP, format: --psp=<module type>,<fw version>,<render type> example: --psp=prx,660,HW')

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
		conf.env.CXXFLAGS += android.cflags(True)
		conf.env.LINKFLAGS += android.linkflags()
		conf.env.LDFLAGS += android.ldflags()

		conf.env.HAVE_M = True
		if android.is_hardfp():
			conf.env.LIB_M = ['m_hard']
		else: conf.env.LIB_M = ['m']

		conf.env.PREFIX = '/lib/%s' % android.apk_arch()

		conf.msg('Selected Android NDK', '%s, version: %d' % (android.ndk_home, android.ndk_rev))
		# no need to print C/C++ compiler, as it would be printed by compiler_c/cxx
		conf.msg('... C/C++ flags', ' '.join(android.cflags()).replace(android.ndk_home, '$NDK/'))
		conf.msg('... link flags', ' '.join(android.linkflags()).replace(android.ndk_home, '$NDK/'))
		conf.msg('... ld flags', ' '.join(android.ldflags()).replace(android.ndk_home, '$NDK/'))

		# conf.env.ANDROID_OPTS = android
		conf.env.DEST_OS2 = 'android'

	if conf.options.PSP_OPTS:
		values = conf.options.PSP_OPTS.split(',')
		if len(values) != 3:
			conf.fatal('Invalid --psp paramater value!')

		valid_module_type = ['elf', 'prx']
		valid_render_type = ['SW', 'HW', 'ALL']

		if values[0] not in valid_module_type:
			conf.fatal('Unknown module type: %s. Supported: %r' % (values[0], ', '.join(valid_module_type)))
		if values[2] not in valid_render_type:
			conf.fatal('Unknown render type: %s. Supported: %r' % (values[2], ', '.join(valid_render_type)))

		conf.psp = psp = PSP(conf, values[0], values[1], values[2])
		conf.environ['CC'] = os.path.join(psp.binutils_path, 'psp-gcc')
		conf.environ['CXX'] = os.path.join(psp.binutils_path, 'psp-gcc')
		conf.environ['AS'] = os.path.join(psp.binutils_path, 'psp-gcc')
		conf.environ['STRIP'] = os.path.join(psp.binutils_path, 'psp-strip')
		conf.environ['LD'] = os.path.join(psp.binutils_path, 'psp-gcc')
		conf.environ['AR'] = os.path.join(psp.binutils_path, 'psp-ar')
		conf.environ['RANLIB'] = os.path.join(psp.binutils_path, 'psp-ranlib')
		conf.environ['OBJCOPY'] = os.path.join(psp.binutils_path, 'psp-objcopy')

#		conf.environ['PRXGEN'] = os.path.join(psp.binutils_path, 'prxgen')
#		conf.environ['MKSFO'] = os.path.join(psp.binutils_path, 'mksfoex')
#		conf.environ['PACK_PBP'] = os.path.join(psp.binutils_path, 'pack-pbp')
#		conf.environ['FIXUP'] = os.path.join(psp.binutils_path, 'psp-fixup-imports')

		conf.env.PRXGEN = conf.find_program('psp-prxgen', path_list = psp.binutils_path)
		conf.env.MKSFO =  conf.find_program('mksfoex', path_list = psp.binutils_path)
		conf.env.PACK_PBP = conf.find_program('pack-pbp', path_list = psp.binutils_path)
		conf.env.FIXUP =  conf.find_program('psp-fixup-imports', path_list = psp.binutils_path)

		conf.env.CFLAGS += psp.cflags()
		conf.env.CXXFLAGS += psp.cflags()
		conf.env.LINKFLAGS += psp.linkflags()
		conf.env.LDFLAGS += psp.ldflags()

		conf.env.PREFIX = '/lib'

		conf.msg('Selected PSPSDK', '%s' % (psp.sdk_home))
		# no need to print C/C++ compiler, as it would be printed by compiler_c/cxx
		conf.msg('... C/C++ flags', ' '.join(psp.cflags()).replace(psp.sdk_home, '$PSPSDK'))
		conf.msg('... link flags', ' '.join(psp.linkflags()).replace(psp.sdk_home, '$PSPSDK'))
		conf.msg('... ld flags', ' '.join(psp.ldflags()).replace(psp.sdk_home, '$PSPSDK'))

		if conf.options.PROFILING:
			conf.env.LDFLAGS += ['-lpspprof']
			conf.env.CFLAGS += ['-pg']
		conf.env.LDFLAGS += psp.stdlibs()

		conf.env.DEST_OS2 = 'psp'
		conf.env.PSP_RENDER_TYPE = psp.render_type
		conf.env.PSP_PATTERN_program = '%s'
		conf.env.PSP_PATTERN_stlib   = 'lib%s.a'
		conf.env.PSP_PATTERN_shlib = '%s.elf'

		if psp.module_type == 'prx':
			conf.env.PSP_BUILD_PRX = True

	MACRO_TO_DESTOS = OrderedDict({ '__ANDROID__' : 'android', '__psp__' : 'psp'})
	for k in c_config.MACRO_TO_DESTOS:
		MACRO_TO_DESTOS[k] = c_config.MACRO_TO_DESTOS[k] # ordering is important
	c_config.MACRO_TO_DESTOS  = MACRO_TO_DESTOS

def build(bld):
	if bld.env.DEST_OS == 'psp':
		bld(rule='${FIXUP} -o ${TGT} ${SRC}', source='engine/xash', target='xash.elf')
		if bld.env.PSP_BUILD_PRX:
			bld(rule='${PRXGEN} ${SRC} ${TGT}', source='xash.elf', target='xash.prx')
			bld(rule='${MKSFO} -d MEMSIZE=1 Xash3D ${TGT}', source='xash.prx', target='PARAM.SFO')
			bld(rule='${PACK_PBP} ${TGT} ${SRC[1]} ${PSP_EBOOT_ICON} ${PSP_EBOOT_ICON1} ${PSP_EBOOT_UNKPNG} ${PSP_EBOOT_PIC1} ${PSP_EBOOT_SND0} ${SRC[0]} ${PSP_EBOOT_PSAR}', source='xash.prx PARAM.SFO', target='EBOOT.PBP')
		else:
			bld(rule='${STRIP} -o ${TGT} ${SRC}', source='xash.elf', target='xash_strip.elf')
			bld(rule='${MKSFO} -d MEMSIZE=1 Xash3D ${TGT}', source='xash_strip.elf', target='PARAM.SFO')
			bld(rule='${PACK_PBP} ${TGT} ${SRC[1]} ${PSP_EBOOT_ICON} ${PSP_EBOOT_ICON1} ${PSP_EBOOT_UNKPNG} ${PSP_EBOOT_PIC1} ${PSP_EBOOT_SND0} ${SRC[0]} ${PSP_EBOOT_PSAR}', source='xash_strip.elf PARAM.SFO', target='EBOOT.PBP')
#		source_target = os.path.join('engine', bld.env.PSP_BUILD_TARGET)
#		Logs.info(source_target)
#		Logs.info(bld.env.PSP_BUILD_TARGET)
#		bld(rule='${FIXUP} -o ${PSP_BUILD_TARGET}.elf ${SRC}', source=source_target, always=True)
#		if bld.env.PSP_BUILD_PRX:
#			bld(rule='${PRXGEN} ${PSP_BUILD_TARGET}.elf ${PSP_BUILD_TARGET}.prx', always=True)
#			if bld.env.PSP_BUILD_EBOOT:
#				bld(rule='${MKSFO} -d MEMSIZE=1 ${PSP_EBOOT_TITLE} ${PSP_EBOOT_SFO}', always=True)
#				bld(rule='${PACK_PBP} ${PSP_EBOOT} ${PSP_EBOOT_SFO} ${PSP_EBOOT_ICON} ${PSP_EBOOT_ICON1} ${PSP_EBOOT_UNKPNG} ${PSP_EBOOT_PIC1} ${PSP_EBOOT_SND0} ${PSP_BUILD_TARGET}.prx ${PSP_EBOOT_PSAR}', always=True)
#		else:
#			if bld.env.PSP_BUILD_EBOOT:
#				bld(rule='${STRIP} -o ${PSP_BUILD_TARGET}.elf ${PSP_BUILD_TARGET}_strip.elf', always=True)
#				bld(rule='${MKSFO} -d MEMSIZE=1 ${PSP_EBOOT_TITLE} ${PSP_EBOOT_SFO}', always=True)
#				bld(rule='${PACK_PBP} ${PSP_EBOOT} ${PSP_EBOOT_SFO} ${PSP_EBOOT_ICON} ${PSP_EBOOT_ICON1} ${PSP_EBOOT_UNKPNG} ${PSP_EBOOT_PIC1} ${PSP_EBOOT_SND0} ${PSP_BUILD_TARGET}_strip.elf ${PSP_EBOOT_PSAR}', always=True)
#				bld(rule='rm -f ${PSP_BUILD_TARGET}_strip.elf', always=True)

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
