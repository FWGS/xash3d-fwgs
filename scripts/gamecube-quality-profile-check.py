#!/usr/bin/env python3
"""Verify the GameCube quality profile contract for G61."""

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


def contains(text: str, *needles: str) -> bool:
	return all(needle in text for needle in needles)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--log-dir", type=Path)
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"quality-profile-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	report_path = log_dir / "report.json"
	summary_path = log_dir / "summary.md"

	mod_studio = read(root / "engine/common/mod_studio.c")
	ref_common = read(root / "engine/client/dll_int/ref_common.c")
	vid_gamecube = read(root / "engine/platform/gamecube/vid_gamecube.c")
	r_local = read(root / "ref/gx/r_local.h")
	platform = read(root / "engine/platform/platform.h")
	goals = read(root / ".ai/goals/GAMECUBE_PORT_GOALS.md")
	plan = read(root / "docs/GAMECUBE_PORT_PLAN.md")

	checks = [
		Check(
			"engine registers gc_quality",
			"PASS" if contains(ref_common, 'Cvar_Get( "gc_quality"', "0=smoke, 1=release, 2=high telemetry-only") else "FAIL",
			"`gc_quality` exists before the renderer loads and has named profile semantics",
		),
		Check(
			"engine shared quality helper",
			"PASS" if contains(mod_studio, "int GC_GetVisualQuality( void )", 'Cvar_VariableInteger( "gc_quality" )', "bound( 0, quality, 2 )") else "FAIL",
			"engine-side model/HUD decisions read the profile cvar instead of a hard-coded smoke mode",
		),
		Check(
			"profile names",
			"PASS" if contains(mod_studio, 'return "smoke"', 'return "release"', 'return "high"') else "FAIL",
			"quality profiles have stable names for logs and evidence",
		),
		Check(
			"profile evidence line",
			"PASS" if contains(mod_studio, "GC_ReportQualityProfile", "quality profile stage=", "purpose=") else "FAIL",
			"runtime emits structured profile evidence",
		),
		Check(
			"video init reports profile",
			"PASS" if contains(vid_gamecube, 'GC_ReportQualityProfile( "video-init" )') else "FAIL",
			"GameCube video initialization records the active profile",
		),
		Check(
			"renderer reads same profile",
			"PASS" if contains(r_local, 'pfnGetCvarFloat( "gc_quality" )', "if( quality > 2 )", "#if XASH_LOW_MEMORY") else "FAIL",
			"GX renderer quality decisions use the shared `gc_quality` value with low-memory clamping",
		),
		Check(
			"public declarations",
			"PASS" if contains(platform, "int GC_GetVisualQuality( void );", "GC_GetQualityProfileName", "GC_ReportQualityProfile") else "FAIL",
			"platform header exposes the shared profile helpers",
		),
		Check(
			"goal ledger complete",
			"PASS" if "## G61 [x] Define final GameCube quality profiles" in goals else "FAIL",
			"G61 is marked complete only after source evidence exists",
		),
		Check(
			"release plan evidence",
			"PASS" if contains(plan, "G61", "quality profile", "smoke", "release", "high") else "FAIL",
			"port plan records the profile contract and evidence",
		),
	]

	failed = [check for check in checks if check.required and check.status != "PASS"]
	report_path.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"ok": not failed,
		"checks": [asdict(check) for check in checks],
	}, indent=2) + "\n", encoding="utf-8")

	with summary_path.open("w", encoding="utf-8") as out:
		out.write("# GameCube Quality Profile Check\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Report: `{report_path}`\n")
		out.write(f"- Required failures: {len(failed)}\n\n")
		out.write("| Check | Status | Detail |\n")
		out.write("|---|---|---|\n")
		for check in checks:
			out.write(f"| {check.name} | {check.status} | {check.detail} |\n")

	print(summary_path)
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
