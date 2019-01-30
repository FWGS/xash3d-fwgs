# encoding: utf-8
# fwgslib.py -- utils for Waf build system by FWGS
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

import os

def get_subproject_name(ctx):
	return os.path.basename(os.path.realpath(str(ctx.path)))

def get_flags_by_compiler(flags, compiler):
	out = []
	if compiler in flags:
		out += flags[compiler]
	elif 'default' in flags:
		out += flags['default']
	return out

def get_flags_by_type(flags, type, compiler):
	out = []
	if 'common' in flags:
		out += get_flags_by_compiler(flags['common'], compiler)
	if type in flags:
		out += get_flags_by_compiler(flags[type], compiler)
	return out