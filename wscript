#! /usr/bin/env python
# encoding: utf-8
# a1batross, mittorn, 2018

from __future__ import print_function
from waflib import Logs, Options
import os
import sys

VERSION = '0.99'
APPNAME = 'xash3d-fwgs'
SUBDIRS = [ 'game_launch', 'mainui', 'vgui_support', 'engine' ]
top = '.'

def get_git_version():
	# try grab the current version number from git
	version = 'notset'
	if os.path.exists('.git'):
		try:
			version = os.popen('git describe --dirty --always').read().strip()
		except Exception as e:
			pass

	if(len(version) == 0):
		version = 'notset'

	return version

def get_flags_by_compiler(flags, compiler):
	out = []
	if compiler in flags:
		out += flags[compiler]
	elif 'default' in flags:
		out += flags['default']
	return out

def get_flags_by_type(flags, type, compiler):
	out = []
	if 'common' in flags:
		out += get_flags_by_compiler(flags['common'], compiler)
	if type in flags:
		out += get_flags_by_compiler(flags[type], compiler)
	return out

def options(opt):
	opt.load('compiler_cxx compiler_c')
	if sys.platform == 'win32':
		opt.load('msvc msvs')

	opt.add_option(
		'--dedicated', action = 'store_true', dest = 'DEDICATED', default = False,
		help = 'build Xash Dedicated Server(XashDS)')

	opt.add_option(
		'--64bits', action = 'store_true', dest = 'ALLOW64', default = False,
		help = 'allow targetting 64-bit engine')

	opt.add_option(
		'--win-style-install', action = 'store_true', dest = 'WIN_INSTALL', default = False,
		help = 'install like Windows build, ignore prefix, useful for development')

	opt.add_option(
		'--no-gcc-colors', action = 'store_false', dest = 'GCC_COLORS', default = True,
		help = 'do not enable gcc colors')

	opt.add_option(
		'--sdl2', action='store', type='string', dest = 'SDL2_PATH', default = None,
		help = 'SDL2 path to build(required for Windows)')

	opt.add_option(
		'--build-type', action='store', type='string', dest='BUILD_TYPE', default = None,
		help = 'build type: debug, release or none(custom flags)')

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

	conf.env.MSVC_TARGETS = ['x86'] # explicitly request x86 target for MSVC
	conf.load('compiler_cxx compiler_c')
	if sys.platform == 'win32':
		conf.load('msvc msvs')

	# Check if we have 64-bit toolchain
	conf.env.DEST_64BIT = False # predict state
	try:
		conf.check_cc(
			fragment='''int main( void )
			{
				int check[sizeof(void*) == 4 ? 1: -1];
				return 0;
			}''',
			msg	= 'Checking if compiler create 32 bit code')
	except conf.errors.ConfigurationError:
		# Program not compiled, we have 64 bit 
		conf.env.DEST_64BIT = True


	if(conf.env.DEST_64BIT):
		if(not conf.options.ALLOW64):
			flag = '-m32'
			# Think different.
			if(conf.env.DEST_OS == 'darwin'):
				flag = '-arch i386'
			conf.env.append_value('LINKFLAGS', [flag])
			conf.env.append_value('CFLAGS',    [flag])
			conf.env.append_value('CXXFLAGS',  [flag])
			Logs.info('NOTE: will build engine with 64-bit toolchain using %s' % flag)
		else:
			Logs.warn('WARNING: 64-bit engine may be unstable')

	linker_flags = {
		'common': {
			'msvc':    ['/DEBUG'],
			'default': ['-Wl,--no-undefined']
		}
	}

	compiler_c_cxx_flags = {
		'common': {
			'msvc':    ['/D_USING_V110_SDK71_'],
			'default': ['-g']
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

	conf.env.append_unique('CFLAGS', get_flags_by_type(
	    compiler_c_cxx_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC))
	conf.env.append_unique('CXXFLAGS', get_flags_by_type(
	    compiler_c_cxx_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC))
	conf.env.append_unique('LINKFLAGS', get_flags_by_type(
	    linker_flags, conf.options.BUILD_TYPE, conf.env.COMPILER_CC))

	# Force XP compability, all build targets should add
	# subsystem=bld.env.MSVC_SUBSYSTEM
	# TODO: wrapper around bld.stlib, bld.shlib and so on?
	conf.env.MSVC_SUBSYSTEM = 'WINDOWS,5.01'

	if(conf.env.DEST_OS == 'linux'):
		conf.check( lib='dl' )

	if(conf.env.DEST_OS != 'win32'):
		conf.check( lib='m' )
		conf.check( lib='pthread' )

	conf.env.DEDICATED     = conf.options.DEDICATED
	conf.env.SINGLE_BINARY = conf.options.DEDICATED # We don't need game launcher on dedicated

	# indicate if we are packaging for Linux/BSD
	if(not conf.options.WIN_INSTALL and 
		conf.env.DEST_OS != 'win32' and 
		conf.env.DEST_OS != 'darwin'):
		conf.env.LIBDIR = conf.env.BINDIR = '${PREFIX}/lib/xash3d'
	else:
		conf.env.LIBDIR = conf.env.BINDIR = conf.env.PREFIX

	conf.start_msg('Checking git hash')
	git_version = get_git_version()
	conf.end_msg(git_version)
	conf.env.append_unique('DEFINES', 'XASH_BUILD_COMMIT="' + git_version + '"')

	for i in SUBDIRS:
		conf.setenv(i, conf.env) # derive new env from global one
		conf.env.ENVNAME = i
		conf.msg(msg='Configuring ' + i, result='in progress', color='BLUE')
		# configure in standalone env
		conf.recurse(i)
		conf.msg(msg='Configuring ' + i, result='done', color='BLUE')
		conf.setenv('')

def build(bld):
	for i in SUBDIRS:
		bld.recurse(SUBDIRS)
