#!/usr/bin/env python
# -*- coding: utf-8 vi:ts=4:noexpandtab

from waflib.Tools import ccroot, gcc, gxx
from waflib.Configure import conf
from waflib.TaskGen import feature, after_method
from waflib import Utils

@conf
def get_emscripten_version(conf, cc):
	k = conf.get_cc_version(cc, clang=True)

	if '__EMSCRIPTEN__' not in k:
		conf.fatal('Could not determine the emscripten compiler version')

	conf.env.DEST_OS = 'emscripten'
	conf.env.DEST_CPU = 'wasm'

@conf
def find_emcc(conf):
	cc = conf.find_program(['emcc'], var='CC')
	conf.get_emscripten_version(cc)
	conf.env.CC_NAME = 'clang'

@conf
def find_emxx(conf):
	cxx = conf.find_program(['em++'], var='CXX')
	conf.get_emscripten_version(cxx)
	conf.env.CXX_NAME = 'clang'

@conf
def gcc_modifier_emscripten(conf):
	v = conf.env

	conf.env.cshlib_PATTERN = 'lib%s.wasm'
	conf.env.cprogram_PATTERN = '%s.html'

	conf.env.CFLAGS_cshlib = ['-fPIC', '-sSIDE_MODULE=1']
	conf.env.CFLAGS_cstlib = ['-fPIC']
	conf.env.CFLAGS_cprogram = ['-sMAIN_MODULE=1']

	conf.env.LINKFLAGS_cshlib = ['-sSIDE_MODULE=1']
	conf.env.LINKFLAGS_cprogram = ['-sMAIN_MODULE=1']

@conf
def gxx_modifier_emscripten(conf):
	v = conf.env

	conf.env.cxxshlib_PATTERN = 'lib%s.wasm'
	conf.env.cxxprogram_PATTERN = '%s.html'
	conf.env.CXXFLAGS_cxxshlib = ['-fPIC', '-sSIDE_MODULE=1']
	conf.env.CXXFLAGS_cxxstlib = ['-fPIC']
	conf.env.CXXFLAGS_cxxprogram = ['-sMAIN_MODULE=1']

	conf.env.LINKFLAGS_cxxshlib = ['-sSIDE_MODULE=1']
	conf.env.LINKFLAGS_cxxprogram = ['-sMAIN_MODULE=1']

@feature('cxxprogram', 'cprogram')
@after_method('apply_link')
def apply_indexhtml(self):
	if self.env.DEST_OS != 'emscripten':
		return

	tsk = self.link_task
	node = tsk.outputs[0]

	tsk.outputs.append(node.change_ext('.js'))
	tsk.outputs.append(node.change_ext('.wasm'))

	if '--preload-file' in getattr(self, 'linkflags', []) + self.env.LINKFLAGS:
		tsk.outputs.append(node.change_ext('.data'))

	inst_to = getattr(self, 'special_install_path', None)
	if inst_to:
		self.add_install_as(install_to=inst_to + '/index.html',
			install_from=tsk.outputs[0], chmod=Utils.O644, task=tsk)

		self.add_install_files(install_to=inst_to,
			install_from=tsk.outputs[1:], chmod=Utils.O644, task=tsk)

def configure(conf):
	if not conf.env.CC:
		conf.find_emcc()
		conf.gcc_common_flags()
		conf.gcc_modifier_platform()
		conf.cc_load_tools()
		conf.cc_add_flags()
		conf.link_add_flags()

	if not conf.env.AR:
		conf.find_program(['emar'], var='AR')
		conf.find_ar()

	if not conf.env.CXX:
		conf.find_emxx()
		conf.gxx_common_flags()
		conf.gxx_modifier_platform()
		conf.cxx_load_tools()
		conf.cxx_add_flags()
		conf.link_add_flags()
