from waflib import TaskGen, Task, Utils
import json

def configure(conf):
	conf.find_program('sebastian.py', var='SEBASTIAN', path_list=[conf.path.abspath()])
	#print(conf)
	#print(conf.env)
	#conf.env.SEBASTIAN = conf.find_file('sebastian.py')

class sebastian(Task.Task):
	color = 'CYAN'
	run_str = '${SEBASTIAN} -o ${TGT} ${SRC} --path ${TGT[0].parent.abspath()}'
	ext_in  = ['.json']

	def keyword(self):
		return 'Compiling meatpipe'

	def scan(self):
		env = self.env
		bld = self.generator.bld

		node = self.inputs[0]
		out = self.outputs[0]

		cmd = env.SEBASTIAN + [node.abspath(), '--path', out.parent.abspath(), '--depend', '-']
		print(cmd)
		output = bld.cmd_and_log(cmd, cwd = self.get_cwd(), env = env.env or None, quiet = True)

		deps = json.loads(output)
		print(deps)

		ndeps = [bld.path.find_resource(dep) for dep in deps]
		print(ndeps)

		return (ndeps, [])

@TaskGen.extension('.json')
def process_meatpipe(self, src):
	tsk = self.create_task('sebastian', src, src.change_ext('.meat'))

	inst_to = getattr(self, 'install_path', None)
	if inst_to:
		self.add_install_files(install_to=inst_to,
			install_from=tsk.outputs[:], chmod=Utils.O755, task=tsk)
