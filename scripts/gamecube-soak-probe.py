#!/usr/bin/env python3
"""Run repeated Dolphin probes and classify GameCube soak/leak evidence (G69/G509).

Default mode repeats bounded map boots. G509 mode adds a representative
changelevel route (from→to) so release soak covers continuity, not only
repeated single-map boots.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


MEM_RE = re.compile(
	r"mem stage=(?P<stage>.*?)\s+total=(?P<total>[0-9.]+)\s*(?P<total_unit>bytes|Kb|Mb|Gb)?"
	r".*?\shwm=(?P<hwm>[0-9.]+)\s*(?P<hwm_unit>bytes|Kb|Mb|Gb)?",
	re.IGNORECASE,
)
# Prefer mem1 telemetry when present (GameCube MEM1 high-water).
MEM1_RE = re.compile(
	r"(?:MEM1|mem1)[^\n]*?(?:hwm|high[-_ ]?water|peak)[=:\s]+(?P<hwm>[0-9.]+)\s*(?P<unit>bytes|Kb|Mb|Gb|KiB|MiB)?",
	re.IGNORECASE,
)
FRAME_RE = re.compile(
	r"FRAME_BUDGET_STATS:\s+samples=(?P<samples>\d+).*?avg=(?P<avg>[0-9.]+)ms"
	r".*?p95=(?P<p95>[0-9.]+)ms.*?max=(?P<max>[0-9.]+)ms",
	re.IGNORECASE,
)
LOG_RE = re.compile(r"^Logs:\s*(?P<path>.+)$", re.MULTILINE)

# Default G509 continuity route: early tram hop already proven on Dolphin.
DEFAULT_CHANGELEVEL_ROUTE = "c0a0:c0a0a"
# Known landmark names for G97–G100 (soak sets DOLPHIN_LANDMARK when unset).
DEFAULT_CHANGELEVEL_LANDMARKS = {
	"c0a0:c0a0a": "c0a0toa",
}


@dataclass
class Iteration:
	iteration: int
	map_name: str
	mode: str
	changelevel_to: str
	status: str
	exit_code: int
	elapsed_sec: float
	hwm_bytes: int | None
	memory_stage: str
	frame_samples: int
	frame_avg_ms: float | None
	frame_p95_ms: float | None
	frame_max_ms: float | None
	landmark_restore: bool
	ladder_ok: bool
	ladder_first_missing: str | None
	log_dir: str
	note: str


def load_evaluate_ladder():
	import importlib.util

	path = Path(__file__).resolve().parent / "gamecube-runtime-ladder.py"
	name = "gamecube_runtime_ladder"
	if name in sys.modules:
		return sys.modules[name].evaluate_ladder
	spec = importlib.util.spec_from_file_location(name, path)
	if spec is None or spec.loader is None:
		raise RuntimeError(f"cannot load {path}")
	module = importlib.util.module_from_spec(spec)
	sys.modules[name] = module
	spec.loader.exec_module(module)
	return module.evaluate_ladder


def parse_size(value: str, unit: str | None) -> int:
	normalized = (unit or "bytes").lower()
	if normalized == "kib":
		normalized = "kb"
	if normalized == "mib":
		normalized = "mb"
	scale = {
		None: 1,
		"bytes": 1,
		"kb": 1024,
		"mb": 1024 * 1024,
		"gb": 1024 * 1024 * 1024,
	}[normalized]
	return int(float(value) * scale)


def parse_route(route: str) -> tuple[str, str]:
	"""Parse `from:to` changelevel route text."""
	text = route.strip()
	if ":" not in text:
		raise ValueError(f"changelevel route must be FROM:TO, got {route!r}")
	source, dest = text.split(":", 1)
	source = source.strip()
	dest = dest.strip()
	if not source or not dest:
		raise ValueError(f"changelevel route must be FROM:TO, got {route!r}")
	return source, dest


def read_logs(root: Path, probe_output: str) -> tuple[str, str]:
	match = LOG_RE.search(probe_output)
	log_dir = match.group("path").strip() if match else ""
	texts: list[str] = [probe_output]
	if log_dir:
		for name in ("stderr.log", "stdout.log"):
			path = root / log_dir / name
			if path.is_file():
				texts.append(path.read_text(encoding="utf-8", errors="replace"))
	return log_dir, "\n".join(texts)


def extract_memory(text: str) -> tuple[int | None, str]:
	hwm_bytes: int | None = None
	memory_stage = "N/A"
	for match in MEM_RE.finditer(text):
		hwm = parse_size(match.group("hwm"), match.group("hwm_unit"))
		if hwm_bytes is None or hwm >= hwm_bytes:
			hwm_bytes = hwm
			memory_stage = " ".join(match.group("stage").split())
	for match in MEM1_RE.finditer(text):
		hwm = parse_size(match.group("hwm"), match.group("unit"))
		if hwm_bytes is None or hwm >= hwm_bytes:
			hwm_bytes = hwm
			if memory_stage == "N/A":
				memory_stage = "mem1"
	# Also accept explicit byte counts used by recent continue soak reports.
	for match in re.finditer(r"MEM1 high-water(?: was)?(?: identical at)?\s*`?(?P<n>[0-9,]+)`?\s*bytes", text, re.I):
		hwm = int(match.group("n").replace(",", ""))
		if hwm_bytes is None or hwm >= hwm_bytes:
			hwm_bytes = hwm
			memory_stage = "mem1"
	return hwm_bytes, memory_stage


def extract_frames(text: str) -> tuple[int, float | None, float | None, float | None]:
	frame_samples = 0
	frame_avg = frame_p95 = frame_max = None
	for match in FRAME_RE.finditer(text):
		frame_samples = int(match.group("samples"))
		frame_avg = float(match.group("avg"))
		frame_p95 = float(match.group("p95"))
		frame_max = float(match.group("max"))
	return frame_samples, frame_avg, frame_p95, frame_max


def parse_iteration(
	root: Path,
	index: int,
	map_name: str,
	*,
	mode: str,
	changelevel_to: str,
	exit_code: int,
	elapsed: float,
	output: str,
) -> Iteration:
	log_dir, text = read_logs(root, output)
	landmark = "G100 landmark restore" in text or "Xash3D GameCube: G100 landmark restore" in text
	status = "FAIL"
	note = "probe did not reach map-ready evidence"

	if mode == "changelevel":
		ready = (
			"CHANGELEVEL_READY" in text
			or f"G68 changelevel ready from={map_name} to={changelevel_to}" in text
			or f"Xash3D GameCube: G68 changelevel ready from={map_name} to={changelevel_to}" in text
		)
		dest_ready = (
			f"MAP_READY {changelevel_to}" in text
			or f"Xash3D GameCube: MAP_READY {changelevel_to}" in text
			or f"map loaded {changelevel_to}" in text
		)
		if ready and (landmark or dest_ready):
			status = "PASS"
			note = "changelevel ready with destination/landmark continuity"
		elif ready:
			status = "WARN"
			note = "changelevel ready without landmark restore marker"
		elif dest_ready:
			status = "WARN"
			note = "destination map ready without CHANGELEVEL_READY"
		elif exit_code != 0:
			note = f"changelevel probe exited {exit_code}"
	else:
		if f"MAP_READY: Xash3D loaded {map_name}" in text:
			status = "PASS"
			note = "map-ready evidence observed"
		elif f"Xash3D GameCube: map loaded {map_name}" in text or "MAP_LOADED_NO_INPUT" in text:
			status = "WARN"
			note = "map loaded but interactive/input evidence was weak"
		elif "FRAME_BUDGET_STATS" in text:
			status = "WARN"
			note = "frame telemetry observed without map-ready marker"
		elif exit_code != 0:
			note = f"probe exited {exit_code}"

	hwm_bytes, memory_stage = extract_memory(text)
	frame_samples, frame_avg, frame_p95, frame_max = extract_frames(text)
	ladder = load_evaluate_ladder()(text)
	ladder_ok = bool(ladder.get("ok"))
	ladder_first_missing = ladder.get("first_missing")

	return Iteration(
		iteration=index,
		map_name=map_name,
		mode=mode,
		changelevel_to=changelevel_to,
		status=status,
		exit_code=exit_code,
		elapsed_sec=round(elapsed, 3),
		hwm_bytes=hwm_bytes,
		memory_stage=memory_stage,
		frame_samples=frame_samples,
		frame_avg_ms=frame_avg,
		frame_p95_ms=frame_p95,
		frame_max_ms=frame_max,
		landmark_restore=landmark,
		ladder_ok=ladder_ok,
		ladder_first_missing=ladder_first_missing,
		log_dir=log_dir or "N/A",
		note=note,
	)


def synthetic_iteration(index: int, map_name: str, *, mode: str, changelevel_to: str) -> Iteration:
	base = 5 * 1024 * 1024
	return Iteration(
		iteration=index,
		map_name=map_name,
		mode=mode,
		changelevel_to=changelevel_to,
		status="PASS",
		exit_code=0,
		elapsed_sec=0.0,
		hwm_bytes=base,
		memory_stage="dry-run synthetic",
		frame_samples=3,
		frame_avg_ms=0.0,
		frame_p95_ms=0.0,
		frame_max_ms=0.0,
		landmark_restore=(mode == "changelevel"),
		ladder_ok=True,
		ladder_first_missing=None,
		log_dir="N/A",
		note="dry run validates soak reporting without launching Dolphin",
	)


def classify(
	iterations: list[Iteration],
	tolerance_bytes: int,
	*,
	require_changelevel: bool,
	require_ladder: bool = False,
) -> tuple[bool, str]:
	failures = [item for item in iterations if item.status == "FAIL"]
	if failures:
		return False, f"{len(failures)} iteration(s) failed"
	missing_memory = [item for item in iterations if item.hwm_bytes is None]
	if missing_memory:
		return False, f"{len(missing_memory)} iteration(s) lack memory telemetry"
	missing_frames = [item for item in iterations if item.frame_samples <= 0]
	if missing_frames:
		return False, f"{len(missing_frames)} iteration(s) lack frame telemetry"

	if require_changelevel:
		routes = [item for item in iterations if item.mode == "changelevel"]
		if not routes:
			return False, "G509 require-changelevel set but no changelevel iterations ran"
		weak = [item for item in routes if item.status != "PASS"]
		if weak:
			return False, f"{len(weak)} changelevel iteration(s) did not fully pass"
		no_landmark = [item for item in routes if not item.landmark_restore]
		if no_landmark:
			return False, f"{len(no_landmark)} changelevel iteration(s) lack G100 landmark restore"

	if require_ladder:
		incomplete = [item for item in iterations if not item.ladder_ok]
		if incomplete:
			first = incomplete[0].ladder_first_missing or "unknown"
			return False, (
				f"{len(incomplete)} iteration(s) fail G504 runtime ladder "
				f"(first_missing={first})"
			)

	values = [item.hwm_bytes or 0 for item in iterations]
	growth = values[-1] - values[0] if len(values) > 1 else 0
	monotonic = all(values[i] >= values[i - 1] for i in range(1, len(values)))
	if monotonic and growth > tolerance_bytes:
		return False, f"monotonic memory growth {growth} bytes exceeds tolerance {tolerance_bytes}"
	return True, "soak evidence passed memory/frame telemetry checks"


def write_experiment_manifest(
	root: Path,
	log_dir: Path,
	*,
	hypothesis: str,
	dry_run: bool,
	changelevel_route: str,
) -> Path | None:
	"""Attach a G501 experiment manifest beside soak reports."""
	import importlib.util

	path = Path(__file__).resolve().parent / "gamecube-experiment-manifest.py"
	spec = importlib.util.spec_from_file_location("gamecube_experiment_manifest", path)
	if spec is None or spec.loader is None:
		return None
	module = importlib.util.module_from_spec(spec)
	sys.modules.setdefault("gamecube_experiment_manifest", module)
	spec.loader.exec_module(module)
	ladder_json = log_dir / "ladder.json"
	# Prefer iteration ladder summary when present.
	if not ladder_json.is_file():
		ladder_json = None
	argv = [
		"--repo", str(root),
		"--hypothesis", hypothesis,
		"--target-file", "scripts/gamecube-soak-probe.py",
		"--decision", "pending",
		"--output-dir", str(log_dir / "experiment"),
	]
	if dry_run:
		argv.append("--dry-run")
	if changelevel_route:
		argv.extend(["--target-file", f"changelevel:{changelevel_route}"])
	if ladder_json is not None:
		argv.extend(["--ladder-json", str(ladder_json)])
	code = module.main(argv)
	manifest_path = log_dir / "experiment" / "manifest.json"
	return manifest_path if code == 0 and manifest_path.is_file() else None


def write_reports(
	log_dir: Path,
	iterations: list[Iteration],
	ok: bool,
	classification: str,
	elapsed_total: float,
	args: argparse.Namespace,
	*,
	root: Path | None = None,
) -> None:
	report = {
		"generated": datetime.now(timezone.utc).isoformat(),
		"ok": ok,
		"classification": classification,
		"elapsed_total_sec": round(elapsed_total, 3),
		"maps": args.maps,
		"changelevel_route": args.changelevel_route,
		"mode": "changelevel" if args.changelevel_route else "map",
		"iterations": args.iterations,
		"timeout": args.timeout,
		"strict": args.strict,
		"dry_run": args.dry_run,
		"require_changelevel": args.require_changelevel,
		"require_ladder": args.require_ladder,
		"memory_growth_tolerance_bytes": args.memory_growth_tolerance_bytes,
		"results": [asdict(item) for item in iterations],
	}
	(log_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

	# Compact ladder summary for G501 attachment.
	ladder_summary = {
		"ok": all(item.ladder_ok for item in iterations) if iterations else False,
		"iterations": [
			{
				"iteration": item.iteration,
				"ladder_ok": item.ladder_ok,
				"ladder_first_missing": item.ladder_first_missing,
			}
			for item in iterations
		],
	}
	(log_dir / "ladder.json").write_text(json.dumps(ladder_summary, indent=2) + "\n", encoding="utf-8")

	with (log_dir / "results.tsv").open("w", encoding="utf-8") as out:
		out.write(
			"iteration\tmode\tmap\tto\tstatus\texit_code\telapsed_sec\thwm_bytes\tmemory_stage\t"
			"frame_samples\tframe_avg_ms\tframe_p95_ms\tframe_max_ms\tlandmark\tladder_ok\t"
			"ladder_first_missing\tlog_dir\tnote\n"
		)
		for item in iterations:
			out.write(
				f"{item.iteration}\t{item.mode}\t{item.map_name}\t{item.changelevel_to or '-'}\t"
				f"{item.status}\t{item.exit_code}\t{item.elapsed_sec}\t"
				f"{item.hwm_bytes if item.hwm_bytes is not None else 'N/A'}\t"
				f"{item.memory_stage}\t{item.frame_samples}\t"
				f"{item.frame_avg_ms if item.frame_avg_ms is not None else 'N/A'}\t"
				f"{item.frame_p95_ms if item.frame_p95_ms is not None else 'N/A'}\t"
				f"{item.frame_max_ms if item.frame_max_ms is not None else 'N/A'}\t"
				f"{int(item.landmark_restore)}\t{int(item.ladder_ok)}\t"
				f"{item.ladder_first_missing or '-'}\t{item.log_dir}\t{item.note}\n"
			)

	with (log_dir / "summary.md").open("w", encoding="utf-8") as out:
		out.write("# GameCube Sustained Soak Probe\n\n")
		out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
		out.write(f"- Status: {'PASS' if ok else 'FAIL'}\n")
		out.write(f"- Classification: {classification}\n")
		out.write(f"- Elapsed total: {elapsed_total:.3f}s\n")
		out.write(f"- Mode: `{'changelevel' if args.changelevel_route else 'map'}`\n")
		out.write(f"- Maps: `{', '.join(args.maps)}`\n")
		if args.changelevel_route:
			out.write(f"- Changelevel route: `{args.changelevel_route}`\n")
		out.write(f"- Iterations: {args.iterations}\n")
		out.write(f"- Timeout per probe: {args.timeout}s\n")
		out.write(f"- Strict release mode: {int(args.strict)}\n")
		out.write(f"- Require changelevel: {int(args.require_changelevel)}\n")
		out.write(f"- Require G504 ladder: {int(args.require_ladder)}\n")
		out.write(f"- Dry run: {int(args.dry_run)}\n")
		out.write(f"- Memory growth tolerance: {args.memory_growth_tolerance_bytes} bytes\n\n")
		out.write("| Iteration | Mode | Map | To | Status | HWM | Frames | Landmark | Ladder | Note |\n")
		out.write("|---:|---|---|---|---|---:|---:|---|---|---|\n")
		for item in iterations:
			hwm = item.hwm_bytes if item.hwm_bytes is not None else "N/A"
			ladder = "yes" if item.ladder_ok else f"no:{item.ladder_first_missing or '?'}"
			out.write(
				f"| {item.iteration} | {item.mode} | {item.map_name} | {item.changelevel_to or '-'} | "
				f"{item.status} | {hwm} | {item.frame_samples} | "
				f"{'yes' if item.landmark_restore else 'no'} | {ladder} | {item.note} |\n"
			)

	if root is not None:
		mode = "changelevel" if args.changelevel_route else "map"
		hypothesis = (
			f"G509 soak {mode} route={args.changelevel_route or ','.join(args.maps)} "
			f"ok={int(ok)} ({classification})"
		)
		manifest_path = write_experiment_manifest(
			root,
			log_dir,
			hypothesis=hypothesis,
			dry_run=bool(args.dry_run),
			changelevel_route=args.changelevel_route or "",
		)
		if manifest_path is not None:
			report["experiment_manifest"] = str(manifest_path)
			(log_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
			with (log_dir / "summary.md").open("a", encoding="utf-8") as out:
				out.write(f"\n- G501 experiment manifest: `{manifest_path}`\n")


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
	parser.add_argument("--log-dir", type=Path)
	parser.add_argument("--maps", nargs="+", default=["c0a0e"])
	parser.add_argument(
		"--changelevel-route",
		default="",
		help="G509 route as FROM:TO (example: c0a0:c0a0a). When set, each iteration runs a changelevel probe.",
	)
	parser.add_argument(
		"--g509",
		action="store_true",
		help=f"enable G509 defaults (changelevel route {DEFAULT_CHANGELEVEL_ROUTE}, require continuity)",
	)
	parser.add_argument("--iterations", type=int, default=2)
	parser.add_argument("--timeout", type=int, default=int(os.environ.get("SOAK_TIMEOUT", "180")))
	parser.add_argument("--strict", action="store_true")
	parser.add_argument("--dry-run", action="store_true")
	parser.add_argument("--require-changelevel", action="store_true")
	parser.add_argument(
		"--require-ladder",
		action="store_true",
		help="Fail soak when any iteration misses a G504 runtime ladder gate",
	)
	parser.add_argument("--min-strict-seconds", type=int, default=30 * 60)
	parser.add_argument("--memory-growth-tolerance-bytes", type=int, default=256 * 1024)
	args = parser.parse_args(argv)

	if args.g509:
		if not args.changelevel_route:
			args.changelevel_route = DEFAULT_CHANGELEVEL_ROUTE
		args.require_changelevel = True
		args.require_ladder = True
		# Keep a short G509 default map list aligned with the route source.
		source, _dest = parse_route(args.changelevel_route)
		args.maps = [source]

	root = args.repo.resolve()
	stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
	log_dir = args.log_dir or root / ".ai/logs" / f"soak-probe-{stamp}"
	log_dir.mkdir(parents=True, exist_ok=True)

	if args.iterations < 1:
		print("soak probe: --iterations must be >= 1", file=sys.stderr)
		return 2

	route_from = route_to = ""
	if args.changelevel_route:
		try:
			route_from, route_to = parse_route(args.changelevel_route)
		except ValueError as exc:
			print(f"soak probe: {exc}", file=sys.stderr)
			return 2

	started = time.monotonic()
	results: list[Iteration] = []
	for index in range(1, args.iterations + 1):
		if args.changelevel_route:
			map_name = route_from
			mode = "changelevel"
			changelevel_to = route_to
		else:
			map_name = args.maps[(index - 1) % len(args.maps)]
			mode = "map"
			changelevel_to = ""

		if args.dry_run:
			results.append(
				synthetic_iteration(index, map_name, mode=mode, changelevel_to=changelevel_to)
			)
			continue

		env = os.environ.copy()
		env["DOLPHIN_SMOKE_MAP"] = map_name
		env["DOLPHIN_TIMEOUT"] = str(args.timeout)
		if mode == "changelevel":
			env["DOLPHIN_CHANGELEVEL"] = changelevel_to
			env["DOLPHIN_NEWGAME"] = env.get("DOLPHIN_NEWGAME", "1")
			route_key = f"{map_name}:{changelevel_to}"
			if not env.get("DOLPHIN_LANDMARK"):
				landmark = DEFAULT_CHANGELEVEL_LANDMARKS.get(route_key) or DEFAULT_CHANGELEVEL_LANDMARKS.get(
					args.changelevel_route or ""
				)
				if landmark:
					env["DOLPHIN_LANDMARK"] = landmark
		command = ["scripts/dolphin-boot-probe.sh"]
		before = time.monotonic()
		proc = subprocess.run(command, cwd=root, env=env, text=True, capture_output=True, check=False)
		elapsed = time.monotonic() - before
		attempt_log = log_dir / f"iteration-{index:02d}.log"
		attempt_log.write_text(proc.stdout + proc.stderr, encoding="utf-8")
		results.append(
			parse_iteration(
				root,
				index,
				map_name,
				mode=mode,
				changelevel_to=changelevel_to,
				exit_code=proc.returncode,
				elapsed=elapsed,
				output=proc.stdout + proc.stderr,
			)
		)

	elapsed_total = time.monotonic() - started
	ok, classification = classify(
		results,
		args.memory_growth_tolerance_bytes,
		require_changelevel=args.require_changelevel,
		require_ladder=args.require_ladder,
	)
	if args.strict and elapsed_total < args.min_strict_seconds:
		ok = False
		classification = (
			f"strict soak elapsed {elapsed_total:.1f}s below required {args.min_strict_seconds}s"
		)
	write_reports(log_dir, results, ok, classification, elapsed_total, args, root=root)
	print(log_dir / "summary.md")
	return 0 if ok else 1


if __name__ == "__main__":
	raise SystemExit(main())
