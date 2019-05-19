#!/usr/bin/env python
# encoding: utf-8
# a1batross, 2019

import os
from waflib import *
from waflib.Tools import javaw

android_sdk_home_env = ['ANDROID_SDK_HOME', 'ANDROID_SDK']

# d8 started to roll out in 28
D8_BUILD_TOOLS_MAJOR_VER = 28

def options(opt):
	#grp = opt.add_option_group('Android options')
	return

def add_paths_to_path(paths):
	save_path = os.environ['PATH']
	os.environ['PATH'] = os.pathsep.join(paths) + os.pathsep + save_path

def get_latest_build_tools_version(sdk):
	build_tools_path = os.path.join(sdk, 'build-tools')
	dirs = os.listdir(build_tools_path)
	dirs.sort(reverse=True)
	return dirs[0]

def get_build_tools(sdk, ver):
	return os.path.join(sdk, 'build-tools', ver)

def configure(conf):
	conf.load('java')
	conf.start_msg('Checking environment variables')

	sdk = None
	for i in android_sdk_home_env:
		if i in os.environ:
			sdk = conf.env.ANDROID_SDK_HOME_ENV = os.environ[i]
			break

	if not sdk:
		conf.fatal('Can\'t find path to SDK. Check if ANDROID_SDK_HOME environment variable is set')
		return

	conf.end_msg('ok')

	btv = get_latest_build_tools_version(sdk)

	conf.msg('Detected build-tools version', btv)

	paths = [os.path.join(sdk, 'tools'), get_build_tools(sdk, btv)]
	paths += os.environ['PATH'].split(os.pathsep) # just in case we have installed tools
	
	conf.env.BUILD_TOOLS_VERSION = [int(x) for x in btv.split('.')]

	# mandatory
	for i in ['aapt2', 'zipalign', 'apksigner', 'zip']:
		conf.find_program(i, path_list = paths)

	# optional
	if conf.env.BUILD_TOOLS_VERSION[0] >= D8_BUILD_TOOLS_MAJOR_VER:
		conf.find_program('d8', path_list = paths)
	else:
		conf.find_program('dx', path_list = paths)


	# TODO: AAPT legacy support

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
	run_str = '${AAPT2} link -v ${AAPT2_LINKFLAGS} -o ${OUTAPK_UNALIGNED_NOCLASSES_NOJNI} -A ${ASSETSDIR} --manifest ${MANIFEST} --java ${OUTRDIR} -I ${CLASSPATH_ANDROID} ${SRC}'
	vars = ['AAPT2', 'OUTAPK_UNALIGNED_NOCLASSES_NOJNI', 'ASSETSDIR', 'MANIFEST', 'OUTRDIR', 'CLASSPATH_ANDROID']

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

		self.outputs = [ self.generator.outdir.make_node(self.env.OUTAPK_UNALIGNED_NOCLASSES_NOJNI) ]

		return super(aapt2link, self).runnable_status()

class d8(javaw.JTask):
	color = 'GREEN' # android green :)
	run_str = '${D8} ${SRC} ${D8_FLAGS} --output ${OUTDIR} --lib ${CLASSPATH_ANDROID} ${D8_CLASSPATH}'
	vars = ['D8', 'D8_FLAGS', 'OUTDIR', 'CLASSPATH_ANDROID', 'D8_CLASSPATH' ]

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

class dx(javaw.JTask):
	color = 'GREEN' # Android green :)
	run_str = '${DX} --dex ${D8_FLAGS} --output=${OUTDIR}/classes.dex ${SRC} ${DX_CLASSPATH}'
	vars = ['DX', 'D8_FLAGS', 'OUTDIR']

	def runnable_status(self):
		for t in self.run_after:
			if not t.hasrun:
				return Task.ASK_LATER

		if not self.inputs:
			self.inputs = self.generator.outdir.ant_glob('**/*.class', quiet=True)

		self.outputs = [ self.generator.outdir.make_node('classes.dex') ]
		return super(dx, self).runnable_status()

def javac(func):
	old_runnable_status = getattr(javaw.javac, 'runnable_status', None)
	setattr(javaw.javac, 'old_runnable_status', old_runnable_status)
	setattr(javaw.javac, 'runnable_status', func)

@javac
def custom_runnable_status(self):
	if not self.inputs:
		outrdir = self.srcdir[0].ctx.root.make_node(self.env.OUTRDIR)
		self.srcdir.append(outrdir)
	return self.old_runnable_status()

class apkjni(Task.Task):
	color = 'BLUE'
	run_str = '${ZIP} ${OUTAPK_UNALIGNED_NOCLASSES_NOJNI} --out ${OUTAPK_UNALIGNED_NOCLASSES} && ${ZIP} -ru ${OUTAPK_UNALIGNED_NOCLASSES} ${JNIDIR}'
	vars = ['ZIP', 'JNIDIR', 'OUTAPK_UNALIGNED_NOCLASSES_NOJNI', 'OUTAPK_UNALIGNED_NOCLASSES']

	def runnable_status(self):
		"""
		Waits for dependent tasks to be complete, then read the file system to find the input nodes.
		"""
		for t in self.run_after:
			if not t.hasrun:
				return Task.ASK_LATER

		# I could use SRC here, but I need to track changes of OUTAPK_UNALIGNED_NOCLASSES_NOJNI also
		self.inputs = self.generator.outdir.ant_glob('{0}/**/*'.format(self.env.JNIDIR), quiet=True)
		self.inputs += self.generator.outdir.ant_glob(self.env.OUTAPK_UNALIGNED_NOCLASSES_NOJNI)
		self.outputs = [self.generator.outdir.make_node(self.env.OUTAPK_UNALIGNED_NOCLASSES)]

		return super(apkjni, self).runnable_status()

class apkdex(Task.Task):
	color = 'GREEN' # android green :)
	run_str = '${ZIP} ${OUTAPK_UNALIGNED_NOCLASSES} --out ${OUTAPK_UNALIGNED} && ${ZIP} -uj ${OUTAPK_UNALIGNED} classes.dex'
	vars = ['ZIP', 'OUTAPK_UNALIGNED_NOCLASSES', 'OUTAPK_UNALIGNED']

	def runnable_status(self):
		"""
		Waits for dependent tasks to be complete, then read the file system to find the input nodes.
		"""
		for t in self.run_after:
			if not t.hasrun:
				return Task.ASK_LATER
		
		self.inputs = [self.generator.outdir.make_node('classes.dex'), self.generator.outdir.make_node(self.env.OUTAPK_UNALIGNED_NOCLASSES)]
		self.outputs = [ self.generator.outdir.make_node(self.env.OUTAPK_UNALIGNED) ]
		return super(apkdex, self).runnable_status()

class apkalign(Task.Task):
	color = 'GREEN' # android green :)
	run_str = '${ZIPALIGN} -f -v 4 ${OUTAPK_UNALIGNED} ${OUTAPK}'
	vars = ['ZIPALIGN', 'OUTAPK_UNALIGNED', 'OUTAPK']

	def runnable_status(self):
		"""
		Waits for dependent tasks to be complete, then read the file system to find the input nodes.
		"""
		for t in self.run_after:
			if not t.hasrun:
				return Task.ASK_LATER

		self.inputs = [ self.generator.outdir.make_node(self.env.OUTAPK_UNALIGNED) ]
		self.outputs = [ self.generator.outdir.make_node(self.env.OUTAPK) ]
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

	srcdir = self.path.find_dir('.')
	sdk = self.env.ANDROID_SDK_HOME_ENV

	self.env.RESDIR = os.path.join(srcdir.abspath(), getattr(self, 'resdir', 'res'))
	self.env.ASSETSDIR = os.path.join(srcdir.abspath(), getattr(self, 'assetsdir', 'assets'))
	self.env.MANIFEST = os.path.join(srcdir.abspath(), getattr(self, 'manifest', 'AndroidManifest.xml'))

	apkname = getattr(self, 'apkname', self.name)
	self.env.OUTAPK_UNALIGNED_NOCLASSES_NOJNI = apkname + '.unaligned.noclasses.nojni.apk'
	self.env.OUTAPK_UNALIGNED_NOCLASSES = apkname + '.unaligned.noclasses.apk'
	self.env.OUTAPK_UNALIGNED = apkname + '.unaligned.apk'
	self.env.OUTAPK = apkname + '.apk'
	if self.env.BUILD_TOOLS_VERSION[0] > 27:
		self.env.append_unique('AAPT2_LINKFLAGS', '--allow-reserved-package-id')
	
	self.env.OUTRDIR = os.path.join(outdir.abspath(), getattr(self, 'gendir', 'gen')) # build/gen
	self.env.RESOUTFILE = os.path.join(outdir.abspath(), 'compiled')
	self.env.OUTDIR = outdir.abspath()
	self.env.TARGET_API = getattr(self, 'target_api', 10) # Android 2.3.3 TODO: parse AndroidManifest.xml to get target API!
	self.env.CLASSPATH_ANDROID = os.path.join(sdk, 'platforms', 'android-' + str(self.env.TARGET_API), 'android.jar')
	
	self.env.JNIDIR = getattr(self, 'jni', 'lib')

	self.aapt2compile_task = self.create_task('aapt2compile')
	self.aapt2compile_task.cwd = outdir

	self.aapt2link_task = self.create_task('aapt2link')
	self.aapt2link_task.cwd = outdir
	self.aapt2link_task.set_run_after(self.aapt2compile_task)

@TaskGen.feature('android')
@TaskGen.after_method('apply_java')
def apply_d8(self):
	self.javac_task.set_run_after(self.aapt2link_task)

	if getattr(self, 'debug', False):
		self.env.D8_FLAGS = '--debug'
	elif self.env.D8: self.env.D8_FLAGS = '--release'

	
	if self.env.D8:
		self.d8_task = self.create_task('d8')
	else:
		self.d8_task = self.create_task('dx')

	self.d8_task.cwd = self.outdir
	self.d8_task.set_run_after(self.javac_task)

	self.apkjni_task = self.create_task('apkjni')
	self.apkjni_task.cwd = self.outdir
	self.apkjni_task.set_run_after(self.d8_task)
	
	self.apkdex_task = self.create_task('apkdex')
	self.apkdex_task.cwd = self.outdir
	self.apkdex_task.set_run_after(self.apkjni_task)

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
