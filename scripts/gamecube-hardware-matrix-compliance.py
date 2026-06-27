#!/usr/bin/env python3
"""Generate G53 hardware and loader evidence matrix preflight evidence."""

from __future__ import annotations

import argparse
import json
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


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--log-dir", type=Path)
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"hardware-matrix-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	matrix = read(root / "docs/GAMECUBE_HARDWARE_MATRIX.md")
	validation = read(root / "docs/GAMECUBE_HARDWARE_VALIDATION.md")
	plan = read(root / "docs/GAMECUBE_PORT_PLAN.md")
	goals = read(root / ".ai/goals/GAMECUBE_PORT_GOALS.md")
	rc = read(root / "scripts/gamecube-rc-check.sh")
	loop = read(root / "scripts/ai-goal-loop.py")

	checks: list[Check] = []
	checks.append(Check(
		"support routes",
		"PASS" if contains_all(matrix, ("GC-DOL-SD", "GC-ISO-RO", "WII-GC-DOL-SD", "DOLPHIN-DOL-SD", "DOLPHIN-ISO-RO")) else "FAIL",
		"matrix names required, recommended, and diagnostic boot routes",
	))
	checks.append(Check(
		"evidence matrix fields",
		"PASS" if contains_all(matrix, ("Artifact commit", "Loader", "Storage route", "Video mode", "Controller", "Boot result", "Map result", "Audio result", "Save result", "Next blocker")) else "FAIL",
		"matrix defines the fields required for each evidence entry",
	))
	checks.append(Check(
		"hardware variants",
		"PASS" if contains_all(matrix, ("SD2SP2", "SD Gecko", "Wii GameCube mode", "Slot A", "Slot B", "WaveBird", "third-party", "no-controller", "mid-game disconnect")) else "FAIL",
		"matrix tracks storage, memory-card, controller, and disconnect variants",
	))
	checks.append(Check(
		"Dolphin boundary",
		"PASS" if contains_all(matrix, ("Dolphin", "Diagnostic", "not accepted as final hardware proof", "Dolphin evidence is diagnostic only")) else "FAIL",
		"matrix keeps emulator evidence separate from real hardware release claims",
	))
	checks.append(Check(
		"proprietary capture boundary",
		"PASS" if contains_all(matrix, ("Proprietary local asset captures", "outside Git", "textual log paths")) else "FAIL",
		"matrix keeps captures containing local game assets out of Git",
	))
	checks.append(Check(
		"hardware validation link",
		"PASS" if contains_all(validation, ("GAMECUBE_HARDWARE_MATRIX.md", "Record For Each Run", "Controller result", "Storage result")) else "FAIL",
		"hardware validation protocol points operators at the matrix and required run fields",
	))
	checks.append(Check(
		"RC hardware matrix gate",
		"PASS" if contains_all(rc, ("hardware_matrix_compliance_gate", "gamecube-hardware-matrix-compliance.py", "G53 hardware/loader evidence matrix preflight passed")) else "FAIL",
		"RC suite runs the G53 hardware matrix verifier",
	))
	checks.append(Check(
		"GUI editable context",
		"PASS" if contains_all(loop, ("docs/GAMECUBE_HARDWARE_MATRIX.md", "gamecube-hardware-matrix-compliance.py")) else "FAIL",
		"goal runner lets the Aider GUI edit and verify the G53 matrix instead of rejecting it",
	))
	checks.append(Check(
		"docs and ledger sync",
		"PASS" if "G53 [x]" in goals and "G53" in plan and "gamecube-hardware-matrix-compliance.py" in plan else "FAIL",
		"goal ledger and port plan describe verifier-backed G53 completion",
	))
	checks.append(Check(
		"hardware evidence boundary",
		"WARN",
		"Real GameCube, Swiss, Wii GameCube-mode, memory-card, WaveBird, third-party, and disconnect evidence remain manual G38/G66 work.",
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
		out.write("# GameCube Hardware Matrix Compliance\n\n")
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
			"This closes the automated G53 source/policy preflight. Real hardware "
			"release evidence still requires dated operator runs for GameCube, Swiss, "
			"Wii GameCube mode, storage, memory-card, controller, audio, save, and "
			"disconnect routes.\n"
		)

	print(f"G53 hardware matrix compliance summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
