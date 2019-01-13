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
SUBDIRS = [ 'engine', 'game_launch', 'mainui', 'vgui_support' ]
top = '.'

def options(opt):
	opt.load('xcompile compiler_cxx compiler_c')
	if sys.platform == 'win32':
		opt.load('msvc msvs')

	opt.add_option(
		'--build-type', action='store', type='string', dest='BUILD_TYPE', default = None,
		help = 'build type: debug, release or none(custom flags)')

	opt.add_option(
		'--dedicated', action = 'store_true', dest = 'DEDICATED', default = False,
		help = 'build Xash Dedicated Server(XashDS)')

	opt.add_option(
		'--64bits', action = 'store_true', dest = 'ALLOW64', default = False,
		help = 'allow targetting 64-bit engine')

	opt.add_option(
		'--win-style-install', action = 'store_true', dest = 'WIN_INSTALL', default = False,
		help = 'install like Windows build, ignore prefix, useful for development')

	opt.recurse(SUBDIRS)

def configure(conf):
	conf.start_msg('Build type')
	if conf.options.BUILD_TYPE == None:
		conf.end_msg('not set', color='RED')
		conf.fatal('Please set a build type, for example "--build-type=release"')
	elif not conf.options.BUILD_TYPE in ['release', 'debug', 'none']:
		conf.end_msg(conf.options.BUILD_TYPE, color='RED')
		conf.fatal('Invalid build type. Valid are "debug", "release" or "none"')
	conf.end_msg(conf.options.BUILD_TYPE)

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
	conf.load('force_32bit')

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
			'msvc':    ['/Z7'],
			'clang':   ['-O0', '-gdwarf-2'],
			'default': ['-O0']
		}
	}

	conf.env.append_unique('CFLAGS', fwgslib.get_flags_by_type(
	    compiler_c_cxx_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC))
	conf.env.append_unique('CXXFLAGS', fwgslib.get_flags_by_type(
	    compiler_c_cxx_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC))
	conf.env.append_unique('LINKFLAGS', fwgslib.get_flags_by_type(
	    linker_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC))

	conf.env.DEDICATED     = conf.options.DEDICATED
	conf.env.SINGLE_BINARY = conf.options.DEDICATED # We don't need game launcher on dedicated

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

	conf.env.append_unique('DEFINES', 'XASH_BUILD_COMMIT="{0}"'.format(conf.env.GIT_VERSION if conf.env.GITVERSION else 'notset'))

	for i in SUBDIRS:
		conf.setenv(i, conf.env) # derive new env from global one
		conf.env.ENVNAME = i
		conf.msg(msg='--> ' + i, result='in progress', color='BLUE')
		# configure in standalone env
		conf.recurse(i)
		conf.msg(msg='<-- ' + i, result='done', color='BLUE')
		conf.setenv('')

def build(bld):
	bld.recurse(SUBDIRS)
