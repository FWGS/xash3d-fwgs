#!/usr/bin/env python3
"""Generate G56 hardware boot preparation checklist evidence."""

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


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--log-dir", type=Path)
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"hardware-boot-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	checklist_path = root / "docs/GAMECUBE_HARDWARE_BOOT_CHECKLIST.md"
	layout_path = root / "scripts/gamecube-hardware-layout-info.sh"
	plan_path = root / "docs/GAMECUBE_PORT_PLAN.md"
	goals_path = root / ".ai/goals/GAMECUBE_PORT_GOALS.md"
	rc_path = root / "scripts/gamecube-rc-check.sh"
	loop_path = root / "scripts/ai-goal-loop.py"

	checklist = read(checklist_path)
	layout = read(layout_path)
	plan = read(plan_path)
	goals = read(goals_path)
	rc = read(rc_path)
	loop = read(loop_path)

	layout_results = {
		route: run(root, ["bash", str(layout_path.relative_to(root)), "--route", route])
		for route in ("all", "sd", "disc", "memcard")
	}

	checks: list[Check] = []
	checks.append(Check(
		"checklist exists",
		"PASS" if checklist_path.is_file() and checklist.strip() else "FAIL",
		str(checklist_path),
	))
	checks.append(Check(
		"operator fields",
		"PASS" if contains_all(checklist, (
			"Pre-Flight", "Artifact", "commit hash", "video", "controller",
			"Expected First-Screen Evidence",
		)) else "FAIL",
		"checklist covers artifact, loader, video, controller, and first-screen fields",
	))
	checks.append(Check(
		"route coverage",
		"PASS" if contains_all(checklist, (
			"SD Gecko", "SD2SP2", "Disc Image", "Memory Card", "Wii in GameCube mode",
		)) else "FAIL",
		"checklist covers SD, disc, memory-card-assisted, and Wii GC-mode routes",
	))
	checks.append(Check(
		"failure triage",
		"PASS" if contains_all(checklist, (
			"Black screen", "No input", "No audio", "Missing assets",
			"Read-only storage", "Memory exhaustion",
		)) else "FAIL",
		"checklist includes required failure triage cases",
	))
	checks.append(Check(
		"layout script executable",
		"PASS" if layout_path.is_file() and layout_path.stat().st_mode & 0o111 else "FAIL",
		str(layout_path),
	))
	checks.append(Check(
		"layout script route parser",
		"PASS" if contains_all(layout, ("--route", "sd|disc|memcard|all", "unknown route")) else "FAIL",
		"script supports --route all/sd/disc/memcard and rejects unknown routes",
	))
	for route, result in layout_results.items():
		checks.append(Check(
			f"layout output {route}",
			"PASS" if result.returncode == 0 and result.stdout.strip() else "FAIL",
			f"exit={result.returncode}",
		))
	checks.append(Check(
		"docs and ledger sync",
		"PASS" if "G56 [x]" in goals and "gamecube-hardware-boot-check.py" in plan else "FAIL",
		"goal ledger and port plan cite G56 verifier-backed completion",
	))
	checks.append(Check(
		"RC G56 gate",
		"PASS" if contains_all(rc, ("hardware_boot_gate", "gamecube-hardware-boot-check.py")) else "FAIL",
		"RC suite runs the G56 hardware boot checklist verifier",
	))
	checks.append(Check(
		"goal runner G56 context",
		"PASS" if contains_all(loop, ("gamecube-hardware-boot-check.py", "gamecube-hardware-layout-info.sh")) else "FAIL",
		"goal runner exposes the G56 verifier/checklist files if revisited",
	))
	checks.append(Check(
		"hardware evidence boundary",
		"WARN",
		"G56 prepares hardware boot evidence; real hardware pass/fail remains G38/G66.",
		required=False,
	))

	failed = [check for check in checks if check.required and check.status != "PASS"]
	report.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"ok": not failed,
		"layout_outputs": {
			route: {
				"exit_code": result.returncode,
				"stdout": result.stdout,
				"stderr": result.stderr,
			}
			for route, result in layout_results.items()
		},
		"checks": [asdict(check) for check in checks],
	}, indent=2) + "\n", encoding="utf-8")

	with summary.open("w", encoding="utf-8") as out:
		out.write("# GameCube Hardware Boot Checklist Compliance\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Report: `{report}`\n")
		out.write(f"- Required failures: {len(failed)}\n\n")
		out.write("| Check | Status | Detail |\n")
		out.write("|---|---|---|\n")
		for check in checks:
			out.write(f"| {check.name} | {check.status} | {check.detail.replace('|', '\\|')} |\n")
		out.write("\n## Evidence Boundary\n\n")
		out.write(
			"This closes G56 automated checklist/source preflight only. Dated real "
			"hardware boot results still belong to G38/G66.\n"
		)

	print(f"G56 hardware boot summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
