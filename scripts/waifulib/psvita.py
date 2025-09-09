# encoding: utf-8
# psvita.py -- PSVita VPK task
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

from waflib.Tools import ccroot
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
	conf.find_program('vita-elf-create', var='ELF_CREATE')
	conf.find_program('vita-make-fself', var='MAKE_FSELF')
	conf.find_program('vita-mksfoex', var='MKSFOEX')
	conf.find_program('vita-pack-vpk', var='PACKVPK')

	conf.env.SCESYS_ST = '-a%s=sce_sys'

class mkvelf(Task.Task):
	color = 'CYAN'
	run_str = '${ELF_CREATE} -g ${YMLFILE} ${ELFFILE} ${TGT}'

class mkfself(Task.Task):
	color = 'CYAN'
	run_str = '${MAKE_FSELF} ${VELFFILE} ${TGT}'

class mksfoex(Task.Task):
	color = 'CYAN'
	# ATTRIBUTE2=12 enables the biggest extended memory mode
	run_str = '${MKSFOEX} -s TITLE_ID=${TITLEID} -d ATTRIBUTE2=12 ${APPNAME} ${TGT}'

class mkvpk(Task.Task):
	color = 'CYAN'
	run_str = '${PACKVPK} -s ${SFOFILE} -b ${FSELFFILE} ${SCESYS_ST:SCESYS} ${TGT}'

@TaskGen.feature('cxxprogram', 'cprogram')
@TaskGen.after_method('apply_link')
def apply_velf(self):
	elffile = self.link_task.outputs[0]
	in_nodes = [elffile]

	ymlfile = elffile.change_ext('.yml')
	velffile = elffile.change_ext('.velf')
	out_nodes = [velffile, ymlfile]

	self.env.ELFFILE = str(elffile)
	self.env.VELFFILE = str(velffile)
	self.env.YMLFILE = str(ymlfile)

	self.velf_task = self.create_task('mkvelf', in_nodes)
	self.velf_task.set_outputs(out_nodes)

@TaskGen.feature('cxxprogram', 'cprogram')
@TaskGen.after_method('apply_velf')
def apply_fself(self):
	velffile = self.velf_task.outputs[0]
	in_nodes = [velffile]

	fselffile = velffile.change_ext('.bin')
	out_nodes = [fselffile]

	self.env.FSELFFILE = str(fselffile)

	self.fself_task = self.create_task('mkfself', in_nodes)
	self.fself_task.set_outputs(out_nodes)

@TaskGen.feature('cxxprogram', 'cprogram')
@TaskGen.after_method('apply_fself')
def apply_sfo(self):
	fselffile = self.fself_task.outputs[0]
	in_nodes = [fselffile]
	scetitleid = getattr(self, 'title_id', 'TEST10000')
	sceappname = getattr(self, 'app_name', 'test')

	sfofile = fselffile.change_ext('.sfo')
	out_nodes = [sfofile]

	if scetitleid: self.env.TITLEID = scetitleid
	if sceappname: self.env.APPNAME = sceappname
	self.env.SFOFILE = str(sfofile)

	self.sfo_task = self.create_task('mksfoex', in_nodes)
	self.sfo_task.set_outputs(out_nodes)

@TaskGen.feature('cxxprogram', 'cprogram')
@TaskGen.after_method('apply_sfo')
def apply_vpk(self):
	fselffile = self.fself_task.outputs[0]
	sfofile = self.sfo_task.outputs[0]
	in_nodes = [fselffile, sfofile]
	scesysdir = add_source_file(self, in_nodes, getattr(self, 'sce_sys', None))

	vpkfile = sfofile.change_ext('.vpk')
	out_nodes = [vpkfile]

	if scesysdir:
		self.env.SCESYS = [str(scesysdir)]
	self.env.VPKFILE = str(vpkfile)

	tsk = self.vpk_task = self.create_task('mkvpk', in_nodes)
	self.vpk_task.set_outputs(out_nodes)

	inst_to = getattr(self, 'special_install_path', None)
	if inst_to:
		self.add_install_files(install_to=inst_to,
			install_from=tsk.outputs[:], chmod=Utils.O755, task=tsk)
