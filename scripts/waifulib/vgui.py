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
		help = 'path to vgui-dev repo [default: %default]')

	grp.add_option('--skip-vgui-sanity-check', action = 'store_false', dest = 'VGUI_SANITY_CHECK', default=True,
		help = 'skip checking VGUI sanity [default: %default]' )
	return

@conf
def check_vgui(conf):
	conf.start_msg('Does this architecture support VGUI?')

	if conf.env.DEST_CPU != 'x86' and not (conf.env.DEST_CPU == 'x86_64' and not conf.options.ALLOW64):
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

	if conf.env.DEST_OS == 'win32':
		conf.env.LIB_VGUI = ['vgui']
		conf.env.LIBPATH_VGUI = [os.path.abspath(os.path.join(vgui_dev, 'lib/win32_vc6/'))]
	else:
		libpath = os.path.abspath(os.path.join(vgui_dev, 'lib'))
		if conf.env.DEST_OS == 'linux':
			conf.env.LIB_VGUI = [':vgui.so']
			conf.env.LIBPATH_VGUI = [libpath]
		elif conf.env.DEST_OS == 'darwin':
			conf.env.LDFLAGS_VGUI = [os.path.join(libpath, 'vgui.dylib')]
		else:
			conf.fatal('vgui is not supported on this OS: ' + conf.env.DEST_OS)
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
