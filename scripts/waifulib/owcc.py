#! /usr/bin/env python
# encoding: utf-8

"""
Compiler definition for OpenWatcom's owcc
"""

from waflib import Errors, Utils
from waflib.Tools import ccroot, ar
from waflib.Configure import conf

@conf
def find_owcc(conf):
	v = conf.env
	cc = None
	if v.CC:
		cc = v.CC
	else:
		cc = conf.find_program('cc', var='CC')
	if not cc:
		conf.fatal('owcc was not found')

	try:
		out = conf.cmd_and_log(cc + ['-v'])
	except Errors.WafError:
		conf.fatal('%r -v could not be executed' % cc)
	if not 'Open Watcom' in out:
		conf.fatal('failed to detect owcc')

	v.CC = cc
	v.CC_NAME = 'owcc'
	v.CXX = v.CC
	v.CXX_NAME = v.cc_NAME
	if not v.AR:
		conf.find_program('wlib', var='AR')
	conf.add_os_flags('ARFLAGS')
	if not v.ARFLAGS:
		v.ARFLAGS = ['-fo']

@conf
def owcc_common_flags(conf):
	v = conf.env

	v.CC_SRC_F            = ''
	v.CXX_SRC_F            = ''
	v.CC_TGT_F            = ['-c', '-o']
	v.CXX_TGT_F            = ['-c', '-o']
	v.CPPPATH_ST          = '-I%s'
	v.DEFINES_ST          = '-D%s'

	if not v.LINK_CC:
		v.LINK_CC = v.CC
	if not v.LINK_CXX:
		v.LINK_CXX = v.CXX

	v.CCLNK_SRC_F         = ''
	v.CCLNK_TGT_F         = ['-o']
	v.CXXLNK_SRC_F         = ''
	v.CXXLNK_TGT_F         = ['-o']

	v.LIB_ST              = '-l%s' # template for adding libs
	v.LIBPATH_ST          = '-L%s' # template for adding libpaths
	v.STLIB_ST            = '-l%s'
	v.STLIBPATH_ST        = '-L%s'

	v.cprogram_PATTERN    = '%s.exe'
	v.cxxprogram_PATTERN    = '%s.exe'
	v.cshlib_PATTERN      = 'lib%s.so'
	v.cxxshlib_PATTERN      = 'lib%s.so'
	v.cstlib_PATTERN      = '%s.a'
	v.cxxstlib_PATTERN      = '%s.a'

def find_target(flags):
	if '-b' in flags:
		return flags[flags.index('-b')+1]

@conf
def owcc_detect_platform(conf):
	v = conf.env
	target = find_target(v.LINKFLAGS)
	if not target:
		target = find_target(v.CC)
	if not target:
		target = find_target(v.CFLAGS)
	if not target:
		target = Utils.unversioned_sys_platform()
	if target in ['dos4g', 'dos4gnz', 'dos32a', 'stub32a', 'stub32ac']:
		v.DEST_BINFMT = 'le'
		v.DEST_OS = 'dos'
	elif target in ['dos32x', 'stub32x', 'stub32xc']:
		v.DEST_BINFMT = 'lx'
		v.DEST_OS = 'dos'
	elif target.startswith('win') or target.startswith('nt'):
		v.DEST_BINFMT = 'pe'
		v.DEST_OS = 'win32'
	elif target == 'qnx386':
		v.DEST_OS = 'qnx'
		v.DEST_BINFMT = 'qnx'
	elif target in ['linux', '386']:
		v.DEST_OS = 'linux'
		v.DEST_BINFMT = 'elf'
	else:
		v.DEST_OS = target
		v.DEST_BINFMT = None

	v.DEST_CPU = 'i386'

	for f in v.LINKFLAGS + v.CC + v.CFLAGS:
		if f.startswith('-march'):
			v.DEST_CPU=f.split('=')[1]
			break


def configure(conf):
	conf.find_owcc()
	conf.owcc_common_flags()
	conf.cc_load_tools()
	conf.cc_add_flags()
	conf.env.append_unique('CFLAGS','-Wc,-xx')
	conf.cxx_load_tools()
	conf.cxx_add_flags()
	conf.env.append_unique('CXXFLAGS','-Wc,-xx')
	conf.link_add_flags()
	conf.owcc_detect_platform()
