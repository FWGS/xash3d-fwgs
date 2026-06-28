#!/usr/bin/env python3
"""Generate G55 release artifact reproducibility evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import tarfile
import zipfile
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass
class Check:
	name: str
	status: str
	detail: str
	required: bool = True


def run(root: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
	return subprocess.run(args, cwd=root, text=True, capture_output=True, check=False)


def read(path: Path) -> str:
	return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def sha256_file(path: Path) -> str:
	hash_value = hashlib.sha256()
	with path.open("rb") as handle:
		for chunk in iter(lambda: handle.read(1024 * 1024), b""):
			hash_value.update(chunk)
	return hash_value.hexdigest()


def file_record(root: Path, path: Path) -> dict[str, object]:
	relative = path.relative_to(root).as_posix()
	return {
		"path": relative,
		"size": path.stat().st_size,
		"sha256": sha256_file(path),
	}


def git_stdout(root: Path, args: list[str], fallback: str = "UNKNOWN") -> str:
	result = run(root, ["git", *args])
	if result.returncode != 0:
		return fallback
	return result.stdout.strip() or fallback


def tool_version(path: Path) -> str:
	if not path.is_file() and not os.access(path, os.X_OK):
		return "missing"
	result = subprocess.run([str(path), "--version"], text=True,
		capture_output=True, check=False)
	if result.returncode != 0:
		return "unknown"
	return result.stdout.splitlines()[0] if result.stdout else "unknown"


def tracked_paths(root: Path) -> list[str]:
	result = run(root, ["git", "ls-files"])
	if result.returncode != 0:
		return []
	return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def generated_archives(root: Path) -> list[Path]:
	out_dir = root / "OUT"
	if not out_dir.is_dir():
		return []
	patterns = ("*.iso", "*.gcm", "*.zip", "*.tar", "*.tar.gz", "*.tgz", "*.tar.xz")
	paths: list[Path] = []
	for pattern in patterns:
		paths.extend(out_dir.glob(pattern))
	return sorted({path for path in paths if path.is_file()})


def archive_entries(path: Path) -> list[str]:
	try:
		if zipfile.is_zipfile(path):
			with zipfile.ZipFile(path) as archive:
				return archive.namelist()
		if tarfile.is_tarfile(path):
			with tarfile.open(path) as archive:
				return archive.getnames()
	except (OSError, tarfile.TarError, zipfile.BadZipFile):
		return []
	return []


def suspicious_release_entries(root: Path) -> list[str]:
	needles = (
		"Half-Life/",
		"/Half-Life/",
		"valve/maps/",
		"valve/models/",
		"valve/sound/",
		"valve/sprites/",
		"devkitpro/",
		"NintendoSDK/",
	)
	suspicious: list[str] = []
	for archive in generated_archives(root):
		if archive.suffix.lower() in {".iso", ".gcm"}:
			continue
		for entry in archive_entries(archive):
			normalized = entry.replace("\\", "/")
			if any(needle in normalized for needle in needles):
				suspicious.append(f"{archive.relative_to(root)}:{normalized}")
	return suspicious


def build_manifest(root: Path) -> dict[str, object]:
	required = (
		"OUT/bin/boot.dol",
		"OUT/bin/xash",
		"OUT/libfilesystem_stdio.a",
		"OUT/libref_gx.a",
		"OUT/valve/extras.pk3",
	)
	artifacts: list[dict[str, object]] = []
	missing: list[str] = []
	for rel in required:
		path = root / rel
		if path.is_file():
			artifacts.append(file_record(root, path))
		else:
			missing.append(rel)
	for path in generated_archives(root):
		artifacts.append(file_record(root, path))

	hlsdk_archives = [
		file_record(root, path)
		for path in sorted((root / "OUT" / "hlsdk-gamecube").glob("**/*.a"))
		if path.is_file()
	]
	return {
		"required_artifacts": required,
		"artifacts": artifacts,
		"missing_required": missing,
		"hlsdk_archives": hlsdk_archives,
	}


def write_tsv(path: Path, records: list[dict[str, object]]) -> None:
	with path.open("w", encoding="utf-8") as out:
		out.write("path\tsize\tsha256\n")
		for record in records:
			out.write(f"{record['path']}\t{record['size']}\t{record['sha256']}\n")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--log-dir", type=Path)
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"reproducibility-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"
	manifest_tsv = log_dir / "artifact-manifest.tsv"

	devkitpro = Path(os.environ.get("DEVKITPRO", "/opt/devkitpro"))
	devkitppc = Path(os.environ.get("DEVKITPPC", str(devkitpro / "devkitPPC")))
	gcc = devkitppc / "bin" / "powerpc-eabi-gcc"
	elf2dol = devkitpro / "tools" / "bin" / "elf2dol"
	manifest = build_manifest(root)
	all_records = [*manifest["artifacts"], *manifest["hlsdk_archives"]]
	write_tsv(manifest_tsv, all_records)

	paths = tracked_paths(root)
	forbidden_tracked_fragments = (
		"Half-Life/valve/",
		"valve/maps/",
		"valve/models/",
		"valve/sound/",
		"valve/sprites/",
		"OUT/bin/",
		"OUT/xash3d-gc.iso",
	)
	tracked_forbidden = [
		path for path in paths
		if any(fragment in path for fragment in forbidden_tracked_fragments)
	]
	archive_suspicious = suspicious_release_entries(root)
	build_config = read(root / "build" / "config.log")
	quality_profile = {
		"build_script_low_memory_mode": "--low-memory-mode=2" in read(root / "scripts" / "build-gamecube.sh"),
		"configure_gamecube": "--gamecube" in build_config or "--gamecube" in read(root / "scripts" / "build-gamecube.sh"),
		"gc_quality_env": os.environ.get("GC_QUALITY", "default"),
		"target_frame_time": os.environ.get("TARGET_FRAME_TIME", "16.67"),
	}

	checks: list[Check] = []
	checks.append(Check(
		"required artifacts",
		"PASS" if not manifest["missing_required"] else "FAIL",
		"missing: " + (", ".join(manifest["missing_required"]) if manifest["missing_required"] else "none"),
	))
	checks.append(Check(
		"artifact hashes",
		"PASS" if all_records and all("sha256" in record and len(str(record["sha256"])) == 64 for record in all_records) else "FAIL",
		f"{len(all_records)} hashed artifact(s) written to {manifest_tsv}",
	))
	checks.append(Check(
		"build commit metadata",
		"PASS" if git_stdout(root, ["rev-parse", "HEAD"]) != "UNKNOWN" else "FAIL",
		"commit, branch, and dirty state are recorded in report.json",
	))
	checks.append(Check(
		"toolchain metadata",
		"PASS" if gcc.is_file() else "FAIL",
		f"DEVKITPPC={devkitppc} gcc={tool_version(gcc)} elf2dol={tool_version(elf2dol)}",
	))
	checks.append(Check(
		"HLSDK archive hashes",
		"PASS" if manifest["hlsdk_archives"] else "FAIL",
		f"{len(manifest['hlsdk_archives'])} HLSDK archive(s) found under OUT/hlsdk-gamecube",
	))
	checks.append(Check(
		"quality profile metadata",
		"PASS" if quality_profile["build_script_low_memory_mode"] and quality_profile["configure_gamecube"] else "FAIL",
		json.dumps(quality_profile, sort_keys=True),
	))
	checks.append(Check(
		"no tracked generated/proprietary assets",
		"PASS" if not tracked_forbidden else "FAIL",
		"tracked forbidden paths: " + (", ".join(tracked_forbidden[:12]) if tracked_forbidden else "none"),
	))
	checks.append(Check(
		"release archive asset boundary",
		"PASS" if not archive_suspicious else "FAIL",
		"archive entries: " + (", ".join(archive_suspicious[:12]) if archive_suspicious else "none"),
	))
	checks.append(Check(
		"reproducibility boundary",
		"WARN",
		"Bit-for-bit reproducibility still requires comparing this manifest against a second clean checkout/toolchain run.",
		required=False,
	))

	failed = [check for check in checks if check.required and check.status != "PASS"]
	report.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"ok": not failed,
		"git": {
			"commit": git_stdout(root, ["rev-parse", "HEAD"]),
			"branch": git_stdout(root, ["rev-parse", "--abbrev-ref", "HEAD"]),
			"dirty": bool(run(root, ["git", "status", "--porcelain"]).stdout.strip()),
			"submodules": git_stdout(root, ["submodule", "status", "--recursive"]),
		},
		"toolchain": {
			"DEVKITPRO": str(devkitpro),
			"DEVKITPPC": str(devkitppc),
			"powerpc_eabi_gcc": tool_version(gcc),
			"elf2dol": tool_version(elf2dol),
		},
		"quality_profile": quality_profile,
		"manifest_tsv": str(manifest_tsv),
		"manifest": manifest,
		"checks": [asdict(check) for check in checks],
	}, indent=2) + "\n", encoding="utf-8")

	with summary.open("w", encoding="utf-8") as out:
		out.write("# GameCube Reproducibility Check\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Report: `{report}`\n")
		out.write(f"- Artifact manifest: `{manifest_tsv}`\n")
		out.write(f"- Required failures: {len(failed)}\n\n")
		out.write("| Check | Status | Detail |\n")
		out.write("|---|---|---|\n")
		for check in checks:
			out.write(f"| {check.name} | {check.status} | {check.detail.replace('|', '\\|')} |\n")
		out.write("\n## Artifact Manifest\n\n")
		out.write("```tsv\n")
		out.write(manifest_tsv.read_text(encoding="utf-8"))
		out.write("```\n")
		out.write("\n## Evidence Boundary\n\n")
		out.write(
			"This closes automated G55 source/build reproducibility preflight for "
			"the current checkout. Final release reproducibility still requires a "
			"second clean checkout/toolchain comparison and real hardware sign-off.\n"
		)

	print(f"G55 reproducibility summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
