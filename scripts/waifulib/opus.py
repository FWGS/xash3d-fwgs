# encoding: utf-8

import os

def options(opt):
	pass

def configure(conf):
	path = conf.path.find_dir('3rdparty/opus')
	conf.env.LIB_OPUS = ['opus']
	conf.env.INCLUDES_OPUS = [path.find_dir('include/').abspath()]

def build(bld):
	path = bld.path.find_dir('3rdparty/opus')

	sources = path.ant_glob([
			'src/*.c',
			'celt/*.c',
			'silk/*.c',
			'silk/float/*.c'])

	includes = [
		path.find_dir('include/'),
		path.find_dir('celt/'),
		path.find_dir('silk/'),
		path.find_dir('silk/float/')
	]

	defines = [
		'USE_ALLOCA',
		'OPUS_BUILD',
		'PACKAGE_VERSION="1.3.1"'
	]

	bld.stlib(
		source = sources,
		target = 'opus',
		features = 'c',
		includes = includes,
		defines = defines,
		subsystem = bld.env.MSVC_SUBSYSTEM
	)