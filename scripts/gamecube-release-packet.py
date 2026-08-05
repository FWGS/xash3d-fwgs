#!/usr/bin/env python3
"""Assemble a fail-closed, reproducible Dolphin release evidence packet."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def load_gate(root: Path):
    path = root / "scripts/gamecube-gameplay-gate.py"
    spec = importlib.util.spec_from_file_location("gameplay_gate", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_evidence(source: Path, destination: Path) -> str:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_dir():
        shutil.copytree(source, destination, dirs_exist_ok=True)
    elif source.is_file():
        shutil.copy2(source, destination)
    return str(destination)


def read_logs(path: Path) -> str:
    if path.is_file():
        return path.read_text(encoding="utf-8", errors="replace")
    parts = []
    for name in ("stdout.log", "stderr.log", "dolphin-user/Logs/dolphin.log"):
        candidate = path / name
        if candidate.is_file():
            parts.append(candidate.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(parts)


def validate_artifacts(root: Path) -> tuple[list[dict[str, object]], list[str]]:
    required = ("OUT/bin/xash", "OUT/bin/boot.dol", "OUT/xash3d-gc.iso")
    records = []
    failures = []
    for relative in required:
        path = root / relative
        if not path.is_file() or path.stat().st_size == 0:
            failures.append(f"missing artifact: {relative}")
            continue
        records.append({"path": relative, "bytes": path.stat().st_size, "sha256": sha256(path)})

    elf = root / "OUT/bin/xash"
    if elf.is_file():
        result = subprocess.run(["readelf", "-h", str(elf)], text=True, capture_output=True, check=False)
        if result.returncode != 0 or "PowerPC" not in result.stdout:
            failures.append("ELF header is not a valid PowerPC executable")

    dol = root / "OUT/bin/boot.dol"
    if dol.is_file():
        if dol.read_bytes()[:0xE4] == b"\0" * 0xE4:
            failures.append("DOL header is empty")
        else:
            nonempty_sections = sum(1 for i in range(18) if int.from_bytes(dol.read_bytes()[0x90 + i * 4:0x94 + i * 4], "big"))
            if nonempty_sections == 0:
                failures.append("DOL contains no non-empty sections")

    iso = root / "OUT/xash3d-gc.iso"
    if iso.is_file():
        with iso.open("rb") as handle:
            handle.seek(0x8001)
            if handle.read(5) != b"CD001":
                failures.append("ISO9660 primary volume descriptor is missing")
    return records, failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runtime-log", type=Path, required=True)
    parser.add_argument("--gameplay-log", type=Path, required=True)
    parser.add_argument("--map-report", type=Path, required=True)
    parser.add_argument("--memory-report", type=Path, required=True)
    parser.add_argument("--audio-report", type=Path, required=True)
    parser.add_argument("--soak-report", type=Path, required=True)
    args = parser.parse_args()
    root = args.repo.resolve()
    output = args.output if args.output.is_absolute() else root / args.output
    output.mkdir(parents=True, exist_ok=True)

    records, artifact_failures = validate_artifacts(root)
    runtime = read_logs(args.runtime_log if args.runtime_log.is_absolute() else root / args.runtime_log)
    gameplay_path = args.gameplay_log if args.gameplay_log.is_absolute() else root / args.gameplay_log
    gameplay = read_logs(gameplay_path)
    gate = load_gate(root)
    gameplay_ok, gameplay_failures = gate.check(gameplay)
    map_path = args.map_report if args.map_report.is_absolute() else root / args.map_report
    memory_path = args.memory_report if args.memory_report.is_absolute() else root / args.memory_report
    audio_path = args.audio_report if args.audio_report.is_absolute() else root / args.audio_report
    soak_path = args.soak_report if args.soak_report.is_absolute() else root / args.soak_report

    runtime_ok = (
        ("MAP_READY:" in runtime or "Xash3D GameCube: map loaded" in runtime)
        and "sampled_nonblack=1" in runtime
        and ("FRAME_BUDGET_STATS" in runtime or "frame time=" in runtime)
        and "FATAL ERROR" not in runtime
    )
    map_ok = "MAP_COMPAT_PROBE: PASS" in map_path.read_text(encoding="utf-8", errors="replace") if map_path.is_file() else False
    memory_ok = memory_path.is_file() and bool(json.loads(memory_path.read_text(encoding="utf-8")).get("runtime", {}).get("samples"))
    audio_text = read_logs(audio_path)
    audio_ok = (
        "audio submitted nonzero PCM" in runtime
        or "audio submitted nonzero PCM" in gameplay
        or "audio submitted nonzero PCM" in audio_text
    )
    soak_data = json.loads(soak_path.read_text(encoding="utf-8")) if soak_path.is_file() and soak_path.suffix == ".json" else {}
    soak_ok = bool(soak_data.get("ok"))

    evidence = {
        "runtime": {"status": "PASS" if runtime_ok else "FAIL", "source": str(args.runtime_log)},
        "map": {"status": "PASS" if map_ok else "FAIL", "source": str(args.map_report)},
        "gameplay": {"status": "PASS" if gameplay_ok else "FAIL", "source": str(args.gameplay_log), "failures": gameplay_failures},
        "memory": {"status": "PASS" if memory_ok else "PARTIAL", "source": str(args.memory_report)},
        "audio": {"status": "PASS" if audio_ok else "UNVERIFIED", "source": str(args.audio_report)},
        "soak": {"status": "PASS" if soak_ok else "FAIL", "source": str(args.soak_report)},
    }
    unsupported = []
    if not gameplay_ok:
        unsupported.append("Player gameplay remains incomplete: " + "; ".join(gameplay_failures))
    if not audio_ok:
        unsupported.append("Nonzero mixed PCM/audio voice playback is not observed in Dolphin.")
    unsupported.extend([
        "Writable config/save round-trip is unverified; current Dolphin run is read-only.",
        "Changelevel and inventory continuity are unverified.",
        "Full campaign smoke route is unverified; only the listed map compatibility set is covered.",
        "Real GameCube hardware, analog video, audible output, and persistent SD behavior are unverified.",
    ])
    manifest = {"commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(), "artifacts": records}
    (output / "artifact-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (output / "validation.json").write_text(json.dumps({"artifacts": artifact_failures, "evidence": evidence}, indent=2) + "\n", encoding="utf-8")
    for source, name in ((args.runtime_log, "runtime"), (args.gameplay_log, "gameplay"), (args.map_report, "map"), (args.memory_report, "memory"), (args.audio_report, "audio"), (args.soak_report, "soak")):
        source_path = source if source.is_absolute() else root / source
        if source_path.exists():
            copy_evidence(source_path, output / "evidence" / name)

    complete = not artifact_failures and all(item["status"] == "PASS" for item in evidence.values())
    packet_status = "COMPLETE / RELEASE-READY" if complete else "INCOMPLETE / NOT RELEASE-READY"
    lines = ["# Dolphin Release Packet", "", f"- Generated: {datetime.now(timezone.utc).isoformat()}", f"- Commit: `{manifest['commit']}`", f"- Packet status: **{packet_status}**", "", "## Evidence", "", "| Area | Status |", "|---|---|"]
    lines.extend(f"| {name} | {item['status']} |" for name, item in evidence.items())
    lines += ["", "## Artifacts", "", "See `artifact-manifest.json`; ELF, DOL, and ISO structural validation failures: " + (", ".join(artifact_failures) if artifact_failures else "none") + ".", "", "## Unsupported or Unverified", ""]
    lines.extend(f"- {item}" for item in unsupported)
    lines += ["", "## Reproduction", "", "```text", "./waf clean", "XASH3D_GC_SKIP_DISC_BUILD=1 scripts/build-gamecube.sh", "python3 scripts/build-gamecube-disc.py --output OUT/xash3d-gc.iso --smoke-map c0a0e", "```", "", "Gameplay acceptance is intentionally fail-closed until its required guest markers are present."]
    (output / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"RELEASE_PACKET: {'COMPLETE' if complete else 'INCOMPLETE'}")
    print(output)
    return 0 if complete else 1


if __name__ == "__main__":
    raise SystemExit(main())
