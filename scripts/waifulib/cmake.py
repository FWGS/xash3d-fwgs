#! /usr/bin/env python
# -*- encoding: utf-8 -*-
# Licensed under The MIT License (MIT)
# Copyright (c) 2016, Michel Mooij
# Copyright (c) 2023, Velaron

'''
Summary
-------
Generate *cmake* files of all C/C++ programs, static- and shared libraries
that have been defined within a *waf* build environment.
Once exported to *cmake*, all exported (C/C++) tasks can be build without
any further need for, or dependency, to the *waf* build system itself.

**cmake** is an open source cross-platform build system designed to build, test
and package software. It is available for all major Desktop Operating Systems
(MS Windows, all major Linux distributions and Macintosh OS-X).
See http://www.cmake.org for a more detailed description on how to install
and use it for your particular Desktop environment.


Description
-----------
When exporting *waf* project data, a single top level **CMakeLists.txt** file
will be exported in the top level directory of your *waf* build environment.
This *cmake* build file will contain references to all exported *cmake*
build files of each individual C/C++ build task. It will also contain generic
variables and settings (e.g compiler to use, global preprocessor defines, link
options and so on).

Example below presents an overview of an environment in which *cmake*
build files already have been exported::

        .
        ├── components
        │   └── clib
        │       ├── program
        │       │   ├── CMakeLists.txt
        │       │   └── wscript
        │       ├── shared
        │       │   ├── CMakeLists.txt
        │       │   └── wscript
        │       └── static
        │           ├── CMakeLists.txt
        │           └── wscript
        │
        ├── CMakeLists.txt
        └── wscript


Usage
-----
Tasks can be exported to *cmake* using the command, as shown in the
example below::

        $ waf cmake

All exported *cmake* build files can be removed in 'one go' using the *cmake*
*cleanup* option::

        $ waf cmake --cmake-clean

Tasks generators to be excluded can be marked with the *skipme* option
as shown below::

    def build(bld):
        bld.program(name='foo', src='foobar.c', cmake_skip=True)

'''


from waflib.Build import BuildContext
from waflib import Utils, Logs, Context, Errors


def get_deps(bld, target):
	'''Returns a list of (nested) targets on which this target depends.

	:param bld: a *waf* build instance from the top level *wscript*
	:type bld: waflib.Build.BuildContext
	:param target: task name for which the dependencies should be returned
	:type target: str
	:returns: a list of task names on which the given target depends
	'''
	try:
		tgen = bld.get_tgen_by_name(target)
	except Errors.WafError:
		return []
	else:
		uses = Utils.to_list(getattr(tgen, 'use', []))
		deps = uses[:]
		for use in uses:
			deps += get_deps(bld, use)
		return list(set(deps))


def get_tgens(bld, names):
	'''Returns a list of task generators based on the given list of task
	generator names.

	:param bld: a *waf* build instance from the top level *wscript*
	:type bld: waflib.Build.BuildContext
	:param names: list of task generator names
	:type names: list of str
	:returns: list of task generators
	'''
	tgens = []
	for name in names:
		try:
			tgen = bld.get_tgen_by_name(name)
		except Errors.WafError:
			pass
		else:
			tgens.append(tgen)
	return list(set(tgens))


def get_targets(bld):
	'''Returns a list of user specified build targets or None if no specific
	build targets has been selected using the *--targets=* command line option.

	:param bld: a *waf* build instance from the top level *wscript*.
	:type bld: waflib.Build.BuildContext
	:returns: a list of user specified target names (using --targets=x,y,z) or None
	'''
	if bld.targets == '':
		return None
	targets = bld.targets.split(',')
	for target in targets:
		targets += get_deps(bld, target)
	return targets


def options(opt):
	'''Adds command line options for the CMake *waftool*.

	:param opt: Options context from the *waf* build environment.
	:type opt: waflib.Options.OptionsContext
	'''
	opt.add_option('--cmake', dest='cmake', default=False, action='store_true', help='select cmake for export/import actions')
	opt.add_option('--cmake-clean', dest='cmake_clean', default=False, action='store_true', help='delete exported cmake files')


def configure(conf):
	'''Method that will be invoked by *waf* when configuring the build
	environment.

	:param conf: Configuration context from the *waf* build environment.
	:type conf: waflib.Configure.ConfigurationContext
	'''
	conf.find_program('cmake', var='CMAKE', mandatory=False)


class CMakeContext(BuildContext):
	'''export C/C++ tasks to CMake.'''
	cmd = 'cmake'

	def execute(self):
		'''Will be invoked when issuing the *cmake* command.'''
		self.restore()
		if not self.all_envs:
			self.load_envs()
		self.recurse([self.run_dir])
		self.pre_build()

		for group in self.groups:
			for tgen in group:
				try:
					f = tgen.post
				except AttributeError:
					pass
				else:
					f()
		try:
			self.get_tgen_by_name('')
		except Exception:
			pass

		self.cmake = True
		if self.options.cmake_clean:
			cleanup(self)
		else:
			export(self)
		self.timer = Utils.Timer()


def export(bld):
	'''Exports all C and C++ task generators to cmake.

	:param bld: a *waf* build instance from the top level *wscript*.
	:type bld: waflib.Build.BuildContext
	'''
	if not bld.options.cmake and not hasattr(bld, 'cmake'):
		return

	cmakes = {}
	loc = bld.path.relpath().replace('\\', '/')
	top = CMake(bld, loc)
	cmakes[loc] = top
	targets = get_targets(bld)

	for tgen in bld.task_gen_cache_names.values():
		if targets and tgen.get_name() not in targets:
			continue
		if getattr(tgen, 'cmake_skip', False):
			continue
		if set(('c', 'cxx')) & set(getattr(tgen, 'features', [])):
			loc = tgen.path.relpath().replace('\\', '/')
			if loc not in cmakes:
				cmake = CMake(bld, loc)
				cmakes[loc] = cmake
				top.add_child(cmake)
			cmakes[loc].add_tgen(tgen)

	for cmake in cmakes.values():
		cmake.export()


def cleanup(bld):
	'''Removes all generated makefiles from the *waf* build environment.

	:param bld: a *waf* build instance from the top level *wscript*.
	:type bld: waflib.Build.BuildContext
	'''
	if not bld.options.cmake_clean:
		return

	loc = bld.path.relpath().replace('\\', '/')
	CMake(bld, loc).cleanup()
	targets = get_targets(bld)

	for tgen in bld.task_gen_cache_names.values():
		if targets and tgen.get_name() not in targets:
			continue
		if getattr(tgen, 'cmake_skip', False):
			continue
		if set(('c', 'cxx')) & set(getattr(tgen, 'features', [])):
			loc = tgen.path.relpath().replace('\\', '/')
			CMake(bld, loc).cleanup()


class CMake(object):
	def __init__(self, bld, location):
		self.bld = bld
		self.location = location
		self.cmakes = []
		self.tgens = []

	def export(self):
		content = self.get_content()
		if not content:
			return

		node = self.make_node()
		if not node:
			return
		node.write(content)
		Logs.pprint('YELLOW', 'exported: %s' % node.abspath())

	def cleanup(self):
		node = self.find_node()
		if node:
			node.delete()
			Logs.pprint('YELLOW', 'removed: %s' % node.abspath())

	def add_child(self, cmake):
		self.cmakes.append(cmake)

	def add_tgen(self, tgen):
		self.tgens.append(tgen)

	def get_location(self):
		return self.location

	def get_fname(self):
		name = '%s/CMakeLists.txt' % (self.location)
		return name

	def find_node(self):
		name = self.get_fname()
		if not name:
			return None
		return self.bld.srcnode.find_node(name)

	def make_node(self):
		name = self.get_fname()
		if not name:
			return None
		return self.bld.srcnode.make_node(name)

	def get_content(self):
		is_top = (self.location == self.bld.path.relpath())

		content = ''
		if is_top:
			content += 'cmake_minimum_required(VERSION 2.8.12)\n'
			content += 'project(%s)\n' % (getattr(Context.g_module,
												  Context.APPNAME))
			content += '\n'

			env = self.bld.env
			defines = env.DEFINES
			if len(defines):
				content += 'add_definitions(\n	-D%s\n)\n' % (
					'\n	-D'.join(defines))
				content += '\n'

			flags = env.CFLAGS
			if len(flags):
				# remove -MMD flag from gccdeps.py as it's already inserted by CMake
				flags = [f for f in flags if not f == '-MMD']
				content += 'set(CMAKE_C_FLAGS "%s")\n' % (' '.join(flags))

			flags = env.CXXFLAGS
			if len(flags):
				flags = [f for f in flags if not f == '-MMD']
				content += 'set(CMAKE_CXX_FLAGS "%s")\n' % (' '.join(flags))

		if len(self.tgens):
			content += '\n'
			for tgen in self.tgens:
				content += self.get_tgen_content(tgen)

		if len(self.cmakes):
			content += '\n'
			for cmake in self.cmakes:
				content += 'add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/%s)\n' % (
					cmake.get_location())

		return content

	def get_tgen_content(self, tgen):
		content = ''
		name = tgen.get_name()

		content += 'set(%s_SRC' % (name.upper())
		for src in tgen.source:
			content += '\n	%s' % (src.path_from(tgen.path).replace('\\', '/'))
		content += '\n)\n\n'

		includes = self.get_includes(tgen)
		# includes.extend(tgen.env.INCLUDES)
		if len(includes):
			content += 'set(%s_INCLUDES' % (name.upper())
			for include in includes:
				content += '\n	%s' % include
			content += '\n)\n\n'
			content += 'include_directories(${%s_INCLUDES})\n' % (name.upper())

		link_dirs = getattr(tgen.env, 'LIBPATH', [])
		if len(link_dirs):
			content += '\nlink_directories('
			for dir in link_dirs:
				content += '\n	\"%s\"' % dir.replace('\\', '/')
			content += '\n)\n\n'

		if set(('cprogram', 'cxxprogram')) & set(tgen.features):
			if tgen.env.DEST_OS == 'win32':
				content += 'add_executable(%s WIN32 ${%s_SRC})\n' % (
					name, name.upper())
			else:
				content += 'add_executable(%s ${%s_SRC})\n' % (name,
															   name.upper())

		elif set(('cshlib', 'cxxshlib')) & set(tgen.features):
			content += 'add_library(%s SHARED ${%s_SRC})\n\n' % (
				name, name.upper())

		else:  # cstlib, cxxstlib or objects
			content += 'add_library(%s ${%s_SRC})\n\n' % (name, name.upper())

		defines = self.get_genlist(tgen, 'defines')
		defines.extend(tgen.env.DEFINES)
		if len(defines):
			content += 'target_compile_definitions(%s PRIVATE\n	-D%s\n)\n' % (
				name, '\n	-D'.join(defines))
			content += '\n'

		libs = getattr(tgen.env, 'LIB', [])
		libs.extend(tgen.env.STLIB)

		if len(libs):
			content += '\n'
			content += 'target_link_libraries(%s\n	%s)\n' % (name, '\n	'.join(libs))
			content += '\n'

		return content

	def get_includes(self, tgen):
		'''returns the include paths for the given task generator.
		'''
		includes = self.get_genlist(tgen, 'includes')
		for use in getattr(tgen, 'use', []):
			key = 'INCLUDES_%s' % use
			try:
				tg = self.bld.get_tgen_by_name(use)
				if 'fake_lib' in tg.features:
					if key in tgen.env:
						includes.extend([l.replace('\\', '/')
										for l in tgen.env[key]])
			except Errors.WafError:
				if key in tgen.env:
					includes.extend([l.replace('\\', '/')
									for l in tgen.env[key]])
		return includes

	def get_genlist(self, tgen, name):
		lst = Utils.to_list(getattr(tgen, name, []))
		lst = [str(l.path_from(tgen.path)) if hasattr(
			l, 'path_from') else l for l in lst]
		return [l.replace('\\', '/') for l in lst]
