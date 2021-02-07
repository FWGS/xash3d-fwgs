# encoding: utf-8
# a1batross, 2020

import os
from waflib import *
from waflib.Tools import c_preproc

def options(opt):
	pass

def configure(conf):
	conf.find_program('glslc')

	self.env.GLSLCFLAGS    = []

class glsl(Task.Task):
	color = 'BLUE'
	run_str = '${GLSLC} ${GLSLCFLAGS} ${GLSLCINCLUDES} ${GLSLCDEFINES} -o ${TGT} ${SRC}'
	vars = ['GLSLC', 'GLSLCFLAGS', 'GLSLCINCLUDES', 'GLSLCDEFINES']

@TaskGen.extension('.vert', '.frag')
def process_glsl_source(self, src):
	includes = self.to_list(getattr(self, 'includes', []))
	defines  = self.to_list(getattr(self, 'defines', []))
	sources  = self.to_list(getattr(self, 'source', []))

	self.env.GLSLCINCLUDES = ['-I' + s for s in includes]
	self.env.GLSLCDEFINES  = ['-D' + s for s in defines]

	tsk = self.create_task('glsl', src, src.parent.find_or_declare('%s.spv' % src.name))

	inst_to = getattr(self, 'install_path', None)
	if inst_to:
		self.add_install_files(install_to=inst_to,
			install_from=tsk.outputs[:], chmod=Utils.O755, task=tsk)
