#!/usr/bin/env python3
"""Accept only guest evidence that proves a bounded gameplay action sequence."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FATAL_RE = re.compile(
    r"Host_Error|Sys_Error|FATAL ERROR|_Mem_Alloc: out of memory|MMU fault|"
    r"Invalid read from|guest.*(?:crash|abort)", re.IGNORECASE
)
FRAME_RE = re.compile(r"frame time=([0-9.]+)ms")

REQUIRED_BEFORE_ACTIONS = (
    "Xash3D GameCube: map loaded ",
    "Xash3D GameCube: entity lump spawn ready",
)

REQUIRED_ACTIONS = (
    "Xash3D GameCube: probe gameplay action attack",
    "Xash3D GameCube: probe gameplay action jump",
    "Xash3D GameCube: probe gameplay action use",
    "Xash3D GameCube: probe gameplay input ready",
)

REQUIRED_POST_ACTION = (
    "Xash3D GameCube: probe native move/look begin",
    "Xash3D GameCube: native axis usercmd ready",
)

# G506 presentation evidence: viewmodel draw, soft-dump viewmodel, HUD composite.
REQUIRED_PRESENTATION = (
    "Xash3D GameCube: G105 viewmodel draw",
    "Xash3D GameCube: G161 soft dump viewmodel ready",
    "Xash3D GameCube: G177 soft dump HUD composite",
)


def read_logs(log_dir: Path) -> str:
    parts = []
    for name in ("stdout.log", "stderr.log", "dolphin-user/Logs/dolphin.log"):
        path = log_dir / name
        if path.is_file():
            parts.append(path.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(parts)


def ordered_markers(text: str, markers: tuple[str, ...]) -> list[str]:
    position = 0
    missing = []
    for marker in markers:
        found = text.find(marker, position)
        if found < 0:
            missing.append(marker)
        else:
            position = found + len(marker)
    return missing


def check(text: str) -> tuple[bool, list[str]]:
    failures = []
    if FATAL_RE.search(text):
        failures.append("guest fatal/runtime error")
    failures.extend(f"missing or out of order: {m}" for m in ordered_markers(text, REQUIRED_BEFORE_ACTIONS))
    failures.extend(f"missing or out of order: {m}" for m in ordered_markers(text, REQUIRED_ACTIONS))
    failures.extend(f"missing or out of order: {m}" for m in ordered_markers(text, REQUIRED_POST_ACTION))
    failures.extend(f"missing or out of order: {m}" for m in ordered_markers(text, REQUIRED_PRESENTATION))

    # These markers are emitted only after the server/client path processed the
    # action; input injection alone cannot satisfy them.
    if "Xash3D GameCube: G120 attack usercmd" not in text:
        failures.append("attack usercmd was not processed by the server")
    if "Xash3D GameCube: G121 PlaybackEvent deliver" not in text:
        failures.append("weapon event was not delivered to the client")
    jump = re.search(
        r"Xash3D GameCube: probe jump PMove ready velocity=\([^,]+,[^,]+,([-.0-9]+)\)", text
    )
    if not jump or float(jump.group(1)) <= 0.0:
        failures.append("jump PMove did not produce positive vertical velocity")
    if "Xash3D GameCube: world interaction " not in text or " done classname=" not in text:
        failures.append("entity use/touch callback did not complete")

    action_at = text.find("Xash3D GameCube: probe gameplay input ready")
    if action_at >= 0:
        post = text[action_at:]
        stable = len(FRAME_RE.findall(post))
        if "Xash3D GameCube: gcmap smoke frames ready" not in post or stable < 3:
            failures.append("post-action stable rendered-frame evidence is incomplete")
    return not failures, failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log-dir", type=Path, required=True)
    args = parser.parse_args()
    ok, failures = check(read_logs(args.log_dir.resolve()))
    if ok:
        print("GAMEPLAY_GATE: PASS")
        print(
            "GAMEPLAY_EVIDENCE: movement/look,jump,attack/weapon,use/entity,"
            "viewmodel/HUD,post-action-stable-frame"
        )
        return 0
    print("GAMEPLAY_GATE: FAIL")
    for failure in failures:
        print(f"GAMEPLAY_MISSING: {failure}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
