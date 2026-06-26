#!/usr/bin/env python3
"""Parse Dolphin boot probe logs and emit G36 frame-budget harness telemetry."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path
from statistics import mean

FRAME_TIME_RE = re.compile(r"frame time=([\d.]+)ms")
GUEST_ERROR_RE = re.compile(
	r"(Host_Error|Sys_Error|Xash Error|_Mem_Alloc: out of memory|fatal error)",
	re.IGNORECASE,
)
MARKERS = {
	"guest": "Xash3D GameCube: bootstrap",
	"ready": "Xash3D GameCube: engine subsystems ready",
	"input": "Xash3D GameCube: input polling active",
	"nonblack": "sampled_nonblack=1",
	"diagnostic": "DIAGNOSTIC MARKER VISIBLE",
}


def read_log_text(log_dir: Path) -> str:
	parts: list[str] = []
	for name in ("stderr.log", "stdout.log"):
		path = log_dir / name
		if path.is_file():
			parts.append(path.read_text(encoding="utf-8", errors="replace"))
	return "\n".join(parts)


def map_marker(smoke_map: str) -> str:
	return f"Xash3D GameCube: map loaded {smoke_map}"


def extract_frame_times(text: str) -> list[float]:
	return [float(match.group(1)) for match in FRAME_TIME_RE.finditer(text)]


def percentile(values: list[float], pct: float) -> float:
	if not values:
		return 0.0
	ordered = sorted(values)
	index = int(round((pct / 100.0) * (len(ordered) - 1)))
	return ordered[max(0, min(len(ordered) - 1, index))]


def classify_g36(
	samples: list[float],
	target_ms: float,
	map_loaded: bool,
	guest_error: bool,
) -> tuple[str, str]:
	if guest_error:
		return "FAIL", "guest error observed after bootstrap"
	if not map_loaded:
		return "FAIL", "map load marker missing"
	if not samples:
		return "FAIL", "no frame timing samples captured"
	steady = [value for value in samples if value > 0.0] or samples
	avg = mean(steady)
	p95 = percentile(steady, 95.0)
	within = avg <= target_ms and p95 <= (target_ms * 1.25)
	if within and len(steady) >= 3:
		return "PASS", f"avg={avg:.2f}ms p95={p95:.2f}ms target={target_ms:.2f}ms"
	if len(steady) >= 1:
		return "WEAK", f"avg={avg:.2f}ms p95={p95:.2f}ms target={target_ms:.2f}ms"
	return "FAIL", "insufficient steady-state frame samples"


def visual_status(text: str) -> str:
	if MARKERS["nonblack"] in text:
		return "nonblack sampled"
	if MARKERS["diagnostic"] in text:
		return "diagnostic marker"
	return "unknown"


def write_harness_latest(
	repo: Path,
	*,
	goal: str,
	status: str,
	visual: str,
	log_dir: Path,
	next_action: str,
	analysis: str,
	g36_status: str,
) -> None:
	state_dir = repo / ".ai/state"
	state_dir.mkdir(parents=True, exist_ok=True)
	latest = state_dir / "dolphin-harness-latest.md"
	latest.write_text(
		"\n".join((
			"# Dolphin Harness Latest",
			"",
			f"- Goal: {goal}",
			f"- Status: {status}",
			f"- Visual: {visual}",
			f"- Audio: unknown",
			f"- Screenshot: none",
			f"- Analysis: {g36_status}",
			f"- Logs: {log_dir.relative_to(repo) if log_dir.is_relative_to(repo) else log_dir}",
			f"- Next action: {next_action}",
			"",
			"## Model Analysis",
			analysis,
			"",
		)),
		encoding="utf-8",
	)
	memory_path = state_dir / "dolphin-harness-memory.json"
	memory: dict[str, object] = {}
	if memory_path.is_file():
		try:
			memory = json.loads(memory_path.read_text(encoding="utf-8"))
		except json.JSONDecodeError:
			memory = {}
	memory.setdefault("purpose", "Persistent Dolphin run memory for Qwable/Codex porting feedback.")
	memory["updated_at"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
	run = {
		"goal": goal,
		"log_dir": str(log_dir.relative_to(repo) if log_dir.is_relative_to(repo) else log_dir),
		"classification": {
			"status": status,
			"visual": visual,
			"audio": "unknown",
			"g36_status": g36_status,
		},
		"analysis": analysis,
		"next_action": next_action,
		"timestamp": datetime.now(timezone.utc).isoformat(timespec="seconds"),
	}
	runs = memory.setdefault("runs", [])
	if isinstance(runs, list):
		runs.insert(0, run)
		del runs[20:]
	memory_path.write_text(json.dumps(memory, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path("."))
	parser.add_argument("--log-dir", type=Path, required=True)
	parser.add_argument("--smoke-map", default=os.environ.get("DOLPHIN_SMOKE_MAP", "c0a0e"))
	parser.add_argument("--target-ms", type=float, default=float(os.environ.get("TARGET_FRAME_TIME", "16.67")))
	parser.add_argument("--goal", default=os.environ.get("DOLPHIN_HARNESS_GOAL", "G36"))
	parser.add_argument("--probe-status", default="unknown")
	parser.add_argument("--update-state", action="store_true")
	args = parser.parse_args()

	repo = args.repo.resolve()
	log_dir = args.log_dir.resolve()
	text = read_log_text(log_dir)
	frame_times = extract_frame_times(text)
	map_loaded = map_marker(args.smoke_map) in text
	input_active = MARKERS["input"] in text
	guest_error = bool(GUEST_ERROR_RE.search(text))
	visual = visual_status(text)

	g36_status, g36_note = classify_g36(frame_times, args.target_ms, map_loaded, guest_error)
	steady = [value for value in frame_times if value > 0.0] or frame_times
	avg = mean(steady) if steady else 0.0
	p95 = percentile(steady, 95.0) if steady else 0.0
	max_ms = max(steady) if steady else 0.0

	print(
		f"FRAME_BUDGET_STATS: samples={len(frame_times)} "
		f"avg={avg:.2f}ms p95={p95:.2f}ms max={max_ms:.2f}ms target={args.target_ms:.2f}ms"
	)
	print(f"G36_STATUS: {g36_status}")
	print(
		f"G36_SUMMARY: {g36_note}; map_loaded={'yes' if map_loaded else 'no'}; "
		f"input={'yes' if input_active else 'no'}; visual={visual}"
	)
	print(f"VISUAL_STATUS: {visual}")

	probe_status = args.probe_status.lower()
	if guest_error and probe_status in {"map_ready", "map_loaded_no_input", "engine_ready"}:
		harness_status = "guest_failure"
	elif probe_status == "map_ready":
		harness_status = "map_ready"
	elif probe_status.startswith("guest"):
		harness_status = "guest_failure"
	elif probe_status.startswith("boot"):
		harness_status = "boot_failure"
	else:
		harness_status = probe_status or "unknown"

	if args.update_state:
		if g36_status == "PASS":
			next_action = "Continue G36 optimization with RC evidence and visual validation."
		elif harness_status == "map_ready" and g36_status == "WEAK":
			next_action = "Collect more frame samples or reduce steady-state CPU cost for G36."
		elif guest_error:
			next_action = "Fix guest memory/runtime failure before frame-budget work."
		else:
			next_action = "Re-run scripts/dolphin-boot-probe.sh after renderer or memory fixes."
		analysis = (
			f"Probe status={probe_status}. {g36_note}. "
			f"Captured {len(frame_times)} frame timing sample(s)."
		)
		write_harness_latest(
			repo,
			goal=args.goal,
			status=harness_status,
			visual=visual,
			log_dir=log_dir,
			next_action=next_action,
			analysis=analysis,
			g36_status=g36_status,
		)

	return 0


if __name__ == "__main__":
	raise SystemExit(main())
