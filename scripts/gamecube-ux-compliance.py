#!/usr/bin/env python3
"""Generate G51 console-style UX and accessibility preflight evidence."""

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
	log_dir = args.log_dir or root / ".ai/logs" / f"ux-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	screen = read(root / "engine/client/cl_scrn.c")
	input_source = read(root / "engine/platform/gamecube/in_gamecube.c")
	save_gate = read(root / "scripts/gamecube-save-compliance.py")
	video_gate = read(root / "scripts/gamecube-video-compliance.py")
	fatal_gate = read(root / "scripts/gamecube-fatal-ux-compliance.py")
	timing_gate = read(root / "scripts/gamecube-timing-compliance.py")
	compliance = read(root / "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md")
	validation = read(root / "docs/GAMECUBE_HARDWARE_VALIDATION.md")
	plan = read(root / "docs/GAMECUBE_PORT_PLAN.md")
	goals = read(root / ".ai/goals/GAMECUBE_PORT_GOALS.md")

	checks: list[Check] = []
	checks.append(Check(
		"console UX runtime marker",
		"PASS" if contains_all(screen, (
			"G51 console UX",
			"title/options/controls/pause/save/error/credits",
			"controller-only navigation",
			"G51 accessibility",
			"no rapid full-screen flashing",
			"visual equivalents",
			"G51 readable prompts",
		)) else "FAIL",
		"screen init reports the GameCube UX/accessibility policy once per run",
	))
	checks.append(Check(
		"controller-only navigation policy",
		"PASS" if contains_all(input_source, ("A confirm", "B cancel/back", "Start pause", "Joystick: GameCube mapping")) else "FAIL",
		"input backend exposes GameCube-facing confirm/cancel/pause semantics",
	))
	checks.append(Check(
		"readable safe-area policy",
		"PASS" if contains_all(video_gate, ("8-10% 4:3 safe area", "safe-area/readability", "CRT or analog capture evidence")) else "FAIL",
		"G44 video gate protects readable title/menu/prompt placement",
	))
	checks.append(Check(
		"destructive prompt policy",
		"PASS" if contains_all(save_gate, ("requires explicit confirmation", "save <savename> confirm", "killsave <name> confirm")) else "FAIL",
		"G46 save gate protects destructive actions with clear confirmation language",
	))
	checks.append(Check(
		"error screen policy",
		"PASS" if contains_all(fatal_gate, ("readable fatal", "on-screen readable fatal payload", "bounded halt")) else "FAIL",
		"G50 fatal UX gate protects readable error/crash screens",
	))
	checks.append(Check(
		"long-operation feedback policy",
		"PASS" if contains_all(timing_gate, ("loading feedback", "about two seconds", "gc_loading_feedback_logged")) else "FAIL",
		"G49 timing gate protects loading feedback and avoids silent long waits",
	))
	checks.append(Check(
		"homebrew UX checklist",
		"PASS" if contains_all(compliance, ("UI/UX", "Accessibility", "Confirm destructive actions", "Avoid rapid full-screen flashing")) else "FAIL",
		"homebrew checklist carries console UX and accessibility requirements",
	))
	checks.append(Check(
		"hardware validation UX protocol",
		"PASS" if contains_all(validation, ("Console UX and Accessibility Preflight", "title/options/controls/pause/save/error/credits", "rapid full-screen flashing")) else "FAIL",
		"operator protocol records readability, navigation, flashing, and visual-equivalent evidence",
	))
	checks.append(Check(
		"docs and ledger sync",
		"PASS" if "G51 [x]" in goals and "G51" in plan and "console UX/accessibility" in plan else "FAIL",
		"goal ledger and port plan describe G51 source/policy preflight state",
	))
	checks.append(Check(
		"hardware accessibility boundary",
		"WARN",
		"Final accessibility acceptance still needs dated CRT/analog capture or operator evidence for menu readability, navigation, flashing, and critical cue visibility.",
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
		out.write("# GameCube UX Compliance\n\n")
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
			"This closes the automated G51 source/policy preflight. Release-complete "
			"console UX and accessibility still require dated Dolphin or hardware "
			"evidence for readable menus/prompts, controller-only navigation, no "
			"rapid full-screen flashing, and visual equivalents for critical cues.\n"
		)

	print(f"G51 UX compliance summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
