#!/usr/bin/env python3
"""Report measured texture-pack coverage for a map compatibility run.

Map readiness does not establish visual quality. Missing telemetry is kept
distinct from a complete pack; each result is read only from its own probe.
"""

from __future__ import annotations

import argparse
import csv
import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location(
    "gamecube_memory_evidence", ROOT / "scripts/gamecube-memory-evidence.py")
assert _spec and _spec.loader
_memory = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_memory)


def audit(results: Path, root: Path = ROOT) -> list[dict[str, str]]:
    rows = []
    with results.open(newline="", encoding="utf-8") as stream:
        for result in csv.DictReader(stream, delimiter="\t"):
            name = result["map"]
            log = Path(result.get("log_path", ""))
            if not log.is_absolute():
                log = root / log
            sources = [log] if log.is_file() else [
                log / "stderr.log", log / "dolphin-user/Logs/dolphin.log"]
            match = None
            for source in sources:
                if not source.is_file():
                    continue
                text = source.read_text(encoding="utf-8", errors="replace")
                candidates = [m for m in _memory.TEXTURE_BUDGET_RE.finditer(text)
                              if m.group("map").casefold() == name.casefold()]
                if candidates:
                    match = candidates[-1]
                    break
            row = {"map": name, "map_status": result.get("status", ""),
                   "coverage": "UNAVAILABLE", "packed": "", "total": "",
                   "pool_bytes": "", "resident_bytes": "", "hot_bytes": "",
                   "split_dim": "", "log_path": result.get("log_path", "")}
            if match:
                data = match.groupdict()
                row.update({key: data[key] or "" for key in (
                    "packed", "total", "pool_bytes", "resident_bytes", "hot_bytes", "split_dim")})
                packed, total = int(data["packed"]), int(data["total"])
                row["coverage"] = "COMPLETE" if total > 0 and packed == total else "INCOMPLETE"
            rows.append(row)
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map-results", type=Path, required=True,
                        help="results.tsv emitted by gamecube-map-compat-probe.sh")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = audit(args.map_results)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fields = ("map", "map_status", "coverage", "packed", "total", "pool_bytes",
              "resident_bytes", "hot_bytes", "split_dim", "log_path")
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    counts = {state: sum(row["coverage"] == state for row in rows)
              for state in ("COMPLETE", "INCOMPLETE", "UNAVAILABLE")}
    print(f"Texture budget evidence: {args.output} ({counts})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
