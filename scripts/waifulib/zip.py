#! /usr/bin/env python
# encoding: utf-8

from waflib import TaskGen, Task, Logs, Utils
import zipfile, sys

class ziparchive(Task.Task):
	color = 'YELLOW'

	def __str__(self):
		tgt_str = ' '.join([a.path_from(a.ctx.launch_node()) for a in self.outputs])
		count = len(self.inputs)
		return '%s: %d files -> %s' % (self.__class__.__name__, count, tgt_str)

	def keyword(self):
		return 'Creating'

	def run(self):
		outfile = self.outputs[0].abspath()
		kwargs = {}
		kwargs['mode'] = 'w'
		kwargs['compression'] = zipfile.ZIP_STORED if self.compresslevel == 0 else zipfile.ZIP_DEFLATED

		if sys.hexversion >= 0x3070000:
			kwargs['compresslevel'] = self.compresslevel

		with zipfile.ZipFile(outfile, **kwargs) as zf:
			for src in self.inputs:
				infile  = src.path_from(src.ctx.launch_node())
				arcfile = src.path_from(self.relative_to)

				# TODO: pass excluded list from TaskGen
				Logs.debug('%s: %s <- %s as %s', self.__class__.__name__, outfile, infile, arcfile)

				if infile.endswith('.png'):
					zf.write(infile, arcfile, zipfile.ZIP_STORED)
				else:
					zf.write(infile, arcfile)

@TaskGen.feature('zip')
def create_zip_archive(self):
	compresslevel = getattr(self, 'compresslevel', 6) # 6 is zip default
	if compresslevel < 0 or compresslevel > 9:
		self.bld.fatal('Invalid compress level')

	files = getattr(self, 'files', None)
	if not files:
		self.bld.fatal('No files to archive')

	relative_to = getattr(self, 'relative_to', None)
	if not relative_to:
		self.bld.fatal('No relative directory supplied')

	self.path.get_bld().mkdir()
	target = self.path.get_bld().make_node(self.name)

	tsk = self.create_task('ziparchive', files, target)

	setattr(tsk, 'compresslevel', compresslevel)
	setattr(tsk, 'relative_to', relative_to)

	try:
		inst_to = self.install_path
		self.install_task = self.add_install_files(
			install_to=inst_to, install_from=target,
			chmod=Utils.O644, task=tsk)

	except AttributeError:
		pass
