# encoding: utf-8
# a1batross, 2020

import os
from waflib import *
from waflib.Tools import c_preproc, ccroot

def configure(conf):
	if conf.env.DEST_OS == 'win32':
		conf.find_program('glslc', path_list=[os.path.join(conf.env.VULKAN_SDK, 'Bin')])
	else:
		conf.find_program('glslc')


	conf.add_os_flags('GLSLCPPFLAGS', dup=False)
	conf.add_os_flags('GLSLCFLAGS', dup=False)

	v = conf.env

	v.GLSLCINCLUDES = []
	v.GLSLCDEFINES  = []

	v.GLSLCPPPATH_ST = '-I%s'
	v.GLSLDEFINES_ST = '-D%s'
	v.GLSLC_SRC_F    = []
	v.GLSLC_TGT_F    = ['-c', '-o']

class glsl(Task.Task):
	color = 'PINK'
	run_str = '${GLSLC} ${GLSLCFLAGS} ${GLSLCPPPATH_ST:INCPATHS} ${GLSLDEFINES_ST:GLSLCDEFINES} ${GLSLC_SRC_F}${SRC} ${GLSLC_TGT_F}${TGT[0].abspath()} ${GLSLCPPFLAGS}'
	vars = ['GLSLCDEPS'] # unused variable to depend on, just in case
	ext_in  = ['.h'] # set the build order easily by using ext_out=['.h']
	scan = c_preproc.scan

	def keyword(self):
		return 'Compiling shader'

@TaskGen.extension('.vert', '.frag', '.comp', '.rgen', '.rchit', '.rmiss', '.rahit')
def process_glsl_source(self, src):
	# see ccroot.apply_incpaths
	lst = self.to_incnodes(self.to_list(getattr(self, 'includes', [])) + self.env.GLSLCINCLUDES)
	self.includes_nodes = lst
	cwd = self.get_cwd()
	self.env.INCPATHS = [x.path_from(cwd) for x in lst]
	self.env.append_unique('GLSLCDEFINES', self.to_list(getattr(self, 'defines', [])))

	flags = getattr(self, 'glslcflags', None)
	if flags:
		self.env.append_unique('GLSLCFLAGS', self.to_list(flags))

	tsk = self.create_task('glsl', src, src.parent.find_or_declare('%s.spv' % src.name))

	inst_to = getattr(self, 'install_path', None)
	if inst_to:
		self.add_install_files(install_to=inst_to,
			install_from=tsk.outputs[:], chmod=Utils.O755, task=tsk)
