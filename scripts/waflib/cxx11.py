# encoding: utf-8
# cxx11.py -- check if compiler can compile C++11 code with lambdas
# Copyright (C) 2018 a1batross
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

from fwgslib import get_flags_by_compiler

# Input:
#   CXX11_MANDATORY(optional) -- fail if C++11 not available
# Output:
#   HAVE_CXX11 -- true if C++11 available, otherwise else

def check_cxx11(ctx, msg):
	try:
		# not best way, but this check
		# was written for exactly mainui_cpp,
		# where lambdas are mandatory
		ctx.check_cxx(
			fragment='int main( void ){ auto pfnLambda = [](){}; return 0;}',
			msg	= msg)
	except ctx.errors.ConfigurationError:
		return False
	return True

def configure(conf):
	conf.env.HAVE_CXX11 = True # predict state
	if not check_cxx11(conf, 'Checking if \'{0}\' supports C++11'.format(conf.env.COMPILER_CC)):
		modern_cpp_flags = {
			'msvc':    [],
			'default': ['-std=c++11']
		}
		flags = get_flags_by_compiler(modern_cpp_flags, conf.env.COMPILER_CC)
		if(len(flags) == 0):
			conf.env.HAVE_CXX11 = False
		else:
			env_stash = conf.env
			conf.env.append_unique('CXXFLAGS', flags)
			if not check_cxx11(conf, '...trying with additional flags'):
				conf.env.HAVE_CXX11 = False
				conf.env = env_stash
	if getattr(conf.env, 'CXX11_MANDATORY'):
		conf.fatal('C++11 support not available!')

