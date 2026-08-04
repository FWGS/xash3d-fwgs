#!/usr/bin/env python3
"""Accept only complete, paced GameCube intro-video evidence."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FATAL_RE = re.compile(
    r"Host_Error|Sys_Error|FATAL ERROR|out of memory|MMU fault|"
    r"decode failed|audio underrun|video lag|guest.*(?:crash|abort)",
    re.IGNORECASE,
)
OPEN_RE = re.compile(
    r"intro GCVID opened .* \((\d+)x(\d+), (\d+) frames.*"
)
PROGRESS_RE = re.compile(
    r"intro AVI progress frame=(\d+)/(\d+) elapsed=([0-9.]+)"
)


def read_log(log_dir: Path) -> str:
    paths = (
        log_dir / "dolphin-user/Logs/dolphin.log",
        log_dir / "dolphin.stderr.log",
        log_dir / "stderr.log",
    )
    return "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in paths
        if path.is_file()
    )


def check(text: str) -> tuple[bool, list[str]]:
    failures: list[str] = []
    if FATAL_RE.search(text):
        failures.append("guest fatal/video/audio error")

    opened = OPEN_RE.search(text)
    if not opened:
        failures.append("GCVID stream was not opened")
        frame_count = 0
    else:
        frame_count = int(opened.group(3))

    required = (
        "intro AVI audio PCM",
        "intro AVI audio attached",
        "intro AVI audio start synced to first uploaded frame",
        "intro AVI decoded first frame",
        "audio submitted nonzero PCM",
    )
    failures.extend(f"missing marker: {marker}" for marker in required if marker not in text)

    progress = {
        int(frame): (int(total), float(elapsed))
        for frame, total, elapsed in PROGRESS_RE.findall(text)
    }
    expected = (15, 30, 60, 120)
    for frame in expected:
        if frame not in progress:
            failures.append(f"missing paced progress marker: frame {frame}")
            continue
        total, elapsed = progress[frame]
        if total != frame_count:
            failures.append(f"progress frame {frame} reports total {total}, expected {frame_count}")
        target = frame / 15.0
        if abs(elapsed - target) > 0.25:
            failures.append(f"frame {frame} pacing outside 250ms: elapsed={elapsed:.2f} expected={target:.2f}")

    if frame_count and f"intro AVI reached end frame={frame_count}/{frame_count}" not in text:
        failures.append("video did not reach its final frame")

    return not failures, failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log-dir", type=Path, required=True)
    args = parser.parse_args()
    ok, failures = check(read_log(args.log_dir.resolve()))
    if ok:
        print("VIDEO_PLAYBACK_GATE: PASS")
        print("VIDEO_PLAYBACK_EVIDENCE: complete-paced-video,audio-synced,nonzero-pcm,no-fatal")
        return 0
    print("VIDEO_PLAYBACK_GATE: FAIL")
    for failure in failures:
        print(f"VIDEO_MISSING: {failure}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
