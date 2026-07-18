#!/usr/bin/env python3
"""Summarize worst-case GameCube performance and memory evidence for G72."""

from __future__ import annotations

import argparse
import csv
import json
import re
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path


MEM1_CRITICAL_BYTES = 18 * 1024 * 1024
MEM1_MAP_ACTIVE_LIMIT = 7 * 1024 * 1024
MEM1_BSP_LOAD_LIMIT = 8 * 1024 * 1024

SIZE_RE = re.compile(r"(?P<value>[0-9.]+)\s*(?P<unit>bytes|Kb|KiB|Mb|MiB|Gb|GiB)", re.I)


@dataclass
class Evidence:
    source: str
    scene: str
    status: str
    hwm_bytes: int | None
    frame_p95_ms: float | None
    frame_max_ms: float | None
    blocker: str
    recommendation: str


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def size_to_bytes(value: str) -> int | None:
    if not value or value == "N/A":
        return None
    match = SIZE_RE.search(value)
    if not match:
        try:
            return int(float(value))
        except ValueError:
            return None
    scale = {
        "bytes": 1,
        "kb": 1024,
        "kib": 1024,
        "mb": 1024 * 1024,
        "mib": 1024 * 1024,
        "gb": 1024 * 1024 * 1024,
        "gib": 1024 * 1024 * 1024,
    }[match.group("unit").lower()]
    return int(float(match.group("value")) * scale)


def iter_tsv(paths: list[Path]) -> list[tuple[Path, dict[str, str]]]:
    rows: list[tuple[Path, dict[str, str]]] = []
    for path in paths:
        if not path.is_file():
            continue
        with path.open("r", encoding="utf-8", errors="replace", newline="") as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            for row in reader:
                rows.append((path, {key: value for key, value in row.items() if key}))
    return rows


def recommendation_for(status: str, hwm: int | None, blocker: str) -> str:
    status_upper = status.upper()
    blocker_lower = blocker.lower()
    if hwm is not None and hwm >= MEM1_CRITICAL_BYTES:
        return "critical MEM1 pressure; reduce caches/assets before declaring scene supported"
    if hwm is not None and hwm >= MEM1_BSP_LOAD_LIMIT:
        return "near BSP/transition limit; keep release profile conservative and retest"
    if status_upper in {"GUEST_FAILURE", "HOST_FAILURE", "TIMEOUT", "INCONCLUSIVE", "MAP_TIMEOUT"}:
        if "sys_initlog" in blocker_lower or "can't create" in blocker_lower:
            return "storage/log path failure; rerun after writable-log route fix before performance tuning"
        return "runtime blocker; classify before optimization"
    if status_upper in {"NOT_TESTED", "MISSING"}:
        return "needs runtime evidence"
    return "supported by current evidence; keep release profile unless later G68/G69 data regresses"


def collect_map_compat(root: Path) -> list[Evidence]:
    paths = sorted((root / ".ai/logs").glob("map-compat-*/results.tsv"))
    evidence: list[Evidence] = []
    for path, row in iter_tsv(paths):
        scene = row.get("map", "unknown")
        status = row.get("status", "UNKNOWN")
        hwm = size_to_bytes(row.get("memory_peak", ""))
        blocker = row.get("blocker", "")
        evidence.append(Evidence(
            source=path.relative_to(root).as_posix(),
            scene=scene,
            status=status,
            hwm_bytes=hwm,
            frame_p95_ms=None,
            frame_max_ms=None,
            blocker=blocker,
            recommendation=recommendation_for(status, hwm, blocker),
        ))
    return evidence


def collect_campaign(root: Path) -> list[Evidence]:
    paths = sorted((root / ".ai/logs").glob("campaign-audit-*/map-results.tsv"))
    evidence: list[Evidence] = []
    for path, row in iter_tsv(paths):
        scene = row.get("map", "unknown")
        chapter = row.get("chapter", "")
        status = row.get("status", "UNKNOWN")
        hwm = size_to_bytes(row.get("memory_peak", ""))
        blocker = row.get("blocker", "")
        evidence.append(Evidence(
            source=path.relative_to(root).as_posix(),
            scene=f"{chapter}/{scene}" if chapter else scene,
            status=status,
            hwm_bytes=hwm,
            frame_p95_ms=None,
            frame_max_ms=None,
            blocker=blocker,
            recommendation=recommendation_for(status, hwm, blocker),
        ))
    return evidence


def collect_soak(root: Path) -> list[Evidence]:
	paths = sorted((root / ".ai/logs").glob("soak-*/results.tsv"))
	evidence: list[Evidence] = []
	for path, row in iter_tsv(paths):
		scene = row.get("map", "unknown")
		status = row.get("status", "UNKNOWN")
		hwm = size_to_bytes(row.get("hwm_bytes", ""))
		frame_p95 = None if row.get("frame_p95_ms") in {"", "N/A", None} else float(row["frame_p95_ms"])
		frame_max = None if row.get("frame_max_ms") in {"", "N/A", None} else float(row["frame_max_ms"])
		blocker = row.get("note", "")
		evidence.append(Evidence(
			source=path.relative_to(root).as_posix(),
			scene=scene,
			status=status,
			hwm_bytes=hwm,
			frame_p95_ms=frame_p95,
			frame_max_ms=frame_max,
			blocker=blocker,
			recommendation=recommendation_for(status, hwm, blocker),
		))
	return evidence


MEM_STAGE_RE = re.compile(
    r"mem stage=(?P<stage>\S+)\s+total=(?P<total>[0-9.]+)\s*(?P<tunit>Kb|KiB|Mb|MiB|Gb|GiB|bytes)?"
    r".*?hwm=(?P<hwm>[0-9.]+)\s*(?P<hunit>Kb|KiB|Mb|MiB|Gb|GiB|bytes)"
    r".*?map=(?P<map>\S+)",
    re.I,
)
FRAME_BUDGET_RE = re.compile(
    r"FRAME_BUDGET_STATS:\s*samples=(?P<samples>\d+)\s+"
    r"avg=(?P<avg>[0-9.]+)ms\s+p95=(?P<p95>[0-9.]+)ms\s+"
    r"max=(?P<max>[0-9.]+)ms",
    re.I,
)
G36_STATUS_RE = re.compile(r"G36_STATUS:\s*(?P<status>\S+)")
MAP_LOADED_RE = re.compile(r"map loaded\s+(?P<map>\S+)")


def collect_dolphin_probes(root: Path) -> list[Evidence]:
    """Ingest recent Dolphin New Game / smoke probe logs for G72 ceilings."""
    evidence: list[Evidence] = []
    probe_dirs = sorted((root / ".ai/logs").glob("dolphin-probe-*"), reverse=True)
    # Prefer dated dirs; skip named copies like dolphin-probe-g94-run1.txt files.
    probe_dirs = [path for path in probe_dirs if path.is_dir()][:40]

    for probe_dir in probe_dirs:
        stderr = probe_dir / "stderr.log"
        if not stderr.is_file():
            continue
        text = read(stderr)
        # Also fold in harness analyze lines if a probe wrapper saved them beside logs.
        for extra_name in ("analyze.txt", "probe-summary.txt", "stdout.log"):
            extra = probe_dir / extra_name
            if extra.is_file():
                text += "\n" + read(extra)

        hwm_bytes: int | None = None
        scene = "unknown"
        for match in MEM_STAGE_RE.finditer(text):
            size = size_to_bytes(f"{match.group('hwm')} {match.group('hunit')}")
            if size is not None and (hwm_bytes is None or size > hwm_bytes):
                hwm_bytes = size
            map_name = match.group("map")
            if map_name and map_name not in {"(none)", "none", "<none>"}:
                scene = map_name
        if scene == "unknown":
            loaded = MAP_LOADED_RE.findall(text)
            if loaded:
                scene = loaded[-1]

        frame_p95 = None
        frame_max = None
        budget = FRAME_BUDGET_RE.search(text)
        if budget and int(budget.group("samples")) > 0:
            frame_p95 = float(budget.group("p95"))
            frame_max = float(budget.group("max"))

        g36 = G36_STATUS_RE.search(text)
        if "G94 load restore present" in text or "G94 lean save ready" in text:
            status = "PASS"
            blocker = "newgame save/load probe"
        elif "changelevel" in text and "world present" in text:
            status = "PASS"
            blocker = "newgame changelevel probe"
        elif g36 and g36.group("status").upper() == "PASS":
            status = "PASS"
            blocker = "dolphin newgame/smoke probe"
        elif "G82: Intentional phase fault" in text:
            status = "PASS"
            blocker = "g82 intentional phase fault (boot isolation)"
            if scene == "unknown":
                scene = "boot"
        elif "map loaded" in text:
            status = "MAP_LOADED"
            blocker = "dolphin probe map loaded"
        elif hwm_bytes is None and scene == "unknown":
            continue
        else:
            status = "INCONCLUSIVE"
            blocker = "dolphin probe without clear G36/map pass"

        # Prefer named New Game route scenes in the risk table.
        if "newgame" in text.lower() or "gcnewgame" in text.lower():
            if scene not in {"unknown", "boot"}:
                scene = f"newgame/{scene}"

        evidence.append(Evidence(
            source=probe_dir.relative_to(root).as_posix(),
            scene=scene,
            status=status,
            hwm_bytes=hwm_bytes,
            frame_p95_ms=frame_p95,
            frame_max_ms=frame_max,
            blocker=blocker,
            recommendation=recommendation_for(status, hwm_bytes, blocker),
        ))
    return evidence


def source_profile_status(root: Path) -> dict[str, bool]:
    build = read(root / "scripts/build-gamecube.sh")
    vid = read(root / "engine/platform/gamecube/vid_gamecube.c")
    r_image = read(root / "ref/gx/r_image.c")
    r_surf = read(root / "ref/gx/r_surf.c")
    r_main = read(root / "ref/gx/r_main.c")
    return {
        "build_low_memory_mode": "--low-memory-mode=2" in build,
        "default_release_profile": 'Cvar_Get( "gc_quality", "1"' in vid,
        "texture_low_memory_clamps": "low-memory" in r_image and "clamp" in r_image,
        "surface_cache_bounds": "bound surface cache" in r_surf or "low-memory surfaces" in r_surf,
        "world_edge_bounds": "low-memory" in r_main and ("caps edges" in r_main or "caps surfaces" in r_main),
    }


def risk_rank(item: Evidence) -> tuple[int, int]:
    status_weight = {
        "GUEST_FAILURE": 0,
        "HOST_FAILURE": 1,
        "TIMEOUT": 2,
        "MAP_TIMEOUT": 3,
        "INCONCLUSIVE": 4,
        "MISSING": 5,
        "NOT_TESTED": 6,
        "WARN": 7,
        "MAP_LOADED": 8,
        "MAP_READY": 9,
        "PASS": 10,
    }.get(item.status.upper(), 5)
    return (status_weight, -(item.hwm_bytes or 0))


def scene_key(item: Evidence) -> str:
    return item.scene.rsplit("/", 1)[-1]


def evidence_mtime(root: Path, item: Evidence) -> float:
    path = root / item.source
    try:
        return path.stat().st_mtime
    except OSError:
        return 0.0


def latest_per_scene(root: Path, evidence: list[Evidence]) -> list[Evidence]:
    latest: dict[str, Evidence] = {}
    for item in evidence:
        key = scene_key(item)
        current = latest.get(key)
        if current is None or evidence_mtime(root, item) >= evidence_mtime(root, current):
            latest[key] = item
    return list(latest.values())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--log-dir", type=Path)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    root = args.repo.resolve()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    log_dir = args.log_dir or root / ".ai/logs" / f"worst-case-{stamp}"
    log_dir.mkdir(parents=True, exist_ok=True)

    evidence = (
        collect_map_compat(root)
        + collect_campaign(root)
        + collect_soak(root)
        + collect_dolphin_probes(root)
    )
    current_evidence = latest_per_scene(root, evidence)
    source_status = source_profile_status(root)
    hard_failures = [
        item for item in current_evidence
        if item.hwm_bytes is not None and item.hwm_bytes >= MEM1_CRITICAL_BYTES
    ]
    runtime_blockers = [
        item for item in current_evidence
        if item.status.upper() in {"GUEST_FAILURE", "HOST_FAILURE", "TIMEOUT", "MAP_TIMEOUT"}
    ]
    missing_source_guards = [name for name, ok in source_status.items() if not ok]
    ok = not hard_failures and not missing_source_guards
    if args.strict and runtime_blockers:
        ok = False

    if missing_source_guards:
        profile_decision = "release profile is not safe: missing source guard(s)"
    elif hard_failures:
        profile_decision = "release profile must be lowered or optimized before support claim"
    elif runtime_blockers:
        profile_decision = "release profile remains conservative; runtime blockers need G68/G74 classification"
    else:
        profile_decision = "release profile can remain gc_quality=1 with current evidence"

    report = {
        "generated": datetime.now(timezone.utc).isoformat(),
        "ok": ok,
        "strict": args.strict,
        "profile_decision": profile_decision,
        "source_profile_status": source_status,
        "hard_failures": [asdict(item) for item in hard_failures],
        "runtime_blockers": [asdict(item) for item in runtime_blockers[:30]],
        "worst_scenes": [asdict(item) for item in sorted(current_evidence, key=risk_rank)[:30]],
        "evidence_count": len(evidence),
        "current_scene_count": len(current_evidence),
    }
    (log_dir / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    with (log_dir / "worst-scenes.tsv").open("w", encoding="utf-8") as out:
        out.write("source\tscene\tstatus\thwm_bytes\tframe_p95_ms\tframe_max_ms\tblocker\trecommendation\n")
        for item in sorted(current_evidence, key=risk_rank)[:100]:
            out.write(
                f"{item.source}\t{item.scene}\t{item.status}\t"
                f"{item.hwm_bytes if item.hwm_bytes is not None else 'N/A'}\t"
                f"{item.frame_p95_ms if item.frame_p95_ms is not None else 'N/A'}\t"
                f"{item.frame_max_ms if item.frame_max_ms is not None else 'N/A'}\t"
                f"{item.blocker.replace(chr(9), ' ')}\t{item.recommendation}\n"
            )

    with (log_dir / "summary.md").open("w", encoding="utf-8") as out:
        out.write("# GameCube Worst-Case Performance and Memory Report\n\n")
        out.write(f"- Generated: {datetime.now(timezone.utc).isoformat()}\n")
        out.write(f"- Status: {'PASS' if ok else 'FAIL'}\n")
        out.write(f"- Strict mode: {int(args.strict)}\n")
        out.write(f"- Evidence rows: {len(evidence)}\n")
        out.write(f"- Current scene rows: {len(current_evidence)}\n")
        out.write(f"- Profile decision: {profile_decision}\n")
        out.write(f"- Hard MEM1 failures: {len(hard_failures)}\n")
        out.write(f"- Runtime blockers: {len(runtime_blockers)}\n")
        out.write(f"- Missing source guards: {', '.join(missing_source_guards) if missing_source_guards else 'none'}\n\n")
        out.write("## Source Profile Guards\n\n")
        out.write("| Guard | Status |\n|---|---|\n")
        for name, status in source_status.items():
            out.write(f"| {name} | {'PASS' if status else 'FAIL'} |\n")
        out.write("\n## Highest-Risk Scenes\n\n")
        out.write("| Scene | Status | HWM bytes | Source | Recommendation |\n")
        out.write("|---|---|---:|---|---|\n")
        for item in sorted(current_evidence, key=risk_rank)[:20]:
            hwm = item.hwm_bytes if item.hwm_bytes is not None else "N/A"
            out.write(f"| {item.scene} | {item.status} | {hwm} | `{item.source}` | {item.recommendation} |\n")

    print(log_dir / "summary.md")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
