#! /usr/bin/env python
# Modified: Alibek Omarov <a1ba.omarov@gmail.com>
# Originally taken from Thomas Nagy's blogpost

"""
Strip executables upon installation
"""

import shutil, os
from waflib import Build, Utils, Context, Errors, Logs

def configure(conf):
	if conf.env.DEST_BINFMT in ['elf', 'mac-o']:
		conf.find_program('strip')
		if not conf.env.STRIPFLAGS:
			conf.env.STRIPFLAGS = os.environ['STRIPFLAGS'] if 'STRIPFLAGS' in os.environ else []

def copy_fun(self, src, tgt):
	inst_copy_fun(self, src, tgt)

	if self.env.KEEP_DEBUG_INFO:
		return # don't strip debug information in debug mode

	if getattr(self.generator, 'link_task', None) and self.generator.link_task.outputs[0] in self.inputs:
		cmd = self.env.STRIP + self.env.STRIPFLAGS + [tgt]
		try:
			if not self.generator.bld.progress_bar:
				c1 = Logs.colors.NORMAL
				c2 = Logs.colors.CYAN

				Logs.info('%s+ strip %s%s%s', c1, c2, tgt, c2)
			self.generator.bld.cmd_and_log(cmd, output=Context.BOTH, quiet=Context.BOTH)
		except Errors.WafError as e:
			print(e.stdout, e.stderr)

inst_copy_fun = Build.inst.copy_fun
Build.inst.copy_fun = copy_fun

