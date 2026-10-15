# encoding: utf-8
# ps4.py -- PlayStation 4 (OpenOrbis toolchain) linking support
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Xash3D FWGS contributors, see 3rdparty/ps4compat/LICENSE

# Every PS4 image (eboot.bin or .prx module) is a PIE ELF linked against
# OpenOrbis startup object, static musl libc and stubs of system libraries,
# later converted to signed ELF by create-fself (see scripts/gha/build_ps4.sh).
#
# Startup object and runtime libraries must appear exactly once per image,
# but task generators often have both C and C++ link features, so we
# can't use LDFLAGS_<feature> variables and add them here instead.

import os
from waflib import TaskGen

PS4_SYSTEM_LIBS = ['-lSceNet', '-lkernel']

# file functions redirected to working directory emulation, see 3rdparty/ps4compat/ps4_cwd.c
PS4_WRAPPED_FUNCS = ['open', 'fopen', 'stat', 'lstat', 'fstat', 'opendir', 'readdir', 'mkdir', 'rename', 'remove',
	'unlink', 'rmdir', 'access', 'chdir', 'getcwd', 'realpath', 'fcntl',
	# memory allocation goes to the process heap in eboot.bin, see ps4_malloc.c
	'malloc', 'free', 'calloc', 'realloc', 'memalign', 'aligned_alloc', 'valloc', 'posix_memalign',
	'malloc_usable_size',
	# standard output is made thread safe, see ps4_stdio.c
	'printf', 'vprintf', 'fprintf', 'vfprintf', 'puts', 'fputs', 'putchar', 'fputc', 'putc', 'fwrite',
	# semaphores are emulated, see ps4_sync.c
	'sem_init', 'sem_destroy', 'sem_post', 'sem_trywait', 'sem_wait', 'sem_timedwait', 'sem_getvalue']

PS4_MALLOC_FUNCS = ['malloc', 'free', 'calloc', 'realloc', 'memalign', 'aligned_alloc', 'valloc',
	'posix_memalign', 'malloc_usable_size']

def configure(conf):
	toolchain = conf.env.PS4_TOOLCHAIN
	conf.env.PS4_CRT_PROGRAM = os.path.join(toolchain, 'lib', 'crt1.o')

	# replace -shared, modules are PIE images too
	for i in ['cprogram', 'cxxprogram', 'cshlib', 'cxxshlib']:
		conf.env['LINKFLAGS_' + i] = ['-Wl,-pie']

	# runtime objects linked into every image:
	# ps4_cwd.c - working directory emulation
	# ps4_crtlib.c - module startup code, replaces broken crtlib.o from the toolchain
	# ps4_malloc.c - process-wide heap
	# ps4_stdio.c - thread safe standard output
	# ps4_sync.c - working semaphores
	# engine and hlsdk-portable keep them in different places
	# (object name, source, extra flags)
	objects = [
		('cwd', 'ps4_cwd', []),
		('stdio', 'ps4_stdio', []),
		('sync', 'ps4_sync', []),
		('crtlib', 'ps4_crtlib', []),
		('malloc', 'ps4_malloc', []),
		('malloc_module', 'ps4_malloc', ['-DPS4_MODULE=1']),
	]

	for key, name, extra in objects:
		for i in ['3rdparty/ps4compat/', 'scripts/ps4/']:
			src = conf.path.find_node(i + name + '.c')
			if src:
				break
		else:
			conf.fatal('%s.c not found' % name)

		obj = conf.bldnode.make_node('ps4_rt_%s.o' % key)
		conf.start_msg('Compiling PS4 runtime %s' % key)
		cmd = conf.env.CC + conf.env.CFLAGS + extra + ['-O2', '-c', src.abspath(), '-o', obj.abspath()]
		try:
			conf.cmd_and_log(cmd)
		except Exception as e:
			conf.end_msg('failed', color='RED')
			conf.fatal('Failed to compile %s: %s' % (src.abspath(), e))
		conf.end_msg('ok')

		conf.env['PS4_RT_' + key.upper()] = obj.abspath()

	# diagnostic switch: PS4_NO_MALLOC_WRAP=1 keeps original per-image musl malloc
	conf.env.PS4_MALLOC_WRAP = not os.environ.get('PS4_NO_MALLOC_WRAP')
	funcs = PS4_WRAPPED_FUNCS
	if not conf.env.PS4_MALLOC_WRAP:
		funcs = [i for i in funcs if i not in PS4_MALLOC_FUNCS]
	conf.msg('PS4 shared locked heap', conf.env.PS4_MALLOC_WRAP)
	conf.env.PS4_WRAP_FLAGS = ['-Wl,--wrap=%s' % i for i in funcs]

@TaskGen.feature('cprogram', 'cxxprogram', 'cshlib', 'cxxshlib')
@TaskGen.after_method('propagate_uselib_vars')
def ps4_add_runtime(self):
	if self.env.DEST_OS != 'ps4' or getattr(self, 'ps4_runtime_added', False):
		return

	self.ps4_runtime_added = True

	is_library = 'cshlib' in self.features or 'cxxshlib' in self.features
	is_cxx = 'cxx' in self.features or 'cxxprogram' in self.features or 'cxxshlib' in self.features

	if is_library:
		flags = [self.env.PS4_RT_CRTLIB]
		if self.env.PS4_MALLOC_WRAP:
			flags += [self.env.PS4_RT_MALLOC_MODULE]
	else:
		flags = [self.env.PS4_CRT_PROGRAM, '-lSceSystemService']
		if self.env.PS4_MALLOC_WRAP:
			# eboot.bin owns the process heap, modules find it at runtime
			flags += [self.env.PS4_RT_MALLOC]
	flags += [self.env.PS4_RT_CWD, self.env.PS4_RT_STDIO, self.env.PS4_RT_SYNC]
	flags += self.env.PS4_WRAP_FLAGS
	if is_cxx:
		flags += ['-lc++']
	flags += ['-lc'] + PS4_SYSTEM_LIBS

	self.env.append_value('LDFLAGS', flags)

@TaskGen.feature('cprogram', 'cxxprogram', 'cshlib', 'cxxshlib')
@TaskGen.after_method('apply_link')
def ps4_runtime_deps(self):
	# runtime objects are passed through LDFLAGS, so waf doesn't know
	# images have to be relinked when they change
	if self.env.DEST_OS != 'ps4' or not getattr(self, 'link_task', None):
		return

	for key in ['PS4_RT_CWD', 'PS4_RT_STDIO', 'PS4_RT_SYNC', 'PS4_RT_CRTLIB', 'PS4_RT_MALLOC', 'PS4_RT_MALLOC_MODULE']:
		path = self.env[key]
		if path:
			node = self.bld.root.find_node(path)
			if node:
				self.link_task.dep_nodes.append(node)
