# encoding: utf-8
# psp.py -- PSP EBOOT task
# Copyright (C) 2023 Sergey Galushko
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

##################################
#            PSP Tools           #
##################################
class psp_fixup(Task.Task):
	run_str = '${FIXUP} -o ${TGT} ${SRC}'
	color   = 'BLUE'

class psp_prxgen(Task.Task):
	run_str = '${PRXGEN} ${SRC} ${TGT}'
	color   = 'BLUE'

class psp_strip(Task.Task):
	run_str = '${STRIP} -o ${TGT} ${SRC}'
	color   = 'BLUE'

class psp_mksfo(Task.Task):
	run_str = '${MKSFO} -d MEMSIZE=1 ${PSP_EBOOT_TITLE} ${TGT}'
	color   = 'YELLOW'

class psp_packpbp(Task.Task):
	run_str = '${PACK_PBP} ${TGT} ${SRC[1].abspath()} ${PSP_EBOOT_ICON} ${PSP_EBOOT_ICON1} ${PSP_EBOOT_UNKPNG} ${PSP_EBOOT_PIC1} ${PSP_EBOOT_SND0} ${SRC[0].abspath()} ${PSP_EBOOT_PSAR}'
	color   = 'GREEN'


@TaskGen.feature('cshlib', 'cxxshlib')
@TaskGen.after_method('apply_link')
def build_module(self):
	link_output = self.link_task.outputs[0]
	for d in self.env.STATIC_LINKING:
		if link_output.name.startswith(d):
			return
	fixup_output = self.path.find_or_declare(link_output.name + '_fixup')
	prxgen_output = self.path.find_or_declare(link_output.change_ext('.prx').name)

	task = self.create_task('psp_fixup', src=link_output, tgt=fixup_output)
	task = self.create_task('psp_prxgen', src=fixup_output, tgt=prxgen_output)

	if getattr(self, 'install_path', None):
		if self.bld.is_install:
			for k in self.install_task.inputs:
				if k == self.path.find_or_declare(link_output.name):
					self.install_task.inputs.remove(k)
		self.add_install_files(install_to=self.install_path, install_from=prxgen_output)

@TaskGen.feature('cprogram', 'cxxprogram', 'cprogram_static', 'cxxprogram_static')
@TaskGen.after_method('apply_link')
def build_eboot(self):
	finalobj_ext = '.elf'
	finalobj_tool = 'psp_strip'
	if self.env.PSP_BUILD_PRX:
		finalobj_ext = '.prx'
		finalobj_tool = 'psp_prxgen'

	link_output = self.link_task.outputs[0]
	fixup_output = self.path.find_or_declare(link_output.name + '_fixup')
	finalobj_output = self.path.find_or_declare(link_output.change_ext(finalobj_ext).name)

	mksfo_output = self.path.find_or_declare('PARAM.SFO')
	packpbp_output = self.path.find_or_declare('EBOOT.PBP')

	task = self.create_task('psp_fixup', src=link_output, tgt=fixup_output)
	task = self.create_task(finalobj_tool, src=fixup_output, tgt=finalobj_output)
	task = self.create_task('psp_mksfo', tgt=mksfo_output)
	task = self.create_task('psp_packpbp', src=[finalobj_output, mksfo_output], tgt=packpbp_output)

	if getattr(self, 'install_path', None):
		if getattr(self, 'install_task', None):
			self.install_task.inputs = self.install_task.outputs = []
		self.add_install_files(install_to=self.install_path, install_from=[packpbp_output, finalobj_output])
