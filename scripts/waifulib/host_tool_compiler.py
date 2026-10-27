#! /usr/bin/env python
# encoding: utf-8

# Environment for tools that run on the build machine while cross-compiling:
# the target compiler and its flags don't leak into it. These tools are only
# used to build the engine, so they don't follow the build type flags.
# Task generators use it with env = bld.all_envs[HOST_ENV].derive()

import sys
from waflib import Configure

HOST_ENV = 'host'

def options(opt):
	grp = opt.add_option_group('Host tools options')

	grp.add_option('--host-cc', action = 'store', dest = 'HOST_CC', default = None,
		help = 'C compiler for tools that run on the build machine [default: autodetect]')

	grp.add_option('--host-cxx', action = 'store', dest = 'HOST_CXX', default = None,
		help = 'C++ compiler for tools that run on the build machine [default: autodetect]')

@Configure.conf
def setup_host_env(conf):
	if HOST_ENV in conf.all_envs:
		return

	# xcompile puts the target compilers here
	saved = dict((k, conf.environ.pop(k)) for k in ('CC', 'CXX', 'AR') if k in conf.environ)

	if conf.options.HOST_CC:
		conf.environ['CC'] = conf.options.HOST_CC
	if conf.options.HOST_CXX:
		conf.environ['CXX'] = conf.options.HOST_CXX

	prev = conf.variant
	conf.setenv(HOST_ENV)
	conf.load('compiler_c compiler_cxx')
	# measured on glslang and opengnm-psbc: a third less build time than -O2,
	# negligible difference in the time they take to compile the shaders
	conf.env.CFLAGS = ['-O1']
	conf.env.CXXFLAGS = ['-O1']
	conf.env.DEFINES = ['NDEBUG']
	conf.env.HOST_BIG_ENDIAN = sys.byteorder == 'big'
	conf.setenv(prev)

	conf.environ.pop('CC', None)
	conf.environ.pop('CXX', None)
	conf.environ.update(saved)
