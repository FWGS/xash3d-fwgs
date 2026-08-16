#!/usr/bin/env python3
"""Validate ordered evidence that the GameCube path mirrors retail runtime policy."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


FATAL_RE = re.compile(
    r"Host_Error|Sys_Error|FATAL ERROR|out of memory|MMU fault|"
    r"guest.*(?:crash|abort)|Mem_FreeBlockBig: not allocated",
    re.IGNORECASE,
)

REQUIRED = (
    "Xash3D GameCube: REF_GX static GetRefAPI retail Flipper policy=on",
    "Xash3D GameCube: quality profile stage=video-init gc_quality=1 name=release",
    "Xash3D GameCube: retail Flipper policy capture=1 softworld=0",
    "Xash3D GameCube: G45 controller ready",
    "Xash3D GameCube: MAP_READY ",
    "Xash3D GameCube: G172 HUD sheets loaded",
    "Xash3D GameCube: lean HUD sprites drawn",
    "Xash3D GameCube: G105 landmark viewmodel ready",
    "Xash3D GameCube: G164 studio gouraud shades=",
    "Xash3D GameCube: G154 disc lightmap bind",
    "Xash3D GameCube: G180 lightmap atlas",
    "Xash3D GameCube: G219 Flipper LM on EDGE/TEX",
    "audio backend ready",
    "audio submitted nonzero PCM",
)

ORDERED_PHASES = (

    (
        "Xash3D GameCube: REF_GX static GetRefAPI retail Flipper policy=on",
        "Xash3D GameCube: quality profile stage=video-init gc_quality=1 name=release",
        "Xash3D GameCube: retail Flipper policy capture=1 softworld=0",
    ),
    (
        "Xash3D GameCube: MAP_READY ",
        "Xash3D GameCube: G172 HUD sheets loaded",
        "Xash3D GameCube: lean HUD sprites drawn",
        "Xash3D GameCube: G105 landmark viewmodel ready",
    ),
)


def read_logs(paths: list[Path]) -> str:
    return "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in paths
        if path.is_file()
    )


def discover(log_dir: Path) -> list[Path]:
    names = ("runtime.log", "gameplay.log", "stderr.log", "stdout.log")
    paths = [log_dir / name for name in names]
    paths.extend(log_dir.glob("**/stderr.log"))
    paths.extend(log_dir.glob("**/stdout.log"))
    return list(dict.fromkeys(path for path in paths if path.is_file()))


def check(text: str) -> list[str]:
	failures: list[str] = []
	if FATAL_RE.search(text):
		failures.append("fatal runtime/allocation error present")

	for marker in REQUIRED:
		if marker not in text:
			failures.append(f"missing: {marker}")

	for phase in ORDERED_PHASES:
		position = 0
		for marker in phase:
			found = text.find(marker, position)
			if found < 0:
				failures.append(f"missing or out of order: {marker}")
				break
			position = found + len(marker)
	return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log-dir", type=Path)
    parser.add_argument("--log", action="append", type=Path, default=[])
    args = parser.parse_args()

    if not args.log_dir and not args.log:
        parser.error("provide --log-dir or at least one --log")
    paths = args.log or discover(args.log_dir.resolve())
    failures = check(read_logs(paths))
    if failures:
        print("RETAIL_MIRRORING_GATE: FAIL")
        for failure in failures:
            print(f"RETAIL_MIRRORING_MISSING: {failure}")
        return 1

    print("RETAIL_MIRRORING_GATE: PASS")
    print("RETAIL_MIRRORING_EVIDENCE: retail-gx-policy,release-profile,controller,map,hud,viewmodel,bone-gouraud,lightmap-tev,audio,no-fatal")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
