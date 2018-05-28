#! /usr/bin/env python
# encoding: utf-8
# a1batross, mittorn, 2018

from __future__ import print_function
from waflib import Logs

import os
import sys

def get_git_version():
	# try grab the current version number from git
	version = "notset"
	if os.path.exists(".git"):
		try:
			version = os.popen("git describe --dirty --always").read().strip()
		except Exception as e:
			print(e)
	return version

VERSION = '0.99'
APPNAME = 'xash3d-fwgs'
GIT_SHA = get_git_version()
SUBDIRS = [ 'game_launch', 'vgui_support', 'engine' ]

top = '.'

def options(opt):
	opt.load('compiler_cxx compiler_c')
	if sys.platform == 'win32':
		opt.load('msvc')
	
	opt.add_option(
		'--dedicated', action = 'store_true', dest = 'DEDICATED', default=False,
		help = 'build Xash Dedicated Server(XashDS)')
	
	opt.add_option(
		'--64bits', action = 'store_true', dest = 'ALLOW64', default=False,
		help = 'allow targetting 64-bit engine')
	
	opt.add_option(
		'--release', action = 'store_true', dest = 'RELEASE', default=False,
		help = 'strip debug info from binary and enable optimizations')

	opt.recurse(SUBDIRS)
        
def configure(conf):
	conf.load('compiler_cxx compiler_c')
	conf.check_cc(
		fragment='''
		#include <stdio.h>
		int main( void ) { printf("%ld", sizeof( void * )); return 0; }
		''',
		execute      = True,
		define_ret   = True,
		uselib_store = 'SIZEOF_VOID_P',
		msg          = 'Checking sizeof(void*)')
	
	if(conf.env.SIZEOF_VOID_P != '4' and not conf.options.ALLOW64):
		conf.env.append_value('LINKFLAGS', '-m32')
		conf.env.append_value('CFLAGS', '-m32')
		conf.env.append_value('CXXFLAGS', '-m32')
		Logs.info('NOTE: will build engine with 64-bit toolchain using -m32')
	else:
		Logs.warn('WARNING: 64-bit engine may be unstable')
	
	if(conf.env.COMPILER_CC == 'gcc'):
		conf.env.append_value('LINKFLAGS', '-Wl,--no-undefined')
	
	if(conf.options.RELEASE):
		conf.env.append_unique('CFLAGS', '-O2')
	else:
		conf.env.append_unique('CFLAGS', '-Og')
		conf.env.append_unique('CFLAGS', '-g')

		
	conf.check( lib='dl' )
	conf.check( lib='m' )
	conf.check( lib='pthread' )
	
	conf.env.DEDICATED     = conf.options.DEDICATED
	conf.env.SINGLE_BINARY = conf.options.DEDICATED
	
	# global
	conf.env.append_unique('XASH_BUILD_COMMIT', GIT_SHA)
	
	for i in SUBDIRS:
		conf.setenv(i, conf.env) # derive new env from global one
		conf.env.ENVNAME = i
		Logs.info('Configuring ' + i)
		# configure in standalone env
		conf.recurse(i)
		conf.setenv('')

def build(bld):
	for i in SUBDIRS:
		bld.recurse(SUBDIRS)
