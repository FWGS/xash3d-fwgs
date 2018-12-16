# encoding: utf-8
# force_32bit.py -- force compiler to create 32-bit code
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
#   BIT32_MANDATORY(optional) -- fail if 32bit mode not available
#   BIT32_ALLOW64(optional)   -- ignore all checks, just set DEST_SIZEOF_VOID_P to 8
# Output:
#   DEST_SIZEOF_VOID_P -- an integer, equals sizeof(void*) on target

def check_32bit(ctx, msg):
	try:
		ctx.check_cc(
			fragment='''int main( void )
			{
				int check[sizeof(void*) == 4 ? 1: -1];
				return 0;
			}''',
			msg	= msg)
	except ctx.errors.ConfigurationError:
		return False
	return True

def configure(conf):
	if getattr(conf.env, 'BIT32_ALLOW64'):
		conf.env.DEST_SIZEOF_VOID_P = 8
	else:
		if check_32bit(conf, 'Checking if \'{0}\' can target 32-bit'.format(conf.env.COMPILER_CC)):
			conf.env.DEST_SIZEOF_VOID_P = 4
		else:
			flags = ['-m32']
			# Think different.
			if(conf.env.DEST_OS == 'darwin'):
				flags = ['-arch', 'i386']
			env_stash = conf.env
			conf.env.append_value('LINKFLAGS', flags)
			conf.env.append_value('CFLAGS',    flags)
			conf.env.append_value('CXXFLAGS',  flags)
			if check_32bit(conf, '...trying with additional flags'):
				conf.env.DEST_SIZEOF_VOID_P = 4
			else:
				conf.env.DEST_SIZEOF_VOID_P = 8
				conf.env = env_stash
		if getattr(conf.env, 'BIT32_MANDATORY') and conf.env.DEST_SIZEOF_VOID_P == 8:
			conf.fatal('Compiler can\'t create 32-bit code!')
