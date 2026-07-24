#!/usr/bin/env python3
"""Generate G44 video mode, safe-area, and readability compliance evidence."""

from __future__ import annotations

import argparse
import json
import re
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


SAFE_PERCENT = 10
MIN_READABLE_WIDTH = 320
MIN_READABLE_HEIGHT = 240


@dataclass
class Check:
	name: str
	status: str
	detail: str
	required: bool = True


def read(path: Path) -> str:
	return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def safe_rect(width: int, height: int) -> tuple[int, int, int, int]:
	margin_x = width * SAFE_PERCENT // 100
	margin_y = height * SAFE_PERCENT // 100
	return margin_x, margin_y, width - margin_x * 2, height - margin_y * 2


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--log-dir", type=Path)
	args = parser.parse_args()

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"video-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	vid_path = root / "engine/platform/gamecube/vid_gamecube.c"
	sys_path = root / "engine/platform/gamecube/sys_gamecube.c"
	matrix_path = root / "docs/GAMECUBE_HARDWARE_MATRIX.md"
	compliance_path = root / "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"
	validation_path = root / "docs/GAMECUBE_HARDWARE_VALIDATION.md"

	vid = read(vid_path)
	sys_text = read(sys_path)
	matrix = read(matrix_path)
	compliance = read(compliance_path)
	validation = read(validation_path)
	checks: list[Check] = []

	checks.append(Check(
		"preferred libogc mode",
		"PASS" if "VIDEO_GetPreferredMode( NULL )" in vid else "FAIL",
		"renderer asks libogc for the console/cable preferred mode",
	))
	checks.append(Check(
		"480p not forced",
		"PASS" if "policy=preferred-4:3-480i" in vid and not re.search(r"VI_.*PROGRESSIVE|TVNtsc480Prog|TVPal528Prog", vid) else "FAIL",
		"source records preferred 4:3/480i policy and does not select progressive-only modes",
	))
	checks.append(Check(
		"safe area marker",
		"PASS" if "GC_VIDEO_SAFE_AREA_PERCENT 10" in vid and "video safe_area" in vid else "FAIL",
		"runtime emits a 10 percent 4:3 title-safe rectangle",
	))
	checks.append(Check(
		"readable minimum marker",
		"PASS" if "GC_VIDEO_MIN_READABLE_WIDTH 320" in vid and "GC_VIDEO_MIN_READABLE_HEIGHT 240" in vid else "FAIL",
		"runtime records the minimum 320x240 readability target",
	))
	checks.append(Check(
		"default software resolution",
		"PASS" if '"320"' in sys_text and '"240"' in sys_text else "FAIL",
		"GameCube argv keeps tip-safe 320x240 soft FB; Flipper EFB/XFB stay native 640x480",
	))
	checks.append(Check(
		"Flipper native EFB policy",
		"PASS" if "efb_native=1" in vid or "Flipper EFB native" in vid or "hardware soft FB clamp" in vid else "FAIL",
		"source documents Flipper native EFB with soft FB clamp for MEM1 tip safety",
	))
	checks.append(Check(
		"hardware matrix video policy",
		"PASS" if "480i-compatible" in matrix and "CRT-safe" in matrix and "480p-only" in matrix else "FAIL",
		"hardware matrix forbids 480p-only or widescreen-only release routes",
	))
	checks.append(Check(
		"homebrew checklist video policy",
		"PASS" if "8-10% 4:3 safe area" in compliance and "480p works or is safely disabled" in compliance else "FAIL",
		"homebrew checklist carries safe-area/readability/480p requirements",
	))
	checks.append(Check(
		"hardware capture boundary",
		"WARN",
		"CRT or analog capture evidence is still required before hardware-complete release claims",
		required=False,
	))

	rects = {
		"320x240": safe_rect(320, 240),
		"640x480": safe_rect(640, 480),
	}
	if "Boot Media Failure Preflight" not in validation:
		checks.append(Check(
			"hardware validation protocol",
			"WARN",
			"hardware validation docs exist but need a G44 capture section",
			required=False,
		))

	failed = [check for check in checks if check.required and check.status != "PASS"]
	report.write_text(json.dumps({
		"generated": datetime.now(timezone.utc).isoformat(),
		"repo": str(root),
		"ok": not failed,
		"safe_percent": SAFE_PERCENT,
		"safe_rects": {key: list(value) for key, value in rects.items()},
		"checks": [asdict(check) for check in checks],
	}, indent=2) + "\n", encoding="utf-8")

	with summary.open("w", encoding="utf-8") as out:
		out.write("# GameCube Video Compliance\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Report: `{report}`\n")
		out.write(f"- Required failures: {len(failed)}\n\n")
		out.write("## Safe Area\n\n")
		out.write("| Mode | X | Y | Width | Height |\n")
		out.write("|---|---:|---:|---:|---:|\n")
		for mode, rect in rects.items():
			out.write(f"| {mode} | {rect[0]} | {rect[1]} | {rect[2]} | {rect[3]} |\n")
		out.write("\n## Checks\n\n")
		out.write("| Check | Status | Detail |\n")
		out.write("|---|---|---|\n")
		for check in checks:
			detail = check.detail.replace("|", "\\|")
			out.write(f"| {check.name} | {check.status} | {detail} |\n")
		out.write("\n## Evidence Boundary\n\n")
		out.write(
			"This closes the automated G44 source/policy preflight. Final CRT "
			"readability still requires dated analog capture or operator evidence "
			"on the target hardware routes.\n"
		)

	print(f"G44 video compliance summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
