#!/usr/bin/env python3
"""Verify the local Qwable/Aider automation guidance is wired into the RC flow."""

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
	log_dir = args.log_dir or root / ".ai/logs" / f"automation-guidance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	mission_path = root / ".ai/prompts/GAMECUBE_LOCAL_MISSION.md"
	context_index = read(root / ".ai/prompts/GAMECUBE_CONTEXT_INDEX.md")
	aider = read(root / ".aider.conf.yml")
	automation = read(root / ".aider.automation.conf.yml")
	loop = read(root / "scripts/ai-goal-loop.py")
	rc = read(root / "scripts/gamecube-rc-check.sh")

	checks: list[Check] = []
	checks.append(Check(
		"mission prompt exists",
		"PASS" if mission_path.is_file() and contains_all(read(mission_path), (
			"Mission:", "devkitPPC/libogc", "scripts/gamecube-rc-check.sh",
			"Do not mark a goal complete from reasoning or docs-only claims",
		)) else "FAIL",
		str(mission_path),
	))
	checks.append(Check(
		"context index links mission",
		"PASS" if "GAMECUBE_LOCAL_MISSION.md" in context_index else "FAIL",
		"context index must name the mission prompt as the top-level operating contract",
	))
	checks.append(Check(
		"interactive Aider reads mission",
		"PASS" if "GAMECUBE_LOCAL_MISSION.md" in aider else "FAIL",
		".aider.conf.yml should load the mission prompt for supervised GUI use",
	))
	checks.append(Check(
		"automation Aider reads mission",
		"PASS" if "GAMECUBE_LOCAL_MISSION.md" in automation else "FAIL",
		".aider.automation.conf.yml should load the mission prompt for bounded local passes",
	))
	checks.append(Check(
		"goal runner reads mission",
		"PASS" if "GAMECUBE_LOCAL_MISSION.md" in loop else "FAIL",
		"scripts/ai-goal-loop.py should include the mission prompt in read-only context",
	))
	checks.append(Check(
		"G54 context carries verifier",
		"PASS" if contains_all(loop, ("G54", "gamecube-compliance-evidence.py", "scripts/gamecube-rc-check.sh")) else "FAIL",
		"G54 editable context should include its verifier and RC wiring",
	))
	checks.append(Check(
		"G54 prompt blocks docs-only completion",
		"PASS" if contains_all(loop, ("Advance G54", "Do not mark G54 complete", "scripted-equivalent")) else "FAIL",
		"goal runner should steer G54 toward source/verifier evidence, not prose-only closure",
	))
	checks.append(Check(
		"RC guidance gate",
		"PASS" if contains_all(rc, ("automation_guidance_gate", "gamecube-automation-guidance-check.py")) else "FAIL",
		"RC suite should fail if local-model guidance becomes unwired",
	))

	failed = [check for check in checks if check.required and check.status != "PASS"]
	report.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"ok": not failed,
		"checks": [asdict(check) for check in checks],
	}, indent=2) + "\n", encoding="utf-8")

	with summary.open("w", encoding="utf-8") as out:
		out.write("# GameCube Automation Guidance Check\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Report: `{report}`\n")
		out.write(f"- Required failures: {len(failed)}\n\n")
		out.write("| Check | Status | Detail |\n")
		out.write("|---|---|---|\n")
		for check in checks:
			out.write(f"| {check.name} | {check.status} | {check.detail.replace('|', '\\|')} |\n")
		out.write("\n## Evidence Boundary\n\n")
		out.write(
			"This gate verifies that the local Qwable/Aider workflow has mission, "
			"context, and G54 verifier guidance wired. It does not prove runtime "
			"behavior by itself; runtime and hardware goals still require their "
			"own logs or operator evidence.\n"
		)

	print(f"automation guidance summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
