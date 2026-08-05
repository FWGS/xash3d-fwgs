#!/usr/bin/env python3
"""G504 reduced runtime ladder — stop at the first missing gate.

Parses guest/OSReport probe logs for an ordered set of boot gates and emits
JSON summarizing pass/fail plus the first missing gate.
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Sequence


@dataclass(frozen=True)
class LadderGate:
	id: str
	label: str
	# Any pattern match counts as passed for this gate.
	any_of: tuple[str, ...]
	# If any of these match, the gate fails even when any_of matched.
	fail_if: tuple[str, ...] = ()


# Ordered gates consumed by G491+ runtime work. Patterns match guest/OSReport text.
DEFAULT_GATES: tuple[LadderGate, ...] = (
	LadderGate(
		"bootstrap",
		"Guest bootstrap heartbeat",
		("Xash3D GameCube: bootstrap",),
	),
	LadderGate(
		"engine_init",
		"Engine subsystems ready",
		("Xash3D GameCube: engine subsystems ready",),
	),
	LadderGate(
		"filesystem_init",
		"Filesystem / media mount",
		(
			"Xash3D GameCube: DVD mount ready",
			"GameCube DVD filesystem mounted",
			"FAT volume ready",
			"FAT preferred volume",
			"FAT mount ready",
		),
	),
	LadderGate(
		"delta_load",
		"delta.lst / delta tables available",
		(
			"G201 delta reinit ready",
			"G201 delta diagnostic exists=1",
		),
		fail_if=(
			"couldn't load file delta.lst",
			"Delta_InitFields: couldn't load",
			"G201 delta reinit skipped (delta.lst not found)",
		),
	),
	LadderGate(
		"server_registration",
		"Server module registration",
		(
			"COM_LoadLibrary server",
			"server (registered)",
			"HLSDK server",
		),
	),
	LadderGate(
		"bsp_open",
		"BSP / map file open",
		(
			"find found 'maps/",
			"find found \"maps/",
			"Loading map",
			"Xash3D GameCube: map loaded",
		),
	),
	LadderGate(
		"entity_spawn",
		"Entity lump spawn ready",
		(
			"Xash3D GameCube: entity lump spawn ready",
			"entity lump spawn ready",
		),
	),
	LadderGate(
		"map_loaded",
		"Map loaded / MAP_READY",
		(
			"Xash3D GameCube: map loaded",
			"Xash3D GameCube: MAP_READY",
			"MAP_READY:",
			"Xash3D GameCube: direct map ready",
		),
	),
	LadderGate(
		"controller_ready",
		"Controller / input ready",
		(
			"Xash3D GameCube: G45 controller ready",
			"Xash3D GameCube: input polling active",
		),
	),
	LadderGate(
		"stable_frame",
		"First stable nonblack frame",
		(
			"sampled_nonblack=1",
		),
	),
)


def read_log_text(path: Path) -> str:
	if path.is_file():
		return path.read_text(encoding="utf-8", errors="replace")
	parts: list[str] = []
	for name in ("stderr.log", "stdout.log", "dolphin-user/Logs/dolphin.log", "combined.log"):
		candidate = path / name
		if candidate.is_file():
			parts.append(candidate.read_text(encoding="utf-8", errors="replace"))
	if not parts and path.is_dir():
		for child in sorted(path.rglob("*.log")):
			parts.append(child.read_text(encoding="utf-8", errors="replace"))
	return "\n".join(parts)


def gate_status(text: str, gate: LadderGate) -> dict:
	lowered = text  # keep case for exact guest strings; patterns are case-sensitive where needed
	failed = any(token in lowered for token in gate.fail_if)
	passed = (not failed) and any(token in lowered for token in gate.any_of)
	return {
		"id": gate.id,
		"label": gate.label,
		"passed": passed,
		"failed_explicit": failed,
		"matched": [token for token in gate.any_of if token in lowered],
		"fail_matched": [token for token in gate.fail_if if token in lowered],
	}


def evaluate_ladder(
	text: str,
	gates: Sequence[LadderGate] = DEFAULT_GATES,
) -> dict:
	results = [gate_status(text, gate) for gate in gates]
	first_missing = None
	for item in results:
		if not item["passed"]:
			first_missing = item["id"]
			break
	passed_ids = [item["id"] for item in results if item["passed"]]
	return {
		"ok": first_missing is None,
		"first_missing": first_missing,
		"passed": passed_ids,
		"gates": results,
		"gate_count": len(results),
		"passed_count": len(passed_ids),
	}


def main(argv: Optional[Sequence[str]] = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--log-dir", type=Path, help="Probe log directory or single log file")
	parser.add_argument("--fixture", type=Path, help="Alias for --log-dir (single file or dir)")
	parser.add_argument("--json", type=Path, help="Write ladder JSON to this path")
	parser.add_argument("--text", type=str, help="Evaluate inline text instead of a log path")
	args = parser.parse_args(list(argv) if argv is not None else None)

	if args.text is not None:
		text = args.text
		source = "<inline>"
	else:
		path = args.fixture or args.log_dir
		if path is None:
			parser.error("provide --log-dir, --fixture, or --text")
		text = read_log_text(path)
		source = str(path)

	report = evaluate_ladder(text)
	report["source"] = source

	if args.json:
		args.json.parent.mkdir(parents=True, exist_ok=True)
		args.json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

	status = "PASS" if report["ok"] else f"FAIL first_missing={report['first_missing']}"
	print(f"RUNTIME_LADDER: {status} passed={report['passed_count']}/{report['gate_count']}")
	if report["first_missing"]:
		print(f"RUNTIME_LADDER_STOP: {report['first_missing']}")
	for gate in report["gates"]:
		mark = "PASS" if gate["passed"] else "MISS"
		print(f"  [{mark}] {gate['id']}: {gate['label']}")
	return 0 if report["ok"] else 1


if __name__ == "__main__":
	raise SystemExit(main())
