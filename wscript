#! /usr/bin/env python
# encoding: utf-8
# a1batross, mittorn, 2018

from waflib import Build, Context, Logs
from waflib.Tools import waf_unit_test, c_tests
import sys
import os

VERSION = '0.99'
APPNAME = 'xash3d-fwgs'
top = '.'
default_prefix = '/' # Waf uses it to set default prefix

Context.Context.line_just = 55 # should fit for everything on 80x26

c_tests.LARGE_FRAGMENT='''#include <unistd.h>
int check[sizeof(off_t) >= 8 ? 1 : -1]; int main(void) { return 0; }'''

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

class RefDll:
	def __init__(self, name, default, key = None):
		self.name = name
		self.default = default
		self.dest = key if key else name.upper()

	def register_option(self, opt):
		kw = dict()
		if self.default:
			act = 'disable'
			kw['action'] = 'store_false'
		else:
			act = 'enable'
			kw['action'] = 'store_true'

		key = '--%s-%s' % (act, self.name)

		kw['dest'] = self.dest
		kw['default'] = self.default
		kw['help'] = '%s %s renderer [default: %%(default)s]' % (act, self.name)

		opt.add_option(key, **kw)

	def register_env(self, env, opts, force):
		env[self.dest] = force or opts.__dict__[self.dest]

	def register_define(self, conf):
		conf.define_cond('XASH_REF_%s_ENABLED' % self.dest, conf.env[self.dest])

SUBDIRS = [
	# always configured and built
	Subproject('public'),
	Subproject('filesystem'),
	Subproject('engine'),
	Subproject('stub/server'),
	Subproject('dllemu'),

	# disable only by engine feature, makes no sense to even parse subprojects in dedicated mode
	Subproject('3rdparty/extras',       lambda x: not x.env.DEDICATED and x.env.DEST_OS != 'android'),
	Subproject('3rdparty/nanogl',       lambda x: not x.env.DEDICATED and x.env.NANOGL),
	Subproject('3rdparty/gl-wes-v2',    lambda x: not x.env.DEDICATED and x.env.GLWES),
	Subproject('3rdparty/gl4es',        lambda x: not x.env.DEDICATED and x.env.GL4ES),
	Subproject('ref/gl',                lambda x: not x.env.DEDICATED and (x.env.GL or x.env.NANOGL or x.env.GLWES or x.env.GL4ES)),
	Subproject('ref/soft',              lambda x: not x.env.DEDICATED and not x.env.SUPPORT_BSP2_FORMAT and x.env.SOFT),
	Subproject('ref/null',              lambda x: not x.env.DEDICATED and x.env.NULL),
	Subproject('3rdparty/mainui',       lambda x: not x.env.DEDICATED),
	Subproject('3rdparty/vgui_support', lambda x: not x.env.DEDICATED),
	Subproject('stub/client',           lambda x: not x.env.DEDICATED),
	Subproject('game_launch',           lambda x: not x.env.DISABLE_LAUNCHER),

	# disable only by external dependency presense
	Subproject('3rdparty/opus', lambda x: not x.env.HAVE_SYSTEM_OPUS and not x.env.DEDICATED),

	# enabled optionally
	Subproject('utils/mdldec',     lambda x: x.env.ENABLE_UTILS),
#	Subproject('utils/xar',        lambda x: x.env.ENABLE_UTILS),
	Subproject('utils/run-fuzzer', lambda x: x.env.ENABLE_FUZZER),

	# enabled on PSVita only
	Subproject('ref/gl/vgl_shim',   lambda x: x.env.DEST_OS == 'psvita'),

]

REFDLLS = [
	RefDll('soft', True),
	RefDll('gl', True),
	RefDll('gles1', False, 'NANOGL'),
	RefDll('gles2', False, 'GLWES'),
	RefDll('gl4es', False),
	RefDll('gles3compat', False),
	RefDll('null', False),
]

def options(opt):
	opt.load('reconfigure compiler_optimizations xshlib xcompile compiler_cxx compiler_c sdl2 clang_compilation_database strip_on_install waf_unit_test msdev msvs subproject cmake')

	grp = opt.add_option_group('Common options')

	grp.add_option('-d', '--dedicated', action = 'store_true', dest = 'DEDICATED', default = False,
		help = 'build Xash Dedicated Server [default: %(default)s]')

	grp.add_option('--gamedir', action = 'store', dest = 'GAMEDIR', default = 'valve',
		help = 'engine default game directory [default: %(default)s]')

	grp.add_option('-8', '--64bits', action = 'store_true', dest = 'ALLOW64', default = False,
		help = 'allow targetting 64-bit engine(Linux/Windows/OSX x86 only) [default: %(default)s]')

	grp.add_option('-P', '--enable-packaging', action = 'store_true', dest = 'PACKAGING', default = False,
		help = 'respect prefix option, useful for packaging for various operating systems [default: %(default)s]')

	grp.add_option('--enable-bundled-deps', action = 'store_true', dest = 'BUILD_BUNDLED_DEPS', default = False,
		help = 'prefer to build bundled dependencies (like opus) instead of relying on system provided')

	grp.add_option('--enable-bsp2', action = 'store_true', dest = 'SUPPORT_BSP2_FORMAT', default = False,
		help = 'build engine and renderers with BSP2 map support(recommended for Quake, breaks compatibility!) [default: %(default)s]')

	grp.add_option('--low-memory-mode', action = 'store', dest = 'LOW_MEMORY', default = 0, type = int,
		help = 'enable low memory mode (only for devices have <128 ram)')

	grp.add_option('--disable-werror', action = 'store_true', dest = 'DISABLE_WERROR', default = False,
		help = 'disable compilation abort on warning')

	grp.add_option('--enable-tests', action = 'store_true', dest = 'TESTS', default = False,
		help = 'enable building standalone tests (does not enable engine tests!) [default: %(default)s]')

	# a1ba: special option for me
	grp.add_option('--debug-all-servers', action='store_true', dest='ALL_SERVERS', default=False, help='')

	grp = opt.add_option_group('Renderers options')

	grp.add_option('--enable-all-renderers', action='store_true', dest='ALL_RENDERERS', default=False,
		help = 'enable all renderers supported by Xash3D FWGS [default: %(default)s]')

	for dll in REFDLLS:
		dll.register_option(grp)

	grp = opt.add_option_group('Utilities options')

	grp.add_option('--enable-utils', action = 'store_true', dest = 'ENABLE_UTILS', default = False,
		help = 'enable building various development utilities [default: %(default)s]')

	grp.add_option('--enable-fuzzer', action = 'store_true', dest = 'ENABLE_FUZZER', default = False,
		help = 'enable building libFuzzer runner [default: %(default)s]' )

	for i in SUBDIRS:
		if not i.is_exists(opt):
			continue

		opt.add_subproject(i.name)

def configure(conf):
	conf.load('fwgslib reconfigure compiler_optimizations')
	conf.env.MSVC_TARGETS = ['x86' if not conf.options.ALLOW64 else 'x64']

	# Load compilers early
	conf.load('xshlib xcompile compiler_c compiler_cxx cmake gccdeps')

	if conf.options.NSWITCH:
		conf.load('nswitch')

	if conf.options.PSVITA:
		conf.load('psvita')

	# HACKHACK: override msvc DEST_CPU value by something that we understand
	if conf.env.DEST_CPU == 'amd64':
		conf.env.DEST_CPU = 'x86_64'

	if conf.env.COMPILER_CC == 'msvc':
		conf.load('msvc_pdb')

	conf.load('msvs msdev subproject clang_compilation_database strip_on_install waf_unit_test enforce_pic cmake')

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
		conf.options.NO_VGUI          = True # skip vgui
		conf.options.NANOGL           = True
		conf.options.GLWES            = False # deprecated
		conf.options.GL4ES            = True
		conf.options.GLES3COMPAT      = True
		conf.options.GL               = False
		conf.define('XASH_SDLMAIN', 1)
	elif conf.env.MAGX:
		conf.options.SDL12            = True
		conf.options.NO_VGUI          = True
		conf.options.GL               = False
		conf.options.LOW_MEMORY       = 1
		conf.options.NO_ASYNC_RESOLVE = True
		conf.define('XASH_SDLMAIN', 1)
		enforce_pic = False
	elif conf.env.DEST_OS == 'nswitch':
		conf.options.NO_VGUI          = True
		conf.options.GL               = True
		conf.options.NO_ASYNC_RESOLVE = True
		conf.options.USE_STBTT        = True
	elif conf.env.DEST_OS == 'psvita':
		conf.options.NO_VGUI          = True
		conf.options.GL               = True
		conf.options.NO_ASYNC_RESOLVE = True
		conf.options.USE_STBTT        = True
		# we'll specify -fPIC by hand for shared libraries only
		enforce_pic                   = False

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

	cflags, linkflags = conf.get_optimization_flags()
	cxxflags = list(cflags) # optimization flags are common between C and C++ but we need a copy

	# on the Switch, allow undefined symbols by default, which is needed for libsolder to work
	# we'll specifically disallow them for the engine executable
	# additionally, shared libs are linked without standard libs, we'll add those back in the engine wscript
	if conf.env.DEST_OS == 'nswitch':
		linkflags.remove('-Wl,--no-undefined')
		conf.env.append_unique('LINKFLAGS_cshlib', ['-nostdlib', '-nostartfiles'])
		conf.env.append_unique('LINKFLAGS_cxxshlib', ['-nostdlib', '-nostartfiles'])
	# same on the vita
	elif conf.env.DEST_OS == 'psvita':
		conf.env.append_unique('CFLAGS_cshlib', ['-fPIC'])
		conf.env.append_unique('CXXFLAGS_cxxshlib', ['-fPIC', '-fno-use-cxa-atexit'])
		conf.env.append_unique('LINKFLAGS_cshlib', ['-nostdlib', '-Wl,--unresolved-symbols=ignore-all'])
		conf.env.append_unique('LINKFLAGS_cxxshlib', ['-nostdlib', '-Wl,--unresolved-symbols=ignore-all'])
	# check if we need to use irix linkflags
	elif conf.env.DEST_OS == 'irix' and conf.env.COMPILER_CC == 'gcc':
		linkflags.remove('-Wl,--no-undefined')
		linkflags.append('-Wl,--unresolved-symbols=ignore-all')
		# check if we're in a sgug environment
		if 'sgug' in os.environ['LD_LIBRARYN32_PATH']:
			linkflags.append('-lc')
	elif conf.env.SAILFISH in ['aurora', 'sailfish']:
		# TODO: enable XASH_MOBILE_PLATFORM
		conf.define('XASH_SAILFISH', 1)
		if conf.env.SAILFISH == 'aurora':
			conf.define('XASH_AURORAOS', 1)

		# Do not warn us about bug in SDL_Audio headers
		conf.env.append_unique('CFLAGS', ['-Wno-attributes'])
		conf.env.append_unique('CXXFLAGS', ['-Wno-attributes'])

	conf.check_cc(cflags=cflags, linkflags=linkflags, msg='Checking for required C flags')
	conf.check_cxx(cxxflags=cxxflags, linkflags=linkflags, msg='Checking for required C++ flags')

	conf.env.append_unique('CFLAGS', cflags)
	conf.env.append_unique('CXXFLAGS', cxxflags)
	conf.env.append_unique('LINKFLAGS', linkflags)

	if conf.env.COMPILER_CC != 'msvc':
		opt_flags = [
			# '-Wall', '-Wextra', '-Wpedantic',
			'-fdiagnostics-color=always',

			# stable diagnostics, forced to error, sorted
			'-Werror=alloc-size',
			'-Werror=bool-compare',
			'-Werror=bool-operation',
			'-Werror=cast-align=strict',
			'-Werror=duplicated-cond',
			'-Werror=format-extra-args',
			'-Werror=implicit-fallthrough=2',
			'-Werror=logical-op',
			'-Werror=nonnull',
			'-Werror=packed',
			'-Werror=packed-not-aligned',
			'-Werror=parentheses',
			'-Werror=return-type',
			'-Werror=sequence-point',
			'-Werror=sizeof-pointer-memaccess',
			'-Werror=sizeof-array-div',
			'-Werror=sizeof-pointer-div',
			'-Werror=strict-aliasing',
			'-Werror=string-compare',
			'-Werror=tautological-compare',
			'-Werror=use-after-free=3',
			'-Werror=unsequenced', # clang's version of -Werror=sequence-point
			'-Werror=vla',
			'-Werror=write-strings',

			# unstable diagnostics, may cause false positives
			'-Walloc-zero',
			'-Wformat=2',
			'-Winit-self',
			'-Wmisleading-indentation',
			'-Wstringop-overflow',
			'-Wunintialized',

			# disabled, flood
			# '-Wdouble-promotion',

			'-Wunused-function',
			'-Wunused-variable',
			'-Wunused-but-set-variable',
		]

		opt_cflags = [
			'-Werror=declaration-after-statement',
			'-Werror=enum-conversion',
			'-Werror=implicit-int',
			'-Werror=implicit-function-declaration',
			'-Werror=incompatible-pointer-types',
			'-Werror=int-conversion',
			'-Werror=jump-misses-init',
			'-Werror=old-style-declaration',
			'-Werror=old-style-definition',
			'-Werror=strict-prototypes',
			'-fnonconst-initializers', # owcc
			'-Wmissing-prototypes', # not an error yet
		]

		opt_cxxflags = [] # TODO:

		if conf.options.DISABLE_WERROR:
			opt_flags = []
			opt_cflags = ['-Werror=implicit-function-declaration']
			opt_cxxflags = []

		conf.env.CFLAGS_werror = conf.filter_cflags(opt_flags + opt_cflags, cflags)
		conf.env.CXXFLAGS_werror = conf.filter_cxxflags(opt_flags + opt_cxxflags, cxxflags)

	conf.env.TESTS         = conf.options.TESTS
	conf.env.ENABLE_UTILS  = conf.options.ENABLE_UTILS
	conf.env.ENABLE_FUZZER = conf.options.ENABLE_FUZZER
	conf.env.DEDICATED     = conf.options.DEDICATED
	conf.env.SUPPORT_BSP2_FORMAT = conf.options.SUPPORT_BSP2_FORMAT

	# disable game_launch compiling on platform where it's not needed
	conf.env.DISABLE_LAUNCHER = conf.env.DEST_OS in ['android', 'nswitch', 'psvita', 'dos'] or conf.env.MAGX or conf.env.DEDICATED

	if conf.env.SAILFISH == 'aurora':
		conf.env.DEFAULT_RPATH = '/usr/share/su.xash.Engine/lib'
	elif conf.env.DEST_OS == 'darwin':
		conf.env.DEFAULT_RPATH = '@loader_path'
	elif conf.env.DEST_OS == 'openbsd':
		# OpenBSD requires -z origin to enable $ORIGIN expansion in RPATH
		conf.env.RPATH_ST = '-Wl,-z,origin,-rpath,%s'
		conf.env.DEFAULT_RPATH = '$ORIGIN'
	else:
		conf.env.DEFAULT_RPATH = '$ORIGIN'

	setattr(conf, 'refdlls', REFDLLS)

	for refdll in REFDLLS:
		refdll.register_env(conf.env, conf.options, conf.options.ALL_RENDERERS)

	conf.env.GAMEDIR = conf.options.GAMEDIR
	conf.define('XASH_GAMEDIR', conf.options.GAMEDIR)
	conf.define_cond('XASH_ALL_SERVERS', conf.options.ALL_SERVERS)

	if conf.env.DEST_OS == 'nswitch':
		conf.check_cfg(package='solder', args='--cflags --libs', uselib_store='SOLDER')
		if conf.env.HAVE_SOLDER and conf.env.LIB_SOLDER and conf.options.BUILD_TYPE == 'debug':
			conf.env.LIB_SOLDER[0] += 'd' # load libsolderd in debug mode
		conf.check_cc(lib='m')
	elif conf.env.DEST_OS == 'psvita':
		conf.check_cc(lib='vrtld')
		conf.check_cc(lib='m')
	elif conf.env.DEST_OS == 'android':
		conf.check_cc(lib='dl')
		conf.check_cc(lib='log')
		# LIB_M added in xcompile!
	elif conf.env.DEST_OS == 'win32':
		# Common Win32 libraries
		# Don't check them more than once, to save time
		# Usually, they are always available
		# but we need them in uselib
		a = [ 'user32', 'shell32', 'gdi32', 'advapi32', 'dbghelp', 'psapi', 'ws2_32' ]
		if conf.env.COMPILER_CC == 'msvc':
			for i in a:
				conf.start_msg('Checking for MSVC library')
				conf.check_lib_msvc(i)
				conf.end_msg(i)
		else:
			for i in a:
				conf.check_cc(lib = i)
	else:
		conf.check_cc(lib='dl', mandatory = False)
		conf.check_cc(lib='m')


	# set _FILE_OFFSET_BITS=64 for filesystems with 64-bit inodes
	# must be set globally as it changes ABI
	if conf.env.DEST_OS not in ['psvita']:
		conf.check_large_file(compiler = 'c', execute = False)

	# indicate if we are packaging for Linux/BSD
	if conf.options.PACKAGING:
		conf.env.PREFIX = conf.options.prefix
		if conf.env.SAILFISH == "aurora":
			conf.env.SHAREDIR = '${PREFIX}/share/su.xash.Engine/rodir'
		elif conf.env.SAILFISH == "sailfish":
			conf.env.SHAREDIR = '${PREFIX}/share/harbour-xash3d-fwgs/rodir'
		else:
			conf.env.SHAREDIR = '${PREFIX}/share/xash3d'
			conf.env.LIBDIR += '/xash3d'
	else:
		conf.env.SHAREDIR = conf.env.LIBDIR = conf.env.BINDIR = conf.env.PREFIX

	if not conf.options.BUILD_BUNDLED_DEPS:
		# search for opus 1.4 or higher, it has fixes for custom modes
		if conf.check_cfg(package='opus', uselib_store='opus', args='opus >= 1.4 --cflags --libs', mandatory=False):
			# now try to link with export that only exists with CUSTOM_MODES defined
			frag='''#include <opus_custom.h>
int main(void) { return !opus_custom_encoder_init((OpusCustomEncoder *)1, (const OpusCustomMode *)1, 1); }'''

			if conf.check_cc(msg='Checking if opus supports custom modes', defines='CUSTOM_MODES=1', use='opus werror', fragment=frag, mandatory=False):
				conf.env.HAVE_SYSTEM_OPUS = True

	conf.define('XASH_LOW_MEMORY', conf.options.LOW_MEMORY)

	for i in SUBDIRS:
		if not i.is_enabled(conf):
			continue

		conf.add_subproject(i.name)

def build(bld):
	# guard rails to not let install to root
	if bld.is_install and not bld.options.PACKAGING and not bld.options.destdir:
		bld.fatal('Set the install destination directory using --destdir option')

	# don't clean QtCreator files and reconfigure saved options
	bld.clean_files = bld.bldnode.ant_glob('**',
		excl='*.user configuration.py .lock* *conf_check_*/** config.log %s/*' % Build.CACHE_DIR,
		quiet=True, generator=True)

	bld.load('xshlib')

	for i in SUBDIRS:
		if not i.is_enabled(bld):
			continue

		bld.add_subproject(i.name)

	if bld.env.TESTS:
		bld.add_post_fun(waf_unit_test.summary)
		bld.add_post_fun(waf_unit_test.set_exit_code)
