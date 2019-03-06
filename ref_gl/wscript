#! /usr/bin/env python
# encoding: utf-8
# mittorn, 2018

from waflib import Logs
import os
from fwgslib import get_subproject_name

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

	bld.load_envs()
	name = get_subproject_name(bld)
	bld.env = bld.all_envs[name]

	libs = [ 'M' ]

	source = bld.path.ant_glob(['*.c'])

	source += [ '../engine/common/mathlib.c', '../engine/common/crtlib.c', '../engine/common/matrixlib.c' ]

	includes = ['.',
		'../engine',
		'../engine/common',
		'../engine/server',
		'../engine/client',
		'../common',
		'../pm_shared' ]

	bld.shlib(
		source   = source,
		target   = name,
		features = 'c',
		includes = includes,
		use      = libs,
		install_path = bld.env.LIBDIR,
		subsystem = bld.env.MSVC_SUBSYSTEM
	)
