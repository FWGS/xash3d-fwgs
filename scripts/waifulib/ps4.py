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
	'printf', 'vprintf', 'fprintf', 'vfprintf', 'puts', 'fputs', 'putchar', 'fputc', 'putc', 'fwrite']

PS4_MALLOC_FUNCS = ['malloc', 'free', 'calloc', 'realloc', 'memalign', 'aligned_alloc', 'valloc',
	'posix_memalign', 'malloc_usable_size']

def configure(conf):
	toolchain = conf.env.PS4_TOOLCHAIN
	conf.env.PS4_CRT_PROGRAM = os.path.join(toolchain, 'lib', 'crt1.o')

	# replace -shared, modules are PIE images too
	for i in ['cprogram', 'cxxprogram', 'cshlib', 'cxxshlib']:
		conf.env['LINKFLAGS_' + i] = ['-Wl,-pie']

	# diagnostic switch: PS4_NO_MALLOC_WRAP=1 keeps original per-image musl malloc
	conf.env.PS4_MALLOC_WRAP = not os.environ.get('PS4_NO_MALLOC_WRAP')
	funcs = PS4_WRAPPED_FUNCS
	if not conf.env.PS4_MALLOC_WRAP:
		funcs = [i for i in funcs if i not in PS4_MALLOC_FUNCS]
	conf.msg('PS4 shared locked heap', conf.env.PS4_MALLOC_WRAP)
	conf.env.PS4_WRAP_FLAGS = ['-Wl,--wrap=%s' % i for i in funcs]

# runtime objects of 3rdparty/ps4compat:
# ps4_cwd.c - working directory emulation
# ps4_crtlib.c - module startup code, replaces broken crtlib.o from the toolchain
# ps4_malloc.c - process-wide heap
# ps4_stdio.c - thread safe standard output
@TaskGen.feature('cprogram', 'cxxprogram', 'cshlib', 'cxxshlib')
@TaskGen.before_method('process_use')
def ps4_use_runtime(self):
	# configuration tests link without it, it's built later
	if self.env.DEST_OS != 'ps4' or getattr(self.bld, 'conf', None):
		return

	is_library = 'cshlib' in self.features or 'cxxshlib' in self.features
	use = ['ps4compat_cwd', 'ps4compat_stdio']

	if is_library:
		use += ['ps4compat_crtlib']
		if self.env.PS4_MALLOC_WRAP:
			use += ['ps4compat_malloc_module']
	elif self.env.PS4_MALLOC_WRAP:
		# eboot.bin owns the process heap, modules find it at runtime
		use += ['ps4compat_malloc']

	self.use = self.to_list(getattr(self, 'use', [])) + use

@TaskGen.feature('cprogram', 'cxxprogram', 'cshlib', 'cxxshlib')
@TaskGen.after_method('propagate_uselib_vars')
def ps4_add_runtime(self):
	if self.env.DEST_OS != 'ps4' or getattr(self, 'ps4_runtime_added', False):
		return

	self.ps4_runtime_added = True

	is_library = 'cshlib' in self.features or 'cxxshlib' in self.features
	is_cxx = 'cxx' in self.features or 'cxxprogram' in self.features or 'cxxshlib' in self.features

	flags = [] if is_library else [self.env.PS4_CRT_PROGRAM, '-lSceSystemService']
	if not getattr(self.bld, 'conf', None):
		flags += self.env.PS4_WRAP_FLAGS
	if is_cxx:
		flags += ['-lc++']
	flags += ['-lc'] + PS4_SYSTEM_LIBS

	self.env.append_value('LDFLAGS', flags)
