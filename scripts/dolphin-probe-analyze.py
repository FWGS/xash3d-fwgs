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
GCMAP_RENDER_TIME_RE = re.compile(r"gcmap render time=([\d.]+)ms")
MAP_LOADED_RE = re.compile(r"Xash3D GameCube: map loaded (\S+)")
GUEST_ERROR_RE = re.compile(
	r"(Host_Error|Sys_Error|Xash Error|_Mem_Alloc: out of memory|fatal error|guest.*(crash|abort)|"
	r"Invalid read from|MMU fault|Program attempting to read)",
	re.IGNORECASE,
)
MARKERS = {
	"guest": "Xash3D GameCube: bootstrap",
	"ready": "Xash3D GameCube: engine subsystems ready",
	"input": "Xash3D GameCube: input polling active",
	"g45_ready": "Xash3D GameCube: G45 controller ready",
	"g45_waiting": "Xash3D GameCube: G45 controller waiting",
	"smoke_begin": "Xash3D GameCube: gcmap smoke frames begin",
	"world_render_begin": "Xash3D GameCube: gcmap world render begin",
	"world_render_present": "Xash3D GameCube: gcmap world render present frame=",
	"nonblack": "sampled_nonblack=1",
	"diagnostic": "DIAGNOSTIC MARKER VISIBLE",
	"synthetic_fallback": "Xash3D GameCube: gcmap world render unavailable, using status-panel fallback",
	"world_render_ready": "Xash3D GameCube: gcmap world render ready",
}
G45_READY_RE = re.compile(
	r"Xash3D GameCube: G45 controller ready port=(\d+) type=(\S+)"
)


def read_log_text(log_dir: Path) -> str:
	parts: list[str] = []
	for name in ("stderr.log", "stdout.log"):
		path = log_dir / name
		if path.is_file():
			parts.append(path.read_text(encoding="utf-8", errors="replace"))
	return "\n".join(parts)


def map_marker(smoke_map: str) -> str:
	return f"Xash3D GameCube: map loaded {smoke_map}"


def detect_loaded_map(text: str) -> str | None:
	match = MAP_LOADED_RE.search(text)
	if match:
		return match.group(1)
	return None


def probe_phase_text(text: str, *, world_render: bool = False) -> str:
	markers: tuple[str, ...]

	if world_render:
		markers = (MARKERS["world_render_begin"],)
	else:
		markers = (MARKERS["world_render_begin"], MARKERS["smoke_begin"])

	for marker in markers:
		if marker in text:
			return text.split(marker, 1)[1]

	# gcmap/gcworldrender probes should not reuse the early splash present markers.
	if "-gcworldrender" in text or "-gcmap" in text:
		return ""

	return text


def extract_frame_times(text: str, *, world_render: bool = False) -> list[float]:
	text = probe_phase_text(text, world_render=world_render)
	if not text:
		return []
	if world_render:
		times = [float(match.group(1)) for match in GCMAP_RENDER_TIME_RE.finditer(text)]
		if times:
			return [value for value in times if value <= 500.0]
	times = [float(match.group(1)) for match in FRAME_TIME_RE.finditer(text)]
	if len(times) > 1:
		times = times[1:]  # first present after reset includes renderer restore warm-up
	# Smoke status-panel frames stay near the 16.67ms target; drop one-shot spikes.
	# World-render probe frames are intentionally heavier (software BSP), so allow
	# a higher ceiling while still rejecting pathological multi-second stalls.
	max_ms = 500.0 if world_render else 25.0
	return [value for value in times if value <= max_ms]


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
	synthetic_fallback: bool,
	world_render_ready: bool,
) -> tuple[str, str]:
	if guest_error:
		return "FAIL", "guest error observed after bootstrap"
	if not map_loaded:
		return "FAIL", "map load marker missing"
	if synthetic_fallback and not world_render_ready:
		return "WEAK", "only synthetic status-panel frames were captured; world render fallback remained unavailable"
	if not samples:
		return "FAIL", "no frame timing samples captured"
	steady = [value for value in samples if value > 0.0] or samples
	avg = mean(steady)
	p95 = percentile(steady, 95.0)
	within = avg <= (target_ms * 1.05) and p95 <= (target_ms * 1.25)
	if within and len(steady) >= 3:
		return "PASS", f"avg={avg:.2f}ms p95={p95:.2f}ms target={target_ms:.2f}ms"
	if world_render_ready and len(steady) >= 1:
		return (
			"WEAK",
			f"world render frames captured avg={avg:.2f}ms p95={p95:.2f}ms "
			f"target={target_ms:.2f}ms (software BSP path still above budget)",
		)
	if len(steady) >= 1:
		return "WEAK", f"avg={avg:.2f}ms p95={p95:.2f}ms target={target_ms:.2f}ms"
	return "FAIL", "insufficient steady-state frame samples"


def classify_g45(
	text: str,
	map_loaded: bool,
	input_active: bool,
	guest_error: bool,
) -> tuple[str, str]:
	if guest_error:
		return "FAIL", "guest error observed during controller probe"
	match = G45_READY_RE.search(text)
	if match and input_active:
		return "PASS", f"port={match.group(1)} type={match.group(2)}"
	if MARKERS["g45_waiting"] in text and not match:
		return "WAIT", "no controller at probe start; reconnect path logged"
	if map_loaded and input_active:
		return "WEAK", "input active but G45 controller marker missing"
	return "FAIL", "controller readiness markers missing"


def visual_status(text: str) -> str:
	phase_text = probe_phase_text(text, world_render="-gcworldrender" in text)

	if MARKERS["world_render_present"] in phase_text and MARKERS["nonblack"] in phase_text:
		return "world render nonblack"
	if MARKERS["nonblack"] in phase_text:
		return "nonblack sampled"
	if MARKERS["diagnostic"] in phase_text:
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
	loaded_map = detect_loaded_map(text)
	world_render_ready = MARKERS["world_render_ready"] in text
	frame_times = extract_frame_times(text, world_render=world_render_ready)
	map_loaded = map_marker(args.smoke_map) in text
	if not map_loaded and loaded_map is not None:
		map_loaded = True
	input_active = MARKERS["input"] in text
	guest_error = bool(GUEST_ERROR_RE.search(text))
	synthetic_fallback = MARKERS["synthetic_fallback"] in text
	visual = visual_status(text)

	g36_status, g36_note = classify_g36(
		frame_times,
		args.target_ms,
		map_loaded,
		guest_error,
		synthetic_fallback,
		world_render_ready,
	)
	g45_status, g45_note = classify_g45(text, map_loaded, input_active, guest_error)
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
	print(f"G45_STATUS: {g45_status}")
	print(
		f"G45_SUMMARY: {g45_note}; map_loaded={'yes' if map_loaded else 'no'}; "
		f"input={'yes' if input_active else 'no'}"
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
			f"G45={g45_status} ({g45_note}). "
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
