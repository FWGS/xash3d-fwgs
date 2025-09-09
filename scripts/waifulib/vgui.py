#! /usr/bin/env python
# encoding: utf-8
# mittorn, 2018

from waflib.Configure import conf
from waflib import Logs
import os

VGUI_SUPPORTED_OS = ['win32', 'darwin', 'linux']

VGUI_FRAGMENT = '''#include <VGUI.h>
int main() { return 0; }'''

def options(opt):
	grp = opt.add_option_group('VGUI options')

	vgui_dev_path = os.path.join(opt.path.path_from(opt.root), 'vgui-dev')

	grp.add_option('--vgui', action = 'store', dest = 'VGUI_DEV', default=vgui_dev_path,
		help = 'path to vgui-dev repo [default: %(default)s]')

	grp.add_option('--enable-unsupported-vgui', action = 'store_true', dest = 'ENABLE_UNSUPPORTED_VGUI', default=False,
		help = 'ignore all checks and allow link against anything [default: %(default)s]')

	grp.add_option('--skip-vgui-sanity-check', action = 'store_false', dest = 'VGUI_SANITY_CHECK', default=True,
		help = 'skip checking VGUI sanity [default: %(default)s]' )
	return

@conf
def check_vgui(conf):
	if not conf.options.ENABLE_UNSUPPORTED_VGUI:
		conf.start_msg('Does this architecture support VGUI?')

		if conf.env.DEST_CPU != 'x86':
			conf.end_msg('no')
			Logs.warn('vgui is not supported on this CPU: ' + str(conf.env.DEST_CPU))
			return False
		else: conf.end_msg('yes')

		conf.start_msg('Does this OS support VGUI?')
		if conf.env.DEST_OS not in VGUI_SUPPORTED_OS:
			conf.end_msg('no')
			Logs.warn('vgui is not supported on this OS: ' + str(conf.env.DEST_OS))
			return False
		else: conf.end_msg('yes')

		conf.start_msg('Does this toolchain able to link VGUI?')
		if conf.env.DEST_OS == 'win32' and conf.env.COMPILER_CXX == 'g++':
			conf.end_msg('no')
			# we have ABI incompatibility ONLY on MinGW
			Logs.warn('vgui can\'t be linked with MinGW')
			return False
		else: conf.end_msg('yes')

	conf.start_msg('Configuring VGUI by provided path')
	vgui_dev = conf.options.VGUI_DEV

	libpath = os.path.abspath(os.path.join(vgui_dev, 'lib'))

	if conf.env.DEST_OS == 'win32':
		conf.env.LIB_VGUI = ['vgui']
		libpath = os.path.join(libpath, 'win32_vc6')
		if conf.env.DEST_CPU != 'x86':
			# for 32-bit x86 it's expected to be under win32_vc6
			# for others, it's expected to be under win32_vc6 subdirectory matching CPU arch (x86_64 for 64-bit CPUs)
			libpath = os.path.join(libpath, conf.env.DEST_CPU)
		conf.env.LIBPATH_VGUI = [libpath]
	elif conf.env.DEST_OS == 'linux':
		conf.env.LIB_VGUI = [':vgui.so']
		if conf.env.DEST_CPU != 'x86':
			libpath = os.path.join(libpath, conf.env.DEST_CPU)
		conf.env.LIBPATH_VGUI = [libpath]
	elif conf.env.DEST_OS == 'darwin':
		if conf.env.DEST_CPU != 'x86':
			conf.env.LDFLAGS_VGUI = [os.path.join(libpath, conf.env.DEST_CPU, 'vgui.dylib')]
		else:
			conf.env.LDFLAGS_VGUI = [os.path.join(libpath, 'vgui.dylib')]
	else:
		# TODO: figure out what to do here
		conf.env.LIB_VGUI = ['vgui']
		conf.env.LIBPATH_VGUI = [os.path.join(libpath, conf.env.DEST_OS, conf.env.DEST_CPU)]

	conf.env.INCLUDES_VGUI = [os.path.abspath(os.path.join(vgui_dev, 'include'))]

	conf.env.HAVE_VGUI = 1
	conf.end_msg('yes: {0}, {1}, {2}'.format(conf.env.LIB_VGUI, conf.env.LIBPATH_VGUI, conf.env.INCLUDES_VGUI))

	if conf.env.HAVE_VGUI and conf.options.VGUI_SANITY_CHECK:
		try:
			conf.check_cxx(fragment=VGUI_FRAGMENT,
				msg = 'Checking for library VGUI sanity',
				use = 'VGUI',
				execute = False)
		except conf.errors.ConfigurationError:
			conf.fatal("Can't compile simple program. Check your path to vgui-dev repository.")

	return True
