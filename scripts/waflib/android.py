#!/usr/bin/env python
# encoding: utf-8
# a1batross, 2019

import os
from waflib import *
from waflib.Tools import javaw

android_sdk_home_env = ['ANDROID_SDK_HOME', 'ANDROID_SDK']
have_aapt2 = False

def options(opt):
	#grp = opt.add_option_group('Android options')
	return

def add_paths_to_path(paths):
	save_path = os.environ['PATH']
	os.environ['PATH'] = os.pathsep.join(paths) + os.pathsep + save_path
	
def get_latest_build_tools(sdk):
	build_tools_path = os.path.join(sdk, 'build-tools')
	dirs = os.listdir(build_tools_path)
	dirs.sort(reverse=True)
	return os.path.join(build_tools_path, dirs[0])

#def find_aapt(conf):
#	try:
#		conf.find_program('aapt2', var='AAPT')
#		have_aapt2 = True
#	except conf.Errors.ConfigurationError:
#		TODO: aapt legacy support
#		have_aapt2 = False
#		try:
#			conf.find_program('aapt', var='AAPT')
#		except conf.Errors.ConfigurationError:
#		conf.fatal('Can\'t find AAPT program. Check that build-tools are installed correctly')

def load_manifest(path):
	# TODO:
	return

def configure(conf):
	conf.load('java')
	conf.start_msg('Checking environment variables')
	
	found = False
	for i in android_sdk_home_env:
		if i in os.environ:
			found = True
			conf.env.ANDROID_SDK_HOME_ENV = i
			break
	
	if not found:
		conf.fatal('Can\'t find path to SDK. Check if ANDROID_SDK_HOME environment variable is set')
		return
	
	conf.end_msg('ok')
	
	sdk = os.environ[conf.env.ANDROID_SDK_HOME_ENV]
	paths = [ os.path.join(sdk, 'tools'), get_latest_build_tools(sdk) ]
	paths += os.environ['PATH'].split(os.pathsep) # just in case we have installed tools
	
	for i in ['aapt2', 'd8', 'zipalign', 'apksigner']:
		conf.find_program(i, path_list = paths)
	conf.find_program('zip')
	# find_aapt()

class aapt2compile(javaw.JTask):
	color = 'GREEN'
	run_str = 'mkdir -p ${RESOUTFILE} && ${AAPT2} compile -v ${SRC} -o ${RESOUTFILE}'
	vars = ['AAPT2', 'RESOUTFILE', 'RESDIR']
	
	def uid(self):
		"""
		Hash by resource directory path
		"""
		return Utils.h_list([self.__class__.__name__, self.generator.outdir.abspath(), self.env.RESDIR])
	
	def runnable_status(self):
		"""
		Waits for dependent tasks to be complete, then read the file system to find the input nodes.
		"""
		for t in self.run_after:
			if not t.hasrun:
				return Task.ASK_LATER

		if not self.inputs:
			root   = self.generator.outdir.ctx.root
			resdir = root.make_node(self.env.RESDIR)
			self.inputs = resdir.ant_glob('**/*', quiet=True)
		return super(aapt2compile, self).runnable_status()
	
	def post_run(self):
		"""
		List class files created
		"""
		for node in self.generator.outdir.ant_glob('**/*.flat', quiet=True):
			self.generator.bld.node_sigs[node] = self.uid()
		self.generator.bld.task_sigs[self.uid()] = self.cache_sig

class aapt2link(javaw.JTask):
	color = 'GREEN' # android green :)
	run_str = '${AAPT2} link -v  --allow-reserved-package-id -o ${OUTAPK} -A ${ASSETSDIR} --manifest ${MANIFEST} --java ${OUTRDIR} -I ${CLASSPATH_ANDROID} ${SRC}'
	vars = ['AAPT2', 'OUTAPK', 'ASSETSDIR', 'MANIFEST', 'OUTRDIR', 'CLASSPATH_ANDROID']
	
	def runnable_status(self):
		"""
		Waits for dependent tasks to be complete, then read the file system to find the input nodes.
		"""
		for t in self.run_after:
			if not t.hasrun:
				return Task.ASK_LATER

		if not self.inputs:
			root   = self.generator.outdir.ctx.root
			resdir = root.make_node(self.env.RESOUTFILE)
			self.inputs = resdir.ant_glob('**/*.flat', quiet=True)
		
		self.outputs = [ self.generator.outdir.make_node(self.env.OUTAPK) ]

		return super(aapt2link, self).runnable_status()


class d8(javaw.JTask):
	color = 'GREEN' # android green :)
	run_str = '${D8} ${SRC} ${D8_FLAGS} --output ${OUTDIR} --lib ${CLASSPATH_ANDROID} ${D8_CLASSPATH}'
	vars = ['D8', 'D8_FLAGS', 'OUTAPK', 'CLASSPATH_ANDROID', 'D8_CLASSPATH']
	
	def runnable_status(self):
		"""
		Waits for dependent tasks to be complete, then read the file system to find the input nodes.
		"""
		for t in self.run_after:
			if not t.hasrun:
				return Task.ASK_LATER

		if not self.inputs:
			self.inputs = self.generator.outdir.ant_glob('**/*.class', quiet=True)
		
		self.outputs = [ self.generator.outdir.make_node('classes.dex') ]
		return super(d8, self).runnable_status()

def javac(func):
	old_runnable_status = getattr(javaw.javac, 'runnable_status', None)
	setattr(javaw.javac, 'old_runnable_status', old_runnable_status)
	setattr(javaw.javac, 'runnable_status', func)

@javac
def custom_runnable_status(self):
	if not self.inputs:
		outrdir = self.srcdir[0].ctx.root.make_node(self.env.OUTRDIR)
		#print(outrdir.abspath())
		self.srcdir.append(outrdir)
	#print(self.srcdir)
	return self.old_runnable_status()

class apkdex(Task.Task):
	color = 'GREEN' # android green :)
	run_str = '${ZIP} -uj ${OUTAPK} ${OUTDIR}/classes.dex'
	vars = ['ZIP', 'OUTAPK', 'OUTDIR']
	
	def runnable_status(self):
		"""
		Waits for dependent tasks to be complete, then read the file system to find the input nodes.
		"""
		for t in self.run_after:
			if not t.hasrun:
				return Task.ASK_LATER

		self.inputs = [
			self.generator.outdir.make_node('classes.dex'),
			self.generator.outdir.make_node(self.env.OUTAPK)
		]
		self.outputs = [ self.generator.outdir.make_node(self.env.OUTAPK) ]
		return super(apkdex, self).runnable_status()

class apkalign(Task.Task):
	color = 'GREEN' # android green :)
	run_str = '${ZIPALIGN} -f -v 4 ${OUTAPK} ${OUTAPK_ALIGNED}'
	vars = ['ZIPALIGN', 'OUTAPK', 'OUTAPK_ALIGNED']
	
	def runnable_status(self):
		"""
		Waits for dependent tasks to be complete, then read the file system to find the input nodes.
		"""
		for t in self.run_after:
			if not t.hasrun:
				return Task.ASK_LATER

		self.inputs = [ self.generator.outdir.make_node(self.env.OUTAPK) ]
		self.outputs = [ self.generator.outdir.make_node(self.env.OUTAPK_ALIGNED) ]
		return super(apkalign, self).runnable_status()


#class apksigner(Task.Task):
#	color = 'GREEN' # android green :)
#	run_str = '${APKSIGNER} sign --ks ${KEYSTORE} ${OUTAPK_ALIGNED}'
#	vars = ['ZIPALIGN', 'OUTAPK', 'OUTAPK_ALIGNED']

@TaskGen.feature('android')
@TaskGen.before_method('apply_java')
def apply_aapt(self):
	# Build resources, generate R.java and create empty APK
	outdir = getattr(self, 'outdir', None)
	if outdir:
		if not isinstance(outdir, Node.Node):
			outdir = self.path.get_bld().make_node(self.outdir)
	else:
		outdir = self.path.get_bld()
	outdir.mkdir()
	self.outdir = outdir
	
	# TODO: assuming that we have old source hierarchy
	srcdir = self.path.find_dir('.')
	sdk = os.environ[self.env.ANDROID_SDK_HOME_ENV]
	
	self.env.RESDIR = os.path.join(srcdir.abspath(), getattr(self, 'resdir', 'res'))
	self.env.ASSETSDIR = os.path.join(srcdir.abspath(), getattr(self, 'assetsdir', 'assets'))
	self.env.MANIFEST = os.path.join(srcdir.abspath(), getattr(self, 'manifest', 'AndroidManifest.xml'))
	
	self.env.OUTAPK = getattr(self, 'apkname', self.name) + '.unaligned.apk'
	self.env.OUTAPK_ALIGNED = getattr(self, 'apkname', self.name) + '.apk'
	self.env.OUTRDIR = os.path.join(outdir.abspath(), getattr(self, 'gendir', 'gen')) # build/gen
	self.env.RESOUTFILE = os.path.join(outdir.abspath(), 'compiled')
	self.env.OUTDIR = outdir.abspath()
	self.env.TARGET_API = getattr(self, 'target_api', 19) # Android 2.3
	self.env.CLASSPATH_ANDROID = os.path.join(sdk, 'platforms', 'android-' + str(self.env.TARGET_API), 'android.jar')

	self.aapt2compile_task = self.create_task('aapt2compile')
	self.aapt2compile_task.cwd = outdir
	
	self.aapt2link_task = self.create_task('aapt2link')
	self.aapt2link_task.cwd = outdir
	self.aapt2link_task.set_run_after(self.aapt2compile_task)
	
	#print(self.env)

@TaskGen.feature('android')
@TaskGen.after_method('apply_java')
def apply_d8(self):
	self.javac_task.set_run_after(self.aapt2link_task)
	
	if getattr(self, 'debug', False):
		self.env.D8_FLAGS = '--debug'
	else: self.env.D8_FLAGS = '--release'
	
	self.d8_task = self.create_task('d8')
	self.d8_task.cwd = self.outdir
	self.d8_task.set_run_after(self.javac_task)
	
	self.apkdex_task = self.create_task('apkdex')
	self.apkdex_task.cwd = self.outdir
	self.apkdex_task.set_run_after(self.d8_task)
	
	self.apkalign_task = self.create_task('apkalign')
	self.apkalign_task.cwd = self.outdir
	self.apkalign_task.set_run_after(self.apkdex_task)

@TaskGen.feature('android')
@TaskGen.after_method('set_classpath')
def set_android_classpath(self):
	if len(self.env.CLASSPATH) == 0: 
		self.env.D8_CLASSPATH = ''
	else:
		self.env.D8_CLASSPATH = '--classpath' + os.pathsep.join(self.env.CLASSPATH) + os.pathsep # old classpath without android.jar for d8
	for x in self.tasks:
		x.env.CLASSPATH = self.env.CLASSPATH_ANDROID + x.env.CLASSPATH
