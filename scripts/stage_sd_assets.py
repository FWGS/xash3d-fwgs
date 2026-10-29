#!/usr/bin/env python3
"""
stage_sd_assets.py - Stage Half-Life onto a Swiss FAT volume for real GameCube hardware

Writes the same payload the retail Dolphin disc carries (build-gamecube-disc.py
stage_retail_data + baked menu/loading art + boot overlays +
gamecube-bootstrap.pk3 + extras.pk3), so the SD route boots the files Dolphin
has tested.

Usage:
    python3 stage_sd_assets.py <source_dir> <sd_card_path> [options]

<source_dir> is the Half-Life install (containing valve/) or its valve/ folder.

Layout written under the mount:
    apps/xash3d-gc/boot.dol
    xash3d/valve/                 (game data, gamecube.cfg)
    xash3d/valve/save/ logs/ screenshots/

Saves and an existing config.cfg on the card are kept.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
ROOT = SCRIPTS.parent
VOLUMES = {"sd": "sd:/", "sd2sp2": "sd:/", "carda": "carda:/", "sdgecko": "carda:/", "cardb": "cardb:/"}


def load_disc_module():
	spec = importlib.util.spec_from_file_location("gamecube_disc", SCRIPTS / "build-gamecube-disc.py")
	mod = importlib.util.module_from_spec(spec)
	sys.modules[spec.name] = mod
	spec.loader.exec_module(mod)
	return mod


def resolve_valve(source: Path) -> Path | None:
	for candidate in (source / "valve", source):
		if (candidate / "liblist.gam").is_file() or (candidate / "gfx.wad").is_file():
			return candidate
	return None


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("source_dir", type=Path, help="Half-Life install or its valve/ folder")
	parser.add_argument("sd_card_path", type=Path, help="host mount point of the FAT32 card")
	parser.add_argument("--route", default="sd", choices=sorted(VOLUMES),
		help="Swiss/libdvm volume the card appears as (default: sd)")
	parser.add_argument("--dol", type=Path, default=ROOT / "OUT/bin/boot.dol",
		help="boot.dol to copy to apps/xash3d-gc/ (default: OUT/bin/boot.dol)")
	parser.add_argument("--no-dol", action="store_true", help="do not copy boot.dol")
	parser.add_argument("--extras", type=Path, default=ROOT / "OUT/valve/extras.pk3")
	parser.add_argument("--skip-startup-vids", action="store_true",
		help="leave the intro movie companions out (same as the disc option)")
	route = parser.add_mutually_exclusive_group()
	route.add_argument("--plain-menu", action="store_true",
		help="no gamecube.cfg: plain retail menu boot instead of the menu New Game route")
	route.add_argument("--boot-cfg", action="append", metavar="LINE",
		help="write this gamecube.cfg line instead of the default 'retailnewgame' (repeatable)")
	args = parser.parse_args()

	valve_src = resolve_valve(args.source_dir)
	if valve_src is None:
		print(f"Error: no liblist.gam or gfx.wad under {args.source_dir} or {args.source_dir}/valve", file=sys.stderr)
		return 1
	if not args.sd_card_path.is_dir():
		print(f"Error: card mount does not exist: {args.sd_card_path}", file=sys.stderr)
		return 1
	if not os.access(args.sd_card_path, os.W_OK):
		print(f"Error: card mount is not writable: {args.sd_card_path}", file=sys.stderr)
		return 1
	dol = None if args.no_dol else args.dol
	if dol is not None and not dol.is_file():
		print(f"Error: {dol} not found (build it with scripts/build-gamecube.sh, or pass --no-dol)", file=sys.stderr)
		return 1
	extras = args.extras if args.extras.is_file() else None

	disc = load_disc_module()
	errors = disc.validate_assets(valve_src)
	if errors:
		print("Source asset validation failed:", file=sys.stderr)
		for error in errors:
			print(f"  - {error}", file=sys.stderr)
		return 1

	if args.plain_menu:
		boot_cfg: tuple[str, ...] = ()
	elif args.boot_cfg:
		boot_cfg = tuple(args.boot_cfg)
	else:
		boot_cfg = disc.SD_DEFAULT_BOOT_CFG

	print(f"Staging {valve_src} -> {args.sd_card_path} (Swiss volume {VOLUMES[args.route]})")
	valve = disc.stage_sd_card(
		valve_src,
		args.sd_card_path,
		dol,
		extras,
		include_startup_vids=not args.skip_startup_vids,
		boot_cfg=boot_cfg,
	)

	errors = disc.validate_staged_retail_assets(valve)
	# The card legitimately carries the player's save/ and logs/ trees.
	errors = [e for e in errors if "/save/" not in e and "/logs/" not in e]
	if errors:
		print("Staged card validation failed:", file=sys.stderr)
		for error in errors:
			print(f"  - {error}", file=sys.stderr)
		return 1

	root = VOLUMES[args.route]
	print("")
	print("Card ready. In Swiss:")
	print(f"  boot {root}apps/xash3d-gc/boot.dol")
	print(f"  game data {root}xash3d/valve/")
	print("Unmount the card cleanly before removing it (FAT32, not exFAT).")
	return 0


if __name__ == "__main__":
	sys.exit(main())
