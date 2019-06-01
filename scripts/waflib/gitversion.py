# encoding: utf-8
# gitversion.py -- waf plugin to get git version
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

def get_git_version():
	# try grab the current version number from git
	version = None
	if os.path.exists('.git'):
		try:
			version = os.popen('git describe --dirty --always').read().strip()
		except Exception as e:
			pass

		if(len(version) == 0):
			version = None

	return version

def configure(conf):
	conf.start_msg('Checking git hash')
	conf.env.GIT_VERSION = get_git_version()
	conf.end_msg(conf.env.GIT_VERSION if conf.env.GIT_VERSION else 'no')
