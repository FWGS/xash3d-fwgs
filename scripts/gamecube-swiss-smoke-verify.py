#!/usr/bin/env python3
"""Verify Swiss smoke FAT image contents without udisks/polkit mounts."""
from __future__ import annotations

import argparse
import hashlib
import sys
from pathlib import Path


def sha256(path: Path) -> str:
	h = hashlib.sha256()
	with path.open("rb") as f:
		for chunk in iter(lambda: f.read(1024 * 1024), b""):
			h.update(chunk)
	return h.hexdigest()


def main() -> int:
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument(
		"--img",
		default="OUT/xash3d-gc-swiss-smoke.img",
		help="FAT32 smoke image path",
	)
	ap.add_argument(
		"--dol",
		default="OUT/bin/boot.dol",
		help="Expected boot.dol to match inside the image",
	)
	ap.add_argument(
		"--venv",
		default=".ai/venv-fat",
		help="venv with pyfatfs (created by gamecube-swiss-smoke-image.sh)",
	)
	args = ap.parse_args()

	root = Path(__file__).resolve().parents[1]
	img = (root / args.img).resolve() if not Path(args.img).is_absolute() else Path(args.img)
	dol = (root / args.dol).resolve() if not Path(args.dol).is_absolute() else Path(args.dol)
	venv_py = root / args.venv / "bin" / "python"

	if not img.is_file():
		print(f"FAIL: missing image {img}", file=sys.stderr)
		return 2
	if not dol.is_file():
		print(f"FAIL: missing dol {dol}", file=sys.stderr)
		return 2

	# Prefer venv interpreter so pyfatfs imports match the image builder.
	# Do not compare resolve() paths — venv python may be a symlink to system
	# python while still having its own site-packages.
	if venv_py.is_file() and Path(sys.executable) != venv_py:
		# Also re-exec when sys.executable is the resolved target of the venv.
		same_venv = False
		try:
			same_venv = Path(sys.executable).resolve() == venv_py.resolve() and str(
				Path(sys.prefix)
			).endswith("venv-fat")
		except OSError:
			same_venv = False
		if not same_venv:
			import subprocess

			raise SystemExit(
				subprocess.call(
					[str(venv_py), str(Path(__file__).resolve()), *sys.argv[1:]]
				)
			)

	try:
		from pyfatfs.PyFatFS import PyFatFS  # type: ignore
	except ImportError:
		print(
			f"FAIL: pyfatfs not importable from {sys.executable}; "
			f"expected venv at {venv_py}",
			file=sys.stderr,
		)
		return 2

	need = [
		"apps/xash3d-gc/boot.dol",
		"xash3d/valve/gfx.wad",
		"xash3d/valve/liblist.gam",
		"xash3d/valve/gamecube.cfg",
		"xash3d/valve/logs",
	]
	dol_sha = sha256(dol)
	ok = True
	fs = PyFatFS(str(img), read_only=True)
	try:
		for rel in need:
			if not fs.exists(rel):
				print(f"MISS {rel}")
				ok = False
			else:
				info = fs.getinfo(rel)
				kind = "dir" if info.is_dir else f"size={info.size}"
				print(f"OK   {rel} ({kind})")

		with fs.openbin("apps/xash3d-gc/boot.dol") as f:
			img_dol_sha = hashlib.sha256(f.read()).hexdigest()
		if img_dol_sha != dol_sha:
			print(f"FAIL DOL mismatch img={img_dol_sha[:16]} expected={dol_sha[:16]}")
			ok = False
		else:
			print(f"OK   DOL sha256={dol_sha[:16]}…")

		with fs.openbin("xash3d/valve/gamecube.cfg") as f:
			cfg = f.read().decode("ascii", errors="replace")
		print(f"cfg  {cfg!r}")
		if "swissquit" not in cfg or "map " not in cfg:
			print("FAIL gamecube.cfg missing map/swissquit")
			ok = False
		# Require the cfg's smoke map BSP (defaults historically c0a0e; tip-safe c1a0a OK).
		map_name = None
		for line in cfg.splitlines():
			line = line.strip()
			if line.lower().startswith("map "):
				map_name = line.split(None, 1)[1].strip().split()[0]
				break
		if map_name:
			bsp = f"xash3d/valve/maps/{map_name}.bsp"
			if not fs.exists(bsp):
				print(f"MISS {bsp}")
				ok = False
			else:
				info = fs.getinfo(bsp)
				print(f"OK   {bsp} (size={info.size})")
		else:
			print("FAIL gamecube.cfg has no map line")
			ok = False

		# Optional Swiss loader copy (fresh-card convenience).
		for rel in ("swiss.dol", "apps/swiss/boot.dol"):
			if fs.exists(rel):
				print(f"OK   {rel} (Swiss loader present)")
	finally:
		fs.close()

	smoke_sha = sha256(img)
	print(f"img  sha256={smoke_sha[:16]}… size={img.stat().st_size}")
	if ok:
		print("PASS")
		return 0
	print("FAIL")
	return 1


if __name__ == "__main__":
	raise SystemExit(main())
