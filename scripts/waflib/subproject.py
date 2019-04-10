#!/usr/bin/env python
# encoding: utf-8
# Copyright (c) 2019 a1batross

'''
Subproject tool

Helps you have standalone environment for each subproject(subdirectory)

Usage::
		def configure(conf):
			conf.add_subproject('folder1 folder2')
		
		def build(bld):
			bld.add_subproject('folder1 folder2')
'''

from waflib import Configure, Logs, Options, Utils
import os

IGNORED_SUBDIRS = []
DEPTH = ''

def depth_push(path):
	global DEPTH
	
	DEPTH = os.path.join(DEPTH, path)
	# print DEPTH

def depth_pop():
	global DEPTH
	
	DEPTH = os.path.dirname(DEPTH)
	# print DEPTH

def depth():
	global DEPTH
	return DEPTH

def opt(f):
	"""
	Decorator: attach new option functions to :py:class:`waflib.Options.OptionsContext`.

	:param f: method to bind
	:type f: function
	"""
	setattr(Options.OptionsContext, f.__name__, f)
	return f

@opt
def add_subproject(ctx, names):
	names_lst = Utils.to_list(names)
	
	for name in names_lst:
		depth_push(name)
	
		wscript_path = os.path.join(depth(), 'wscript')
	
		if not os.path.isfile(wscript_path):
			# HACKHACK: this way we get warning message right in the help
			# so this just becomes more noticeable
			ctx.add_option_group('Cannot find wscript in ' + name + '. You probably missed submodule update')
		else:
			ctx.recurse(name)

		depth_pop()

def options(opt):
	grp = opt.add_option_group('Subproject options')
	
	grp.add_option('-S', '--skip-subprojects', action='store', dest = 'SKIP_SUBDIRS', default=None, 
		help = 'don\'t recurse into specified subprojects. Use only directory name.')

def get_subproject_env(ctx, path, log=False):
	# remove top dir path
	path = str(path)
	if path.startswith(ctx.top_dir):
		if ctx.top_dir[-1] != os.pathsep:
			path = path[len(ctx.top_dir) + 1:]
		else: path = path[len(ctx.top_dir):]

	# iterate through possible subprojects names
	folders = os.path.normpath(path).split(os.sep)
	# print(folders)
	for i in range(1, len(folders)+1):
		name = folders[-i]
		# print(name)
		if name in ctx.all_envs:
			if log: Logs.pprint('YELLOW', 'env: changed to %s' % name)
			return ctx.all_envs[name]
	if log: Logs.pprint('YELLOW', 'env: changed to default env')
	raise IndexError('top env')


def configure(conf):
	if conf.options.SKIP_SUBDIRS:
		global IGNORED_SUBDIRS
		IGNORED_SUBDIRS += conf.options.SKIP_SUBDIRS.split(',')
		print IGNORED_SUBDIRS

@Configure.conf
def add_subproject(ctx, names):
	global IGNORED_SUBDIRS
	names_lst = Utils.to_list(names)
	
	if isinstance(ctx, Configure.ConfigurationContext):
		for name in names_lst:
			depth_push(name)
			if name in IGNORED_SUBDIRS:
				ctx.msg(msg='--X %s' % depth(), result='ignored', color='YELLOW')
				depth_pop()
				return
			ctx.setenv(name, ctx.env) # derive new env from 
			ctx.env.ENVNAME = name
			ctx.msg(msg='--> %s' % depth(), result='in progress', color='BLUE')
			ctx.recurse(name)
			ctx.msg(msg='<-- %s' % depth(), result='done', color='BLUE')
			ctx.setenv('')
			depth_pop()
	else:
		if not ctx.all_envs:
			ctx.load_envs()
		for name in names_lst:
			if name in IGNORED_SUBDIRS:
				return
			ctx.env = ctx.all_envs[name]
			ctx.recurse(name)		
