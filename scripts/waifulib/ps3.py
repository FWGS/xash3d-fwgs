# encoding: utf-8
# ps3.py -- PS3 task
# Copyright (C) 2023 fgsfds
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

from waflib.Tools import ccroot, c, cxx
from waflib import *

def add_source_file(ctx, nodes, f):
	if f:
		if isinstance(f, str):
			node = ctx.path.make_node(f)
		elif isinstance(f, Node.Node):
			node = f
		nodes += [node]
		return node
	return None

def configure(conf):
	conf.find_program('make_fself', var='MAKE_FSELF')

class mkfself(Task.Task):
	color = 'CYAN'
	run_str = '${MAKE_FSELF} ${ELFFILE} ${TGT}'


class cprogram_ps3(c.cprogram):
	run_str = '${LINK_CC} ${LINKFLAGS} ${CCLNK_SRC_F}${SRC} ${CCLNK_TGT_F}${TGT[0].abspath()} ${RPATH_ST:RPATH} ${FRAMEWORKPATH_ST:FRAMEWORKPATH} ${FRAMEWORK_ST:FRAMEWORK} ${ARCH_ST:ARCH} ${STLIB_MARKER} ${STLIBPATH_ST:STLIBPATH} ${STLIB_ST:STLIB} ${STLIB_MARKER} ${LIBPATH_ST:LIBPATH} ${LIB_ST:LIB} ${LDFLAGS}'

class cxxprogram_ps3(cxx.cxxprogram):
	run_str = 'echo ${LINK_CXX} ${LINKFLAGS} -Wl,--start-group ${CXXLNK_SRC_F}${SRC} ${LIB} -Wl,--end-group ${CXXLNK_TGT_F} ${TGT[0].abspath()}.prx'

class cxx_ps3(cxx.cxx):
	run_str = 'echo ${CXX} ${ARCH_ST:ARCH} ${CXXFLAGS} ${FRAMEWORKPATH_ST:FRAMEWORKPATH} ${CPPPATH_ST:INCPATHS} ${DEFINES_ST:DEFINES} ${CXX_SRC_F}${SRC} ${CXX_TGT_F}${TGT[0].abspath()} ${CPPFLAGS}'

class c_ps3(cxx.cxx):
	run_str = 'echo ${CXX} ${ARCH_ST:ARCH} ${CXXFLAGS} ${FRAMEWORKPATH_ST:FRAMEWORKPATH} ${CPPPATH_ST:INCPATHS} ${DEFINES_ST:DEFINES} ${CXX_SRC_F}${SRC} ${CXX_TGT_F}${TGT[0].abspath()} ${CPPFLAGS}'


#@TaskGen.extension('.cpp','.cc','.cxx','.C','.c++')
#def cxx_ps3_hook(self, node):
#	return self.create_compiled_task('cxxprogram_ps3', node)


@TaskGen.feature('cxxprogram_ps3')
@TaskGen.after_method('apply_link')
def apply_fself(self):
	elffile = self.link_task.outputs[0]
	in_nodes = [elffile]

	fselffile = elffile.change_ext('.prx')
	out_nodes = [fselffile]

	self.env.FSELFFILE = str(fselffile)

	self.fself_task = self.create_task('mkfself', in_nodes)
	self.fself_task.set_outputs(out_nodes)
	
	
	inst_to = getattr(self, 'ps3_install_path', None)
	if inst_to:
		self.add_install_files(install_to=inst_to,
			install_from=self.fself_task.outputs[:], chmod=Utils.O755, task=self.fself_task)
