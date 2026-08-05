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


def load_ladder(root: Path):
    import sys

    path = root / "scripts/gamecube-runtime-ladder.py"
    name = "gamecube_runtime_ladder"
    if name in sys.modules:
        return sys.modules[name]
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
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


def evaluate_persist_and_changelevel(runtime: str, gameplay: str, audio_text: str = "") -> dict:
    persist_text = "\n".join((runtime, gameplay, audio_text))
    persist_ok = (
        "G508 config round trip ready" in persist_text
        or "G94 round trip present" in persist_text
    )
    changelevel_ok = (
        "CHANGELEVEL_READY" in persist_text
        or "G68 changelevel ready" in persist_text
        or "G100 landmark restore" in persist_text
    )
    return {
        "persist_ok": persist_ok,
        "changelevel_ok": changelevel_ok,
        "persist_text_bytes": len(persist_text),
    }


def evaluate_soak(soak_data: dict) -> bool:
    soak_ok = bool(soak_data.get("ok"))
    if soak_ok and soak_data.get("require_changelevel"):
        soak_ok = soak_data.get("mode") == "changelevel" and bool(soak_data.get("changelevel_route"))
    elif soak_ok and soak_data.get("mode") == "changelevel":
        soak_ok = bool(soak_data.get("changelevel_route"))
    if soak_ok and soak_data.get("require_ladder"):
        results = soak_data.get("results") or []
        if results:
            soak_ok = all(bool(item.get("ladder_ok")) for item in results)
        elif "ladder_ok" in soak_data:
            soak_ok = bool(soak_data.get("ladder_ok"))
        else:
            # G509 reports must carry ladder evidence when require_ladder is set.
            soak_ok = False
    return soak_ok


def evaluate_memory_report(memory_path: Path) -> tuple[bool, dict]:
    """PASS only when MEM1/high-water samples exist; empty stubs stay PARTIAL."""
    if not memory_path.is_file():
        return False, {}
    try:
        data = json.loads(memory_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False, {}
    runtime = data.get("runtime") or {}
    samples = runtime.get("samples") or []
    mem1 = runtime.get("mem1_high_water_bytes")
    has_hwm = isinstance(mem1, int) and mem1 > 0
    if not has_hwm:
        for row in samples:
            if isinstance(row, dict) and isinstance(row.get("hwm_bytes"), int) and row["hwm_bytes"] > 0:
                has_hwm = True
                break
    return has_hwm and bool(samples), data


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
    parser.add_argument(
        "--experiment-manifest",
        type=Path,
        help="Optional G501 manifest.json to copy into the packet.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Skip ELF/DOL/ISO artifact validation; evaluate evidence fixtures only.",
    )
    args = parser.parse_args()
    root = args.repo.resolve()
    output = args.output if args.output.is_absolute() else root / args.output
    output.mkdir(parents=True, exist_ok=True)

    if args.dry_run:
        records, artifact_failures = [], []
    else:
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
    memory_ok, memory_data = evaluate_memory_report(memory_path)
    mem1_bytes = (memory_data.get("runtime") or {}).get("mem1_high_water_bytes") if memory_data else None
    audio_text = read_logs(audio_path)
    audio_ok = (
        "audio submitted nonzero PCM" in runtime
        or "audio submitted nonzero PCM" in gameplay
        or "audio submitted nonzero PCM" in audio_text
    )
    soak_data = json.loads(soak_path.read_text(encoding="utf-8")) if soak_path.is_file() and soak_path.suffix == ".json" else {}
    soak_ok = evaluate_soak(soak_data)
    continuity = evaluate_persist_and_changelevel(runtime, gameplay, audio_text)
    persist_ok = continuity["persist_ok"]
    changelevel_ok = continuity["changelevel_ok"]

    ladder_mod = load_ladder(root)
    ladder_report = ladder_mod.evaluate_ladder("\n".join((runtime, gameplay, audio_text)))
    ladder_ok = bool(ladder_report.get("ok"))
    (output / "ladder.json").write_text(json.dumps(ladder_report, indent=2) + "\n", encoding="utf-8")

    experiment_manifest_path = None
    if args.experiment_manifest:
        src = args.experiment_manifest if args.experiment_manifest.is_absolute() else root / args.experiment_manifest
        if src.is_file():
            experiment_manifest_path = output / "experiment-manifest.json"
            shutil.copy2(src, experiment_manifest_path)
    if experiment_manifest_path is None:
        # Synthesize a G501 pointer from current evidence when none was provided.
        import importlib.util as _ilu
        manifest_script = root / "scripts/gamecube-experiment-manifest.py"
        spec = _ilu.spec_from_file_location("g501_manifest", manifest_script)
        if spec is not None and spec.loader is not None:
            mod = _ilu.module_from_spec(spec)
            sys_modules = __import__("sys").modules
            sys_modules.setdefault("g501_manifest", mod)
            spec.loader.exec_module(mod)
            argv = [
                "--repo", str(root),
                "--hypothesis", f"release packet dry_run={int(bool(args.dry_run))} ladder_ok={int(ladder_ok)}",
                "--target-file", "scripts/gamecube-release-packet.py",
                "--decision", "pending" if not (ladder_ok and soak_ok and persist_ok) else "keep",
                "--output-dir", str(output / "experiment"),
                "--ladder-json", str(output / "ladder.json"),
            ]
            if args.dry_run:
                argv.append("--dry-run")
            if mod.main(argv) == 0 and (output / "experiment" / "manifest.json").is_file():
                experiment_manifest_path = output / "experiment" / "manifest.json"
                shutil.copy2(experiment_manifest_path, output / "experiment-manifest.json")
                experiment_manifest_path = output / "experiment-manifest.json"

    evidence = {
        "runtime": {"status": "PASS" if runtime_ok else "FAIL", "source": str(args.runtime_log)},
        "map": {"status": "PASS" if map_ok else "FAIL", "source": str(args.map_report)},
        "gameplay": {"status": "PASS" if gameplay_ok else "FAIL", "source": str(args.gameplay_log), "failures": gameplay_failures},
        "memory": {
            "status": "PASS" if memory_ok else "PARTIAL",
            "source": str(args.memory_report),
            "mem1_high_water_bytes": mem1_bytes,
        },
        "audio": {"status": "PASS" if audio_ok else "UNVERIFIED", "source": str(args.audio_report)},
        "soak": {"status": "PASS" if soak_ok else "FAIL", "source": str(args.soak_report)},
        "ladder": {
            "status": "PASS" if ladder_ok else "FAIL",
            "first_missing": ladder_report.get("first_missing"),
            "passed_count": ladder_report.get("passed_count"),
            "gate_count": ladder_report.get("gate_count"),
        },
        "persist": {"status": "PASS" if persist_ok else "FAIL"},
        "changelevel": {"status": "PASS" if changelevel_ok else "FAIL"},
    }
    unsupported = []
    if not ladder_ok:
        unsupported.append(
            f"G504 runtime ladder incomplete; first missing gate: {ladder_report.get('first_missing')}"
        )
    if not gameplay_ok:
        unsupported.append("Player gameplay remains incomplete: " + "; ".join(gameplay_failures))
    if not audio_ok:
        unsupported.append("Nonzero mixed PCM/audio voice playback is not observed in Dolphin.")
    if not persist_ok:
        unsupported.append(
            "Writable config/save round-trip is unverified; current Dolphin run is read-only."
        )
    if not changelevel_ok:
        unsupported.append("Changelevel and inventory continuity are unverified.")
    unsupported.extend([
        "Full campaign smoke route is unverified; only the listed map compatibility set is covered.",
        "Real GameCube hardware, analog video, audible output, and persistent SD behavior are unverified.",
    ])
    manifest = {
        "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "artifacts": records,
        "dry_run": bool(args.dry_run),
    }
    (output / "artifact-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (output / "validation.json").write_text(
        json.dumps({
            "artifacts": artifact_failures,
            "evidence": evidence,
            "persist_ok": persist_ok,
            "changelevel_ok": changelevel_ok,
            "ladder_ok": ladder_ok,
            "ladder_first_missing": ladder_report.get("first_missing"),
            "mem1_high_water_bytes": mem1_bytes,
            "experiment_manifest": str(experiment_manifest_path) if experiment_manifest_path else None,
            "dry_run": bool(args.dry_run),
        }, indent=2) + "\n",
        encoding="utf-8",
    )
    for source, name in ((args.runtime_log, "runtime"), (args.gameplay_log, "gameplay"), (args.map_report, "map"), (args.memory_report, "memory"), (args.audio_report, "audio"), (args.soak_report, "soak")):
        source_path = source if source.is_absolute() else root / source
        if source_path.exists():
            copy_evidence(source_path, output / "evidence" / name)

    # Dry-run judges evidence/continuity only; artifact structural checks are skipped.
    # Non-dry-run: require core PASS areas + persist/changelevel + G504 ladder;
    # memory may be PARTIAL, audio may be UNVERIFIED without blocking the historic core set.
    required_evidence = ("runtime", "map", "gameplay", "soak", "ladder")
    if args.dry_run:
        complete = all(
            evidence[k]["status"] == "PASS" for k in required_evidence
        ) and persist_ok and changelevel_ok
    else:
        complete = (
            not artifact_failures
            and all(evidence[k]["status"] == "PASS" for k in required_evidence)
            and persist_ok
            and changelevel_ok
        )
    packet_status = "COMPLETE / RELEASE-READY" if complete else "INCOMPLETE / NOT RELEASE-READY"
    lines = ["# Dolphin Release Packet", "", f"- Generated: {datetime.now(timezone.utc).isoformat()}", f"- Commit: `{manifest['commit']}`", f"- Packet status: **{packet_status}**", f"- Dry-run: `{bool(args.dry_run)}`", "", "## Evidence", "", "| Area | Status |", "|---|---|"]
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
