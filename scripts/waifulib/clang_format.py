#!/usr/bin/env python
# encoding: utf-8
# clang_format - `./waf format` runs clang-format -i in parallel on opted-in source files.
# Copyright (C) 2026 Xash3D FWGS contributors
#
# To override clang-format binary:
#   CLANG_FORMAT=xxx ./waf configure
#   CLANG_FORMAT=xxx ./waf format # for post-configure override, won't check for minimum version
#
# Opt task gens in:
#   bld.stlib(features='format', source='...', target='...', ...)

from waflib import Build, Logs, Task, TaskGen, Utils
import os
import re
import subprocess

# Enforce minimum major version
CLANG_FORMAT_MIN_MAJOR = None

# File extensions to format
CLANG_FORMAT_EXTS = ['.c', '.cpp', '.h']

def probe_major(binary):
	try:
		out = subprocess.check_output([binary, '--version'], stderr=subprocess.DEVNULL).decode('utf-8', 'replace')
	except (OSError, subprocess.CalledProcessError):
		return None

	m = re.search(r'version\s+(\d+)', out)
	return int(m.group(1)) if m else None

def configure(conf):
	conf.find_program('clang-format', var='CLANG_FORMAT', mandatory=False)

	if conf.env.CLANG_FORMAT and CLANG_FORMAT_MIN_MAJOR is not None:
		major = probe_major(conf.env.CLANG_FORMAT[0])

		if major is None:
			Logs.warn('clang-format is not an executable or invalid')
			return

		if major < CLANG_FORMAT_MIN_MAJOR:
			conf.fatal('clang-format %d found, need >= %d' % (major, CLANG_FORMAT_MIN_MAJOR))

		conf.msg('Checking clang-format version', major)

class ClangFormatTask(Task.Task):
	color = 'PINK'
	always_run = True
	binary = 'clang-format' # just a default, will be overriden

	def keyword(self):
		return 'Formatting'

	def run(self):
		return self.exec_command([self.binary, '-i', self.inputs[0].abspath()], env=self.env.env or None)

# need this because if we add headers to to `source`
# we can catch it in `format` feature, as nothing else
# catches .h
@TaskGen.extension('.h')
def format_stub(self, node):
	pass

@TaskGen.feature('format')
def make_format_tasks(self):
	if self.bld.cmd != 'format':
		return

	for n in self.to_nodes(Utils.to_list(getattr(self, 'source', []))):
		if n.suffix() in CLANG_FORMAT_EXTS:
			self.create_task('ClangFormatTask', n)

	self.source = [] # remove anything build related

class FormatContext(Build.BuildContext):
	cmd = 'format'
	fun = 'build'

	def execute(self):
		self.restore()
		if not self.all_envs:
			self.load_envs()
		self.recurse([self.run_dir])

		binary = os.environ.get('CLANG_FORMAT') or (self.env.CLANG_FORMAT and self.env.CLANG_FORMAT[0])
		if not binary:
			self.fatal('format: clang-format not configured')
		ClangFormatTask.binary = binary

		# loops below filter taskgens by target name, format feature and by c-f task class
		wanted = set(self.targets.split(',')) if self.targets else None
		tasks = []
		for group in self.groups:
			for tg in group:
				if not isinstance(tg, TaskGen.task_gen):
					continue

				if 'format' not in Utils.to_list(tg.features):
					continue

				if wanted and getattr(tg, 'name', None) not in wanted:
					continue

				tg.post()
				for t in tg.tasks:
					if isinstance(t, ClangFormatTask):
						tasks.append(t)

		if not tasks:
			self.fatal("format: no sources matched (need features='format' on a task gen)")

		Logs.info('format: %s on %d file(s)' % (binary, len(tasks)))
		self.progress_bar = 0 # FIXME: doesn't work with progress bar for some reason
		self.groups = [tasks]
		self.cur_group = 0
		self.targets = ''
		self.compile()
