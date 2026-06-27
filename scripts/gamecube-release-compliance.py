#!/usr/bin/env python3
"""Generate G52 release manifest and legal audit preflight evidence."""

from __future__ import annotations

import argparse
import json
import subprocess
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


@dataclass
class Check:
	name: str
	status: str
	detail: str
	required: bool = True


def read(path: Path) -> str:
	return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def contains_all(text: str, needles: tuple[str, ...]) -> bool:
	return all(needle in text for needle in needles)


def run(root: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
	return subprocess.run(args, cwd=root, text=True, capture_output=True, check=False)


def git_grep(root: Path, pattern: str) -> bool:
	result = run(root, ["git", "grep", "-n", "-E", pattern, "--", "."])
	return result.returncode == 0


def tracked_paths(root: Path) -> list[str]:
	result = run(root, ["git", "ls-files"])
	if result.returncode != 0:
		return []
	return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--log-dir", type=Path)
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"release-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	manifest_path = root / "docs/GAMECUBE_RELEASE_MANIFEST.md"
	plan_path = root / "docs/GAMECUBE_PORT_PLAN.md"
	goals_path = root / ".ai/goals/GAMECUBE_PORT_GOALS.md"
	rc_path = root / "scripts/gamecube-rc-check.sh"
	compliance_path = root / "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"
	handoff_path = root / "scripts/gamecube-hardware-handoff.sh"

	manifest = read(manifest_path)
	plan = read(plan_path)
	goals = read(goals_path)
	rc = read(rc_path)
	compliance = read(compliance_path)
	handoff = read(handoff_path)
	paths = tracked_paths(root)

	forbidden_path_fragments = (
		"Half-Life/valve/",
		"valve/maps/",
		"valve/models/",
		"valve/sound/",
		"valve/sprites/",
		"OUT/xash3d-gc.iso",
	)
	tracked_forbidden = [path for path in paths if any(fragment in path for fragment in forbidden_path_fragments)]

	checks: list[Check] = []
	checks.append(Check(
		"release manifest exists",
		"PASS" if manifest_path.is_file() and manifest.strip() else "FAIL",
		str(manifest_path),
	))
	checks.append(Check(
		"package contents",
		"PASS" if contains_all(manifest, ("boot.dol", "README.md", "LICENSE", "THIRD-PARTY-NOTICES", "CHANGES.md")) else "FAIL",
		"manifest lists the expected release package files",
	))
	checks.append(Check(
		"version and hashes",
		"PASS" if contains_all(manifest, ("Build Hash", "Artifact Hashes", "OUT/release-hashes.txt")) else "FAIL",
		"manifest requires per-release build hash and artifact hashes",
	))
	checks.append(Check(
		"legal exclusions",
		"PASS" if contains_all(manifest, ("Nintendo SDK", "BIOS", "IPL", "Copyrighted game assets", "redistribute copyrighted game content")) else "FAIL",
		"manifest excludes proprietary SDK/firmware/game content and requires user-owned assets",
	))
	checks.append(Check(
		"local asset staging",
		"PASS" if contains_all(manifest, ("Local Asset Staging Instructions", "legal copy of Half-Life", "xash3d/valve", "filenames are lowercase")) else "FAIL",
		"manifest documents user-side legal Half-Life asset staging",
	))
	checks.append(Check(
		"controls and troubleshooting",
		"PASS" if contains_all(manifest, ("Controls", "Troubleshooting", "A Button", "B Button", "Black Screen", "Missing Textures")) else "FAIL",
		"manifest carries controls and basic support notes",
	))
	checks.append(Check(
		"homebrew legal checklist",
		"PASS" if contains_all(compliance, ("Legal", "No Nintendo SDK/proprietary files included", "No ripped commercial assets included")) else "FAIL",
		"homebrew checklist includes legal/package release checks",
	))
	checks.append(Check(
		"hardware handoff packaging warning",
		"PASS" if contains_all(handoff, ("does not copy or package proprietary Half-Life assets", "Do not copy Nintendo SDK files")) else "FAIL",
		"hardware handoff keeps proprietary assets and platform SDK material out of release artifacts",
	))
	checks.append(Check(
		"no obvious proprietary SDK strings",
		"PASS" if not git_grep(root, r"(Dolphin [S]DK|Revolution [S]DK|official Nintendo [S]DK)") else "FAIL",
		"tracked files avoid obvious proprietary SDK references",
	))
	checks.append(Check(
		"no tracked local game/release assets",
		"PASS" if not tracked_forbidden else "FAIL",
		"tracked forbidden paths: " + (", ".join(tracked_forbidden[:10]) if tracked_forbidden else "none"),
	))
	checks.append(Check(
		"RC release gate",
		"PASS" if contains_all(rc, ("release_compliance_gate", "gamecube-release-compliance.py", "G52 release manifest/legal audit preflight passed")) else "FAIL",
		"RC suite runs the G52 release compliance verifier",
	))
	checks.append(Check(
		"docs and ledger sync",
		"PASS" if "G52 [x]" in goals and "G52" in plan and "gamecube-release-compliance.py" in plan else "FAIL",
		"goal ledger and port plan describe G52 verifier-backed completion",
	))
	checks.append(Check(
		"release packaging boundary",
		"WARN",
		"Final archives still need a dated release build with recorded hashes and human review of bundled notices before public distribution.",
		required=False,
	))

	failed = [check for check in checks if check.required and check.status != "PASS"]
	report.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"ok": not failed,
		"checks": [asdict(check) for check in checks],
	}, indent=2) + "\n", encoding="utf-8")

	with summary.open("w", encoding="utf-8") as out:
		out.write("# GameCube Release Compliance\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Report: `{report}`\n")
		out.write(f"- Required failures: {len(failed)}\n\n")
		out.write("| Check | Status | Detail |\n")
		out.write("|---|---|---|\n")
		for check in checks:
			detail = check.detail.replace("|", "\\|")
			out.write(f"| {check.name} | {check.status} | {detail} |\n")
		out.write("\n## Evidence Boundary\n\n")
		out.write(
			"This closes the automated G52 source/policy preflight. Public release "
			"still requires a dated archive build, artifact hashes, third-party "
			"notice review, and confirmation that no copyrighted game assets, "
			"firmware dumps, or proprietary platform SDK material are bundled.\n"
		)

	print(f"G52 release compliance summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
