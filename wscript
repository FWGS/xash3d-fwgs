#! /usr/bin/env python
# encoding: utf-8
# a1batross, mittorn, 2018

from __future__ import print_function
from waflib import Logs, Context, Configure
import sys
import os

VERSION = '0.99'
APPNAME = 'xash3d-fwgs'
top = '.'

Context.Context.line_just = 55 # should fit for everything on 80x26

class Subproject:
	name      = ''
	dedicated = True  # if true will be ignored when building dedicated server
	singlebin = False # if true will be ignored when singlebinary is set
	ignore    = False # if true will be ignored, set by user request

	def __init__(self, name, dedicated=True, singlebin=False):
		self.name = name
		self.dedicated = dedicated
		self.singlebin = singlebin

	def is_enabled(self, ctx):
		if ctx.env.SINGLE_BINARY and self.singlebin:
			return False

		if ctx.env.DEST_OS == 'android' and self.singlebin:
			return False

		if ctx.env.DEDICATED and self.dedicated:
			return False

		return True

SUBDIRS = [
	Subproject('public',      dedicated=False),
	Subproject('game_launch', singlebin=True),
	Subproject('ref_gl'),
#rsw	Subproject('ref_soft'),
	Subproject('mainui'),
	Subproject('vgui_support'),
	Subproject('engine', dedicated=False),
]

def subdirs():
	return map(lambda x: x.name, SUBDIRS)

def options(opt):
	grp = opt.add_option_group('Common options')

	grp.add_option('-T', '--build-type', action='store', dest='BUILD_TYPE', default = None,
		help = 'build type: debug, release or none(custom flags)')

	grp.add_option('-d', '--dedicated', action = 'store_true', dest = 'DEDICATED', default = False,
		help = 'build Xash Dedicated Server [default: %default]')

	grp.add_option('--single-binary', action = 'store_true', dest = 'SINGLE_BINARY', default = False,
		help = 'build single "xash" binary (always enabled for dedicated) [default: %default]')

	grp.add_option('-8', '--64bits', action = 'store_true', dest = 'ALLOW64', default = False,
		help = 'allow targetting 64-bit engine(Linux/Windows/OSX x86 only) [default: %default]')

	grp.add_option('-W', '--win-style-install', action = 'store_true', dest = 'WIN_INSTALL', default = False,
		help = 'install like Windows build, ignore prefix, useful for development [default: %default]')

	grp.add_option('--enable-bsp2', action = 'store_true', dest = 'SUPPORT_BSP2_FORMAT', default = False,
		help = 'build engine and renderers with BSP2 map support(recommended for Quake, breaks compatibility!) [default: %default]')

	grp.add_option('--enable-lto', action = 'store_true', dest = 'LTO', default = False,
		help = 'enable Link Time Optimization if possible [default: %default]')

	grp.add_option('--enable-poly-opt', action = 'store_true', dest = 'POLLY', default = False,
		help = 'enable polyhedral optimization if possible [default: %default]')

	grp.add_option('--low-memory-mode', action = 'store', dest = 'LOW_MEMORY', default = 0, type = 'int',
		help = 'enable low memory mode (only for devices have <128 ram)')

	grp.add_option('--enable-magx', action = 'store_true', dest = 'MAGX', default = False,
		help = 'enable targetting for MotoMAGX phones [default: %default]')

	opt.load('subproject')

	opt.add_subproject(subdirs())

	opt.load('xcompile compiler_cxx compiler_c sdl2 clang_compilation_database strip_on_install waf_unit_test')
	if sys.platform == 'win32':
		opt.load('msvc msdev msvs')
	opt.load('reconfigure')

def configure(conf):
	enforce_pic = True # modern defaults
	valid_build_types = ['fastnative', 'fast', 'release', 'debug', 'nooptimize', 'sanitize', 'none']
	conf.load('fwgslib reconfigure')
	conf.start_msg('Build type')
	if conf.options.BUILD_TYPE == None:
		conf.end_msg('not set', color='RED')
		conf.fatal('Please set a build type, for example "-T release"')
	elif not conf.options.BUILD_TYPE in valid_build_types:
		conf.end_msg(conf.options.BUILD_TYPE, color='RED')
		conf.fatal('Invalid build type. Valid are: %s' % ', '.join(valid_build_types))
	conf.end_msg(conf.options.BUILD_TYPE)

	# -march=native should not be used
	if conf.options.BUILD_TYPE.startswith('fast'):
		Logs.warn('WARNING: \'%s\' build type should not be used in release builds', conf.options.BUILD_TYPE)

	# Force XP compability, all build targets should add
	# subsystem=bld.env.MSVC_SUBSYSTEM
	# TODO: wrapper around bld.stlib, bld.shlib and so on?
	conf.env.MSVC_SUBSYSTEM = 'WINDOWS,5.01'
	conf.env.MSVC_TARGETS = ['x86'] # explicitly request x86 target for MSVC
	if sys.platform == 'win32':
		conf.load('msvc msvc_pdb msdev msvs')
	conf.load('subproject xcompile compiler_c compiler_cxx gitversion clang_compilation_database strip_on_install waf_unit_test')

	try:
		conf.env.CC_VERSION[0]
	except IndexError:
		conf.env.CC_VERSION = (0,)

	# modify options dictionary early
	if conf.env.DEST_OS == 'android':
		conf.options.NO_VGUI= True # skip vgui
		conf.options.NANOGL = True
		conf.options.GLWES  = True
		conf.options.GL     = False

	conf.env.MAGX = conf.options.MAGX
	if conf.options.MAGX:
		conf.options.USE_SELECT = True
		conf.options.SDL12 = True
		conf.options.NO_VGUI = True
		conf.options.GL = False
		conf.options.LOW_MEMORY = 1
		conf.options.SINGLE_BINARY = True
		conf.options.NO_ASYNC_RESOLVE = True
		conf.define('XASH_SDLMAIN', 1)
		enforce_pic = False

		# useless to change toolchain path, as toolchain meant to be placed in this path
		toolchain_path = '/opt/toolchains/motomagx/arm-eabi2/lib/'
		conf.env.INCLUDES_MAGX = [toolchain_path + i for i in ['ezx-z6/include', 'qt-2.3.8/include']]
		conf.env.LIBPATH_MAGX  = [toolchain_path + i for i in ['ezx-z6/lib', 'qt-2.3.8/lib']]
		conf.env.LINKFLAGS_MAGX = ['-Wl,-rpath-link=' + i for i in conf.env.LIBPATH_MAGX]
		for lib in ['qte-mt', 'ezxappbase', 'ezxpm', 'log_util']:
			conf.check_cc(lib=lib, use='MAGX', uselib_store='MAGX')

	if enforce_pic:
		# Every static library must have fPIC
		if conf.env.DEST_OS != 'win32' and '-fPIC' in conf.env.CFLAGS_cshlib:
			conf.env.append_unique('CFLAGS_cstlib', '-fPIC')
			conf.env.append_unique('CXXFLAGS_cxxstlib', '-fPIC')
	else:
		conf.env.CFLAGS_cshlib.remove('-fPIC')
		conf.env.CXXFLAGS_cxxshlib.remove('-fPIC')
		conf.env.CFLAGS_MACBUNDLE.remove('-fPIC')
		conf.env.CXXFLAGS_MACBUNDLE.remove('-fPIC')

	# We restrict 64-bit builds ONLY for Win/Linux/OSX running on Intel architecture
	# Because compatibility with original GoldSrc
	if conf.env.DEST_OS in ['win32', 'linux', 'darwin'] and conf.env.DEST_CPU == 'x86_64':
		conf.env.BIT32_MANDATORY = not conf.options.ALLOW64
		if not conf.env.BIT32_MANDATORY:
			Logs.info('WARNING: will build engine for 32-bit target')
	else:
		conf.env.BIT32_MANDATORY = False

	conf.load('force_32bit')

	linker_flags = {
		'common': {
			'msvc':  ['/DEBUG'], # always create PDB, doesn't affect result binaries
			'gcc':   ['-Wl,--no-undefined']
		},
		'sanitize': {
			'clang': ['-fsanitize=undefined', '-fsanitize=address'],
			'gcc':   ['-fsanitize=undefined', '-fsanitize=address'],
		}
	}

	compiler_c_cxx_flags = {
		'common': {
			# disable thread-safe local static initialization for C++11 code, as it cause crashes on Windows XP
			'msvc':    ['/D_USING_V110_SDK71_', '/Zi', '/FS', '/Zc:threadSafeInit-', '/MT'],
			'clang':   ['-g', '-gdwarf-2', '-fvisibility=hidden'],
			'gcc':     ['-g', '-fvisibility=hidden']
		},
		'fast': {
			'msvc':    ['/O2', '/Oy'],
			'gcc': {
				'3':       ['-O3', '-fomit-frame-pointer'],
				'default': ['-Ofast', '-funsafe-math-optimizations', '-funsafe-loop-optimizations', '-fomit-frame-pointer']
			},
			'clang':   ['-Ofast'],
			'default': ['-O3']
		},
		'fastnative': {
			'msvc':    ['/O2', '/Oy'],
			'gcc':     ['-Ofast', '-march=native', '-funsafe-math-optimizations', '-funsafe-loop-optimizations', '-fomit-frame-pointer'],
			'clang':   ['-Ofast', '-march=native'],
			'default': ['-O3']
		},
		'release': {
			'msvc':    ['/O2'],
			'default': ['-O3']
		},
		'debug': {
			'msvc':    ['/O1'],
			'gcc':     ['-Og'],
			'default': ['-O1']
		},
		'sanitize': {
			'msvc':    ['/Od', '/RTC1'],
			'gcc':     ['-Og', '-fsanitize=undefined', '-fsanitize=address'],
			'clang':   ['-O0', '-fsanitize=undefined', '-fsanitize=address'],
			'default': ['-O0']
		},
		'nooptimize': {
			'msvc':    ['/Od'],
			'default': ['-O0']
		}
	}

	compiler_optional_flags = [
#		'-Wall', '-Wextra', '-Wpedantic',
		'-fdiagnostics-color=always',
		'-Werror=return-type',
		'-Werror=parentheses',
		'-Werror=vla',
		'-Werror=tautological-compare',
		'-Werror=duplicated-cond',
		'-Werror=duplicated-branches', # BEWARE: buggy
		'-Werror=bool-compare',
		'-Werror=bool-operation',
		'-Wuninitialized',
		'-Winit-self',
		'-Werror=implicit-fallthrough=2', # clang incompatible without "=2"
#		'-Wdouble-promotion', # disable warning flood
		'-Wstrict-aliasing',
	]

	c_compiler_optional_flags = [
		'-Werror=incompatible-pointer-types',
		'-Werror=implicit-function-declaration',
		'-Werror=int-conversion',
		'-Werror=implicit-int',
		'-Werror=strict-prototypes',
		'-Werror=old-style-declaration',
		'-Werror=old-style-definition',
		'-Werror=declaration-after-statement',
		'-Werror=enum-conversion'
	]

	linkflags = conf.get_flags_by_type(linker_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC, conf.env.CC_VERSION[0])
	cflags    = conf.get_flags_by_type(compiler_c_cxx_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC, conf.env.CC_VERSION[0])

	# Here we don't differentiate C or C++ flags
	if conf.options.LTO:
		lto_cflags = {
			'msvc':  ['/GL'],
			'gcc':   ['-flto'],
			'clang': ['-flto']
		}

		lto_linkflags = {
			'msvc':  ['/LTCG'],
			'gcc':   ['-flto'],
			'clang': ['-flto']
		}
		cflags    += conf.get_flags_by_compiler(lto_cflags, conf.env.COMPILER_CC)
		linkflags += conf.get_flags_by_compiler(lto_linkflags, conf.env.COMPILER_CC)

	if conf.options.POLLY:
		polly_cflags = {
			'gcc':   ['-fgraphite-identity'],
			'clang': ['-mllvm', '-polly']
			# msvc sosat :(
		}

		cflags   += conf.get_flags_by_compiler(polly_cflags, conf.env.COMPILER_CC)

	# And here C++ flags starts to be treated separately
	cxxflags = list(cflags)
	if conf.env.COMPILER_CC != 'msvc':
		conf.check_cc(cflags=cflags, linkflags=linkflags, msg= 'Checking for required C flags')
		conf.check_cxx(cxxflags=cflags, linkflags=linkflags, msg= 'Checking for required C++ flags')

		conf.env.append_unique('CFLAGS', cflags)
		conf.env.append_unique('CXXFLAGS', cxxflags)
		conf.env.append_unique('LINKFLAGS', linkflags)

		cxxflags += conf.filter_cxxflags(compiler_optional_flags, cflags)
		cflags += conf.filter_cflags(compiler_optional_flags + c_compiler_optional_flags, cflags)

	conf.env.append_unique('CFLAGS', cflags)
	conf.env.append_unique('CXXFLAGS', cxxflags)
	conf.env.append_unique('LINKFLAGS', linkflags)

	# check if we can use C99 tgmath
	if conf.check_cc(header_name='tgmath.h', mandatory=False):
		if conf.env.COMPILER_CC == 'msvc':
			conf.define('_CRT_SILENCE_NONCONFORMING_TGMATH_H', 1)
		tgmath_usable = conf.check_cc(fragment='''#include<tgmath.h>
			int main(void){ return (int)sin(2.0f); }''',
			msg='Checking if tgmath.h is usable', mandatory=False)
		conf.define_cond('HAVE_TGMATH_H', tgmath_usable)
	else:
		conf.undefine('HAVE_TGMATH_H')

	conf.env.DEDICATED     = conf.options.DEDICATED
	conf.env.SINGLE_BINARY = conf.options.SINGLE_BINARY or conf.env.DEDICATED

	if conf.env.DEST_OS != 'win32':
		conf.check_cc(lib='dl', mandatory=False)

		if not conf.env.LIB_M: # HACK: already added in xcompile!
			conf.check_cc(lib='m')
	else:
		# Common Win32 libraries
		# Don't check them more than once, to save time
		# Usually, they are always available
		# but we need them in uselib
		a = map(lambda x: {
			# 'features': 'c',
			# 'message': '...' + x,
			'lib': x,
			# 'uselib_store': x.upper(),
			# 'global_define': False,
		}, [
			'user32',
			'shell32',
			'gdi32',
			'advapi32',
			'dbghelp',
			'psapi',
			'ws2_32'
		])

		for i in a:
			conf.check_cc(**i)

		# conf.multicheck(*a, run_all_tests = True, mandatory = True)

	# indicate if we are packaging for Linux/BSD
	if not conf.options.WIN_INSTALL and conf.env.DEST_OS not in ['win32', 'darwin', 'android']:
		conf.env.LIBDIR = conf.env.BINDIR = '${PREFIX}/lib/xash3d'
	else:
		conf.env.LIBDIR = conf.env.BINDIR = conf.env.PREFIX

	conf.define('XASH_BUILD_COMMIT', conf.env.GIT_VERSION if conf.env.GIT_VERSION else 'notset')

	if conf.options.LOW_MEMORY:
		conf.define('XASH_LOW_MEMORY', conf.options.LOW_MEMORY)

	for i in SUBDIRS:
		if not i.is_enabled(conf):
			continue

		conf.add_subproject(i.name)

def build(bld):
	for i in SUBDIRS:
		if not i.is_enabled(bld):
			continue

		bld.add_subproject(i.name)
