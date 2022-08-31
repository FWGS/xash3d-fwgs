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
	def __init__(self, name, fnFilter = None):
		self.name = name
		self.fnFilter = fnFilter

	def is_exists(self, ctx):
		return ctx.path.find_node(self.name + '/wscript')

	def is_enabled(self, ctx):
		if not self.is_exists(ctx):
			return False

		if self.fnFilter:
			return self.fnFilter(ctx)

		return True

SUBDIRS = [
	# always configured and built
	Subproject('public'),
	Subproject('filesystem'),
	Subproject('engine'),
	Subproject('stub/server'),
	Subproject('dllemu'),

	# disable only by engine feature, makes no sense to even parse subprojects in dedicated mode
	Subproject('ref_gl',       lambda x: not x.env.DEDICATED),
	Subproject('ref_soft',     lambda x: not x.env.DEDICATED),
	Subproject('mainui',       lambda x: not x.env.DEDICATED),
	Subproject('vgui_support', lambda x: not x.env.DEDICATED),
	Subproject('stub/client',  lambda x: not x.env.DEDICATED),
	Subproject('game_launch',  lambda x: not x.env.SINGLE_BINARY and x.env.DEST_OS != 'android'),

	# disable only by external dependency presense
	Subproject('3rdparty/opus', lambda x: not x.env.HAVE_SYSTEM_OPUS and not x.env.DEDICATED),

	# enabled optionally
	Subproject('utils/mdldec',     lambda x: x.env.ENABLE_UTILS),
	Subproject('utils/run-fuzzer', lambda x: x.env.ENABLE_FUZZER),
]

def subdirs():
	return map(lambda x: x.name, SUBDIRS)

def options(opt):
	grp = opt.add_option_group('Common options')

	grp.add_option('-d', '--dedicated', action = 'store_true', dest = 'DEDICATED', default = False,
		help = 'build Xash Dedicated Server [default: %default]')

	grp.add_option('--single-binary', action = 'store_true', dest = 'SINGLE_BINARY', default = False,
		help = 'build single "xash" binary (always enabled for dedicated) [default: %default]')

	grp.add_option('-8', '--64bits', action = 'store_true', dest = 'ALLOW64', default = False,
		help = 'allow targetting 64-bit engine(Linux/Windows/OSX x86 only) [default: %default]')

	grp.add_option('-P', '--enable-packaging', action = 'store_true', dest = 'PACKAGING', default = False,
		help = 'respect prefix option, useful for packaging for various operating systems [default: %default]')

	grp.add_option('--enabled-bundled-deps', action = 'store_true', dest = 'BUILD_BUNDLED_DEPS', default = False,
		help = 'prefer to build bundled dependencies (like opus) instead of relying on system provided')

	grp.add_option('--enable-bsp2', action = 'store_true', dest = 'SUPPORT_BSP2_FORMAT', default = False,
		help = 'build engine and renderers with BSP2 map support(recommended for Quake, breaks compatibility!) [default: %default]')

	grp.add_option('--low-memory-mode', action = 'store', dest = 'LOW_MEMORY', default = 0, type = 'int',
		help = 'enable low memory mode (only for devices have <128 ram)')

	grp.add_option('--disable-werror', action = 'store_true', dest = 'DISABLE_WERROR', default = False,
		help = 'disable compilation abort on warning')

	grp = opt.add_option_group('Utilities options')

	grp.add_option('--enable-utils', action = 'store_true', dest = 'ENABLE_UTILS', default = False,
		help = 'enable building various development utilities [default: %default]')

	grp.add_option('--enable-fuzzer', action = 'store_true', dest = 'ENABLE_FUZZER', default = False,
		help = 'enable building libFuzzer runner [default: %default]' )

	opt.load('compiler_optimizations subproject')

	for i in SUBDIRS:
		if not i.is_exists(opt):
			continue

		opt.add_subproject(i.name)

	opt.load('xshlib xcompile compiler_cxx compiler_c sdl2 clang_compilation_database strip_on_install waf_unit_test msdev msvs')
	if sys.platform == 'win32':
		opt.load('msvc')
	opt.load('reconfigure')

def configure(conf):
	conf.load('fwgslib reconfigure compiler_optimizations')
	conf.env.MSVC_TARGETS = ['x86' if not conf.options.ALLOW64 else 'x64']

	# Load compilers early
	conf.load('xshlib xcompile compiler_c compiler_cxx')

	# HACKHACK: override msvc DEST_CPU value by something that we understand
	if conf.env.DEST_CPU == 'amd64':
		conf.env.DEST_CPU = 'x86_64'

	if conf.env.COMPILER_CC == 'msvc':
		conf.load('msvc_pdb')

	conf.load('msvs msdev subproject gitversion clang_compilation_database strip_on_install waf_unit_test enforce_pic')

	# Force XP compatibility, all build targets should add subsystem=bld.env.MSVC_SUBSYSTEM
	if conf.env.MSVC_TARGETS[0] == 'x86':
		conf.env.MSVC_SUBSYSTEM = 'WINDOWS,5.01'
		conf.env.CONSOLE_SUBSYSTEM = 'CONSOLE,5.01'
	else:
		conf.env.MSVC_SUBSYSTEM = 'WINDOWS'
		conf.env.CONSOLE_SUBSYSTEM = 'CONSOLE'

	enforce_pic = True # modern defaults

	# modify options dictionary early
	if conf.env.DEST_OS == 'android':
		conf.options.NO_VGUI= True # skip vgui
		conf.options.NANOGL = True
		conf.options.GLWES  = True
		conf.options.GL4ES  = True
		conf.options.GL     = False
	elif conf.env.MAGX:
		conf.options.USE_SELECT       = True
		conf.options.SDL12            = True
		conf.options.NO_VGUI          = True
		conf.options.GL               = False
		conf.options.LOW_MEMORY       = 1
		conf.options.SINGLE_BINARY    = True
		conf.options.NO_ASYNC_RESOLVE = True
		conf.define('XASH_SDLMAIN', 1)
		enforce_pic = False
	elif conf.env.DEST_OS == 'dos':
		conf.options.SINGLE_BINARY = True

	if conf.env.STATIC_LINKING:
		enforce_pic = False # PIC may break full static builds

	conf.check_pic(enforce_pic)

	# We restrict 64-bit builds ONLY for Win/Linux/OSX running on Intel architecture
	# Because compatibility with original GoldSrc
	if conf.env.DEST_OS in ['win32', 'linux', 'darwin'] and conf.env.DEST_CPU == 'x86_64':
		conf.env.BIT32_MANDATORY = not conf.options.ALLOW64
		if conf.env.BIT32_MANDATORY:
			Logs.info('WARNING: will build engine for 32-bit target')
	else:
		conf.env.BIT32_MANDATORY = False

	conf.load('force_32bit')

	compiler_optional_flags = [
#		'-Wall', '-Wextra', '-Wpedantic',
		'-fdiagnostics-color=always',
		'-Werror=return-type',
		'-Werror=parentheses',
		'-Werror=vla',
		'-Werror=tautological-compare',
		'-Werror=duplicated-cond',
		'-Werror=bool-compare',
		'-Werror=bool-operation',
		'-Wcast-align',
		'-Werror=cast-align=strict', # =strict is for GCC >=8
		'-Werror=packed',
		'-Werror=packed-not-aligned',
		'-Wuninitialized', # older GCC versions have -Wmaybe-uninitialized enabled by this switch, which is not accurate
                                   # so just warn, not error
		'-Winit-self',
		'-Werror=implicit-fallthrough=2', # clang incompatible without "=2"
		'-Werror=logical-op',
		'-Werror=write-strings',
#		'-Werror=format=2',
#		'-Wdouble-promotion', # disable warning flood
		'-Wstrict-aliasing',
		'-Wmisleading-indentation',
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
		'-Werror=enum-conversion',
		'-Werror=jump-misses-init',
		'-Werror=strict-prototypes',
#		'-Werror=nested-externs',
		'-fnonconst-initializers' # owcc
	]

	cflags, linkflags = conf.get_optimization_flags()

	# And here C++ flags starts to be treated separately
	cxxflags = list(cflags)
	if conf.env.COMPILER_CC != 'msvc' and not conf.options.DISABLE_WERROR:
		conf.check_cc(cflags=cflags, linkflags=linkflags, msg='Checking for required C flags')
		conf.check_cxx(cxxflags=cflags, linkflags=linkflags, msg='Checking for required C++ flags')

		conf.env.append_unique('CFLAGS', cflags)
		conf.env.append_unique('CXXFLAGS', cxxflags)
		conf.env.append_unique('LINKFLAGS', linkflags)

		cxxflags += conf.filter_cxxflags(compiler_optional_flags, cflags)
		cflags += conf.filter_cflags(compiler_optional_flags + c_compiler_optional_flags, cflags)

	conf.env.append_unique('CFLAGS', cflags)
	conf.env.append_unique('CXXFLAGS', cxxflags)
	conf.env.append_unique('LINKFLAGS', linkflags)

	# check if we can use C99 stdint
	if conf.check_cc(header_name='stdint.h', mandatory=False):
		# use system
		conf.define('STDINT_H', 'stdint.h')
	else:
		# include portable stdint by Paul Hsich
		conf.define('STDINT_H', 'pstdint.h')

	conf.env.ENABLE_UTILS  = conf.options.ENABLE_UTILS
	conf.env.ENABLE_FUZZER = conf.options.ENABLE_FUZZER
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
		a = [
			'user32',
			'shell32',
			'gdi32',
			'advapi32',
			'dbghelp',
			'psapi',
			'ws2_32'
		]

		if conf.env.COMPILER_CC == 'msvc':
			for i in a:
				conf.start_msg('Checking for MSVC library')
				conf.check_lib_msvc(i)
				conf.end_msg(i)
		else:
			for i in a:
				conf.check_cc(lib = i)

	# check if we can use C99 tgmath
	if conf.check_cc(header_name='tgmath.h', mandatory=False):
		if conf.env.COMPILER_CC == 'msvc':
			conf.define('_CRT_SILENCE_NONCONFORMING_TGMATH_H', 1)
		tgmath_usable = conf.check_cc(fragment='''#include<tgmath.h>
			const float val = 2, val2 = 3;
			int main(void){ return (int)(-asin(val) + cos(val2)); }''',
			msg='Checking if tgmath.h is usable', mandatory=False, use='M')
		conf.define_cond('HAVE_TGMATH_H', tgmath_usable)
	else:
		conf.undefine('HAVE_TGMATH_H')

	# set _FILE_OFFSET_BITS=64 for filesystems with 64-bit inodes
	if conf.env.DEST_OS != 'win32' and conf.env.DEST_SIZEOF_VOID_P == 4:
		# check was borrowed from libarchive source code
		file_offset_bits_usable = conf.check_cc(fragment='''
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
#define KB ((off_t)1024)
#define MB ((off_t)1024 * KB)
#define GB ((off_t)1024 * MB)
#define TB ((off_t)1024 * GB)
int t2[(((64 * GB -1) % 671088649) == 268434537)
       && (((TB - (64 * GB -1) + 255) % 1792151290) == 305159546)? 1: -1];
int main(void) { return 0; }''',
		msg='Checking if _FILE_OFFSET_BITS can be defined to 64', mandatory=False)
		if file_offset_bits_usable:
			conf.define('_FILE_OFFSET_BITS', 64)
		else: conf.undefine('_FILE_OFFSET_BITS')

	if conf.env.DEST_OS != 'win32':
		strcasestr_frag = '''#include <string.h>
int main(int argc, char **argv) { strcasestr(argv[1], argv[2]); return 0; }'''

		if conf.check_cc(msg='Checking for strcasestr', mandatory=False, fragment=strcasestr_frag):
			conf.define('HAVE_STRCASESTR', 1)
		elif conf.check_cc(msg='... with _GNU_SOURCE?', mandatory=False, fragment=strcasestr_frag, defines='_GNU_SOURCE=1'):
			conf.define('_GNU_SOURCE', 1)
			conf.define('HAVE_STRCASESTR', 1)

	# check if we can use alloca.h or malloc.h
	if conf.check_cc(header_name='alloca.h', mandatory=False):
		conf.define('ALLOCA_H', 'alloca.h')
	elif conf.check_cc(header_name='malloc.h', mandatory=False):
		conf.define('ALLOCA_H', 'malloc.h')

	# indicate if we are packaging for Linux/BSD
	if conf.options.PACKAGING:
		conf.env.LIBDIR = conf.env.BINDIR = '${PREFIX}/lib/xash3d'
		conf.env.SHAREDIR = '${PREFIX}/share/xash3d'
	else:
		if sys.platform != 'win32' and not conf.env.DEST_OS == 'android':
			conf.env.PREFIX = '/'

		conf.env.SHAREDIR = conf.env.LIBDIR = conf.env.BINDIR = conf.env.PREFIX

	if not conf.options.BUILD_BUNDLED_DEPS:
		# check if we can use system opus
		conf.define('CUSTOM_MODES', 1)

		# try to link with export that only exists with CUSTOM_MODES defined
		if conf.check_pkg('opus', 'opus', '''#include <opus_custom.h>
int main(void){ return !opus_custom_encoder_init(0, 0, 0); }''', fatal = False):
			conf.env.HAVE_SYSTEM_OPUS = True
		else:
			conf.undefine('CUSTOM_MODES')

	conf.define('XASH_BUILD_COMMIT', conf.env.GIT_VERSION if conf.env.GIT_VERSION else 'notset')
	conf.define('XASH_LOW_MEMORY', conf.options.LOW_MEMORY)

	for i in SUBDIRS:
		if not i.is_enabled(conf):
			continue

		conf.add_subproject(i.name)

def build(bld):
	bld.load('xshlib')

	for i in SUBDIRS:
		if not i.is_enabled(bld):
			continue

		bld.add_subproject(i.name)
