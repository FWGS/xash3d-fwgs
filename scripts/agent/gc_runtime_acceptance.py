#!/usr/bin/env python3
"""Single fail-closed acceptance contract for GameCube runtime patches."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


SOURCE_ROOTS = ("engine/", "ref/", "public/", "stub/", "scripts/", "3rdparty/hlsdk-portable/")
FATAL_RE = re.compile(
    r"Host_Error|Sys_Error|Xash Error|GUEST_FAILURE|guest.*(?:crash|abort)|"
    r"MMU fault|Invalid read from|out of memory|MEM1 exhaustion|fatal message=",
    re.IGNORECASE,
)
DEFAULT_REGRESSION_MARKERS = ("G36_STATUS: PASS", "G45_STATUS: PASS")


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""
    except OSError:
        return ""


def git(root: Path, *args: str) -> str:
    result = subprocess.run(["git", *args], cwd=root, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False)
    return result.stdout


def changed_paths(root: Path, baseline: str | None) -> list[str]:
    paths: set[str] = set()
    if baseline:
        paths.update(p for p in git(root, "diff", "--name-only", f"{baseline}..HEAD").splitlines() if p)
    for line in git(root, "status", "--short", "--untracked-files=all").splitlines():
        if len(line) < 4:
            continue
        path = line[3:].strip()
        if " -> " in path:
            path = path.split(" -> ", 1)[1].strip()
        paths.add(path)
    return sorted(paths)


def source_paths(paths: list[str]) -> list[str]:
    return [p for p in paths if p.startswith(SOURCE_ROOTS)]


def expand_probe_evidence(root: Path, probe_log: str) -> str:
    """Include the guest logs named by dolphin-boot-probe.sh in the contract."""
    evidence = [probe_log]
    for raw in re.findall(r"^Logs:\s+(.+)$", probe_log, re.MULTILINE):
        log_dir = Path(raw.strip())
        if not log_dir.is_absolute():
            log_dir = root / log_dir
        for name in ("stdout.log", "stderr.log"):
            evidence.append(read_text(log_dir / name))
    return "\n".join(evidence)


def valid_dol(path: Path) -> tuple[bool, str]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        return False, f"missing DOL: {exc}"
    if len(data) < 0x100 or data[:4] == b"\x7fELF":
        return False, "not an elf2dol DOL"
    sections = []
    for base, count in ((0x00, 7), (0x1C, 11)):
        for index in range(count):
            offset = int.from_bytes(data[base + index * 4:base + index * 4 + 4], "big")
            size_base = 0x90 if base == 0 else 0xAC
            size = int.from_bytes(data[size_base + index * 4:size_base + index * 4 + 4], "big")
            if size:
                sections.append((offset, size))
    if not sections or any(offset < 0x100 or offset + size > len(data) for offset, size in sections):
        return False, "DOL section table is invalid"
    return True, f"valid DOL ({len(data)} bytes, {len(sections)} sections)"


def valid_disc(path: Path) -> tuple[bool, str]:
    try:
        with path.open("rb") as stream:
            stream.seek(0x1C)
            magic = int.from_bytes(stream.read(4), "big")
        size = path.stat().st_size
    except OSError as exc:
        return False, f"missing disc: {exc}"
    if magic != 0xC2339F3D or size <= 0x3000:
        return False, f"invalid disc magic/size (magic={magic:#x}, bytes={size})"
    return True, f"valid GameCube disc ({size} bytes)"


def evaluate_runtime_evidence(
    root: Path,
    *,
    build_code: int,
    build_log: str,
    disc_code: int,
    disc_log: str,
    probe_log: str,
    baseline: str | None = None,
    intended_paths: list[str] | None = None,
    dol_path: Path | None = None,
    disc_path: Path | None = None,
    required_map: str = "c0a0e",
    regression_markers: tuple[str, ...] = DEFAULT_REGRESSION_MARKERS,
    require_visual: bool = False,
    require_frame: bool = False,
    require_audio: bool = False,
) -> dict[str, object]:
    dol_path = dol_path or root / "OUT/bin/boot.dol"
    disc_path = disc_path or root / "OUT/xash3d-gc.iso"
    probe_evidence = expand_probe_evidence(root, probe_log)
    changed = changed_paths(root, baseline)
    changed_source = source_paths(changed)
    intended = sorted(set(intended_paths or []))
    outside = sorted(p for p in changed_source if p not in intended)
    dol_ok, dol_detail = valid_dol(dol_path)
    disc_ok, disc_detail = valid_disc(disc_path)

    required: dict[str, dict[str, object]] = {
        "clean_build": {
            "ok": build_code == 0 and "GameCube build installed to OUT/" in build_log
            and "Build failed" not in build_log,
            "detail": "build completed" if build_code == 0 else f"build exit={build_code}",
        },
        "valid_dol": {"ok": dol_ok, "detail": dol_detail},
        "disc_build": {
            "ok": disc_code == 0 and disc_ok and "Built " in disc_log,
            "detail": disc_detail if disc_ok else f"disc exit={disc_code}; {disc_detail}",
        },
        "map_loaded": {
            "ok": f"Xash3D GameCube: map loaded {required_map}" in probe_evidence
            or f"MAP_READY: Xash3D loaded {required_map}" in probe_evidence,
            "detail": f"map={required_map}",
        },
        "no_guest_fatal": {"ok": not bool(FATAL_RE.search(probe_evidence)), "detail": "no fatal marker"},
        "source_scope": {
            "ok": not outside,
            "detail": "no source mutation outside intended patch" if not outside else f"outside={outside}",
        },
        "regression_markers": {
            "ok": all(marker in probe_evidence for marker in regression_markers),
            "detail": ", ".join(marker for marker in regression_markers if marker in probe_evidence)
            or "required regression marker missing",
        },
    }
    optional = {
        "visual": {"required": require_visual, "ok": "VISUAL_STATUS: nonblack" in probe_evidence},
        "frame": {"required": require_frame, "ok": bool(re.search(r"present frame=|frame time=", probe_evidence))},
        "audio": {"required": require_audio, "ok": "audio" in probe_evidence.lower() and "ready" in probe_evidence.lower()},
    }
    required_failures = [name for name, result in required.items() if not result["ok"]]
    optional_failures = [name for name, result in optional.items() if result["required"] and not result["ok"]]
    report = {
        "schema": "gamecube.runtime-evidence.v1",
        "accepted": not required_failures and not optional_failures,
        "required": required,
        "optional": optional,
        "required_failures": required_failures,
        "optional_failures": optional_failures,
        "changed_paths": changed,
        "intended_paths": intended,
        "baseline_commit": baseline,
        "artifacts": {"dol": str(dol_path), "disc": str(disc_path)},
    }
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-code", type=int, required=True)
    parser.add_argument("--build-log", type=Path, required=True)
    parser.add_argument("--disc-code", type=int, required=True)
    parser.add_argument("--disc-log", type=Path, required=True)
    parser.add_argument("--probe-log", type=Path, required=True)
    parser.add_argument("--baseline")
    parser.add_argument("--intended-path", action="append", default=[])
    parser.add_argument("--map", default="c0a0e")
    parser.add_argument("--require-visual", action="store_true")
    parser.add_argument("--require-frame", action="store_true")
    parser.add_argument("--require-audio", action="store_true")
    args = parser.parse_args()
    root = args.repo.resolve()
    report = evaluate_runtime_evidence(
        root, build_code=args.build_code, build_log=read_text(args.build_log),
        disc_code=args.disc_code, disc_log=read_text(args.disc_log),
        probe_log=read_text(args.probe_log), baseline=args.baseline,
        intended_paths=args.intended_path, required_map=args.map,
        require_visual=args.require_visual, require_frame=args.require_frame,
        require_audio=args.require_audio,
    )
    print(json.dumps(report, indent=2))
    return 0 if report["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
