#!/usr/bin/env python3
"""Fail if the latest Dolphin smoke probe lost required GameCube runtime evidence."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


DEFAULT_STATE = Path(".ai/state/dolphin-harness-latest.md")
MEMORY_TELEMETRY_RE = re.compile(r"Xash3D GameCube: mem stage=.*hwm=", re.IGNORECASE)
PERF_TELEMETRY_RE = re.compile(r"Xash3D GameCube: perf stage=.*hwm=", re.IGNORECASE)
OOM_RE = re.compile(r"out of memory|malloc failed|allocation failed|MEM1 exhaustion",
	re.IGNORECASE)


def read_text(path: Path) -> str:
	try:
		return path.read_text(encoding="utf-8", errors="replace")
	except OSError:
		return ""


def latest_log_dir(root: Path, state: str) -> Path | None:
	match = re.search(r"^- Logs:\s+(.+)$", state, re.MULTILINE)
	if match:
		path = Path(match.group(1).strip())
		return path if path.is_absolute() else root / path

	logs = sorted((root / ".ai/logs").glob("dolphin-probe-*"))
	return logs[-1] if logs else None


def require(label: str, ok: bool, failures: list[str]) -> None:
	if not ok:
		failures.append(label)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--state", type=Path, default=DEFAULT_STATE)
	parser.add_argument("--smoke-map", default="c0a0e")
	args = parser.parse_args()

	root = args.repo.resolve()
	state_path = args.state if args.state.is_absolute() else root / args.state
	state = read_text(state_path)
	if not state:
		print(f"runtime gate: missing Dolphin state: {state_path}", file=sys.stderr)
		return 1

	log_dir = latest_log_dir(root, state)
	if log_dir is None:
		print("runtime gate: no Dolphin probe log directory found", file=sys.stderr)
		return 1

	log_text = "\n".join([
		read_text(log_dir / "stdout.log"),
		read_text(log_dir / "stderr.log"),
	])
	combined = state + "\n" + log_text
	memory_samples = len(MEMORY_TELEMETRY_RE.findall(log_text))
	perf_samples = len(PERF_TELEMETRY_RE.findall(log_text))

	failures: list[str] = []
	require("state status is map_ready", "- Status: map_ready" in state or
		"- Status: newgame_ready" in state, failures)
	require("G45 input passed", "G45=PASS" in state or "G45_STATUS: PASS" in combined, failures)
	require("visual output is nonblack", "Visual: nonblack sampled" in state or
		"VISUAL_STATUS: nonblack sampled" in combined or
		"VISUAL_STATUS: world render nonblack" in combined, failures)

	disc_ready = (
		"Xash3D GameCube: DVD mount ready" in log_text
		or "GameCube DVD filesystem mounted" in log_text
	)
	disc_data = (
		"read-only fallback gcdisc:/xash3d" in log_text
		or "GameCube data directory: gcdisc:/xash3d" in log_text
	)
	fat_ready = (
		"FAT volume ready" in log_text
		or "FAT preferred volume" in log_text
		or "FAT mount ready" in log_text
	)
	swiss_volume = any(
		token in log_text
		for token in ("sd:/", "carda:/", "cardb:/")
	)
	require(
		"media mount ready (DVD or Swiss FAT)",
		disc_ready or fat_ready,
		failures,
	)
	require(
		"data path selected (gcdisc or Swiss FAT volume)",
		disc_data or (fat_ready and swiss_volume),
		failures,
	)

	# Prefer G504 ladder.json when analyze already ran.
	ladder_path = log_dir / "ladder.json"
	ladder_ok = False
	if ladder_path.is_file():
		try:
			import json
			ladder_ok = bool(json.loads(ladder_path.read_text(encoding="utf-8")).get("ok"))
		except (OSError, ValueError):
			ladder_ok = False
	if "LADDER_STATUS: PASS" in combined:
		ladder_ok = True
	require("G504 runtime ladder passed", ladder_ok, failures)

	presentation_path = log_dir / "presentation.json"
	if presentation_path.is_file() or "G506_STATUS:" in combined:
		presentation_ok = "G506_STATUS: PASS" in combined
		if presentation_path.is_file():
			try:
				import json
				presentation_ok = bool(
					json.loads(presentation_path.read_text(encoding="utf-8")).get("ok")
				) or presentation_ok
			except (OSError, ValueError):
				pass
		require("G506 presentation markers passed", presentation_ok, failures)

	require(f"{args.smoke_map} map loaded", f"Xash3D GameCube: map loaded {args.smoke_map}" in log_text or
		f"MAP_READY: Xash3D loaded {args.smoke_map}" in combined, failures)
	require(
		"map reached ready marker",
		"Xash3D GameCube: direct map ready" in log_text
		or "Xash3D GameCube: MAP_READY" in log_text
		or "MAP_READY:" in combined,
		failures,
	)
	require("frame timing samples captured", "no frame timing samples captured" not in state and
		re.search(r"frame time=([\d.]+)ms", log_text) is not None, failures)
	require("memory telemetry captured", memory_samples > 0, failures)
	require("no runtime allocation failure", not OOM_RE.search(log_text), failures)

	if failures:
		print("runtime gate: FAIL", file=sys.stderr)
		print(f"runtime gate: logs={log_dir.relative_to(root) if log_dir.is_relative_to(root) else log_dir}", file=sys.stderr)
		for failure in failures:
			print(f"- missing: {failure}", file=sys.stderr)
		print(f"- telemetry: memory_samples={memory_samples} perf_samples={perf_samples}", file=sys.stderr)
		return 1

	print("runtime gate: OK")
	print(f"runtime telemetry: memory_samples={memory_samples} perf_samples={perf_samples}")
	print(f"runtime gate: logs={log_dir.relative_to(root) if log_dir.is_relative_to(root) else log_dir}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
