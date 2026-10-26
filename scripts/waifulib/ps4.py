# encoding: utf-8
# ps4.py -- PlayStation 4 (OpenOrbis toolchain) linking and packaging support
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Xash3D FWGS contributors, see 3rdparty/ps4compat/LICENSE

# Every PS4 image (eboot.bin or .prx module) is a PIE ELF linked against
# OpenOrbis startup object, static musl libc and stubs of system libraries,
# then converted to signed ELF by create-fself. The engine program puts all of
# them into an installable .pkg.
#
# Startup object and runtime libraries must appear exactly once per image,
# but task generators often have both C and C++ link features, so we
# can't use LDFLAGS_<feature> variables and add them here instead.

import ctypes, os, shutil, subprocess, time
from waflib import Errors, Logs, Task, TaskGen, Utils

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

PS4_PAID = '0x3800000000000011'

def openssl1_found():
	# names .NET Core 3.0 looks for
	for i in ['libssl.so.1.1', 'libssl.so.1.0.2', 'libssl.so.1.0.0', 'libssl.so.10']:
		try:
			ctypes.CDLL(i)
		except OSError:
			continue
		return True
	return False

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

	tools = os.path.join(toolchain, 'bin', 'linux')
	conf.find_program('create-fself', var = 'PS4_CREATE_FSELF', path_list = [tools])
	conf.find_program('PkgTool.Core', var = 'PS4_PKGTOOL', path_list = [tools])

	# PkgTool.Core is built for .NET Core 3.0, it can only load OpenSSL 1.0 or 1.1
	conf.env.PS4_PKG = openssl1_found()
	conf.msg('Building PS4 package', 'yes' if conf.env.PS4_PKG else 'no, PkgTool.Core needs OpenSSL 1.1',
		color = 'GREEN' if conf.env.PS4_PKG else 'YELLOW')

	gamelibs = conf.options.PS4_GAMELIBS or os.path.join(conf.path.abspath(), '..', 'hlsdk-portable', 'build', 'ps4', 'valve')
	if os.path.isdir(gamelibs):
		conf.env.PS4_GAMELIBS = os.path.abspath(gamelibs)
	elif conf.options.PS4_GAMELIBS:
		conf.fatal('%s is not a directory' % gamelibs)
	conf.msg('PS4 game libraries', conf.env.PS4_GAMELIBS or 'not found, the package will need them on the console',
		color = 'GREEN' if conf.env.PS4_GAMELIBS else 'YELLOW')

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

class ps4_fself(Task.Task):
	color = 'CYAN'

	def keyword(self):
		return 'Signing'

	def run(self):
		cmd = self.env.PS4_CREATE_FSELF + ['-in=%s' % self.inputs[0].abspath(), '-out=%s' % self.outputs[1].abspath(),
			'-%s=%s' % (self.kind, self.outputs[0].abspath()), '-paid', PS4_PAID]
		return self.exec_command(cmd, env = dict(os.environ, OO_PS4_TOOLCHAIN = self.env.PS4_TOOLCHAIN))

@TaskGen.feature('cprogram', 'cxxprogram', 'cshlib', 'cxxshlib')
@TaskGen.after_method('apply_link')
def ps4_sign(self):
	if self.env.DEST_OS != 'ps4' or getattr(self.bld, 'conf', None):
		return

	elf = self.link_task.outputs[0]

	if 'cshlib' in self.features or 'cxxshlib' in self.features:
		tsk = self.create_task('ps4_fself', elf, [elf.change_ext('.prx'), elf.change_ext('.oelf')])
		tsk.kind = 'lib'
	else:
		tsk = self.create_task('ps4_fself', elf, [elf.parent.find_or_declare('eboot.bin'), elf.change_ext('.oelf')])
		tsk.kind = 'eboot'

	self.ps4_fself_task = tsk

def write_gp4(path, content_id, files):
	dirs = set()

	for f in files:
		d = os.path.dirname(f)
		while d:
			dirs.add(d)
			d = os.path.dirname(d)

	def write_dirs(out, parent, depth):
		for d in sorted(i for i in dirs if os.path.dirname(i) == parent):
			indent = '\t' * depth
			name = os.path.basename(d)
			if any(os.path.dirname(i) == d for i in dirs):
				out.append('%s<dir targ_name="%s">' % (indent, name))
				write_dirs(out, d, depth + 1)
				out.append('%s</dir>' % indent)
			else:
				out.append('%s<dir targ_name="%s" />' % (indent, name))

	out = [
		'<?xml version="1.0"?>',
		'<psproject xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" fmt="gp4" version="1000">',
		'\t<volume>',
		'\t\t<volume_type>pkg_ps4_app</volume_type>',
		'\t\t<volume_id>PS4VOLUME</volume_id>',
		'\t\t<volume_ts>%s</volume_ts>' % time.strftime('%Y-%m-%d %H:%M:%S'),
		'\t\t<package content_id="%s" passcode="00000000000000000000000000000000" storage_type="digital50" app_type="full" />' % content_id,
		'\t\t<chunk_info chunk_count="1" scenario_count="1">',
		'\t\t\t<chunks>',
		'\t\t\t\t<chunk id="0" layer_no="0" label="Chunk #0" />',
		'\t\t\t</chunks>',
		'\t\t\t<scenarios default_id="0">',
		'\t\t\t\t<scenario id="0" type="sp" initial_chunk_count="1" label="Scenario #0">0</scenario>',
		'\t\t\t</scenarios>',
		'\t\t</chunk_info>',
		'\t</volume>',
		'\t<files img_no="0">',
	]
	out += ['\t\t<file targ_path="%s" orig_path="%s" />' % (f, f) for f in sorted(files)]
	out += ['\t</files>', '\t<rootdir>']
	write_dirs(out, '', 2)
	out += ['\t</rootdir>', '</psproject>']

	with open(path, 'w') as f:
		f.write('\n'.join(out) + '\n')

class ps4_pkg(Task.Task):
	color = 'CYAN'
	vars = ['PS4_PKG_LAYOUT']

	def keyword(self):
		return 'Packing'

	def pkgtool(self, args):
		cmd = self.env.PS4_PKGTOOL + args
		env = dict(os.environ, DOTNET_SYSTEM_GLOBALIZATION_INVARIANT = '1')
		proc = subprocess.run(cmd, cwd = self.stage.abspath(), env = env, stdout = subprocess.PIPE, stderr = subprocess.STDOUT)

		if proc.returncode:
			Logs.error('%s\n%s', ' '.join(cmd), proc.stdout.decode('utf-8', 'replace'))
		return proc.returncode

	def run(self):
		stage = self.stage.abspath()
		shutil.rmtree(stage, ignore_errors = True)

		for targ, node in zip(self.layout, self.inputs):
			path = os.path.join(stage, targ)
			os.makedirs(os.path.dirname(path), exist_ok = True)
			shutil.copyfile(node.abspath(), path)

		sfo = os.path.join('sce_sys', 'param.sfo')
		entries = [
			('APP_TYPE', 'Integer', 4, 1),
			('APP_VER', 'Utf8', 8, '1.00'),
			# common dialogs (system keyboard) confirm with Cross, not Circle
			('ATTRIBUTE', 'Integer', 4, 2),
			('CATEGORY', 'Utf8', 4, 'gd'),
			('CONTENT_ID', 'Utf8', 48, self.content_id),
			('DOWNLOAD_DATA_SIZE', 'Integer', 4, 0),
			('SYSTEM_VER', 'Integer', 4, 0),
			('TITLE', 'Utf8', 128, self.title),
			('TITLE_ID', 'Utf8', 12, self.content_id[7:16]),
			('VERSION', 'Utf8', 8, '1.00'),
		]

		ret = self.pkgtool(['sfo_new', sfo])
		for key, kind, size, value in entries:
			if not ret:
				ret = self.pkgtool(['sfo_setentry', sfo, key, '--type', kind, '--maxsize', str(size), '--value', str(value)])
		if ret:
			return ret

		write_gp4(os.path.join(stage, 'pkg.gp4'), self.content_id, self.layout + [sfo])
		return self.pkgtool(['pkg_build', 'pkg.gp4', self.outputs[0].parent.abspath()])

@TaskGen.feature('cprogram', 'cxxprogram')
@TaskGen.after_method('ps4_sign')
def ps4_package(self):
	content_id = getattr(self, 'ps4_content_id', None)

	if self.env.DEST_OS != 'ps4' or not content_id or not self.env.PS4_PKG or getattr(self.bld, 'conf', None):
		return

	files = [('eboot.bin', self.ps4_fself_task.outputs[0])]

	# installed shared libraries go next to eboot.bin, see lib_ps4.c
	for tg in self.bld.get_all_task_gen():
		features = Utils.to_list(getattr(tg, 'features', []))
		if tg is self or not getattr(tg, 'install_path', None) or not ( 'cshlib' in features or 'cxxshlib' in features ):
			continue
		tg.post()
		fself = getattr(tg, 'ps4_fself_task', None)
		if fself:
			files.append((fself.outputs[0].name, fself.outputs[0]))

	try:
		extras = self.bld.get_tgen_by_name('extras.pk3')
	except Errors.WafError:
		extras = None
	if extras:
		extras.post()
		files.append((os.path.join(self.env.GAMEDIR, 'extras.pk3'), extras.path.get_bld().make_node(extras.name)))

	# game libraries from hlsdk-portable with function names for savegames
	if self.env.PS4_GAMELIBS:
		gamelibs = self.bld.root.find_dir(self.env.PS4_GAMELIBS)
		for node in gamelibs.ant_glob(['cl_dlls/*.prx', 'cl_dlls/*.prx.sym', 'dlls/*.prx', 'dlls/*.prx.sym']):
			files.append((node.name, node))

	# icon0.png is 512x512, pic0.png and pic1.png are 1920x1080, the system shows one of
	# them as splash until the engine hides it
	sce_sys = self.path.find_dir(self.sce_sys)
	pic0 = sce_sys.find_node('pic0.png')
	files.append(('sce_sys/icon0.png', sce_sys.find_node('icon0.png')))
	files.append(('sce_sys/pic0.png', pic0))
	files.append(('sce_sys/pic1.png', sce_sys.find_node('pic1.png') or pic0))

	sample = self.bld.root.find_dir(os.path.join(self.env.PS4_TOOLCHAIN, 'samples', 'hello_world'))
	files.append(('sce_sys/about/right.sprx', sample.find_node('sce_sys/about/right.sprx')))
	files.append(('sce_module/libc.prx', sample.find_node('sce_module/libc.prx')))
	files.append(('sce_module/libSceFios2.prx', sample.find_node('sce_module/libSceFios2.prx')))

	out = self.path.get_bld().make_node(content_id + '.pkg')
	tsk = self.create_task('ps4_pkg', [node for _, node in files], out)
	tsk.layout = [targ for targ, _ in files]
	tsk.stage = self.path.get_bld().make_node('ps4pkg')
	tsk.content_id = content_id
	tsk.title = getattr(self, 'ps4_title', content_id)
	self.env.PS4_PKG_LAYOUT = tsk.layout + [content_id, tsk.title]
