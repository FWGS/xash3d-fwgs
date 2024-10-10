#! /usr/bin/env python
# encoding: utf-8
# mittorn, 2018

from waflib import Logs
import os

top = '.'

def options(opt):
	return

def configure(conf):
	return

def build(bld):
	bld.stlib(
		source   = bld.path.ant_glob(['src/*.cpp']),
		target   = 'MultiEmulator',
		features = 'cxx',
		includes = ['include/', 'src/'],
		export_includes = ['include/'],
		subsystem = bld.env.MSVC_SUBSYSTEM
	)
