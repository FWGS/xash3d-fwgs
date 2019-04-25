#! /usr/bin/env python
# encoding: utf-8
# a1batross 2019

from __future__ import print_function
from waflib import Logs
import sys
import os

sys.path.append(os.path.realpath('scripts/waflib'))
# print(sys.path)

VERSION = '0.99'
APPNAME = 'xash3d-fwgs-android'
top = '.'

class Subproject:
	name      = ''
	options   = False # if true options() will be ignored
	ignore    = False # if true will be ignored, set by user request

	def __init__(self, name, options = False):
		self.name = name
		self.options = options

SUBDIRS = [
#	Subproject('xash3d-fwgs', True), # we set multiple hardcoded configurations, so ignore options
	Subproject('android')
]

def subdirs():
	return map(lambda x: x.name, SUBDIRS)

def options(opt):
	grp = opt.add_option_group('Common options')

	grp.add_option('-T', '--build-type', action='store', dest='BUILD_TYPE', default = None,
		help = 'build type: debug, release or none(custom flags)')

	grp.add_option('-S', '--skip-subprojects', action='store', dest = 'SKIP_SUBDIRS', default=None,
		help = 'don\'t recurse into specified subprojects. Current subdirs: ' + str(subdirs()))

	for i in SUBDIRS:
		if not os.path.isfile(os.path.join(i.name, 'wscript')):
			# HACKHACK: this way we get warning message right in the help
			# so this just becomes more noticeable
			opt.add_option_group('Cannot find wscript in ' + i.name + '. You probably missed submodule update')
			continue

		if i.options:
			continue

		opt.recurse(i.name)

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
		conf.fatal('Please set a build type, for example "--build-type=release"')
	elif not conf.options.BUILD_TYPE in ['release', 'debug', 'none']:
		conf.end_msg(conf.options.BUILD_TYPE, color='RED')
		conf.fatal('Invalid build type. Valid are "debug", "release" or "none"')
	conf.end_msg(conf.options.BUILD_TYPE)

	# skip some subdirectories, if requested
	if conf.options.SKIP_SUBDIRS:
		skip_subdirs = conf.options.SKIP_SUBDIRS.split(',')
		set_ignored_subdirs(skip_subdirs)

	for i in SUBDIRS:
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
	for i in SUBDIRS:
		if i.ignore:
			continue

		bld.load_envs()
		saveenv = bld.env
		bld.env = bld.all_envs[i.name]
		bld.recurse(i.name)
		bld.env = saveenv
