#!/usr/bin/env python3
"""Generate evidence-only GameCube memory data from artifacts and runtime logs."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

SIZE_RE = re.compile(r"(?P<value>[0-9.]+)\s*(?P<unit>bytes|Kb|KiB|Mb|MiB|Gb|GiB)", re.I)
MEM_RE = re.compile(
    r"mem stage=(?P<stage>\S+)\s+total=(?P<total>[0-9.]+(?:\s*(?:bytes|Kb|KiB|Mb|MiB|Gb|GiB))?)\s+"
    r"(?:delta=(?P<delta>[0-9.]+(?:\s*(?:bytes|Kb|KiB|Mb|MiB|Gb|GiB))?)\s+)?"
    r"hwm=(?P<hwm>[0-9.]+(?:\s*(?:bytes|Kb|KiB|Mb|MiB|Gb|GiB)?))"
    r"(?:\s+map=(?P<map>\S+))?", re.I)
# Soak / OSReport MEM1 high-water lines (G69/G509).
MEM1_RE = re.compile(
    r"(?:MEM1|mem1)[^\n]*?(?:hwm|high[-_ ]?water|peak)[=:\s]+(?P<hwm>[0-9.]+)\s*"
    r"(?P<unit>bytes|Kb|Mb|Gb|KiB|MiB)?",
    re.I,
)
MEM1_IDENTICAL_RE = re.compile(
    r"MEM1 high-water(?: was)?(?: identical at)?\s*`?(?P<n>[0-9,]+)`?\s*bytes",
    re.I,
)
PRESSURE_RE = re.compile(
    r"map-load pressure stage=(?P<stage>\S+)\s+peak=(?P<peak>[^ ]+)\s+"
    r"delta=(?P<delta>[^ ]+)\s+base=(?P<base>[^ ]+)", re.I)
FAIL_RE = re.compile(
    r"mem FAIL subsystem=(?P<subsystem>\S+)\s+size=(?P<size>[0-9.]+(?:\s*(?:bytes|Kb|KiB|Mb|MiB|Gb|GiB))?)\s+"
    r"map=(?P<map>\S+)\s+at=(?P<at>\S+)\s+total=(?P<total>[0-9.]+(?:\s*(?:bytes|Kb|KiB|Mb|MiB|Gb|GiB))?)\s+"
    r"hwm=(?P<hwm>[0-9.]+(?:\s*(?:bytes|Kb|KiB|Mb|MiB|Gb|GiB))?)", re.I)


def parse_size(value: str) -> int | None:
    match = SIZE_RE.fullmatch(value.strip())
    if not match:
        # Bare number → treat as bytes when callers pass "5242880".
        try:
            return int(float(value.strip().replace(",", "")))
        except ValueError:
            return None
    scale = {
        "bytes": 1, "kb": 1024, "kib": 1024, "mb": 1024**2,
        "mib": 1024**2, "gb": 1024**3, "gib": 1024**3,
    }
    return int(float(match.group("value")) * scale[match.group("unit").lower()])


def parse_size_parts(value: str, unit: str | None) -> int | None:
    if unit:
        return parse_size(f"{value} {unit}")
    return parse_size(value)


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def command_text(command: list[str]) -> str:
    try:
        result = subprocess.run(command, check=False, text=True,
            capture_output=True)
    except OSError:
        return ""
    return result.stdout + result.stderr


def dol_sections(path: Path) -> list[dict[str, int | str]]:
    data = path.read_bytes()
    if len(data) < 0x100:
        return []
    sections: list[dict[str, int | str]] = []
    for index in range(18):
        offset = int.from_bytes(data[index * 4:index * 4 + 4], "big")
        address = int.from_bytes(data[0x48 + index * 4:0x4c + index * 4], "big")
        size = int.from_bytes(data[0x90 + index * 4:0x94 + index * 4], "big")
        if offset or address or size:
            sections.append({"index": index, "offset": offset,
                "address": address, "size": size})
    return sections


def elf_sections(path: Path) -> list[str]:
    if not path.is_file():
        return []
    text = command_text(["readelf", "-SW", str(path)])
    return [line.strip() for line in text.splitlines()
        if re.match(r"^\s*\[\s*\d+\]", line)]


def collect_logs(root: Path, explicit: list[Path]) -> str:
    paths = [path if path.is_absolute() else root / path for path in explicit]
    paths.extend(sorted((root / ".ai/logs").glob("**/*.log")))
    return "\n".join(read_text(path) for path in dict.fromkeys(paths))


def generate(root: Path, log_paths: list[Path]) -> dict[str, object]:
    dol = root / "OUT/bin/boot.dol"
    elfs = sorted((root / "OUT").glob("**/*.elf"))
    maps = sorted((root / "OUT").glob("**/*.map")) + sorted((root / "OUT").glob("**/*.ld.map"))
    maps += sorted((root / "build").glob("**/*.map")) + sorted(root.glob("*.map"))
    log_text = collect_logs(root, log_paths)
    samples = []
    for match in MEM_RE.finditer(log_text):
        row = match.groupdict()
        row["total_bytes"] = parse_size(row["total"]) if row.get("total") else None
        row["hwm_bytes"] = parse_size(row["hwm"]) if row.get("hwm") else None
        if row.get("delta"):
            row["delta_bytes"] = parse_size(row["delta"])
        if not row.get("map"):
            row["map"] = "(none)"
        samples.append(row)
    for match in MEM1_RE.finditer(log_text):
        hwm = parse_size_parts(match.group("hwm"), match.group("unit"))
        if hwm is None:
            continue
        samples.append({
            "stage": "mem1",
            "total": None,
            "delta": None,
            "hwm": str(hwm),
            "map": "(none)",
            "total_bytes": None,
            "hwm_bytes": hwm,
            "source": "mem1_line",
        })
    for match in MEM1_IDENTICAL_RE.finditer(log_text):
        hwm = int(match.group("n").replace(",", ""))
        samples.append({
            "stage": "mem1",
            "total": None,
            "delta": None,
            "hwm": str(hwm),
            "map": "(none)",
            "total_bytes": None,
            "hwm_bytes": hwm,
            "source": "mem1_identical",
        })
    pressure = []
    for match in PRESSURE_RE.finditer(log_text):
        row = match.groupdict()
        row["peak_bytes"] = parse_size(row["peak"])
        row["base_bytes"] = parse_size(row["base"])
        pressure.append(row)
    failures = []
    for match in FAIL_RE.finditer(log_text):
        row = match.groupdict()
        row["size_bytes"] = parse_size(row["size"])
        row["total_bytes"] = parse_size(row["total"])
        row["hwm_bytes"] = parse_size(row["hwm"])
        failures.append(row)
    largest = max(failures, key=lambda row: row.get("size_bytes") or -1,
        default=None)
    per_map: dict[str, dict[str, object]] = {}
    for row in samples:
        map_name = str(row.get("map") or "(none)")
        current = per_map.setdefault(map_name, {"map": map_name,
            "peak_bytes": None, "peak_stage": None, "samples": 0})
        current["samples"] = int(current["samples"]) + 1
        hwm = row.get("hwm_bytes")
        if isinstance(hwm, int) and (current["peak_bytes"] is None or
                hwm > int(current["peak_bytes"])):
            current["peak_bytes"] = hwm
            current["peak_stage"] = row.get("stage")
    mem1_peak = max((row.get("hwm_bytes") for row in samples
        if isinstance(row.get("hwm_bytes"), int)), default=None)
    return {
        "schema": "gamecube.memory-evidence.v1",
        "assumptions": {"mem1_bytes": 24 * 1024**2,
            "mem2_bytes": 16 * 1024**2},
        "artifacts": {
            "dol": {"path": str(dol.relative_to(root)),
                "bytes": dol.stat().st_size if dol.is_file() else None,
                "sections": dol_sections(dol) if dol.is_file() else []},
            "elf": [{"path": str(path.relative_to(root)),
                "sections": elf_sections(path)} for path in elfs],
            "linker_maps": [str(path.relative_to(root)) for path in maps],
        },
        "runtime": {"mem1_high_water_bytes": mem1_peak,
            "mem2_high_water_bytes": "UNAVAILABLE",
            "samples": samples, "map_load_pressure": pressure,
            "per_map_peak": list(per_map.values()), "largest_failed_allocation": largest,
            "texture_lightmap_audio_cache": "UNAVAILABLE without tagged telemetry"},
        "interpretation": "measured fields only; unavailable fields are not estimates",
    }


def markdown(report: dict[str, object]) -> str:
    artifacts = report["artifacts"]
    runtime = report["runtime"]
    return "\n".join([
        "# Generated GameCube Memory Evidence", "",
        f"Schema: `{report['schema']}`", "",
        "| Field | Value |", "|---|---|",
        f"| MEM1 operating assumption | {report['assumptions']['mem1_bytes']} bytes |",
        f"| MEM2 operating assumption | {report['assumptions']['mem2_bytes']} bytes |",
        f"| DOL | {artifacts['dol']['path']} ({artifacts['dol']['bytes'] or 'UNAVAILABLE'} bytes) |",
        f"| ELF artifacts | {len(artifacts['elf'])} |",
        f"| Linker maps | {', '.join(artifacts['linker_maps']) or 'UNAVAILABLE'} |",
        f"| MEM1 high-water | {runtime['mem1_high_water_bytes'] or 'UNAVAILABLE'} bytes |",
        f"| MEM2 high-water | {runtime['mem2_high_water_bytes']} |",
        f"| Runtime samples | {len(runtime['samples'])} |",
        f"| Map-load pressure samples | {len(runtime['map_load_pressure'])} |",
        f"| Largest failed allocation | {runtime['largest_failed_allocation'] or 'UNAVAILABLE'} |",
        f"| Texture/lightmap/audio/cache | {runtime['texture_lightmap_audio_cache']} |",
        "", "This file is generated. Do not turn unavailable fields into estimates.", "",
    ])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--markdown", type=Path)
    parser.add_argument("--log", type=Path, action="append", default=[])
    args = parser.parse_args()
    root = args.repo.resolve()
    report = generate(root, args.log)
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        output = args.output if args.output.is_absolute() else root / args.output
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
    if args.markdown:
        output = args.markdown if args.markdown.is_absolute() else root / args.markdown
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(markdown(report), encoding="utf-8")
    if not args.output and not args.markdown:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
