#! /usr/bin/env python
# encoding: utf-8
# a1batross, mittorn, 2018

from __future__ import print_function
from waflib import Logs
import sys
import os

sys.path.append(os.path.realpath('scripts/waflib'))
# print(sys.path)
import fwgslib

VERSION = '0.99'
APPNAME = 'xash3d-fwgs'
top = '.'

class Subproject:
	name      = ''
	dedicated = True  # if true will be ignored when building dedicated server
	singlebin = False # if true will be ignored when singlebinary is set
	ignore    = False # if true will be ignored, set by user request

	def __init__(self, name, dedicated=True, singlebin=False):
		self.name = name
		self.dedicated = dedicated
		self.singlebin = singlebin

SUBDIRS = [
	Subproject('public',      dedicated=False),
	Subproject('engine',      dedicated=False),
	Subproject('game_launch', singlebin=True),
	Subproject('ref_gl'),
	Subproject('ref_soft'),
	Subproject('mainui'),
	Subproject('vgui_support'),
]

def subdirs():
	return map(lambda x: x.name, SUBDIRS)

def options(opt):
	grp = opt.add_option_group('Common options')

	grp.add_option('-T', '--build-type', action='store', dest='BUILD_TYPE', default = None,
		help = 'build type: debug, release or none(custom flags)')

	grp.add_option('-d', '--dedicated', action = 'store_true', dest = 'DEDICATED', default = False,
		help = 'build Xash Dedicated Server(XashDS)')

	grp.add_option('--single-binary', action = 'store_true', dest = 'SINGLE_BINARY', default = False,
		help = 'build single "xash" binary instead of xash.dll/libxash.so (forced for dedicated)')

	grp.add_option('-8', '--64bits', action = 'store_true', dest = 'ALLOW64', default = False,
		help = 'allow targetting 64-bit engine')

	grp.add_option('--win-style-install', action = 'store_true', dest = 'WIN_INSTALL', default = False,
		help = 'install like Windows build, ignore prefix, useful for development')

	grp.add_option('--enable-bsp2', action = 'store_true', dest = 'SUPPORT_BSP2_FORMAT', default = False,
		help = 'build engine and renderers with BSP2 map support(recommended for Quake, breaks compability!)')

	grp.add_option('-S', '--skip-subprojects', action='store', dest = 'SKIP_SUBDIRS', default=None,
		help = 'don\'t recurse into specified subprojects. Current subdirs: ' + str(subdirs()))

	for i in SUBDIRS:
		if not os.path.isfile(os.path.join(i.name, 'wscript')):
			# HACKHACK: this way we get warning message right in the help
			# so this just becomes more noticeable
			opt.add_option_group('Cannot find wscript in ' + i.name + '. You probably missed submodule update')
		else: opt.recurse(i.name)

	opt.load('xcompile compiler_cxx compiler_c sdl2')
	if sys.platform == 'win32':
		opt.load('msvc msvs')

def set_ignored_subdirs(subdirs):
	for i in SUBDIRS:
		if i.ignore:
			continue

		if i.name in subdirs:
			i.ignore = True

def configure(conf):
	conf.start_msg('Build type')
	if conf.options.BUILD_TYPE == None:
		conf.end_msg('not set', color='RED')
		conf.fatal('Please set a build type, for example "-T release"')
	elif not conf.options.BUILD_TYPE in ['release', 'debug', 'none']:
		conf.end_msg(conf.options.BUILD_TYPE, color='RED')
		conf.fatal('Invalid build type. Valid are "debug", "release" or "none"')
	conf.end_msg(conf.options.BUILD_TYPE)

	# skip some subdirectories, if requested
	if conf.options.SKIP_SUBDIRS:
		skip_subdirs = conf.options.SKIP_SUBDIRS.split(',')
		set_ignored_subdirs(skip_subdirs)

	# Force XP compability, all build targets should add
	# subsystem=bld.env.MSVC_SUBSYSTEM
	# TODO: wrapper around bld.stlib, bld.shlib and so on?
	conf.env.MSVC_SUBSYSTEM = 'WINDOWS,5.01'
	conf.env.MSVC_TARGETS = ['x86'] # explicitly request x86 target for MSVC
	conf.load('xcompile compiler_c compiler_cxx gitversion clang_compilation_database')
	if sys.platform == 'win32':
		conf.load('msvc msvs')

	# print(conf.options.ALLOW64)

	conf.env.BIT32_MANDATORY = not conf.options.ALLOW64
	conf.env.BIT32_ALLOW64 = conf.options.ALLOW64
	conf.load('force_32bit sdl2')

	if conf.env.DEST_SIZEOF_VOID_P == 4:
		Logs.info('NOTE: will build engine for 32-bit target')
	else:
		Logs.warn('WARNING: 64-bit engine may be unstable')

	linker_flags = {
		'common': {
			'msvc':    ['/DEBUG'], # always create PDB, doesn't affect result binaries
			'gcc': ['-Wl,--no-undefined']
		}
	}

	compiler_c_cxx_flags = {
		'common': {
			'msvc':    ['/D_USING_V110_SDK71_'],
			'default': ['-g', '-Werror=implicit-function-declaration']
		},
		'release': {
			'msvc':    ['/Zi', '/O2'],
			'default': ['-O3']
		},
		'debug': {
			'msvc':    ['/O2', '/Ox', 'Oy'],
			'clang':   ['-O0', '-gdwarf-2'],
			'default': ['-Og']
		}
	}

	conf.env.append_unique('CFLAGS', fwgslib.get_flags_by_type(
	    compiler_c_cxx_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC))
	conf.env.append_unique('CXXFLAGS', fwgslib.get_flags_by_type(
	    compiler_c_cxx_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC))
	conf.env.append_unique('LINKFLAGS', fwgslib.get_flags_by_type(
	    linker_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC))

	conf.env.DEDICATED     = conf.options.DEDICATED
	# we don't need game launcher on dedicated
	conf.env.SINGLE_BINARY = conf.options.SINGLE_BINARY or conf.env.DEDICATED
	if conf.env.DEST_OS == 'linux':
		conf.check_cc( lib='dl' )

	if conf.env.DEST_OS != 'win32':
		conf.check_cc( lib='m' )
		if conf.env.DEST_OS2 != 'android':
			conf.check_cc( lib='pthread' )
	else:
		# Common Win32 libraries
		# Don't check them more than once, to save time
		# Usually, they are always available
		# but we need them in uselib
		conf.check_cc( lib='user32' )
		conf.check_cc( lib='shell32' )
		conf.check_cc( lib='gdi32' )
		conf.check_cc( lib='advapi32' )
		conf.check_cc( lib='dbghelp' )
		conf.check_cc( lib='psapi' )


	# indicate if we are packaging for Linux/BSD
	if(not conf.options.WIN_INSTALL and
		conf.env.DEST_OS != 'win32' and
		conf.env.DEST_OS != 'darwin'):
		conf.env.LIBDIR = conf.env.BINDIR = '${PREFIX}/lib/xash3d'
	else:
		conf.env.LIBDIR = conf.env.BINDIR = conf.env.PREFIX

	conf.env.append_unique('DEFINES', 'XASH_BUILD_COMMIT="{0}"'.format(conf.env.GIT_VERSION if conf.env.GIT_VERSION else 'notset'))

	for i in SUBDIRS:
		if conf.env.SINGLE_BINARY and i.singlebin:
			continue

		if conf.env.DEDICATED and i.dedicated:
			continue

		if i.ignore:
			continue

		conf.setenv(i.name, conf.env) # derive new env from global one
		conf.env.ENVNAME = i.name
		conf.msg(msg='--> ' + i.name, result='in progress', color='BLUE')
		# configure in standalone env
		conf.recurse(i.name)
		conf.msg(msg='<-- ' + i.name, result='done', color='BLUE')
		conf.setenv('')

def build(bld):
	bld.load_envs()
	for i in SUBDIRS:
		if bld.env.SINGLE_BINARY and i.singlebin:
			continue

		if bld.env.DEDICATED and i.dedicated:
			continue

		if i.ignore:
			continue

		bld.env = bld.all_envs[i.name]
		bld.recurse(i.name)
