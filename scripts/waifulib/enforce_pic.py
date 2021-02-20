# encoding: utf-8
# enforce_pic.py -- enforcing PIC if requested
# Copyright (C) 2021 a1batross
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

from waflib.Configure import conf

def options(opt):
	pass

def configure(conf):
	pass

@conf
def check_pic(conf, enable):
	if enable:
		# Every static library must have fPIC
		if conf.env.DEST_OS != 'win32' and '-fPIC' in conf.env.CFLAGS_cshlib:
			conf.env.append_unique('CFLAGS_cstlib', '-fPIC')
			conf.env.append_unique('CXXFLAGS_cxxstlib', '-fPIC')
	else:
		if '-fPIC' in conf.env.CFLAGS_cshlib:
			conf.env.CFLAGS_cshlib.remove('-fPIC')
		if '-fPIC' in conf.env.CXXFLAGS_cshlib:
			conf.env.CXXFLAGS_cxxshlib.remove('-fPIC')
		if '-fPIC' in conf.env.CFLAGS_MACBUNDLE:
			conf.env.CFLAGS_MACBUNDLE.remove('-fPIC')
		if '-fPIC' in conf.env.CXXFLAGS_MACBUNDLE:
			conf.env.CXXFLAGS_MACBUNDLE.remove('-fPIC')
