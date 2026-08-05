# encoding: utf-8
"""Resolve the GameCube OGC stack for Swiss-oriented builds.

Default preference (Swiss / Extrems ecosystem):
  1. libogc2 under $DEVKITPRO/libogc2/gamecube
  2. classic libogc under $DEVKITPRO/libogc

FAT provider preference when using libogc2:
  libdvm (provides -lfat API compatibility) over stock libfat.

Override with XASH_GAMECUBE_OGC_STACK=libogc2|libogc|auto (default auto).
"""
from __future__ import annotations

import os
from typing import Any, Dict, List, Optional


STACK_LIBOGC2 = "libogc2"
STACK_LIBOGC = "libogc"
STACK_AUTO = "auto"

ENV_STACK = "XASH_GAMECUBE_OGC_STACK"
ENV_DEVKITPRO = "DEVKITPRO"


def default_devkitpro() -> str:
	return os.environ.get(ENV_DEVKITPRO, "/opt/devkitpro")


def requested_stack(environ: Optional[Dict[str, str]] = None) -> str:
	env = environ if environ is not None else os.environ
	raw = (env.get(ENV_STACK) or STACK_AUTO).strip().lower()
	if raw in (STACK_LIBOGC2, STACK_LIBOGC, STACK_AUTO):
		return raw
	return STACK_AUTO


def _is_libogc2_root(root: str) -> bool:
	inc = os.path.join(root, "include")
	lib = os.path.join(root, "lib")
	return (
		os.path.isdir(inc)
		and os.path.isfile(os.path.join(lib, "libogc.a"))
	)


def _is_classic_libogc_root(root: str) -> bool:
	inc = os.path.join(root, "include")
	lib = os.path.join(root, "lib", "cube")
	return (
		os.path.isdir(inc)
		and os.path.isfile(os.path.join(lib, "libogc.a"))
	)


def _fat_provider(lib_dir: str) -> str:
	"""Return fat|dvm|none based on archives present in lib_dir."""
	has_fat = os.path.isfile(os.path.join(lib_dir, "libfat.a"))
	has_dvm = os.path.isfile(os.path.join(lib_dir, "libdvm.a"))
	if has_dvm and has_fat:
		# libdvm installs both libdvm.a and a fat-compat libfat.a
		return "libdvm"
	if has_dvm:
		return "libdvm"
	if has_fat:
		return "libfat"
	return "none"


def resolve_ogc_stack(
	dkp: Optional[str] = None,
	environ: Optional[Dict[str, str]] = None,
) -> Dict[str, Any]:
	"""Return a resolved stack description.

	Keys: stack, root, include, lib, fat_provider, cflags_defines,
	linkflags, ldflags, available, error
	"""
	env = environ if environ is not None else os.environ
	dkp_dir = os.path.abspath(dkp or env.get(ENV_DEVKITPRO) or default_devkitpro())
	want = requested_stack(env)

	libogc2_root = os.path.join(dkp_dir, "libogc2", "gamecube")
	classic_root = os.path.join(dkp_dir, "libogc")

	libogc2_ok = _is_libogc2_root(libogc2_root)
	classic_ok = _is_classic_libogc_root(classic_root)

	chosen = None
	error = None

	if want == STACK_LIBOGC2:
		if libogc2_ok:
			chosen = STACK_LIBOGC2
		else:
			error = (
				"XASH_GAMECUBE_OGC_STACK=libogc2 but libogc2 not found under %s "
				"(expected include/ and lib/libogc.a)" % libogc2_root
			)
	elif want == STACK_LIBOGC:
		if classic_ok:
			chosen = STACK_LIBOGC
		else:
			error = (
				"XASH_GAMECUBE_OGC_STACK=libogc but classic libogc not found under %s "
				"(expected include/ and lib/cube/libogc.a)" % classic_root
			)
	else:
		# auto: Swiss-first
		if libogc2_ok:
			chosen = STACK_LIBOGC2
		elif classic_ok:
			chosen = STACK_LIBOGC
		else:
			error = (
				"GameCube requires libogc2 (preferred for Swiss) or classic libogc "
				"under DEVKITPRO (%s)" % dkp_dir
			)

	if error or chosen is None:
		return {
			"stack": None,
			"root": None,
			"include": None,
			"lib": None,
			"fat_provider": "none",
			"cflags_defines": [],
			"linkflags": [],
			"ldflags": [],
			"available": False,
			"error": error or "unable to resolve GameCube OGC stack",
			"devkitpro": dkp_dir,
			"libogc2_available": libogc2_ok,
			"libogc_available": classic_ok,
		}

	if chosen == STACK_LIBOGC2:
		root = libogc2_root
		include = os.path.join(root, "include")
		lib = os.path.join(root, "lib")
		# libogc2 make rules use MACHDEP on the link line; -mogc selects the
		# GameCube multilib / linker script without classic share/ogc.specs.
		linkflags = ["-mogc", "-L%s" % lib]
		defines = ["-DXASH_GAMECUBE_LIBOGC2=1"]
	else:
		root = classic_root
		include = os.path.join(root, "include")
		lib = os.path.join(root, "lib", "cube")
		specs = os.path.join(root, "share", "ogc.specs")
		linkflags = ["-specs=%s" % specs, "-L%s" % lib]
		defines = ["-DXASH_GAMECUBE_LIBOGC=1"]

	fat = _fat_provider(lib)
	# Prefer -lfat: libdvm installs a fat-compat libfat.a that already embeds
	# the dvm objects. Do not also link -ldvm when libfat.a is present (duplicate
	# symbols). Only request -ldvm alone if somehow only libdvm.a exists.
	if fat == "libdvm":
		if os.path.isfile(os.path.join(lib, "libfat.a")):
			fat_libs = ["-lfat"]
		else:
			fat_libs = ["-ldvm"]
		defines.append("-DXASH_GAMECUBE_LIBDVM=1")
	elif fat == "libfat":
		fat_libs = ["-lfat"]
		defines.append("-DXASH_GAMECUBE_LIBFAT=1")
	else:
		fat_libs = ["-lfat"]  # still request it; linker will report missing

	ldflags = ["-logc", "-lm"] + fat_libs

	return {
		"stack": chosen,
		"root": root,
		"include": include,
		"lib": lib,
		"fat_provider": fat,
		"cflags_defines": defines,
		"linkflags": linkflags,
		"ldflags": ldflags,
		"available": True,
		"error": None,
		"devkitpro": dkp_dir,
		"libogc2_available": libogc2_ok,
		"libogc_available": classic_ok,
	}


def format_summary(info: Dict[str, Any]) -> str:
	if not info.get("available"):
		return "unavailable: %s" % info.get("error", "unknown")
	return "stack=%s root=%s fat=%s" % (
		info.get("stack"),
		info.get("root"),
		info.get("fat_provider"),
	)


def engine_extra_ldflags(info: Optional[Dict[str, Any]] = None) -> List[str]:
	"""Full engine GameCube LDFLAGS after HLSDK archives (iso9660/asnd/ogc/fat)."""
	if info is None:
		info = resolve_ogc_stack()
	fat = info.get("fat_provider") or "libfat"
	lib = info.get("lib") or ""
	flags = ["-lstdc++", "-liso9660", "-lasnd", "-logc", "-lm"]
	if fat == "libdvm":
		# libdvm's libfat.a already embeds dvm objects; avoid -lfat -ldvm together.
		if lib and os.path.isfile(os.path.join(lib, "libfat.a")):
			flags.append("-lfat")
		elif lib and os.path.isfile(os.path.join(lib, "libdvm.a")):
			flags.append("-ldvm")
		else:
			flags.append("-lfat")
	else:
		flags.append("-lfat")
	flags.append("-Wl,--allow-multiple-definition")
	return flags


if __name__ == "__main__":
	import json
	print(json.dumps(resolve_ogc_stack(), indent=2, sort_keys=True))
