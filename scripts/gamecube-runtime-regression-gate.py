#!/usr/bin/env python3
"""Fail if the latest Dolphin smoke probe lost required GameCube runtime evidence."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


DEFAULT_STATE = Path(".ai/state/dolphin-harness-latest.md")


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

	failures: list[str] = []
	require("state status is map_ready", "- Status: map_ready" in state, failures)
	require("G45 input passed", "G45=PASS" in state or "G45_STATUS: PASS" in combined, failures)
	require("visual output is nonblack", "Visual: nonblack sampled" in state or
		"VISUAL_STATUS: nonblack sampled" in combined, failures)
	require("DVD mounted successfully", "Xash3D GameCube: DVD mount ready" in log_text, failures)
	require("disc data path selected", "read-only fallback gcdisc:/xash3d" in log_text or
		"GameCube data directory: gcdisc:/xash3d" in log_text, failures)
	require(f"{args.smoke_map} map loaded", f"Xash3D GameCube: map loaded {args.smoke_map}" in log_text or
		f"MAP_READY: Xash3D loaded {args.smoke_map}" in combined, failures)
	require("direct map reached ready marker", "Xash3D GameCube: direct map ready" in log_text, failures)
	require("frame timing samples captured", "no frame timing samples captured" not in state and
		re.search(r"frame time=([\d.]+)ms", log_text) is not None, failures)

	if failures:
		print("runtime gate: FAIL", file=sys.stderr)
		print(f"runtime gate: logs={log_dir.relative_to(root) if log_dir.is_relative_to(root) else log_dir}", file=sys.stderr)
		for failure in failures:
			print(f"- missing: {failure}", file=sys.stderr)
		return 1

	print("runtime gate: OK")
	print(f"runtime gate: logs={log_dir.relative_to(root) if log_dir.is_relative_to(root) else log_dir}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
