#! /usr/bin/env python
# Modified: Alibek Omarov <a1ba.omarov@gmail.com>
# Originally taken from Thomas Nagy's blogpost

"""
Strip executables upon installation
"""

import shutil, os
from waflib import Build, Utils, Context, Errors, Logs

def options(opt):
	grp = opt.option_groups['install/uninstall options']
	grp.add_option('--no-strip', dest='no_strip', action='store_true', default=False,
		help='don\'t strip binaries. You must pass this flag to install command(default: False)')

def configure(conf):
	if conf.env.DEST_BINFMT in ['elf', 'mac-o']:
		conf.find_program('strip', var='STRIP')
		if not conf.env.STRIPFLAGS:
			conf.env.STRIPFLAGS = os.environ['STRIPFLAGS'] if 'STRIPFLAGS' in os.environ else []

def copy_fun(self, src, tgt):
	inst_copy_fun(self, src, tgt)

	if self.generator.bld.options.no_strip:
		return

	if self.env.DEST_BINFMT not in ['elf', 'mac-o']: # don't strip unknown formats or PE
		return

	if getattr(self.generator, 'link_task', None) and self.generator.link_task.outputs[0] in self.inputs:
		cmd = self.env.STRIP + self.env.STRIPFLAGS + [tgt]
		try:
			self.generator.bld.cmd_and_log(cmd, output=Context.BOTH, quiet=Context.BOTH)
			if not self.generator.bld.progress_bar:
				c1 = Logs.colors.NORMAL
				c2 = Logs.colors.CYAN

				f1 = os.path.getsize(src)
				f2 = os.path.getsize(tgt)

				Logs.info('%s+ strip %s%s%s (%d bytes change)', c1, c2, tgt, c1, f2 - f1)
		except Errors.WafError as e:
			print(e.stdout, e.stderr)

inst_copy_fun = Build.inst.copy_fun
Build.inst.copy_fun = copy_fun

