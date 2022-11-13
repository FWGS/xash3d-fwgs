from waflib import TaskGen
import json

def configure(conf):
	conf.find_program('sebastian.py', var='SEBASTIAN', path_list=[conf.path.abspath()])

def sebafun(task):
	env = task.env
	bld = task.generator.bld

	cmd = env.SEBASTIAN + ['-o', task.outputs[0], task.inputs[0], '--path', task.outputs[0].parent.abspath()+'/shaders']
	print("LOL", cmd)

	# CRASHES WITH WEIRD ERRORS (?!?!?!?)
	#bld.exec_command([env.SEBASTIAN, '-o', task.outputs[0], task.inputs[0], '--path', task.inputs[0].parent.abspath()+'/shaders'], cwd = task.get_cwd(), env=env.env or None)
	#task.exec_command([env.SEBASTIAN, '-o', task.outputs[0], task.inputs[0], '--path', task.inputs[0].parent.abspath()+'/shaders'])
	#bld.cmd_and_log(cmd, cwd = task.get_cwd(), env=env.env or None, quiet = 0)

	# install_path = getattr(task, 'install_path', None)
	# if install_path:
	# 	bld.add_install_files(install_to = install_path, install_from = task.outputs[:], task = task)

#TaskGen.declare_chain(
#	color = 'BLUE',
#	name = 'sebastian',
#	rule = '${SEBASTIAN} -o ${TGT} ${SRC} --path ${TGT[0].parent.abspath()}/shaders',
#	rule = sebafun,
#	shell = False,
#	ext_in = '.json',
#	ext_out = '.meat'
#)

import os
from waflib import *
from waflib.Tools import c_preproc, ccroot

class sebastian(Task.Task):
	color = 'BLUE'
	#run_str = '${SEBASTIAN} -o ${TGT} ${SRC}' # --path ${TGT[0].parent.abspath()}/shaders'
	run_str = '${SEBASTIAN} -o ${TGT} ${SRC} --path ${TGT[0].parent.abspath()}'
	ext_in  = ['.json']

	def keyword(self):
		return 'Compiling meatpipe'

	def scan(self):
		env = self.env
		bld = self.generator.bld

		node = self.inputs[0]
		out = self.outputs[0]

		print("############################################### Scanning", node)

		#bld = self.generator.bld
		cmd = env.SEBASTIAN + [node.abspath(), '--path', out.parent.abspath(), '--depend', '-']
		#cmd = env.SEBASTIAN + ['--path', out.parent.abspath() + '/shaders', node.abspath(), '--depend', '-']

		output = bld.cmd_and_log(cmd, cwd = self.get_cwd(), env = env.env or None)

		print("LOOOO99000000000000000OOL", output)
		deps = json.loads(output)

		ndeps = [bld.path.find_resource(dep) for dep in deps]
		print("FOOOOOOOOOOOOOOUND", ndeps)

		return (ndeps, [])
		# dep = node.parent.find_resource(node.name.replace('.dep'))
		# if not dep:
		# 	raise ValueError("Could not find the .dep file for %r" % node)
		# return ([dep], [])

@TaskGen.extension('.json')
def process_meatpipe(self, src):
	tsk = self.create_task('sebastian', src, src.change_ext('.meat'))

	inst_to = getattr(self, 'install_path', None)
	if inst_to:
		self.add_install_files(install_to=inst_to,
			install_from=tsk.outputs[:], chmod=Utils.O755, task=tsk)
