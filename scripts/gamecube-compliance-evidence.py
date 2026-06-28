#!/usr/bin/env python3
"""Generate G54 scripted compliance evidence preflight output."""

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
	log_dir = args.log_dir or root / ".ai/logs" / f"compliance-evidence-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	goal_text = read(root / ".ai/goals/GAMECUBE_PORT_GOALS.md")
	plan = read(root / "docs/GAMECUBE_PORT_PLAN.md")
	rc = read(root / "scripts/gamecube-rc-check.sh")
	probe = read(root / "scripts/dolphin-boot-probe.sh")
	analyze = read(root / "scripts/dolphin-probe-analyze.py")
	mem = read(root / "engine/platform/gamecube/mem_gamecube.c")
	vid = read(root / "engine/platform/gamecube/vid_gamecube.c")
	sys_gc = read(root / "engine/platform/gamecube/sys_gamecube.c")
	system = read(root / "engine/common/system.c")
	server_init = read(root / "engine/server/sv_init.c")
	input_gc = read(root / "engine/platform/gamecube/in_gamecube.c")
	audio = read(root / "engine/platform/gamecube/snddma_gamecube.c")
	client_screen = read(root / "engine/client/cl_scrn.c")

	checks: list[Check] = []
	checks.append(Check(
		"frame timing evidence",
		"PASS" if "FRAME_BUDGET_STATS" in analyze and "target=" in analyze and "frame time=" in vid else "FAIL",
		"Dolphin probe/parser and video backend expose frame-budget telemetry",
	))
	checks.append(Check(
		"memory evidence",
		"PASS" if contains_all(mem, ("mem stage=%s", "hwm=%s", "map=%s")) and "MEM1-only" in plan else "FAIL",
		"GameCube memory sampler reports MEM1 stage/high-water/map and documents the ARAM boundary",
	))
	checks.append(Check(
		"map and route evidence",
		"PASS" if contains_all(server_init + probe, ("map loaded", "MAP_READY")) else "FAIL",
		"server and Dolphin probe expose current map readiness",
	))
	checks.append(Check(
		"storage and loader evidence",
		"PASS" if contains_all(sys_gc, ("read-only fallback", "writable storage", "disc-only mode")) else "FAIL",
		"GameCube platform startup reports storage route and read-only fallback",
	))
	checks.append(Check(
		"build and crash breadcrumbs",
		"PASS" if contains_all(system, ("fatal breadcrumb begin", "build=%s", "gcmap=%s", "route=%s")) else "FAIL",
		"fatal breadcrumb reports build, map, storage route, and subsystem",
	))
	checks.append(Check(
		"controller evidence",
		"PASS" if contains_all(input_gc, ("G45 controller ready", "G45 controller waiting", "G45 controller disconnected")) else "FAIL",
		"controller path reports ready/no-controller/disconnect states",
	))
	checks.append(Check(
		"audio evidence",
		"PASS" if contains_all(audio, ("audio submitted nonzero PCM", "peak=", "audio voice started")) else "FAIL",
		"audio backend reports voice startup and nonzero PCM output",
	))
	checks.append(Check(
		"player and entity visibility path",
		"PASS" if contains_all(client_screen, ("cl_showpos", "cl_showents")) else "FAIL",
		"existing debug cvars expose player position and active entities for operator/test routes",
	))
	checks.append(Check(
		"route coverage chain",
		"PASS" if contains_all(rc, (
			"controller_compliance_gate", "save_compliance_gate",
			"audio_compliance_gate", "timing_compliance_gate",
			"fatal_ux_compliance_gate", "video_compliance_gate",
		)) else "FAIL",
		"RC gate covers controller, save, audio, timing, fatal UX, and video cases",
	))
	checks.append(Check(
		"RC G54 gate",
		"PASS" if contains_all(rc, ("compliance_evidence_gate", "gamecube-compliance-evidence.py")) else "FAIL",
		"RC suite runs this G54 scripted compliance evidence verifier",
	))
	checks.append(Check(
		"docs and ledger sync",
		"PASS" if "G54 [x]" in goal_text and "gamecube-compliance-evidence.py" in plan else "FAIL",
		"goal ledger and port plan should name this verifier before G54 is considered complete",
	))
	checks.append(Check(
		"hardware evidence boundary",
		"WARN",
		"G54 scripted evidence is local/Dolphin/source preflight. Real hardware release sign-off remains G38/G66.",
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
		out.write("# GameCube Compliance Evidence\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Report: `{report}`\n")
		out.write(f"- Required failures: {len(failed)}\n\n")
		out.write("| Check | Status | Detail |\n")
		out.write("|---|---|---|\n")
		for check in checks:
			out.write(f"| {check.name} | {check.status} | {check.detail.replace('|', '\\|')} |\n")
		out.write("\n## Evidence Boundary\n\n")
		out.write(
			"This closes the automated G54 source/policy preflight for the scripted "
			"compliance evidence route. It proves the source and RC tooling expose "
			"the required evidence channels; it does not replace sustained Dolphin "
			"route logs or real hardware release sign-off.\n"
		)

	print(f"G54 compliance evidence summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
