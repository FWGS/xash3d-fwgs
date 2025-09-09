# encoding: utf-8
# xshlib.py -- advanced linking utils
# Copyright (C) 2019 mittorn
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

from waflib import Logs, Utils, TaskGen, Task
from waflib.Tools import ccroot, c, cxx

MAIN_BINARY = 'xash'

def options(opt):
	opt.add_option('--static-linking', action='store', dest='STATIC_LINKING', default=None)

def configure(conf):
	if conf.options.STATIC_LINKING:
		conf.find_program('ld')
		conf.find_program('objcopy')
		conf.env.STATIC_LINKING = conf.options.STATIC_LINKING
		conf.add_os_flags('LD_RELOCATABLE_FLAGS')

def build(bld):
	if bld.env.STATIC_LINKING:
		apply_static(MAIN_BINARY,*bld.env.STATIC_LINKING.split(','))

class objcopy_relocatable_lib(Task.Task):
	"remove all exports except of lib_${NAME}_exports"
	no_errcheck_out = True
	run_str = '${OBJCOPY} -G lib_${NAME}_exports ${SRC[0].abspath()} ${TGT[0].abspath()}'
	def keyword(self):
		return 'ObjCopy'

class xshlib(ccroot.link_task):
	"make relocatable library"
	no_errcheck_out = True
	run_str = '${LD} -r -o ${TGT[0].abspath()} ${LD_RELOCATABLE_FLAGS} ${CCLNK_SRC_F}${SRC}'

	def add_target(self, target):
		"create objcopy task for target"
		if not self.env.LD_RELOCATABLE_FLAGS:
			self.env.LD_RELOCATABLE_FLAGS = []
			if '-m32' in self.env.LINKFLAGS:
				self.env.LD_RELOCATABLE_FLAGS.append('-melf_i386')

		base = self.generator.path
		target_unstripped = base.find_or_declare('%s.unstripped.o'% target)
		target_stripped = base.find_or_declare('%s.o'% target)

		self.set_outputs(target_unstripped)
		self.generator.objcopy_task= self.generator.create_task('objcopy_relocatable_lib', target_unstripped, target_stripped)
		self.generator.objcopy_task.env['NAME'] = target

class cprogram_static(c.cprogram):
	"build static c program"
	run_str = '${LINK_CC} -static ${LINKFLAGS} ${CCLNK_SRC_F}${SRC} ${CCLNK_TGT_F}${TGT[0].abspath()} ${RPATH_ST:RPATH} ${FRAMEWORKPATH_ST:FRAMEWORKPATH} ${FRAMEWORK_ST:FRAMEWORK} ${ARCH_ST:ARCH} ${STLIB_MARKER} ${STLIBPATH_ST:STLIBPATH} ${STLIB_ST:STLIB} ${STLIB_MARKER} ${LIBPATH_ST:LIBPATH} ${LIB_ST:LIB} ${LDFLAGS}'

class cxxprogram_static(cxx.cxxprogram):
	"build static cxx program"
	run_str = '${LINK_CXX} -static ${LINKFLAGS} ${CXXLNK_SRC_F}${SRC} ${CXXLNK_TGT_F}${TGT[0].abspath()} ${RPATH_ST:RPATH} ${FRAMEWORKPATH_ST:FRAMEWORKPATH} ${FRAMEWORK_ST:FRAMEWORK} ${ARCH_ST:ARCH} ${STLIB_MARKER} ${STLIBPATH_ST:STLIBPATH} ${STLIB_ST:STLIB} ${STLIB_MARKER} ${LIBPATH_ST:LIBPATH} ${LIB_ST:LIB} ${LDFLAGS}'

# usevars are same
ccroot.USELIB_VARS['cprogram_static'] = ccroot.USELIB_VARS['cxxprogram_static'] = ccroot.USELIB_VARS['cxxprogram']

def apply_static(main, *reloc):
	"apply xshlib tasks and generate files"

	def write_libraries_list(out_node):
		"generate library list"

		libraries = reloc
		externs = '\n'.join(['extern table_t lib_%s_exports[];' % e for e in libraries])
		table = '\n'.join(['{ "%s", &lib_%s_exports },' % (e, e) for e in libraries])
		out_node.write('%s\nstruct {const char *name;void *func;} libs[] = {\n%s\n{0,0}\n};\n' % (externs, table ))


	def write_export_list(name, in_node, out_node):
		"generate exports list for library"

		exports = in_node.read().splitlines()
		externs = '\n'.join(['extern void %s(void);' % e for e in exports])
		table = '\n'.join(['{ "%s", &%s },' % (e, e) for e in exports])
		out_node.write('%s\nstruct {const char *name;void *func;} lib_%s_exports[] = {\n%s\n{0,0}\n};\n' % (externs, name, table ))

	@TaskGen.feature('cshlib', 'cxxshlib')
	@TaskGen.before('process_source', 'propogate_uselib_vars')
	def apply_xshlib(self):
		"apply xshlib feature and inject link_helper.c to sources"
		if self.name in reloc:
			for k in ('cshlib', 'cxxshlib'):
				if k in self.features:
					self.features.remove(k)
			self.features.insert(0, 'xshlib')
			in_node = self.path.get_src().make_node('exports.txt')
			bldnode = self.path.get_bld()
			bldnode.mkdir()
			out_node = bldnode.make_node('link_helper.c')
			write_export_list(self.name,in_node, out_node)
			self.source = Utils.to_list(self.source) + [out_node]

	@TaskGen.feature('cshlib', 'cxxshlib', 'cprogram', 'cxxprogram', 'cprogram_static', 'cxxprogram_static')
	@TaskGen.before('process_source')
	def add_deps(self):
		"add all relocatable objects to main binary source list"
		if self.name == main:
			write_libraries_list(self.path.get_bld().make_node('generated_library_tables.h'))

			for t in reloc:
				self.source += [self.bld.get_tgen_by_name(t).objcopy_task.outputs[0]]
