#!/usr/bin/env python3
"""Generate G45 controller presence, reconnect, and mapping evidence."""

from __future__ import annotations

import argparse
import json
import re
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
	log_dir = args.log_dir or root / ".ai/logs" / f"controller-compliance-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)
	summary = log_dir / "summary.md"
	report = log_dir / "report.json"

	input_path = root / "engine/platform/gamecube/in_gamecube.c"
	matrix_path = root / "docs/GAMECUBE_HARDWARE_MATRIX.md"
	validation_path = root / "docs/GAMECUBE_HARDWARE_VALIDATION.md"
	compliance_path = root / "docs/GAMECUBE_HOMEBREW_COMPLIANCE.md"

	source = read(input_path)
	matrix = read(matrix_path)
	validation = read(validation_path)
	compliance = read(compliance_path)
	checks: list[Check] = []

	checks.append(Check(
		"PAD polling",
		"PASS" if contains_all(source, ("PAD_Init();", "PAD_ScanPads();", "PAD_Read( gc_pad );", "PAD_Clamp( gc_pad );")) else "FAIL",
		"uses libogc PAD init/scan/read/clamp path",
	))
	checks.append(Check(
		"no-controller-at-boot",
		"PASS" if "G45 controller waiting" in source and "gc_no_controller_logged" in source else "FAIL",
		"logs a bounded waiting diagnostic instead of blocking boot",
	))
	checks.append(Check(
		"alternate-port reconnect",
		"PASS" if "PAD_CHANMAX" in source and "GC_FindActivePort" in source and "fallback scans ports 1-4" in source else "FAIL",
		"scans all controller ports and reports fallback behavior",
	))
	checks.append(Check(
		"disconnect cleanup",
		"PASS" if contains_all(source, ("GC_ReleaseAllInput", "Key_Event( gc_buttons[i].key, false )", "JOY_AXIS_SIDE", "G45 controller disconnected")) else "FAIL",
		"releases held buttons/axes and logs disconnect marker",
	))
	checks.append(Check(
		"controller type changes",
		"PASS" if contains_all(source, ("SI_GetType", "gc_controller_type", "GC_ControllerTypeName", "WaveBird", "third-party")) else "FAIL",
		"tracks type changes and names standard/WaveBird/third-party controllers",
	))
	checks.append(Check(
		"stick and trigger deadzones",
		"PASS" if contains_all(source, ("GC_STICK_DEAD", "GC_TRIGGER_DEAD", "GC_ApplyStickDeadzone", "GC_ApplyTriggerDeadzone")) else "FAIL",
		"applies GameCube-specific stick and trigger deadzones before axis events",
	))
	checks.append(Check(
		"GameCube button names",
		"PASS" if contains_all(source, ("A confirm", "B cancel/back", "Start pause", "Joystick: GC %s -> %s")) else "FAIL",
		"logs GameCube-facing button/action names",
	))
	checks.append(Check(
		"A/B/Start mapping",
		"PASS" if contains_all(source, ("PAD_BUTTON_A", "K_B_BUTTON", "PAD_BUTTON_B", "K_A_BUTTON", "PAD_BUTTON_START", "K_START_BUTTON")) else "FAIL",
		"keeps A confirm/use, B cancel/back, and Start routed through the engine gamepad start key",
	))
	checks.append(Check(
		"hardware protocol coverage",
		"PASS" if "Controller result" in validation and "disconnect/reconnect" in validation and "WaveBird" in matrix else "FAIL",
		"hardware docs require controller type, no-controller, and disconnect/reconnect evidence",
	))
	checks.append(Check(
		"hardware controller boundary",
		"WARN",
		"Official controller, WaveBird, third-party, no-controller, and mid-game reconnect still need dated hardware/operator evidence.",
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
		out.write("# GameCube Controller Compliance\n\n")
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
			"This closes the automated G45 source/policy preflight. Hardware-complete "
			"controller acceptance still requires dated operator evidence for official "
			"controller, WaveBird, third-party pad, no-controller boot, and reconnect "
			"during gameplay.\n"
		)

	print(f"G45 controller compliance summary: {summary}")
	for check in checks:
		print(f"{check.status}: {check.name} - {check.detail}")
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
