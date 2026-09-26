# encoding: utf-8
# ogc.py -- ogc DOL task
# Copyright (C) 2026 MintFerret
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

from waflib.Tools import ccroot
from waflib import *

def configure(conf):
	conf.find_program('elf2dol')

class elf2dol(Task.Task):
	color = 'CYAN'

	run_str = '${ELF2DOL} ${ELFFILE} ${TGT}'

	def keyword(self):
		return 'Converting ${TGT} to .dol format'

@TaskGen.feature('cxxprogram', 'cprogram')
@TaskGen.after_method('apply_link')
def apply_dol(self):
	elffile = self.link_task.outputs[0]

	nodes = [elffile]

	def add_source_file(ctx, nodes, f):
		if f:
			if isinstance(f, str):
				node = ctx.path.make_node(f)
			elif isinstance(f, Node.Node):
				node = f

			nodes += [node]
			return node
		return None

	self.env.ELFFILE = str(elffile)

	tsk = self.dol_task = self.create_task('elf2dol', nodes)
	self.dol_task.set_outputs(nodes[0].change_ext('.dol'))

	inst_to = getattr(self, 'special_install_path', None)
	if inst_to:
		self.add_install_files(install_to=inst_to,
			install_from=tsk.outputs[:], chmod=Utils.O755, task=tsk)
