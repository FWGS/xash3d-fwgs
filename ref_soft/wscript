#! /usr/bin/env python
# encoding: utf-8
# mittorn, 2018

from waflib import Logs
import os

top = '.'

def options(opt):
	# stub
	return

def configure(conf):
	# check for dedicated server build
	if conf.options.DEDICATED:
		return

	if conf.options.SUPPORT_BSP2_FORMAT:
		conf.env.append_unique('DEFINES', 'SUPPORT_BSP2_FORMAT')

	conf.env.append_unique('DEFINES', 'REF_DLL')

def build(bld):
	if bld.env.DEDICATED:
		return

	libs = [ 'public', 'M' ]

	source = bld.path.ant_glob(['*.c'])

	includes = ['.',
		'../engine',
		'../engine/common',
		'../engine/server',
		'../engine/client',
		'../public',
		'../common',
		'../pm_shared' ]

	bld.shlib(
		source   = source,
		target   = 'ref_soft',
		features = 'c',
		includes = includes,
		use      = libs,
		install_path = bld.env.LIBDIR,
		subsystem = bld.env.MSVC_SUBSYSTEM
	)
