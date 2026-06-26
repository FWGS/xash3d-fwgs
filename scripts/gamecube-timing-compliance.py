#!/usr/bin/env python3
"""Generate G49 frame timing, loading feedback, and timing-independence preflight evidence."""

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
	log_dir = args.log_dir or root / ".ai/logs" / f"timing-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	host = read(root / "engine/common/host.c")
	view = read(root / "engine/client/cl_view.c")
	video = read(root / "engine/platform/gamecube/vid_gamecube.c")
	probe = read(root / "scripts/dolphin-probe-analyze.py")
	plan = read(root / "docs/GAMECUBE_PORT_PLAN.md")
	validation = read(root / "docs/GAMECUBE_HARDWARE_VALIDATION.md")
	goals = read(root / ".ai/goals/GAMECUBE_PORT_GOALS.md")

	checks: list[Check] = []
	checks.append(Check(
		"release frame budget",
		"PASS" if contains_all(host, ("G49 release target", "frame budget", "bound( MIN_FPS", "60.0")) else "FAIL",
		"logs a capped NTSC release target and frame-time budget at startup",
	))
	checks.append(Check(
		"timing independence",
		"PASS" if contains_all(host, ("host.frametime = host.realtime - oldtime", "bound( MIN_FRAMETIME", "bound( MIN_FRAMETIME, host.frametime, MAX_FRAMETIME )")) else "FAIL",
		"derives gameplay timing from bounded real frametime instead of a fixed tick",
	))
	checks.append(Check(
		"loading feedback threshold",
		"PASS" if contains_all(view, ("G49 loading feedback", "load_elapsed < 2.0", "gc_loading_feedback_logged")) else "FAIL",
		"allows loading plaque rendering and logs feedback after about two seconds",
	))
	checks.append(Check(
		"worst-case frame telemetry",
		"PASS" if contains_all(video, ("frame time=", "G49 slow frame", "gc_worst_frame_ms", "elapsed_ms >= 33.0")) else "FAIL",
		"reports slow-frame and worst-case present timing for Dolphin probes",
	))
	checks.append(Check(
		"probe frame parser",
		"PASS" if contains_all(probe, ("FRAME_TIME_RE", "frame time=", "FRAME_BUDGET_STATS")) else "FAIL",
		"keeps automated Dolphin log analysis for frame-budget evidence",
	))
	checks.append(Check(
		"docs and ledger sync",
		"PASS" if "G49 [x]" in goals and "G49" in plan and "Frame Timing Preflight" in validation else "FAIL",
		"goal ledger, port plan, and hardware protocol describe the G49 state",
	))
	checks.append(Check(
		"worst-case scene evidence boundary",
		"WARN",
		"Representative gameplay/menu/worst-case FPS, frame time, map, position, and entity evidence still need dated Dolphin or hardware/operator runs.",
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
		out.write("# GameCube Timing Compliance\n\n")
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
			"This closes the automated G49 source/policy preflight. Release-complete "
			"timing still requires dated Dolphin or hardware evidence for representative "
			"gameplay, menu, loading, and worst-case scenes with FPS, frame time, map, "
			"player position, and active entities.\n"
		)

	print(summary)
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
