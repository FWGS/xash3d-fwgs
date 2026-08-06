#!/usr/bin/env python3
"""Apply the local GameCube build hook patch to hlsdk-portable."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


GAMECUBE_CLASS = r'''
class NintendoGameCube:
	ctx      = None
	dkp_dir  = None
	ppc_dir  = None
	libogc   = None
	stack    = None
	ogc_info = None

	def __init__( self, ctx ):
		self.ctx = ctx

		for i in GAMECUBE_ENVVARS:
			self.dkp_dir = os.getenv( i )
			if self.dkp_dir != None:
				break
		else:
			ctx.fatal( 'Set %s environment variable pointing to the DEVKITPRO home!' %
				' or '.join( GAMECUBE_ENVVARS ))

		self.dkp_dir = os.path.abspath( self.dkp_dir )
		self.ppc_dir = os.path.join( self.dkp_dir, 'devkitPPC' )
		if not os.path.exists( self.ppc_dir ):
			ctx.fatal( 'devkitPPC not found in `%s`. Install devkitPPC!' % self.ppc_dir )

		# Swiss-first: prefer Extrems libogc2 + libdvm, fall back to classic libogc.
		try:
			from waflib.extras.gamecube_ogc_stack import resolve_ogc_stack, format_summary
		except ImportError:
			from gamecube_ogc_stack import resolve_ogc_stack, format_summary

		self.ogc_info = resolve_ogc_stack( self.dkp_dir )
		if not self.ogc_info.get( 'available' ):
			ctx.fatal( self.ogc_info.get( 'error' ) or 'GameCube OGC stack not found' )

		self.stack = self.ogc_info['stack']
		self.libogc = self.ogc_info['root']
		Logs.info( 'GameCube OGC stack: %s' % format_summary( self.ogc_info ))

	def gen_toolchain_prefix( self ):
		return 'powerpc-eabi-'

	def gen_gcc_toolchain_path( self ):
		return os.path.join( self.ppc_dir, 'bin', self.gen_toolchain_prefix() )

	def cc( self ):
		return self.gen_gcc_toolchain_path() + 'gcc'

	def cxx( self ):
		return self.gen_gcc_toolchain_path() + 'g++'

	def strip( self ):
		return self.gen_gcc_toolchain_path() + 'strip'

	def cflags( self, cxx = False ):
		cflags = []
		cflags += ['-DGEKKO', '-D__GAMECUBE__', '-mogc', '-mcpu=750', '-meabi', '-mhard-float']
		cflags += ['-ffunction-sections', '-fdata-sections']
		cflags += ['-I%s' % self.ogc_info['include']]
		cflags += list( self.ogc_info.get( 'cflags_defines' ) or [] )
		if cxx:
			cflags += ['-std=gnu++17', '-D_GNU_SOURCE']
		else:
			cflags += ['-std=gnu11', '-D_GNU_SOURCE']
		return cflags

	def linkflags( self ):
		return list( self.ogc_info.get( 'linkflags' ) or [] )

	def ldflags( self ):
		return list( self.ogc_info.get( 'ldflags' ) or ['-logc', '-lm', '-lfat'] )
'''


def repo_root() -> Path:
	result = subprocess.run(["git", "rev-parse", "--show-toplevel"],
		text=True, capture_output=True, check=True)
	return Path(result.stdout.strip())


def replace(path: Path, old: str, new: str) -> bool:
	text = path.read_text(encoding="utf-8")
	if new in text:
		return False
	if old not in text:
		raise SystemExit(f"patch failed: expected text not found in {path}")
	path.write_text(text.replace(old, new, 1), encoding="utf-8")
	return True


def insert_before(path: Path, marker: str, addition: str) -> bool:
	text = path.read_text(encoding="utf-8")
	if addition.strip() in text:
		return False
	if marker not in text:
		raise SystemExit(f"patch failed: marker not found in {path}: {marker!r}")
	path.write_text(text.replace(marker, addition + marker, 1), encoding="utf-8")
	return True


def main() -> int:
	root = repo_root()
	hlsdk = Path(os.environ.get("HLSDK_PORTABLE_DIR", root / "3rdparty/hlsdk-portable"))
	if not (hlsdk / "wscript").is_file():
		print(f"patch: hlsdk-portable checkout not found: {hlsdk}", file=sys.stderr)
		return 2

	changed = 0
	xcompile = hlsdk / "scripts/waifulib/xcompile.py"
	stack_src = root / "scripts/waifulib/gamecube_ogc_stack.py"
	stack_dst = hlsdk / "scripts/waifulib/gamecube_ogc_stack.py"
	if stack_src.is_file():
		shutil.copy2(stack_src, stack_dst)
		changed += 1
		print(f"patch: installed {stack_dst.relative_to(hlsdk)}")

	changed += replace(xcompile,
		"NSWITCH_ENVVARS = ['DEVKITPRO']\n\nPSVITA_ENVVARS = ['VITASDK']",
		"NSWITCH_ENVVARS = ['DEVKITPRO']\nGAMECUBE_ENVVARS = ['DEVKITPRO']\n\nPSVITA_ENVVARS = ['VITASDK']")
	changed += insert_before(xcompile, "class NintendoSwitch:", GAMECUBE_CLASS + "\n")
	changed += replace(xcompile,
		"\txc.add_option('--enable-msvc-wine', action='store_true', dest='MSVC_WINE', default=False,\n"
		"\t\thelp='enable building with MSVC using Wine [default: %(default)s]')\n"
		"\txc.add_option('--nswitch', action='store_true', dest='NSWITCH', default = False,",
		"\txc.add_option('--enable-msvc-wine', action='store_true', dest='MSVC_WINE', default=False,\n"
		"\t\thelp='enable building with MSVC using Wine [default: %(default)s]')\n"
		"\txc.add_option('--gamecube', action='store_true', dest='GAMECUBE', default = False,\n"
		"\t\thelp='enable building for Nintendo GameCube [default: %(default)s]')\n"
		"\txc.add_option('--nswitch', action='store_true', dest='NSWITCH', default = False,")
	changed += replace(xcompile,
		"\telif conf.options.NSWITCH:\n",
		"\telif conf.options.GAMECUBE:\n"
		"\t\tconf.gamecube = gamecube = NintendoGameCube( conf )\n"
		"\t\tconf.environ['CC'] = gamecube.cc()\n"
		"\t\tconf.environ['CXX'] = gamecube.cxx()\n"
		"\t\tconf.environ['STRIP'] = gamecube.strip()\n"
		"\t\tconf.env.CFLAGS += gamecube.cflags()\n"
		"\t\tconf.env.CXXFLAGS += gamecube.cflags( True )\n"
		"\t\tconf.env.LINKFLAGS += gamecube.linkflags()\n"
		"\t\tconf.env.LDFLAGS += gamecube.ldflags()\n"
		"\t\tconf.env.HAVE_M = True\n"
		"\t\tconf.env.LIB_M = ['m']\n"
		"\t\tconf.env.DEST_OS = 'gamecube'\n"
		"\t\tconf.env.DEST_CPU = 'powerpc'\n"
		"\telif conf.options.NSWITCH:\n")
	changed += replace(xcompile,
		"MACRO_TO_DESTOS = OrderedDict({ '__ANDROID__' : 'android', '__SWITCH__' : 'nswitch', '__vita__' : 'psvita', '__wasi__': 'wasi', '__EMSCRIPTEN__' : 'emscripten' })",
		"MACRO_TO_DESTOS = OrderedDict({ '__ANDROID__' : 'android', '__GAMECUBE__' : 'gamecube', '__SWITCH__' : 'nswitch', '__vita__' : 'psvita', '__wasi__': 'wasi', '__EMSCRIPTEN__' : 'emscripten' })")

	build_h = hlsdk / "public/build.h"
	changed += replace(build_h,
		"\t#elif defined __SWITCH__\n\t\t#define XASH_NSWITCH 1",
		"\t#elif defined __GAMECUBE__\n\t\t#define XASH_GAMECUBE 1\n\t#elif defined __SWITCH__\n\t\t#define XASH_NSWITCH 1")
	changed += replace(build_h,
		"#if ( XASH_ANDROID && !XASH_TERMUX ) || XASH_IOS || XASH_NSWITCH || XASH_PSVITA || XASH_SAILFISH",
		"#if ( XASH_ANDROID && !XASH_TERMUX ) || XASH_IOS || XASH_GAMECUBE || XASH_NSWITCH || XASH_PSVITA || XASH_SAILFISH")

	naming_py = hlsdk / "scripts/waifulib/library_naming.py"
	changed += replace(naming_py,
		"'XASH_FREEBSD',\n'XASH_HAIKU',",
		"'XASH_FREEBSD',\n'XASH_GAMECUBE',\n'XASH_HAIKU',")
	changed += replace(naming_py,
		"\telif conf.env.XASH_NSWITCH:\n\t\tbuildos = \"nswitch\"",
		"\telif conf.env.XASH_GAMECUBE:\n\t\tbuildos = \"gamecube\"\n\telif conf.env.XASH_NSWITCH:\n\t\tbuildos = \"nswitch\"")

	naming_cmake = hlsdk / "cmake/LibraryNaming.cmake"
	changed += replace(naming_cmake,
		"elseif(XASH_NSWITCH)\n\tset(BUILDOS \"nswitch\")",
		"elseif(XASH_GAMECUBE)\n\tset(BUILDOS \"gamecube\")\nelseif(XASH_NSWITCH)\n\tset(BUILDOS \"nswitch\")")

	server_wscript = hlsdk / "dlls/wscript"
	changed += replace(server_wscript,
		"\tbld.shlib(\n"
		"\t\tsource   = source,",
		"\tbuilder = bld.stlib if bld.env.DEST_OS == 'gamecube' else bld.shlib\n"
		"\tbuilder(\n"
		"\t\tsource   = source,")

	client_wscript = hlsdk / "cl_dll/wscript"
	changed += replace(client_wscript,
		"\tbld.shlib(\n"
		"\t\tsource   = source,",
		"\tbuilder = bld.stlib if bld.env.DEST_OS == 'gamecube' else bld.shlib\n"
		"\tbuilder(\n"
		"\t\tsource   = source,")

	print(f"patch: applied GameCube HLSDK hooks ({changed} file edits)")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
