#!/usr/bin/env python3
"""Run repeated Dolphin probes and classify GameCube soak/leak evidence for G69."""

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
FRAME_RE = re.compile(
    r"FRAME_BUDGET_STATS:\s+samples=(?P<samples>\d+).*?avg=(?P<avg>[0-9.]+)ms"
    r".*?p95=(?P<p95>[0-9.]+)ms.*?max=(?P<max>[0-9.]+)ms",
    re.IGNORECASE,
)
LOG_RE = re.compile(r"^Logs:\s*(?P<path>.+)$", re.MULTILINE)


@dataclass
class Iteration:
    iteration: int
    map_name: str
    status: str
    exit_code: int
    elapsed_sec: float
    hwm_bytes: int | None
    memory_stage: str
    frame_samples: int
    frame_avg_ms: float | None
    frame_p95_ms: float | None
    frame_max_ms: float | None
    log_dir: str
    note: str


def parse_size(value: str, unit: str | None) -> int:
    scale = {
        None: 1,
        "bytes": 1,
        "kb": 1024,
        "mb": 1024 * 1024,
        "gb": 1024 * 1024 * 1024,
    }[(unit or "bytes").lower()]
    return int(float(value) * scale)


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


def parse_iteration(
    root: Path,
    index: int,
    map_name: str,
    exit_code: int,
    elapsed: float,
    output: str,
) -> Iteration:
    log_dir, text = read_logs(root, output)
    status = "FAIL"
    note = "probe did not reach map-ready evidence"
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

    hwm_bytes: int | None = None
    memory_stage = "N/A"
    for match in MEM_RE.finditer(text):
        hwm = parse_size(match.group("hwm"), match.group("hwm_unit"))
        if hwm_bytes is None or hwm >= hwm_bytes:
            hwm_bytes = hwm
            memory_stage = " ".join(match.group("stage").split())

    frame_samples = 0
    frame_avg = frame_p95 = frame_max = None
    for match in FRAME_RE.finditer(text):
        frame_samples = int(match.group("samples"))
        frame_avg = float(match.group("avg"))
        frame_p95 = float(match.group("p95"))
        frame_max = float(match.group("max"))

    return Iteration(
        iteration=index,
        map_name=map_name,
        status=status,
        exit_code=exit_code,
        elapsed_sec=round(elapsed, 3),
        hwm_bytes=hwm_bytes,
        memory_stage=memory_stage,
        frame_samples=frame_samples,
        frame_avg_ms=frame_avg,
        frame_p95_ms=frame_p95,
        frame_max_ms=frame_max,
        log_dir=log_dir or "N/A",
        note=note,
    )


def synthetic_iteration(index: int, map_name: str) -> Iteration:
    base = 5 * 1024 * 1024
    return Iteration(
        iteration=index,
        map_name=map_name,
        status="PASS",
        exit_code=0,
        elapsed_sec=0.0,
        hwm_bytes=base,
        memory_stage="dry-run synthetic",
        frame_samples=3,
        frame_avg_ms=0.0,
        frame_p95_ms=0.0,
        frame_max_ms=0.0,
        log_dir="N/A",
        note="dry run validates soak reporting without launching Dolphin",
    )


def classify(iterations: list[Iteration], tolerance_bytes: int) -> tuple[bool, str]:
    failures = [item for item in iterations if item.status == "FAIL"]
    if failures:
        return False, f"{len(failures)} iteration(s) failed"
    missing_memory = [item for item in iterations if item.hwm_bytes is None]
    if missing_memory:
        return False, f"{len(missing_memory)} iteration(s) lack memory telemetry"
    missing_frames = [item for item in iterations if item.frame_samples <= 0]
    if missing_frames:
        return False, f"{len(missing_frames)} iteration(s) lack frame telemetry"

    values = [item.hwm_bytes or 0 for item in iterations]
    growth = values[-1] - values[0] if len(values) > 1 else 0
    monotonic = all(values[i] >= values[i - 1] for i in range(1, len(values)))
    if monotonic and growth > tolerance_bytes:
        return False, f"monotonic memory growth {growth} bytes exceeds tolerance {tolerance_bytes}"
    return True, "soak evidence passed memory/frame telemetry checks"


def write_reports(
    log_dir: Path,
    iterations: list[Iteration],
    ok: bool,
    classification: str,
    elapsed_total: float,
    args: argparse.Namespace,
) -> None:
    report = {
        "generated": datetime.now(timezone.utc).isoformat(),
        "ok": ok,
        "classification": classification,
        "elapsed_total_sec": round(elapsed_total, 3),
        "maps": args.maps,
        "iterations": args.iterations,
        "timeout": args.timeout,
        "strict": args.strict,
        "dry_run": args.dry_run,
        "memory_growth_tolerance_bytes": args.memory_growth_tolerance_bytes,
        "results": [asdict(item) for item in iterations],
    }
    (log_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    with (log_dir / "results.tsv").open("w", encoding="utf-8") as out:
        out.write(
            "iteration\tmap\tstatus\texit_code\telapsed_sec\thwm_bytes\tmemory_stage\t"
            "frame_samples\tframe_avg_ms\tframe_p95_ms\tframe_max_ms\tlog_dir\tnote\n"
        )
        for item in iterations:
            out.write(
                f"{item.iteration}\t{item.map_name}\t{item.status}\t{item.exit_code}\t"
                f"{item.elapsed_sec}\t{item.hwm_bytes if item.hwm_bytes is not None else 'N/A'}\t"
                f"{item.memory_stage}\t{item.frame_samples}\t"
                f"{item.frame_avg_ms if item.frame_avg_ms is not None else 'N/A'}\t"
                f"{item.frame_p95_ms if item.frame_p95_ms is not None else 'N/A'}\t"
                f"{item.frame_max_ms if item.frame_max_ms is not None else 'N/A'}\t"
                f"{item.log_dir}\t{item.note}\n"
            )

    with (log_dir / "summary.md").open("w", encoding="utf-8") as out:
        out.write("# GameCube Sustained Soak Probe\n\n")
        out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
        out.write(f"- Status: {'PASS' if ok else 'FAIL'}\n")
        out.write(f"- Classification: {classification}\n")
        out.write(f"- Elapsed total: {elapsed_total:.3f}s\n")
        out.write(f"- Maps: `{', '.join(args.maps)}`\n")
        out.write(f"- Iterations: {args.iterations}\n")
        out.write(f"- Timeout per probe: {args.timeout}s\n")
        out.write(f"- Strict release mode: {int(args.strict)}\n")
        out.write(f"- Dry run: {int(args.dry_run)}\n")
        out.write(f"- Memory growth tolerance: {args.memory_growth_tolerance_bytes} bytes\n\n")
        out.write("| Iteration | Map | Status | HWM bytes | Frame samples | Log | Note |\n")
        out.write("|---:|---|---|---:|---:|---|---|\n")
        for item in iterations:
            hwm = item.hwm_bytes if item.hwm_bytes is not None else "N/A"
            out.write(
                f"| {item.iteration} | {item.map_name} | {item.status} | {hwm} | "
                f"{item.frame_samples} | `{item.log_dir}` | {item.note} |\n"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--log-dir", type=Path)
    parser.add_argument("--maps", nargs="+", default=["c0a0e"])
    parser.add_argument("--iterations", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=int(os.environ.get("SOAK_TIMEOUT", "180")))
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--min-strict-seconds", type=int, default=30 * 60)
    parser.add_argument("--memory-growth-tolerance-bytes", type=int, default=256 * 1024)
    args = parser.parse_args()

    root = args.repo.resolve()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    log_dir = args.log_dir or root / ".ai/logs" / f"soak-probe-{stamp}"
    log_dir.mkdir(parents=True, exist_ok=True)

    if args.iterations < 1:
        print("soak probe: --iterations must be >= 1", file=sys.stderr)
        return 2

    started = time.monotonic()
    results: list[Iteration] = []
    for index in range(1, args.iterations + 1):
        map_name = args.maps[(index - 1) % len(args.maps)]
        if args.dry_run:
            results.append(synthetic_iteration(index, map_name))
            continue

        env = os.environ.copy()
        env["DOLPHIN_SMOKE_MAP"] = map_name
        env["DOLPHIN_TIMEOUT"] = str(args.timeout)
        command = ["scripts/dolphin-boot-probe.sh"]
        before = time.monotonic()
        proc = subprocess.run(command, cwd=root, env=env, text=True, capture_output=True, check=False)
        elapsed = time.monotonic() - before
        attempt_log = log_dir / f"iteration-{index:02d}.log"
        attempt_log.write_text(proc.stdout + proc.stderr, encoding="utf-8")
        results.append(parse_iteration(root, index, map_name, proc.returncode, elapsed, proc.stdout + proc.stderr))

    elapsed_total = time.monotonic() - started
    ok, classification = classify(results, args.memory_growth_tolerance_bytes)
    if args.strict and elapsed_total < args.min_strict_seconds:
        ok = False
        classification = (
            f"strict soak elapsed {elapsed_total:.1f}s below required {args.min_strict_seconds}s"
        )
    write_reports(log_dir, results, ok, classification, elapsed_total, args)
    print(log_dir / "summary.md")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
