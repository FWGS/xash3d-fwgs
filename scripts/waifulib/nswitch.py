# encoding: utf-8
# nswitch.py -- switch NRO task
# Copyright (C) 2018 a1batross
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
	conf.find_program('elf2nro')

	v = conf.env

	v.ELF2NRO_NACP_F     = ['--nacp=']
	v.ELF2NRO_ICON_F     = ['--icon=']

class elf2nro(Task.Task):
	color = 'RED'
	run_str = '${ELF2NRO} ${ELFFILE} ${TGT} ${ELF2NRO_NACP_F?NACP}${NACP} ${ELF2NRO_ICON_F?ICON}${ICON}'

	def keyword(self):
		if Logs.colors_lst['USE']: # red/blue switch colors :)
			return '%sConverting to NRO' % Logs.colors_lst['CYAN']
		return 'Converting to NRO'

@TaskGen.feature('cxxprogram', 'cprogram')
@TaskGen.after_method('apply_link')
def apply_nro(self):
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

	nacpfile = add_source_file(self, nodes, getattr(self, 'nacp', None))
	iconfile = add_source_file(self, nodes, getattr(self, 'icon', None))
	self.env.ELFFILE = str(elffile)
	if nacpfile: self.env.NACP = str(nacpfile)
	if iconfile: self.env.ICON = str(iconfile)

	tsk = self.nro_task = self.create_task('elf2nro', nodes)
	self.nro_task.set_outputs(nodes[0].change_ext('.nro'))

	inst_to = getattr(self, 'special_install_path', None)
	if inst_to:
		self.add_install_files(install_to=inst_to,
			install_from=tsk.outputs[:], chmod=Utils.O755, task=tsk)
