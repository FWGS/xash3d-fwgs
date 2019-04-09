#!/usr/bin/env python
# -*- encoding: utf-8 -*-
# Michel Mooij, michel.mooij7@gmail.com
# modified: Alibek Omarov, a1ba.omarov@gmail.com

'''
Summary
-------
Exports and converts *waf* project data, for C/C++ programs, static- and shared
libraries, into **Microsoft Visual Studio**, also known as **msdev**, 
project files (.vcproj) and solution (.sln) files.

**Microsoft Visual Studio** is a mature and stable integrated development 
environment for, amongst others, the C and C++ programming language. A free
version of this IDE, known as the *express* version can be obtained from Microsoft
at http://wwww.visualstudio.com.

Description
-----------
When exporting *waf* project data, a single **Visual Studio** solution will be
exported in the top level directory of your *WAF* build environment. This 
solution file will contain references to all exported **Visual Studio** 
projects and will include dependencies between those projects and will have the
same name as APPNAME variable from the top level *wscript* file.

For each single task generator (*waflib.TaskGenerator*), for instance a 
*bld.program(...)* which has been defined within a *wscript* file somewhere in
the build environment, a single **Visual Studio** project file will be generated
in the same directory as where the task generator has been defined.
The name of this task generator will be used as name for the exported **Visual
Studio** project file. If for instance the name of the task generator is 
*hello*, then a **Visual Studio** project file named *hello.vcproj* will be
exported.

Example below presents an overview of an environment in which **Visual Studio** 
files already have been exported::

        .
        ├── components
        │   └── clib
        │       ├── program
        │       │   ├── cprogram.vcproj
        │       │   └── wscript
        │       ├── shared
        │       │   ├── cshlib.vcproj
        │       │   └── wscript
        │       └── static
        │           ├── cstlib.vcproj
        │           └── wscript
        │
        ├── waf.vcproj
        ├── appname.sln
        └── wscript


Projects will be exported such that they will use the same settings and 
structure as has been defined for that build task within the *waf* build 
environment as much as possible. Note that since cross compilation is not 
really supported in this IDE, only the first environment encountered that
is targeted for **MS Windows** will be exported; i.e. an environment in 
which::

    bld.env.DEST_OS == 'win32'

is true.


Please note that in contrast to a *normal* IDE setup the exported projects 
will contain either a *debug* **or** a *release* build target but not both at
the same time. By doing so exported projects will always use the same settings
(e.g. compiler options, installation paths) as when building the same task in
the *waf* build environment from command line.


Usage
-----
**Visual Studio** project and workspace files can be exported using the *msdev* 
command, as shown in the example below::

    $ waf msdev

When needed, exported **Visual Studio** project- and solution files can be 
removed using the *clean* command, as shown in the example below::

    $ waf msdev --clean

Once exported simply open the *appname.sln* using **Visual Studio**
this will automatically open all exported projects as well.

Tasks generators to be excluded can be marked with the *skipme* option 
as shown below::

    def build(bld):
        bld.program(name='foo', src='foobar.c', msdev_skip=True)

'''

import os
import sys
import copy
import uuid
import shutil
import xml.etree.ElementTree as ElementTree
from xml.dom import minidom
from waflib import Utils, Logs, Errors, Context
from waflib.Build import BuildContext
# import waftools
# from waftools import deps
from deps import get_targets


def options(opt):
	'''Adds command line options to the *waf* build environment 

	:param opt: Options context from the *waf* build environment.
	:type opt: waflib.Options.OptionsContext
	'''
	opt.add_option('--msdev', dest='msdev', default=False, action='store_true', help='select msdev for export/import actions')
	opt.add_option('--clean', dest='clean', default=False, action='store_true', help='delete exported files')


def configure(conf):
	'''Method that will be invoked by *waf* when configuring the build 
	environment.
	
	:param conf: Configuration context from the *waf* build environment.
	:type conf: waflib.Configure.ConfigurationContext
	'''
	pass


class MsDevContext(BuildContext):
	'''export C/C++ tasks to MS Visual Studio projects and solutions.'''
	cmd = 'msdev'

	def execute(self):
		'''Will be invoked when issuing the *msdev* command.'''
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
		
		self.msdev = True
		if self.options.clean:
			cleanup(self)
		else:
			export(self)
		self.timer = Utils.Timer()

def get_subproject_env(bld, path):
	# remove top dir path
	path = str(path)
	if path.startswith(bld.top_dir):
		if bld.top_dir[-1] != os.pathsep:
			path = path[len(bld.top_dir) + 1:]
		else: path = path[len(bld.top_dir):]

	# iterate through possible subprojects names
	folders = os.path.normpath(path).split(os.sep)
	# print(folders)
	for i in range(1, len(folders)+1):
		name = folders[-i]
		# print(name)
		if name in bld.all_envs:
			Logs.pprint('YELLOW', 'env: changed to %s' % name)
			return bld.all_envs[name]
	Logs.pprint('YELLOW', 'env: changed to default env')
	raise IndexError('top env')


def export(bld):
	'''Exports all C and C++ task generators as **Visual Studio** projects
	and creates a **Visual Studio** solution containing references to 
	those project.

	:param bld: a *waf* build instance from the top level *wscript*.
	:type bld: waflib.Build.BuildContext
	'''
	if not bld.options.msdev and not hasattr(bld, 'msdev'):
		return

	Logs.pprint('RED', '''This tool is intended only to ease development for Windows-fags.
Don't use it for release builds, as it doesn't enables WinXP compatibility for now!''')

	solution = MsDevSolution(bld)
	targets = get_targets(bld)

	saveenv = bld.env # root env
	for tgen in bld.task_gen_cache_names.values():
		if targets and tgen.get_name() not in targets:
			continue
		if getattr(tgen, 'msdev_skipme', False):
			continue
		try:
			bld.env = get_subproject_env(bld, tgen.path)
		except IndexError:
			bld.env = saveenv
		if set(('c', 'cxx')) & set(getattr(tgen, 'features', [])):
			project = MsDevProject(bld, tgen)
			project.export()

			(name, fname, deps, pid) = project.get_metadata()
			solution.add_project(name, fname, deps, pid)

	solution.export()


def cleanup(bld):
	'''Removes all **Visual Studio** projects and workspaces from the 
	*waf* build environment.
	
	:param bld: a *waf* build instance from the top level *wscript*.
	:type bld: waflib.Build.BuildContext
	'''
	if not bld.options.msdev and not hasattr(bld, 'msdev'):
		return

	targets = get_targets(bld)
	saveenv = bld.env

	for tgen in bld.task_gen_cache_names.values():
		if targets and tgen.get_name() not in targets:
			continue
		if getattr(tgen, 'msdev_skipme', False):
			continue
		try:
			bld.env = get_subproject_env(bld, tgen.path)
		except IndexError:
			bld.env = saveenv
		if set(('c', 'cxx')) & set(getattr(tgen, 'features', [])):
			project = MsDevProject(bld, tgen)
			project.cleanup()

	solution = MsDevSolution(bld)
	solution.cleanup()


class MsDev(object):
	'''Abstract base class used for exporting *waf* project data to 
	**Visual Studio** projects and solutions.

	REMARK:
	bld.objects() taks generators are treated as static libraries.
	
	:param bld: Build context as used in *wscript* files of your *waf* build
	            environment.
	:type bld:  waflib.Build.BuildContext
	'''

	PROGRAM = '1'
	'''Identifier for projects containing an executable'''

	SHLIB   = '2'
	'''Identifier for projects containing a shared library'''
	
	STLIB   = '4'
	'''Identifier for projects containing a static library'''

	C = 'c'
	'''Identifier for projects using C language'''
	
	CXX = 'cxx'
	'''Identifier for projects using C++ language'''
	
	def __init__(self, bld):
		self.bld = bld

	def export(self):
		'''Exports a **Visual Studio** solution or project.'''
		content = self.get_content()
		if not content:
			return
		if self.xml_clean:
			content = self.xml_clean(content)

		node = self.make_node()
		if not node:
			return
		node.write(content)
		Logs.pprint('YELLOW', 'exported: %s' % node.abspath())

	def cleanup(self):
		'''Deletes a **Visual Studio** solution or project file including 
		associated files (e.g. *.ncb*).
		'''
		cwd = self.get_cwd()
		for node in cwd.ant_glob('*.user'):
			node.delete()
			Logs.pprint('YELLOW', 'removed: %s' % node.abspath())
		for node in cwd.ant_glob('*.ncb'):
			node.delete()
			Logs.pprint('YELLOW', 'removed: %s' % node.abspath())
		for node in cwd.ant_glob('*.suo'):
			node.delete()
			Logs.pprint('YELLOW', 'removed: %s' % node.abspath())
		for node in cwd.ant_glob('*.sln'):
			node.delete()
			Logs.pprint('YELLOW', 'removed: %s' % node.abspath())
		node = self.find_node()
		if node:
			node.delete()
			Logs.pprint('YELLOW', 'removed: %s' % node.abspath())

	def get_cwd(self):
		cwd = os.path.dirname(self.get_fname())
		if cwd == "":
			cwd = "."
		return self.bld.srcnode.find_node(cwd)

	def find_node(self):
		name = self.get_fname()
		if not name:
			return None    
		return self.bld.srcnode.find_node(name)

	def make_node(self):
		name = self.get_fname()
		if not name:
			return None    
		return self.bld.srcnode.make_node(name.lower())

	def get_fname(self):
		'''<abstract> Returns file name.'''
		return None

	def get_content(self):
		'''<abstract> Returns file content.'''
		return None

	def xml_clean(self, content):
		s = minidom.parseString(content).toprettyxml(indent="\t")
		lines = [l for l in s.splitlines() if not l.isspace() and len(l)]
		lines[0] = '<?xml version="1.0" encoding="UTF-8"?>'
		return '\n'.join(lines)


class MsDevSolution(MsDev):
	'''Class used for exporting *waf* project data to a **Visual Studio** 
	solution located in the lop level directory of the *waf* build
	environment.

	:param bld: Build context as used in *wscript* files of your *waf* build
	            environment.
	:type bld:  waflib.Build.BuildContext
	'''
	
	def __init__(self, bld):
		super(MsDevSolution, self).__init__(bld)
		self.projects = {}
		self.xml_clean = None

	def get_fname(self):
		'''Returns the workspace's file name.'''
		return '%s.sln' % getattr(Context.g_module, Context.APPNAME)

	def export(self):
		'''Exports a **Visual Studio** solution.'''
		src = '%s/msdev.sln' % os.path.dirname(__file__)
		if not os.path.exists(src):
			self.bld.fatal("file not found: '%s'" % src)
		dst = self.get_fname()
		shutil.copyfile(src, dst)

		with open(src, 'r') as f:
			s = f.readlines()

		with open(dst, 'w') as f:
			for line in s[0:3]:
				f.write(line)
			for name, (fname, deps, pid) in self.projects.items():
				sid = str(uuid.uuid4()).upper()
				f.write('Project("{%s}") = "%s", "%s", "{%s}"\n' % (sid, name, fname, pid))
				if len(deps):
					f.write('\tProjectSection(ProjectDependencies) = postProject\n')
					for d in deps:
						try:
							(_, _, pid) = self.projects[d]
						except KeyError:
							pass
						else:
							f.write('\t\t{%s} = {%s}\n' % (pid, pid))
					f.write('\tEndProjectSection\n')
				f.write('EndProject\n')
			for line in s[3:8]:
				f.write(line)
			for _, (_, _, pid) in self.projects.items():
				f.write('\t\t{%s}.Debug|Win32.ActiveCfg = Debug|Win32\n' % (pid))
				f.write('\t\t{%s}.Debug|Win32.Build.0 = Debug|Win32\n' % (pid))
			for line in s[8:]:
				f.write(line)
		Logs.pprint('YELLOW', 'exported: %s' % os.path.abspath(dst))

	def add_project(self, name, fname, deps, pid):
		'''Adds a project to the workspace.
		
		:param name:    Name of the project.
		:type name:     str
		:param fname:   Complete path to the project file
		:type fname:    str
		:param deps:    List of names on which this project depends
		:type deps:     list of str
		'''
		self.projects[name] = (fname, deps, pid)


class MsDevProject(MsDev):
	'''Class used for exporting *waf* project data to **Visual Studio** 
	projects.

	:param bld: Build context as used in *wscript* files of your *waf* build
	            environment.
	:type bld:  waflib.Build.BuildContext
	
	:param gen: Task generator that contains all information of the task to be
	            converted and exported to the **Visual Studio** project.
	:type gen:  waflib.Task.TaskGen
	'''

	def __init__(self, bld, gen):
		super(MsDevProject, self).__init__(bld)
		self.gen = gen
		self.id = str(uuid.uuid4()).upper()
		self.type = self.get_type(gen)
		self.language = self.get_language(gen)
		self.buildpath = self.get_buildpath(bld, gen)
		
	def get_type(self, gen):
		if set(('cprogram', 'cxxprogram')) & set(gen.features):
			return MsDev.PROGRAM
		elif set(('cshlib', 'cxxshlib')) & set(gen.features):
			return MsDev.SHLIB
		else:
			return MsDev.STLIB
			
	def get_language(self, gen):
		return MsDev.CXX if 'cxx' in gen.features else MsDev.C
	
	def get_buildpath(self, bld, gen):
		pth = '%s/%s' % (bld.path.get_bld().path_from(gen.path), gen.path.relpath())
		return pth.replace('/', '\\')	

	def get_fname(self):
		'''Returns the project's file name.'''
		return '%s/%s.vcproj' % (self.gen.path.relpath().replace('\\', '/'), self.gen.get_name())

	def get_root(self):
		'''Returns a document root, either from an existing file, or from template.'''
		fname = self.get_fname()
		if os.path.exists(fname):
			tree = ElementTree.parse(fname)
			root = tree.getroot()
		else:
			root = ElementTree.fromstring(MSDEV_PROJECT)
		return root

	def get_content(self):
		'''Returns the content of a project file.'''
		root = self.get_root()
		root.set('Name', self.gen.get_name())
		root.set('ProjectGUID', '{%s}' % self.id)
		configurations = root.find('Configurations')
		for configuration in configurations.iter('Configuration'):
			configuration.set('ConfigurationType', '%s' % self.type)
			configuration.set('OutputDirectory', '%s\\msdev' % self.buildpath)
			configuration.set('IntermediateDirectory', '%s\\msdev' % self.buildpath)
			for tool in configuration.iter('Tool'):
				name = tool.get('Name')
				if name == 'VCCLCompilerTool':
					tool.set('PreprocessorDefinitions', '%s' % self.get_compiler_defines(self.gen))
					includes = []
					for include in self.get_compiler_includes(self.bld, self.gen):
						includes.append('%s' % include)
					tool.set('AdditionalIncludeDirectories', ';'.join(includes))
				if name == 'VCLinkerTool':
					if self.type == MsDev.PROGRAM:
						# Force Windows Subsystem
						# TODO: this isn't enables Windows XP compatibility!
						tool.set('SubSystem', '2')
					self.update_link_deps(tool)
					self.update_link_paths(tool)
		files = root.find('Files')
		self.update_includes(files)
		self.update_sources(files)
		return ElementTree.tostring(root)

	def update_includes(self, files):
		'''Add include files.'''
		includes = []
		for filtr in files.iter('Filter'):
			if filtr.get('Name') == 'Header Files':
				for include in filtr.iter('File'):
					includes.append(include.get('RelativePath'))
				break
		if len(includes) == 0:
			filtr = ElementTree.SubElement(files, 'Filter', attrib={'Name':'Header Files', 'Filter':'h;hpp;hxx;hm;inl;inc;xsd'})
			filtr.set('UniqueIdentifier', '{%s}' % str(uuid.uuid4()).upper())
		for include in self.get_include_files(self.bld, self.gen):
			if include not in includes:
				ElementTree.SubElement(filtr, 'File', attrib={'RelativePath':'%s' % include})

	def update_sources(self, files):
		'''Add source files.'''
		sources = []
		for filtr in files.iter('Filter'):
			if filtr.get('Name') == 'Source Files':
				for source in filtr.iter('File'):
					sources.append(source.get('RelativePath'))
				break
		if len(sources) == 0:
			filtr = ElementTree.SubElement(files, 'Filter', attrib={'Name':'Source Files', 'Filter':'cpp;c;cc;cxx;def;odl;idl;hpj;bat;asm;asmx'})
			filtr.set('UniqueIdentifier', '{%s}' % str(uuid.uuid4()).upper())
		for source in self.get_genlist(self.gen, 'source'):
			if source not in sources:
				ElementTree.SubElement(filtr, 'File', attrib={'RelativePath':'%s' % source})

	def update_link_deps(self, tool):
		'''Add libraries on which this project depends.'''
		deps = tool.get('AdditionalDependencies')

		deps = [] # clean out deps everytime

		libs = self.get_link_libs(self.bld, self.gen)
		for lib in libs:
			dep = '%s.lib' % lib
			if dep not in deps:
				deps.append(dep)
		if len(deps):
			add_deps = " ".join(deps) # work around when converting to vcxproj by inserting spaces
			tool.set('AdditionalDependencies', add_deps)

	def update_link_paths(self, tool):
		deps = tool.get('AdditionalLibraryDirectories', '')
		deps = []
		dirs = self.get_link_paths(self.bld, self.gen)
		for dep in dirs:
			if dep not in deps:
				deps.append(dep)
		if len(deps):
			tool.set('AdditionalLibraryDirectories', ';'.join(deps))

	def get_metadata(self):
		'''Returns a tuple containing project information (name, file name and 
		dependencies).
		'''
		name = self.gen.get_name()
		fname = self.get_fname().replace('/', '\\')
		deps = Utils.to_list(getattr(self.gen, 'use', []))
		return (name, fname, deps, self.id)

	def get_genlist(self, gen, name):
		lst = Utils.to_list(getattr(gen, name, []))
		lst = [str(l.path_from(gen.path)) if hasattr(l, 'path_from') else l for l in lst]
		return [l.replace('/', '\\') for l in lst]

	def get_compiler_options(self, bld, gen):
		if self.language == MsDev.CXX:
			flags = getattr(gen, 'cxxflags', []) + bld.env.CXXFLAGS
		else:
			flags = getattr(gen, 'cflags', []) + bld.env.CFLAGS
		if self.type == MsDev.SHLIB:
			if self.language == MsDev.CXX:
				flags.extend(bld.env.CXXFLAGS_cxxshlib)
			else:
				flags.extend(bld.env.CFLAGS_cshlib)
		return list(set(flags))

	def get_compiler_includes(self, bld, gen):
		includes = self.get_genlist(gen, 'includes')
		for include in bld.env['INCLUDES']:
			root = bld.path.abspath().replace('\\', '/')
			pref = os.path.commonprefix([root, include])
			if pref == root:
				node = bld.root.find_dir(include)
				if node:
					includes.append(node.path_from(gen.path).replace('/', '\\'))

		deps = Utils.to_list(getattr(gen, 'use', []))
		for dep in deps:
			uselib_incs = bld.env['INCLUDES_%s' % dep]
			for uselib_inc in uselib_incs:
				root = bld.path.abspath().replace('\\', '/')
				pref = os.path.commonprefix([root, userlib_inc])
				if pref == root:
					node = bld.root.find_dir(include)
					if node:
						includes.append(node.path_from(gen.path).replace('/', '\\'))
				incluses.append(userlib_inc)
		return includes
		
	def get_compiler_defines(self, gen):
		defines = self.get_genlist(gen, 'defines') + gen.bld.env.DEFINES
		if 'win32' in sys.platform:
			defines = [d.replace('"', '\\"') for d in defines]
		defines = ';'.join(defines)
		return defines

	def get_link_options(self, bld, gen):
		flags = getattr(gen, 'linkflags', []) + bld.env.LINKFLAGS
		if self.language == MsDev.CXX:
			if self.type == MsDev.SHLIB:
				flags.extend(bld.env.LINKFLAGS_cxxshlib)
			else:
				flags.extend(bld.env.LINKFLAGS_cshlib)
		return list(set(flags))

	def get_link_libs(self, bld, gen):
		libs = Utils.to_list(getattr(gen, 'lib', []))
		deps = Utils.to_list(getattr(gen, 'use', []))
		for dep in deps:
			try:
				tgen = bld.get_tgen_by_name(dep)
			except Errors.WafError:
				uselib_libs = bld.env['LIB_%s' % dep]
				for uselib_lib in uselib_libs:
					libs.append(uselib_lib)
				pass
			else:
				if self.type == MsDev.STLIB:
					libs.append(dep)
		return libs

	def get_link_paths(self, bld, gen):
		dirs = []
		deps = Utils.to_list(getattr(gen, 'use', []))
		for dep in deps:
			try:
				tgen = bld.get_tgen_by_name(dep)
			except Errors.WafError:
				uselib_paths = bld.env['LIBPATH_%s' % dep]
				for uselib_path in uselib_paths:
					dirs.append(uselib_path)
				pass
			else:
				if self.type in (MsDev.STLIB, MsDev.SHLIB):
					directory = '%s\\msdev' % tgen.path.get_bld().path_from(gen.path)
					if directory not in dirs:
						dirs.append(directory.replace('/', '\\'))
				elif self.type in (MsDev.PROGRAM):
					for directory in tgen.lib_paths:
						if directory not in dirs:
							dirs.append(directory.replace('/', '\\'))
		return dirs

	def get_include_files(self, bld, gen):
		includes = []
		for include in self.get_genlist(gen, 'includes'):
			node = gen.path.find_dir(include)
			if node:
				for header in node.ant_glob('*.h*'):
					includes.append(header.path_from(gen.path).replace('/', '\\'))

		for include in bld.env['INCLUDES']:
			root = bld.path.abspath().replace('\\', '/')
			pref = os.path.commonprefix([root, include])
			if pref == root:
				node = bld.root.find_dir(include)
				if node:
					for header in node.ant_glob('*.h*'):
						includes.append(header.path_from(gen.path).replace('/', '\\'))

		return includes


MSDEV_PROJECT = \
'''<?xml version="1.0" encoding="UTF-8"?>
<VisualStudioProject
	ProjectType="Visual C++"
	Version="9,00"
	Name=""
	ProjectGUID=""
	Keyword="Win32Proj"
	TargetFrameworkVersion="0"
	>
	<Platforms>
		<Platform
			Name="Win32"
		/>
	</Platforms>
	<ToolFiles>
	</ToolFiles>
	<Configurations>
		<Configuration
			Name="Debug|Win32"
			OutputDirectory="Debug"
			IntermediateDirectory="Debug"
			ConfigurationType="1"
			>
			<Tool
				Name="VCPreBuildEventTool"
			/>
			<Tool
				Name="VCCustomBuildTool"
			/>
			<Tool
				Name="VCXMLDataGeneratorTool"
			/>
			<Tool
				Name="VCWebServiceProxyGeneratorTool"
			/>
			<Tool
				Name="VCMIDLTool"
			/>
			<Tool
				Name="VCCLCompilerTool"
				Optimization="0"
				PreprocessorDefinitions=""
				MinimalRebuild="true"
				BasicRuntimeChecks="3"
				RuntimeLibrary="3"
				UsePrecompiledHeader="0"
				WarningLevel="3"
				DebugInformationFormat="4"
			/>
			<Tool
				Name="VCManagedResourceCompilerTool"
			/>
			<Tool
				Name="VCResourceCompilerTool"
			/>
			<Tool
				Name="VCPreLinkEventTool"
			/>
			<Tool
				Name="VCLinkerTool"
				LinkIncremental="2"
				GenerateDebugInformation="true"
				SubSystem="1"
				TargetMachine="1"
			/>
			<Tool
				Name="VCALinkTool"
			/>
			<Tool
				Name="VCManifestTool"
			/>
			<Tool
				Name="VCXDCMakeTool"
			/>
			<Tool
				Name="VCBscMakeTool"
			/>
			<Tool
				Name="VCFxCopTool"
			/>
			<Tool
				Name="VCAppVerifierTool"
			/>
			<Tool
				Name="VCPostBuildEventTool"
			/>
		</Configuration>
	</Configurations>
	<References>
	</References>
	<Files>
	</Files>
	<Globals>
	</Globals>
</VisualStudioProject>
'''

