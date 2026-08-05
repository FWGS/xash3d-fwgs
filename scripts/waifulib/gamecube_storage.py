# encoding: utf-8
"""Swiss / libdvm GameCube storage volume helpers (host-safe).

Mirrors engine/platform/gamecube/sys_gamecube.c volume preference:
  sd: (SD2SP2) → carda: (SD Gecko A) → cardb: (SD Gecko B)

Prefer a mounted root that already has xash3d/valve; otherwise first mounted.
"""
from __future__ import annotations

import re
from typing import Iterable, List, Optional, Sequence


FAT_VOLUME_ROOTS: tuple[str, ...] = ("sd:/", "carda:/", "cardb:/")
DATA_PATH = "xash3d"
LAYOUT_SUBDIRS: tuple[str, ...] = ("valve", "valve/save", "valve/logs", "valve/screenshots")

_DEVICE_PREFIX_RE = re.compile(
	r"^(?:sd|carda|cardb|gcdisc|gcprobe):/+",
	re.IGNORECASE,
)

_FAT_VOLUME_READY_RE = re.compile(
	r"FAT volume ready\s+(sd:/|carda:/|cardb:/)",
	re.IGNORECASE,
)
_FAT_PREFERRED_RE = re.compile(
	r"FAT preferred volume\s+(sd:/|carda:/|cardb:/)",
	re.IGNORECASE,
)
_G508_READY_RE = re.compile(
	r"G508 config round trip ready\s+route=(\S+)",
	re.IGNORECASE,
)


def normalize_volume_root(root: str) -> str:
	text = (root or "").strip()
	if not text:
		return ""
	if not text.endswith("/"):
		text += "/"
	return text.lower() if text.lower().startswith(("sd:", "carda:", "cardb:")) else text


def strip_device_prefix(path: str) -> str:
	"""Strip sd:/ carda:/ cardb:/ gcdisc:/ gcprobe:/ for host filesystem checks."""
	if not path:
		return path
	return _DEVICE_PREFIX_RE.sub("", path)


def writable_layout_paths(volume_root: str) -> List[str]:
	"""Return device-prefixed layout paths under volume_root/xash3d/..."""
	root = normalize_volume_root(volume_root)
	if not root:
		raise ValueError("volume_root required")
	base = f"{root}{DATA_PATH}"
	paths = [base]
	for sub in LAYOUT_SUBDIRS:
		paths.append(f"{base}/{sub}")
	return paths


def select_fat_volume(
	mounted_roots: Sequence[str],
	*,
	has_valve: Optional[Iterable[str]] = None,
) -> Optional[str]:
	"""Pick preferred FAT volume from mounted roots.

	has_valve: iterable of volume roots that already contain xash3d/valve.
	"""
	mounted = []
	seen = set()
	for raw in mounted_roots:
		root = normalize_volume_root(raw)
		if not root or root not in FAT_VOLUME_ROOTS:
			continue
		if root in seen:
			continue
		seen.add(root)
		mounted.append(root)

	if not mounted:
		return None

	valve_set = {normalize_volume_root(v) for v in (has_valve or []) if v}

	# Prefer probe order among volumes that already have valve content.
	for root in FAT_VOLUME_ROOTS:
		if root in mounted and root in valve_set:
			return root

	# Otherwise first mounted in Swiss/libdvm order.
	for root in FAT_VOLUME_ROOTS:
		if root in mounted:
			return root
	return mounted[0]


def parse_fat_volume_status(text: str) -> dict:
	"""Parse guest FAT volume markers from probe logs."""
	ready = [m.group(1).lower() for m in _FAT_VOLUME_READY_RE.finditer(text or "")]
	# de-dupe preserving order
	seen = set()
	volumes = []
	for root in ready:
		if root not in seen:
			seen.add(root)
			volumes.append(root)
	preferred = None
	match = _FAT_PREFERRED_RE.search(text or "")
	if match:
		preferred = match.group(1).lower()
	elif volumes:
		preferred = select_fat_volume(volumes)
	return {
		"volumes": volumes,
		"preferred": preferred,
		"ok": bool(volumes),
	}


def parse_g508_status(text: str) -> dict:
	"""Parse G508 config round-trip markers from probe logs."""
	body = text or ""
	match = _G508_READY_RE.search(body)
	route = match.group(1) if match else None
	ready = bool(match) or "G508 config round trip ready" in body
	return {
		"ready": ready,
		"route": route,
		"write_ready": "G508 config write ready" in body,
		"read_ready": "G508 config read ready" in body,
		"write_failed": "G508 config write failed" in body,
		"read_failed": "G508 config read failed" in body,
	}


def format_layout_help(volume_root: str = "sd:/") -> str:
	root = normalize_volume_root(volume_root) or "sd:/"
	paths = writable_layout_paths(root)
	lines = [
		f"== Swiss FAT layout ({root.rstrip(':')}) ==",
		"Copy:",
		f"  OUT/bin/boot.dol -> {root}apps/xash3d-gc/boot.dol",
		f"  legal Half-Life assets -> {root}{DATA_PATH}/valve/",
		"",
		"Writable layout:",
	]
	lines.extend(f"  {p}" for p in paths)
	lines += [
		"",
		"Volumes probed by the DOL (libdvm): sd: (SD2SP2), carda:/cardb: (SD Gecko).",
	]
	return "\n".join(lines)


if __name__ == "__main__":
	import argparse
	import json

	parser = argparse.ArgumentParser(description="GameCube Swiss storage helpers")
	parser.add_argument("--layout", choices=list(FAT_VOLUME_ROOTS) + ["sd", "carda", "cardb"],
		default="sd:/", help="print layout for volume")
	parser.add_argument("--select", nargs="*", help="select among mounted roots")
	parser.add_argument("--has-valve", nargs="*", default=[], help="roots with xash3d/valve")
	parser.add_argument("--parse-log", type=str, help="parse FAT/G508 markers from a log file")
	args = parser.parse_args()

	if args.parse_log:
		text = open(args.parse_log, encoding="utf-8", errors="replace").read()
		print(json.dumps({
			"fat": parse_fat_volume_status(text),
			"g508": parse_g508_status(text),
		}, indent=2))
	elif args.select is not None:
		print(select_fat_volume(args.select, has_valve=args.has_valve) or "")
	else:
		vol = args.layout
		if not vol.endswith(":/"):
			vol = vol.rstrip(":/") + ":/"
		print(format_layout_help(vol))
