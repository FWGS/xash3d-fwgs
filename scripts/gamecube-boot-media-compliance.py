#!/usr/bin/env python3
"""Generate G43 boot media and loader-failure compliance evidence."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


DISC_MAGIC = 0xC2339F3D


@dataclass
class Check:
	name: str
	status: str
	detail: str
	evidence: str = ""
	required: bool = True


def load_disc_module(root: Path):
	script = root / "scripts/build-gamecube-disc.py"
	spec = importlib.util.spec_from_file_location("gamecube_disc", script)
	if spec is None or spec.loader is None:
		raise RuntimeError(f"unable to load {script}")
	module = importlib.util.module_from_spec(spec)
	sys.modules[spec.name] = module
	spec.loader.exec_module(module)
	return module


def sha256(path: Path) -> str:
	digest = hashlib.sha256()
	with path.open("rb") as handle:
		for chunk in iter(lambda: handle.read(1024 * 1024), b""):
			digest.update(chunk)
	return digest.hexdigest()


def artifact_detail(path: Path) -> str:
	if not path.is_file():
		return "missing"
	return f"{path} size={path.stat().st_size} sha256={sha256(path)}"


def check_disc_header(path: Path) -> tuple[bool, str]:
	if not path.is_file():
		return False, f"BOOT_MEDIA_FAILURE: missing media image: {path}"
	data = path.read_bytes()[:0x440]
	if len(data) < 0x440:
		return False, f"BOOT_MEDIA_FAILURE: media image is too small: {path} ({len(data)} bytes)"
	magic = struct.unpack_from(">I", data, 0x1C)[0]
	dol_offset = struct.unpack_from(">I", data, 0x420)[0]
	fst_offset = struct.unpack_from(">I", data, 0x424)[0]
	if magic != DISC_MAGIC:
		return False, f"BOOT_MEDIA_FAILURE: invalid GameCube magic 0x{magic:08x} in {path}"
	if dol_offset <= 0 or fst_offset <= dol_offset:
		return False, (
			f"BOOT_MEDIA_FAILURE: invalid boot layout in {path}: "
			f"dol_offset=0x{dol_offset:x} fst_offset=0x{fst_offset:x}"
		)
	return True, (
		f"disc header ok: magic=0x{magic:08x} "
		f"dol_offset=0x{dol_offset:x} fst_offset=0x{fst_offset:x}"
	)


def run(command: list[str], root: Path, log_path: Path) -> int:
	with log_path.open("w", encoding="utf-8") as log:
		log.write("$ " + " ".join(command) + "\n")
		log.flush()
		process = subprocess.run(
			command,
			cwd=root,
			text=True,
			stdout=log,
			stderr=subprocess.STDOUT,
			check=False,
		)
		log.write(f"\n[exit {process.returncode}]\n")
		return process.returncode


def stage_smoke(disc, source: Path, destination: Path, smoke_map: str) -> Path:
	staged = disc.stage_smoke_data(source, destination, smoke_map)
	errors = disc.validate_smoke_assets(staged, smoke_map)
	if errors:
		raise RuntimeError("\n".join(errors))
	return staged


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--smoke-map", default="c0a0e")
	parser.add_argument("--log-dir", type=Path)
	parser.add_argument("--build", action="store_true",
		help="run scripts/build-gamecube.sh before checking artifacts")
	parser.add_argument("--build-disc", action="store_true",
		help="build a fresh smoke ISO before corrupt-media checks")
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"boot-media-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"
	disc = load_disc_module(root)
	checks: list[Check] = []

	if args.build:
		log_path = log_dir / "build-gamecube.log"
		rc = run(["scripts/build-gamecube.sh"], root, log_path)
		checks.append(Check(
			"build command",
			"PASS" if rc == 0 else "FAIL",
			"scripts/build-gamecube.sh",
			str(log_path),
		))

	dol = root / "OUT/bin/boot.dol"
	iso = root / "OUT/xash3d-gc.iso"
	xash = root / "OUT/bin/xash"
	for name, path in (("boot.dol artifact", dol), ("xash artifact", xash)):
		checks.append(Check(
			name,
			"PASS" if path.is_file() and path.stat().st_size > 0 else "FAIL",
			artifact_detail(path),
			required=True,
		))

	source = root / "Half-Life/valve"
	if not source.is_dir():
		checks.append(Check(
			"legal asset source",
			"WARN",
			"Half-Life/valve is missing; staged-content negative tests skipped",
			required=False,
		))
	else:
		with tempfile.TemporaryDirectory(prefix="xash3d-gc-g43-") as temp_name:
			temp = Path(temp_name)
			baseline = stage_smoke(disc, source, temp / "baseline" / "valve", args.smoke_map)
			baseline_errors = disc.validate_smoke_assets(baseline, args.smoke_map)
			checks.append(Check(
				"baseline smoke staging",
				"PASS" if not baseline_errors else "FAIL",
				"staged legal smoke subset validates cleanly"
				if not baseline_errors else "; ".join(baseline_errors),
			))

			missing = temp / "missing-map" / "valve"
			shutil.copytree(baseline, missing)
			missing_map = missing / "maps" / f"{Path(args.smoke_map).stem}.bsp"
			missing_map.unlink(missing_ok=True)
			missing_errors = disc.validate_smoke_assets(missing, args.smoke_map)
			checks.append(Check(
				"missing staged map failure",
				"PASS" if any("MISSING" in error and "maps/" in error for error in missing_errors) else "FAIL",
				"; ".join(missing_errors) or "validator did not reject missing map",
			))

			case_bad = temp / "case-mismatch" / "valve"
			shutil.copytree(baseline, case_bad)
			palette = case_bad / "gfx/palette.lmp"
			palette_bad = case_bad / "gfx/Palette.lmp"
			if palette.is_file():
				palette.rename(palette_bad)
			case_errors = disc.validate_smoke_assets(case_bad, args.smoke_map)
			checks.append(Check(
				"case-mismatched staged asset failure",
				"PASS" if any("CASE_MISMATCH" in error for error in case_errors)
				and any("MISSING" in error and "gfx/palette.lmp" in error for error in case_errors)
				else "FAIL",
				"; ".join(case_errors) or "validator did not reject case mismatch",
			))

	if args.build_disc:
		log_path = log_dir / "build-disc.log"
		rc = run([
			"scripts/build-gamecube-disc.py",
			"--smoke-map",
			args.smoke_map,
			"--output",
			str(iso),
		], root, log_path)
		checks.append(Check(
			"disc build command",
			"PASS" if rc == 0 else "FAIL",
			f"scripts/build-gamecube-disc.py --smoke-map {args.smoke_map} --output {iso}",
			str(log_path),
		))

	if iso.is_file():
		ok, detail = check_disc_header(iso)
		checks.append(Check("ISO/GCM header", "PASS" if ok else "FAIL", detail))
		corrupt = log_dir / "corrupt-xash3d-gc.iso"
		shutil.copy2(iso, corrupt)
		with corrupt.open("r+b") as handle:
			handle.seek(0x1C)
			handle.write(b"\0\0\0\0")
		ok, detail = check_disc_header(corrupt)
		checks.append(Check(
			"corrupt ISO/GCM failure",
			"PASS" if not ok and "BOOT_MEDIA_FAILURE" in detail else "FAIL",
			detail,
			str(corrupt),
		))
	else:
		checks.append(Check(
			"ISO/GCM header",
			"WARN",
			"OUT/xash3d-gc.iso is missing; run with --build-disc or RC_BUILD_DISC=1",
			required=False,
		))

	checks.append(Check(
		"Swiss loader evidence boundary",
		"WARN",
		"Manual Swiss/real-hardware loader evidence remains required; this command records local preflight evidence only.",
		"docs/GAMECUBE_HARDWARE_VALIDATION.md",
		required=False,
	))

	failed = [check for check in checks if check.required and check.status != "PASS"]
	warned = [check for check in checks if check.status == "WARN"]
	report.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"smoke_map": args.smoke_map,
		"ok": not failed,
		"checks": [asdict(check) for check in checks],
	}, indent=2) + "\n", encoding="utf-8")
	with summary.open("w", encoding="utf-8") as out:
		out.write("# GameCube Boot Media Compliance\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Smoke map: `{args.smoke_map}`\n")
		out.write(f"- Report: `{report}`\n")
		out.write(f"- Required failures: {len(failed)}\n")
		out.write(f"- Warnings: {len(warned)}\n\n")
		out.write("| Check | Status | Detail | Evidence |\n")
		out.write("|---|---|---|---|\n")
		for check in checks:
			detail = check.detail.replace("|", "\\|")
			evidence = check.evidence.replace("|", "\\|")
			out.write(f"| {check.name} | {check.status} | {detail} | `{evidence}` |\n")
		out.write("\n## Hardware Boundary\n\n")
		out.write(
			"This closes the automated G43 preflight: bad staged content and corrupt "
			"boot media produce explicit diagnostics instead of a silent launch. "
			"Swiss and physical-console evidence must still be recorded in the "
			"hardware matrix before release-complete claims.\n"
		)

	print(f"G43 boot media compliance summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	if failed:
		return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
