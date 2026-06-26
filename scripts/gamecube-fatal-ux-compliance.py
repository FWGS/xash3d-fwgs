#!/usr/bin/env python3
"""Generate G50 fatal-error UX and crash-breadcrumb evidence."""

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
	log_dir = args.log_dir or root / ".ai/logs" / f"fatal-ux-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	system_source = read(root / "engine/common/system.c")
	video_source = read(root / "engine/platform/gamecube/vid_gamecube.c")
	probe = read(root / "scripts/dolphin-boot-probe.sh")
	plan = read(root / "docs/GAMECUBE_PORT_PLAN.md")
	validation = read(root / "docs/GAMECUBE_HARDWARE_VALIDATION.md")
	goals = read(root / ".ai/goals/GAMECUBE_PORT_GOALS.md")

	checks: list[Check] = []
	checks.append(Check(
		"subsystem classification",
		"PASS" if contains_all(system_source, ("Sys_GameCubeFatalSubsystem", "filesystem", "allocation", "renderer", "audio", "game-code", "storage", "missing-asset")) else "FAIL",
		"classifies fatal messages into release-facing subsystem buckets",
	))
	checks.append(Check(
		"bounded OSReport breadcrumb",
		"PASS" if contains_all(system_source, ("fatal breadcrumb begin", "fatal breadcrumb end", "subsystem=%s", "build=%s", "mem=%s")) else "FAIL",
		"emits compact OSReport breadcrumbs with build, subsystem, route, map, frame, and memory",
	))
	checks.append(Check(
		"on-screen readable fatal payload",
		"PASS" if contains_all(video_source, ("GC_FatalGlyphRow", "GC_FatalDrawWrapped", "XASH3D GAMECUBE FATAL", "HALTED: POWER CYCLE OR RESET")) else "FAIL",
		"draws asset-free text to the framebuffer instead of a silent black/magenta screen",
	))
	checks.append(Check(
		"asset-independent rendering",
		"PASS" if contains_all(video_source, ("static const unsigned char digit", "static const unsigned char alpha", "GC_FatalPutPixel", "GC_DrawFatalBreadcrumb( const char *message, const char *details )")) else "FAIL",
		"uses built-in glyphs and direct XFB pixels so missing assets/filesystem failures remain visible",
	))
	checks.append(Check(
		"bounded halt visibility",
		"PASS" if contains_all(video_source, ("VIDEO_SetNextFramebuffer", "VIDEO_Flush", "VIDEO_WaitVSync", "for( i = 0; i < 3; i++ )")) else "FAIL",
		"flushes the fatal frame and waits a bounded number of VSyncs",
	))
	checks.append(Check(
		"intentional fatal probe route",
		"PASS" if contains_all(probe, ("GC_FATAL_TEST", "G37_VERIFIED", "Intentional fatal error triggered")) else "FAIL",
		"Dolphin probe retains an intentional fatal route for regression testing",
	))
	checks.append(Check(
		"docs and ledger sync",
		"PASS" if "G50 [x]" in goals and "G50" in plan and "Fatal Error UX Preflight" in validation else "FAIL",
		"goal ledger, port plan, and hardware protocol describe the G50 state",
	))
	checks.append(Check(
		"hardware capture boundary",
		"WARN",
		"Readable fatal screens still need dated analog-capture or real-hardware evidence for release-complete claims.",
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
		out.write("# GameCube Fatal UX Compliance\n\n")
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
			"This closes the automated G50 source/policy preflight. Release-complete "
			"fatal UX still requires dated hardware or analog-capture evidence showing "
			"the fatal text is readable and that the route ends in a bounded halt, "
			"return path, or restart prompt rather than a silent black screen.\n"
		)

	print(summary)
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
